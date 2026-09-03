#include "pch.h"
#include "Actor/StaticPropActor.h"

#include "Level/Level.h"
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
	std::shared_ptr<const PropSpriteSet> propSet,
	std::string propName,
	const Vector2& position,
	EFacing facing)
	: Actor("", position)
	, propName(std::move(propName))
	, facing(facing)
	, propSet(std::move(propSet))
	, displaySlot(facing)
{
	// 그릴 글자가 없다. Actor::Draw의 텍스트 경로는 image가 비어 있으면 건너뛴다.
	// 이 액터는 픽셀 스프라이트만 그린다.

	if (nullptr == this->propSet)
	{
		::OutputDebugStringA("[StaticPropActor] prop set is null\n");
		return;
	}

	auto it = this->propSet->find(this->propName);

	if (it == this->propSet->end())
	{
		// 프롭 이름 오타. 이 액터만 안 보이고 나머지는 정상 동작한다.
		::OutputDebugStringA("[StaticPropActor] prop name not found in prop set\n");
		return;
	}

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
	const std::shared_ptr<Level> level = GetOwner();
	const int tileSize = (level != nullptr) ? level->GetTileSize() : DefaultTileSize;

	const int span = (propSprite != nullptr) ? propSprite->GetTileSpan() : 1;
	const int spanInCells = span * tileSize;

	// 타일 영역은 정사각이 아니다. 정사각인 것은 타일 한 칸이고,
	// 오브젝트는 크기만큼 타일을 여러 개 먹는다. 깊이는 언제나 1타일이다.
	//
	// 스팬이 걸리는 축은 카메라가 아니라 facing이 정한다. facing은 월드 값이라
	// 화면을 돌려도 이 영역은 그대로다(이동형 액터의 판정이 뷰에 따라 달라지지 않는다).
	// 기준점의 위치는 슬롯과 무관하다 - 피벗 기본값과 같은 방침이다.
	//
	// 세로 위치는 피벗과 같은 값에서 온다. 기본 피벗이 이미지 하단에서
	// GetFloorOffset만큼 위라, 그려진 이미지의 바닥 행이 곧
	// (기준점.y + GetFloorOffset)이고 타일 영역의 마지막 행도 거기에 맞춘다.
	// 그래야 발밑과 충돌 영역이 겹친다.
	//
	// 스프라이트가 @pivot으로 기본값을 덮었더라도 이 영역은 따라가지 않는다.
	// 충돌 판정이 아트 미세조정에 끌려다니면 안 된다.
	const int floorOffset = PropSprite::GetFloorOffset(tileSize);

	const bool isSideAxis = IsSideFacing(facing);

	const int width = isSideAxis ? tileSize : spanInCells;
	const int height = isSideAxis ? spanInCells : tileSize;

	return Rect(
		position.x - (width / 2),
		position.y + floorOffset - height + 1,
		width,
		height);
}

void StaticPropActor::Draw()
{
	if (!IsActive())
	{
		return;
	}

	// 이름을 못 찾은 액터는 그릴 것이 없다(생성자에서 이미 이유를 남겼다).
	if (propSprite != nullptr)
	{
		const Sprite& sprite = propSprite->GetSprite(displaySlot);

		// 기준점에 놓이는 건 스프라이트의 좌상단이 아니라 피벗이다.
		//
		// ★ 피벗을 월드 좌표에 더하면 안 된다 ★
		// 월드 앵커는 기준점까지만이고, 피벗은 뷰 변환 "뒤에" 화면 공간에서 빼야 한다.
		// 앵커까지 함께 회전시키면 90도에서 그림이 옆으로 샌다(빌보드).
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
