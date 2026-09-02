#include "pch.h"
#include "Level/TileMapLevel.h"

#include "Asset/AssetManager.h"
#include "Asset/LevelDataAsset.h"
#include "Asset/LevelMap.h"
#include "Camera/CameraManager.h"
#include "Math/SymbolPalette.h"
#include "Math/ViewTransform.h"
#include "Render/RenderLayer.h"
#include "Render/Renderer.h"

#include <cmath>

NAME_SPACE_BEGIN(Craft)

TileMapLevel::TileMapLevel(std::string levelName)
	: levelName(std::move(levelName))
{
}

void TileMapLevel::OnInitialized()
{
	// super를 반드시 부른다. hasInitialized를 세우는 건 base뿐이고,
	// Engine::OnInitialized는 그 플래그로 재진입을 막는다.
	// 빠뜨리면 컴파일 에러 없이 이 함수가 매 프레임 다시 돈다.
	super::OnInitialized();

	rowScratch.reserve(Renderer::Get().GetScreenSize().x);

	// 이름 -> 경로는 LevelData가 알고 있다. 격자 파일은 그 경로로 따로 읽는다.
	// (AnimationData에서 클립 경로를 찾아 LoadClipsFromFileAsync로 넘기는 것과 같은 구조)
	const std::shared_ptr<const LevelDataAsset> levelData =
		AssetManager::Get().GetPrimaryAsset<LevelDataAsset>("LevelData");

	if (nullptr == levelData)
	{
		::OutputDebugStringA("[TileMapLevel] LevelData primary asset not found\n");
		return;
	}

	const std::wstring& path = levelData->FindLevelPath(levelName);

	if (path.empty())
	{
		::OutputDebugStringA("[TileMapLevel] level name not found in LevelData\n");
		return;
	}

	// 워커가 파싱하고 콜백은 게임 쓰레드(AssetManager::Tick)에서 불린다.
	// 레벨이 그 사이에 교체될 수 있으므로 weak로 잡아 자신의 생존을 확인한다.
	std::weak_ptr<Level> weakSelf = weak_from_this();

	AssetManager::Get().LoadAsync<LevelMap>(path.c_str(),
		[weakSelf](std::shared_ptr<const LevelMap> loaded)
		{
			const std::shared_ptr<TileMapLevel> self = Cast<TileMapLevel>(weakSelf.lock());

			if (nullptr == self)
			{
				return;
			}

			self->OnLevelMapLoaded(std::move(loaded));
		});
}

void TileMapLevel::OnLevelMapLoaded(std::shared_ptr<const LevelMap> loaded)
{
	if (nullptr == loaded || loaded->IsEmpty())
	{
		::OutputDebugStringA("[TileMapLevel] level map is empty\n");
		return;
	}

	levelMap = std::move(loaded);

	// 카메라 클램프의 근거. 비어 있으면 무한 월드로 취급되어 화면이 레벨 밖으로 나간다.
	// 격자가 도착한 뒤에야 크기를 알 수 있으므로 여기서 세운다.
	SetWorldBounds(Rect(0, 0, levelMap->GetWidth(), levelMap->GetHeight()));
}

void TileMapLevel::SubmitRow(int screenY, int width)
{
	// 전부 투명한 줄은 제출하지 않는다.
	// 월드 가장자리나 회전 보간 중에는 이런 줄이 여럿 나오는데,
	// 제출하면 색 벡터와 더미 문자열을 할당해놓고 DrawRenderQueue가 버린다.
	bool anyOpaque = false;

	for (int index = 0; index < width; ++index)
	{
		if (rowScratch[index] != SymbolPalette::TransparentSymbol)
		{
			anyOpaque = true;
			break;
		}
	}

	if (anyOpaque == false)
	{
		return;
	}

	// SubmitWorld가 아니라 Submit(화면 공간)이다. 역변환을 이미 적용했으므로
	// 여기서 뷰 변환을 한 번 더 태우면 두 번 변환된다.
	Renderer::Get().SubmitPixels(
		rowScratch,
		SymbolPalette::GetTable(),
		Vector2(0, screenY),
		RenderLayer::Background,
		SymbolPalette::TransparentSymbol);
}

void TileMapLevel::Draw()
{
	// 지형을 먼저 깔고 그 위에 액터를 올린다.
	if (nullptr != levelMap && CameraManager::HasInstance())
	{
		const CameraManager& camera = CameraManager::Get();
		const Vector2 screenSize = Renderer::Get().GetScreenSize();
		const Vector2 center = camera.GetViewCenterWorld();
		const Vector2 half(screenSize.x / 2, screenSize.y / 2);

		// 월드가 아니라 화면을 순회한다.
		//
		// 월드 한 행을 문자열 하나로 제출하는 방식은 뷰가 돌면 깨진다.
		// Submit은 위치만 변환하고 글자는 언제나 화면 +x 방향으로 눕히기 때문이다.
		// 화면을 순회하고 각 칸을 월드로 역변환하면 회전 여부와 무관하게 항상 맞고,
		// 루프가 목적지 기준이라 모든 화면 칸이 반드시 채워진다(구멍이 안 생긴다).
		//
		// 비용도 화면 크기에 고정된다. 레벨이 커져도 늘지 않는다.
		rowScratch.resize(screenSize.x);

		if (camera.IsRotationBlending())
		{
			// CameraManager::ScreenToWorld를 칸마다 부르면 안 된다.
			// 그 함수는 호출할 때마다 cosf/sinf를 다시 계산해서,
			// 하필 회전 보간 중에만 프레임당 수만 번의 초월함수 호출이 된다.
			// 여기서 프레임당 한 번만 구해 자유 함수에 넘긴다.
			const float radians = camera.GetViewAngleDegrees() * (3.14159265358979323846f / 180.0f);
			const float viewCos = ::cosf(radians);
			const float viewSin = ::sinf(radians);

			for (int screenY = 0; screenY < screenSize.y; ++screenY)
			{
				for (int screenX = 0; screenX < screenSize.x; ++screenX)
				{
					// ViewScreenToWorldF가 내부에서 sin 부호를 뒤집으므로 그대로 넘긴다.
					const Vector2 world = ViewScreenToWorldF(
						Vector2(screenX, screenY), center, half, viewCos, viewSin);

					rowScratch[screenX] = levelMap->GetCell(world.x, world.y);
				}

				SubmitRow(screenY, screenSize.x);
			}
		}
		else
		{
			// 정지 상태에서는 칸마다 변환할 필요가 아예 없다.
			//
			// ViewScreenToWorld(p) = center + Rotate90(p - half, 4-k) 이므로
			// screenX를 1 늘리면 월드 좌표는 항상 같은 단위 벡터만큼 움직인다.
			// 그래서 줄 시작점만 변환하고 안쪽은 덧셈으로 간다.
			const int quarterTurns = camera.GetViewQuarterTurns();
			const int inverseTurns = (4 - (((quarterTurns % 4) + 4) % 4)) % 4;
			const Vector2 stepX = Rotate90(Vector2(1, 0), inverseTurns);

			for (int screenY = 0; screenY < screenSize.y; ++screenY)
			{
				Vector2 world = ViewScreenToWorld(Vector2(0, screenY), center, half, quarterTurns);

				for (int screenX = 0; screenX < screenSize.x; ++screenX)
				{
					rowScratch[screenX] = levelMap->GetCell(world.x, world.y);
					world = world + stepX;
				}

				SubmitRow(screenY, screenSize.x);
			}
		}
	}

	super::Draw();
}

NAME_SPACE_END
