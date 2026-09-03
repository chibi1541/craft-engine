#include "pch.h"
#include "Actor/StaticPropActor.h"

#include "Asset/AssetManager.h"
#include "Asset/PropDataAsset.h"
#include "Level/TileMetrics.h"
#include "Math/SymbolPalette.h"
#include "Render/Renderer.h"

#include <cmath>

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 피벗은 실수(짝수 너비의 가운데는 5.5)지만 화면은 셀 격자다.
	// (int) 절단이 아니라 반올림이어야 한다 - ViewTransform의 RoundToCell과 같은 이유.
	int RoundPivot(float value)
	{
		return static_cast<int>(::floorf(value + 0.5f));
	}
}

StaticPropActor::StaticPropActor(
	std::string propSetName,
	std::string propName,
	const Vector2& position,
	EFacing facing)
	: Actor("", position)
	, propSetName(std::move(propSetName))
	, propName(std::move(propName))
	, facing(facing)
	, displaySlot(facing)
{
	// 그릴 글자가 없다. Actor::Draw의 텍스트 경로는 image가 비어 있으면 건너뛴다.
	// 이 액터는 픽셀 스프라이트만 그린다.
}

void StaticPropActor::BeginPlay()
{
	super::BeginPlay();

	// 이름 -> 경로는 PropData가 알고 있다. 스프라이트 파일은 그 경로로 따로 읽는다.
	// (TileMapLevel이 LevelData에서 격자 경로를 찾는 것과 같은 구조)
	const std::shared_ptr<const PropDataAsset> propData =
		AssetManager::Get().GetPrimaryAsset<PropDataAsset>("PropData");

	if (nullptr == propData)
	{
		::OutputDebugStringA("[StaticPropActor] PropData primary asset not found\n");
		return;
	}

	const std::wstring& path = propData->FindPropSetPath(propSetName);

	if (path.empty())
	{
		::OutputDebugStringA("[StaticPropActor] prop set name not found in PropData\n");
		return;
	}

	// 워커가 파싱하고 콜백은 게임 쓰레드(AssetManager::Tick)에서 불린다.
	// 그 사이에 액터가 파괴될 수 있으므로 weak로 잡아 자신의 생존을 확인한다.
	std::weak_ptr<Actor> weakSelf = weak_from_this();

	AssetManager::Get().LoadAsync<PropSpriteSet>(path.c_str(),
		[weakSelf](std::shared_ptr<const PropSpriteSet> loaded)
		{
			const std::shared_ptr<StaticPropActor> self = Cast<StaticPropActor>(weakSelf.lock());

			if (nullptr == self)
			{
				return;
			}

			self->OnPropSetLoaded(std::move(loaded));
		});
}

void StaticPropActor::OnPropSetLoaded(std::shared_ptr<const PropSpriteSet> loaded)
{
	if (nullptr == loaded)
	{
		::OutputDebugStringA("[StaticPropActor] prop set failed to load\n");
		return;
	}

	auto it = loaded->find(propName);

	if (it == loaded->end())
	{
		// 프롭 이름 오타. 이 액터만 안 보이고 나머지는 정상 동작한다.
		::OutputDebugStringA("[StaticPropActor] prop name not found in prop set\n");
		return;
	}

	// 묶음을 먼저 붙들어야 아래 포인터가 유효해진다.
	// (놓으면 AssetManager가 유휴로 보고 30초 뒤에 내린다)
	loadedSet = std::move(loaded);
	propSprite = &it->second;
}

void StaticPropActor::OnViewRotationChanged(int quarterTurns)
{
	// 이 한 줄이 이 클래스의 전부다.
	//
	// facing은 월드 값이라 그대로 두고, 화면에 보여줄 슬롯만 카메라만큼 돌린다.
	// 카메라를 시계로 k번 돌리면 오브젝트는 화면에서 같은 방향으로 k번 돈 것처럼 보인다.
	displaySlot = RotateFacing(facing, quarterTurns);
}

Rect StaticPropActor::GetTileBounds() const
{
	const int span = (propSprite != nullptr) ? propSprite->GetTileSpan() : 1;
	const int sizeInCells = span * PropTileSize;

	// 기준점은 타일 영역의 하단 중앙이다. 그래서 영역은 기준점에서 위로 뻗는다.
	// GetBottom()이 "마지막 셀"이라 높이에서 1을 빼야 기준점 행이 영역의 마지막 행이 된다.
	return Rect(
		position.x - (sizeInCells / 2),
		position.y - sizeInCells + 1,
		sizeInCells,
		sizeInCells);
}

void StaticPropActor::Draw()
{
	if (!IsActive())
	{
		return;
	}

	// 애셋이 아직 안 왔으면 이번 프레임은 건너뛴다. 몇 프레임 뒤에 도착한다.
	if (propSprite != nullptr)
	{
		const Sprite& sprite = propSprite->GetSprite(displaySlot);

		// 기준점에 놓이는 건 스프라이트의 좌상단이 아니라 피벗이다.
		//
		// ★ 피벗을 월드 좌표에 더하면 안 된다 ★
		// 월드 앵커는 기준점까지만이고, 피벗은 뷰 변환 "뒤에" 화면 공간에서 빼야 한다.
		// 앵커까지 함께 회전시키면 90°에서 그림이 옆으로 샌다(빌보드).
		// SubmitPixelsWorld의 screenPixelOffset이 정확히 이걸 위해 있는 인자다.
		const Vector2 pivotOffset(
			RoundPivot(propSprite->GetPivotX(displaySlot)),
			RoundPivot(propSprite->GetPivotY(displaySlot)));

		Renderer::Get().SubmitPixelsWorld(
			sprite.GetPixelMap(),
			SymbolPalette::GetTable(),
			position,
			sortingOrder,
			SymbolPalette::TransparentSymbol,
			1,
			1,
			std::nullopt,
			Vector2(-pivotOffset.x, -pivotOffset.y));
	}

	// 컴포넌트 그리기 체인. 지금은 붙는 컴포넌트가 없지만 규약을 지킨다.
	super::Draw();
}

NAME_SPACE_END
