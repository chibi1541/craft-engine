#include "pch.h"
#include "Level/TileMapLevel.h"

#include "Asset/AssetManager.h"
#include "Asset/LevelDataAsset.h"
#include "Actor/StaticPropActor.h"
#include "Asset/LevelLayout.h"
#include "Asset/LevelMap.h"
#include "Asset/PropDataAsset.h"
#include "Camera/CameraManager.h"
#include "Math/SymbolPalette.h"
#include "Math/ViewTransform.h"
#include "Render/RenderLayer.h"
#include "Render/Renderer.h"

#include <cmath>
#include <cstdio>

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

	// 배치는 선택 항목이다. 없으면 지형만 있는 레벨.
	const std::wstring& layoutPath = levelData->FindLayoutPath(levelName);

	if (layoutPath.empty())
	{
		return;
	}

	AssetManager::Get().LoadAsync<LevelLayout>(layoutPath.c_str(),
		[weakSelf](std::shared_ptr<const LevelLayout> loaded)
		{
			const std::shared_ptr<TileMapLevel> self = Cast<TileMapLevel>(weakSelf.lock());

			if (nullptr == self)
			{
				return;
			}

			self->OnLayoutLoaded(std::move(loaded));
		});
}

void TileMapLevel::OnLayoutLoaded(std::shared_ptr<const LevelLayout> loaded)
{
	if (nullptr == loaded || loaded->IsEmpty())
	{
		::OutputDebugStringA("[TileMapLevel] level layout is empty\n");
		return;
	}

	levelLayout = std::move(loaded);

	// 배치 격자를 레벨에 반영한다. 0이면 데이터가 값을 안 정한 것이니 기본값을 유지한다.
	if (levelLayout->GetTileSize() > 0)
	{
		SetTileSize(levelLayout->GetTileSize());
	}

	// 스프라이트 묶음은 여기서 한 번만 로드한다.
	// 액터마다 걸면 같은 파일에 콜백만 배치 개수만큼 쌓인다.
	const std::shared_ptr<const PropDataAsset> propData =
		AssetManager::Get().GetPrimaryAsset<PropDataAsset>("PropData");

	if (nullptr == propData)
	{
		::OutputDebugStringA("[TileMapLevel] PropData primary asset not found\n");
		return;
	}

	const std::wstring& propSetPath = propData->FindPropSetPath(levelLayout->GetPropSetName());

	if (propSetPath.empty())
	{
		::OutputDebugStringA("[TileMapLevel] prop set name not found in PropData\n");
		return;
	}

	std::weak_ptr<Level> weakSelf = weak_from_this();

	AssetManager::Get().LoadAsync<PropSpriteSet>(propSetPath.c_str(),
		[weakSelf](std::shared_ptr<const PropSpriteSet> loaded)
		{
			const std::shared_ptr<TileMapLevel> self = Cast<TileMapLevel>(weakSelf.lock());

			if (nullptr == self)
			{
				return;
			}

			self->OnPropSetLoaded(std::move(loaded));
		});
}

void TileMapLevel::OnPropSetLoaded(std::shared_ptr<const PropSpriteSet> loaded)
{
	if (nullptr == loaded || loaded->empty())
	{
		::OutputDebugStringA("[TileMapLevel] prop sprite set is empty\n");
		return;
	}

	propSet = std::move(loaded);

	if (nullptr == levelLayout)
	{
		return;
	}

	for (const LevelLayout::Placement& placement : levelLayout->GetPlacements())
	{
		SpawnProp(placement.name, placement.tileX, placement.tileY, placement.facing);
	}
}

