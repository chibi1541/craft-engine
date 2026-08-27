#include "pch.h"
#include "AnimInstance.h"
#include "Math/SymbolPalette.h"
#include <algorithm>
#include <cmath>

NAME_SPACE_BEGIN(Craft)

void AnimInstance::AddClip(const std::shared_ptr<const AnimationClip>& clip)
{
	// 이름으로 찾을 수 없는 클립은 상태가 지목할 방법이 없으므로 데이터 실수로 본다.
	ASSERT_CRASH(nullptr != clip);
	ASSERT_CRASH(!clip->GetName().empty());

	clipMap[clip->GetName()] = clip;
}

std::shared_ptr<const AnimationClip> AnimInstance::FindClip(const std::string& name) const
{
	auto it = clipMap.find(name);

	if (it == clipMap.end())
	{
		return nullptr;
	}

	return it->second;
}

void AnimInstance::Tick(float deltaTime)
{
	TickLayer(baseLayer, deltaTime);
	TickLayer(overlayLayer, deltaTime);

	Composite();
}

void AnimInstance::TickLayer(AnimLayer& layer, float deltaTime)
{
	// 얼리 아웃 - Overlay를 안 쓰는 액터는 상태 머신이 비어 있다.
	// AnimStateMachine::Evaluate/GetCurrentState가 이미 널로 처리하므로 굳이 막지 않아도
	// 안전하지만, 빈 레이어에서 매 틱 컨텍스트를 만들 이유가 없어 여기서 끝낸다.
	if (layer.stateMachine.IsEmpty())
	{
		return;
	}

	// 1) 이 레이어의 지금 상황을 모아 조건 평가용 컨텍스트를 만든다.
	//    animTime/animFinished/stateTime은 레이어마다 다르므로 공용 블랙보드에 넣지 않는다.
	AnimEvalContext context;
	context.parameters = &parameters;
	context.animTime = layer.player.GetNormalizedTime();
	context.animFinished = layer.player.HasFinished();
	context.stateTime = layer.stateTime;

	// 2) 전이 평가.
	//    시간 전진(3)보다 먼저 하는 게 중요하다.
	//    그래야 논루프 클립이 끝난 프레임에 곧바로 다음 상태의 그림이 나오고,
	//    마지막 프레임이 한 틱 더 남아 보이지 않는다.
	if (layer.stateMachine.Evaluate(context))
	{
		layer.stateTime = 0.0f;
	}

	// 현재 상태의 클립을 적용한다.
	// 매 틱 무조건 호출해도 되는 이유 - AnimationPlayer::Play()는 같은 클립이면 무시한다.
	// 덕분에 "처음 진입할 때 한 번만 재생" 같은 특수 처리가 필요 없다.
	const AnimState* currentState = layer.stateMachine.GetCurrentState();

	if (nullptr != currentState)
	{
		layer.player.Play(FindClip(currentState->clipName));
	}

	// 3) 시간 전진.
	layer.player.Tick(deltaTime);
	layer.stateTime += deltaTime;
}

void AnimInstance::Composite()
{
	compositeBuffer.clear();
	compositeWidth = 0;
	compositeHeight = 0;

	// 1) BaseLayer를 그대로 깐다. BaseLayer는 항상 전신을 담당하는 구조적 바탕이라
	//    예전처럼 "그릴 게 있는 첫 레이어를 찾는" 탐색이 필요 없다.
	//    그 탐색이 있었을 때, 탐색된 레이어가 우연히 바뀌면 마스크를 잃는 버그가 있었다 -
	//    BaseLayer가 항상 바탕인 지금은 그 버그 자체가 성립하지 않는다.
	const Sprite* baseSprite = baseLayer.player.GetCurrentSprite();

	if (nullptr == baseSprite || baseSprite->IsEmpty())
	{
		return;
	}

	compositeWidth = baseSprite->GetWidth();
	compositeHeight = baseSprite->GetHeight();

	// Sprite는 "width칸 + '\n'"이 반복되는 형태로 정규화되어 있으므로 그대로 복사해도 된다.
	compositeBuffer = baseSprite->GetPixelMap();

	const AnimationClip* baseClip = baseLayer.player.GetClip().get();

	compositePivotX = (nullptr != baseClip)
		? baseClip->GetPivotX() : AnimationClip::GetDefaultPivotX(compositeWidth);
	compositePivotY = (nullptr != baseClip)
		? baseClip->GetPivotY() : AnimationClip::GetDefaultPivotY(compositeHeight);

	// 2) BaseLayer 현재 상태가 허용해야만 Overlay를 얹는다.
	// canBlend가 false인 BaseLayer 상태(구르기/사망 등)는 여기서 끝나 BaseLayer 한 장만 남는다.
	const AnimState* baseState = baseLayer.stateMachine.GetCurrentState();
	const bool baseAllowsOverlay = (nullptr == baseState) || baseState->canBlend;

	if (baseAllowsOverlay)
	{
		const Sprite* overlaySprite = overlayLayer.player.GetCurrentSprite();
		const AnimState* overlayState = overlayLayer.stateMachine.GetCurrentState();

		if (nullptr != overlaySprite && !overlaySprite->IsEmpty() && nullptr != overlayState)
		{
			// 같은 캐릭터의 레이어끼리는 크기가 같아야 합성이 성립한다.
			// 다르면 어느 칸을 어디에 겹칠지 정할 방법이 없으므로 데이터 실수로 본다.
			// TODO : 가로 가변 폭을 지원하면 이 가로 검사를 풀고
			//        캔버스 폭을 BaseLayer/Overlay 각각의 [-pivotX, width-pivotX) 합집합으로 잡는다.
			ASSERT_CRASH(overlaySprite->GetWidth() == compositeWidth);
			ASSERT_CRASH(overlaySprite->GetHeight() == compositeHeight);

			// 크기가 같은데 피벗이 다르면 두 클립을 어긋나게 겹쳐야 한다.
			// 아트 정렬 실수를 여기서 잡는다.
			const AnimationClip* overlayClip = overlayLayer.player.GetClip().get();

			if (nullptr != overlayClip)
			{
				ASSERT_CRASH(overlayClip->GetPivotX() == compositePivotX);
				ASSERT_CRASH(overlayClip->GetPivotY() == compositePivotY);
			}

			for (int row = 0; row < compositeHeight; ++row)
			{
				// 이 상태가 담당하지 않는 행은 BaseLayer 그림을 그대로 둔다.
				if (!overlayState->region.Contains(row))
				{
					continue;
				}

				for (int col = 0; col < compositeWidth; ++col)
				{
					const char symbol = overlaySprite->GetPixel(row, col);

					// canBlend가 true(기본값)면 불투명한 칸만 덮는다 - 예전 Overlay 모드.
					// 투명한 칸으로는 BaseLayer가 그대로 비친다.
					//
					// false면 투명한 칸까지 그대로 써서 담당 행을 통째로 가져간다 - 예전 Replace 모드.
					// 이게 없으면 픽셀을 빼는 동작(다리를 드는 등)이 BaseLayer에 남아있는
					// 픽셀에 가려져 화면에 나타나지 않는다.
					if (overlayState->canBlend && symbol == SymbolPalette::TransparentSymbol)
					{
						continue;
					}

					compositeBuffer[overlaySprite->GetPixelIndex(row, col)] = symbol;
				}
			}
		}
	}

	// 3) 좌우 반전은 합성이 다 끝난 뒤 한 번만 한다.
	// 행 단위로 뒤집는 것이라 마스크(행 기준)와는 서로 간섭하지 않는다.
	if (!flipX)
	{
		return;
	}

	for (int row = 0; row < compositeHeight; ++row)
	{
		// 줄 끝의 '\n'은 건드리지 않고 [0, width)만 뒤집는다.
		std::string::iterator lineBegin =
			compositeBuffer.begin() + (static_cast<size_t>(row) * (compositeWidth + 1));

		std::reverse(lineBegin, lineBegin + compositeWidth);
	}
}

void AnimInstance::SetFlipX(bool newFlipX)
{
	if (flipX == newFlipX)
	{
		return;
	}

	flipX = newFlipX;

	// 이번 프레임 합성은 이미 끝났을 수 있다.
	// 다음 Tick까지 기다리지 않고 지금 결과를 뒤집어 둔다.
	if (compositeBuffer.empty() || compositeWidth <= 0)
	{
		return;
	}

	for (int row = 0; row < compositeHeight; ++row)
	{
		std::string::iterator lineBegin =
			compositeBuffer.begin() + (static_cast<size_t>(row) * (compositeWidth + 1));

		std::reverse(lineBegin, lineBegin + compositeWidth);
	}
}

Vector2 AnimInstance::GetCurrentPivotCell() const
{
	// 얼리 아웃 - 그릴 게 없으면 기준점도 없다.
	if (compositeBuffer.empty() || compositeWidth <= 0)
	{
		return Vector2::Zero;
	}

	// 행을 뒤집으면 열 c가 (W-1-c)로 가므로 피벗도 같이 옮겨간다.
	//
	// 피벗이 박스 중심 (W-1)/2와 같으면 뒤집은 값이 자기 자신이라 좌상단이 그대로다.
	// 짝수 너비에서 피벗을 정수로 반올림해 저장했다면 이 두 값이 1 차이가 나고,
	// 방향을 바꿀 때마다 그림이 한 칸씩 튄다. 피벗을 실수로 들고 있는 이유가 이것이다.
	const float pivotX = flipX ? ((compositeWidth - 1) - compositePivotX) : compositePivotX;

	// 반올림은 여기서 한 번만 한다. 정상/반전 양쪽에 같은 함수를 써야
	// 피벗이 박스 중심일 때 두 결과가 정확히 같아진다.
	return Vector2(
		static_cast<int>(::floorf(pivotX + 0.5f)),
		static_cast<int>(::floorf(compositePivotY + 0.5f))
	);
}

NAME_SPACE_END