std::shared_ptr<StaticPropActor> TileMapLevel::SpawnProp(
	const std::string& propName, int tileX, int tileY, EFacing facing)
{
	if (nullptr == propSet)
	{
		return nullptr;
	}

	auto it = propSet->find(propName);

	if (it == propSet->end())
	{
		// 이름 오타. 이 항목만 건너뛰고 나머지는 세운다.
		::OutputDebugStringA("[TileMapLevel] layout refers to an unknown prop name\n");
		return nullptr;
	}

	const int tileSize = GetTileSize();

	// 아트가 그려진 격자와 레벨의 격자가 다르면 피벗이 통째로 어긋난다.
	// 화면만 봐서는 원인을 못 찾으므로 여기서 잡는다.
	ASSERT_CRASH(it->second.GetTileSize() == tileSize);

	// 타일 영역은 배치/충돌 격자가 공유하는 PropTileBounds 하나로 구한다.
	const Rect tileBounds = PropTileBounds(
		tileX, tileY, facing, it->second.GetTileSpan(), tileSize);

	const int width = tileBounds.size.x;
	const int height = tileBounds.size.y;

	// 타일 좌상단 -> 기준점. StaticPropActor::GetTileBounds의 역산이다.
	//
	// 세로는 피벗 규칙에서 나온다 - 타일 영역의 마지막 행이
	// (기준점.y + GetFloorOffset)이므로 되짚으면
	// 기준점.y = 타일 위쪽 + 높이 - GetFloorOffset - 1 이다.
	const int floorOffset = PropSprite::GetFloorOffset(tileSize);

	const Vector2 position(
		tileBounds.GetLeft() + (width / 2),
		tileBounds.GetTop() + height - floorOffset - 1);

	return SpawnActor<StaticPropActor>(propSet, propName, position, facing);
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

void TileMapLevel::BuildCollision() const
{
	if (collisionBuilt)
	{
		return;
	}

	// 셋 다 async 로드. 하나라도 안 왔으면 다음 질의에서 다시 시도한다.
	if (nullptr == levelMap || nullptr == levelLayout || nullptr == propSet)
	{
		return;
	}

	const int w = levelMap->GetWidth();
	const int h = levelMap->GetHeight();

	if (w <= 0 || h <= 0)
	{
		return;
	}

	const int tileSize = GetTileSize();

	std::vector<uint8_t> grid(static_cast<size_t>(w) * h, 0);

	for (const LevelLayout::Placement& placement : levelLayout->GetPlacements())
	{
		const auto it = propSet->find(placement.name);

		if (it == propSet->end())
		{
			continue;	// 이름 오타 - SpawnProp 도 이 항목을 건너뛴다.
		}

		const Rect bounds = PropTileBounds(
			placement.tileX, placement.tileY, placement.facing,
			it->second.GetTileSpan(), tileSize);

		const int x0 = (bounds.GetLeft() < 0) ? 0 : bounds.GetLeft();
		const int y0 = (bounds.GetTop() < 0) ? 0 : bounds.GetTop();
		const int x1 = (bounds.GetRight() >= w) ? (w - 1) : bounds.GetRight();
		const int y1 = (bounds.GetBottom() >= h) ? (h - 1) : bounds.GetBottom();

		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				grid[static_cast<size_t>(y) * w + x] = 1;
			}
		}
	}

	blocked = std::move(grid);
	collisionWidth = w;
	collisionHeight = h;
	collisionBuilt = true;

	char message[96];
	::snprintf(message, sizeof(message),
		"[TileMapLevel] collision grid %d x %d built (%d props)\n",
		w, h, static_cast<int>(levelLayout->GetPlacements().size()));
	::OutputDebugStringA(message);
}

bool TileMapLevel::IsCellBlocked(int cellX, int cellY) const
{
	if (nullptr == levelMap && nullptr == levelLayout)
	{
		// 지형 없는 레벨(테스트 등) - 막는 것 없음.
		return false;
	}

	if (false == collisionBuilt)
	{
		BuildCollision();
	}

	// 로드 중이라 아직 못 구웠으면 막지 않는다. 완료되면 자동으로 유효해진다.
	if (false == collisionBuilt)
	{
		return false;
	}

	// 월드 밖 = 벽 (서버 Level::IsCellBlocked 와 동일).
	if (cellX < 0 || cellY < 0 || cellX >= collisionWidth || cellY >= collisionHeight)
	{
		return true;
	}

	return blocked[static_cast<size_t>(cellY) * collisionWidth + cellX] != 0;
}

NAME_SPACE_END
