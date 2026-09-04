#include "pch.h"
#include "AnimInstance.h"
#include "Math/SymbolPalette.h"
#include <algorithm>
#include <cmath>

NAME_SPACE_BEGIN(Craft)

namespace
{
	// EFacingSlotSpec -> 이 클립이 들어갈 슬롯.
	// Side는 "측면 한 장으로 좌우를 덮는다"는 선언이라 아트가 그려진 쪽(Right)에 들어가고,
	// 반대편은 폴백이 미러로 채운다.
	bool ToFacingSlot(EFacingSlotSpec spec, OUT EFacing& outFacing)
	{
		switch (spec)
		{
		case EFacingSlotSpec::Up:    outFacing = EFacing::Up;    return true;
		case EFacingSlotSpec::Right: outFacing = EFacing::Right; return true;
		case EFacingSlotSpec::Down:  outFacing = EFacing::Down;  return true;
		case EFacingSlotSpec::Left:  outFacing = EFacing::Left;  return true;
		case EFacingSlotSpec::Side:  outFacing = EFacing::Right; return true;
		default:                     return false;   // None - 방향 없는 클립
		}
	}

	// 폴백의 마지막 수단으로 원본을 고르는 순서.
	//
	// Right가 먼저인 이유 - 방향 없는 아트가 들어가는 자리라 가장 자주 채워져 있다.
	// 그 다음이 Down인 것은 탑다운에서 정면이 가장 자주 보이고 아트도 대개 정면부터 그려서다.
	const EFacing FallbackSourceOrder[FacingCount] =
	{
		EFacing::Right, EFacing::Down, EFacing::Up, EFacing::Left
	};
}

void AnimInstance::AddClip(const std::shared_ptr<const AnimationClip>& clip)
{
	// 이름으로 찾을 수 없는 클립은 상태가 지목할 방법이 없으므로 데이터 실수로 본다.
	ASSERT_CRASH(nullptr != clip);
	ASSERT_CRASH(!clip->GetName().empty());

	clipMap[clip->GetName()] = clip;

	// 방향 테이블은 다음 조회 때 다시 만든다.
	clipsDirty = true;
}

bool AnimInstance::HasClip(const std::string& name) const
{
	if (clipMap.find(name) != clipMap.end())
	{
		return true;
	}

	if (clipsDirty)
	{
		FinalizeClips();
	}

	return directionalClips.find(name) != directionalClips.end();
}

std::shared_ptr<const AnimationClip> AnimInstance::FindClip(const std::string& name) const
{
	// 등록 이름으로 직접 찾는 경로가 먼저다. PlayClip이 방향 변형을 콕 집어 틀 수 있어야 한다.
	auto it = clipMap.find(name);

	if (it != clipMap.end())
	{
		return it->second;
	}

	// 논리 이름이면 지금 보고 있는 방향의 슬롯을 돌려준다.
	const DirectionalClipSlot* slot = ResolveSlot(name);

	if (nullptr == slot)
	{
		return nullptr;
	}

	return slot->clip;
}

void AnimInstance::SetFacing(EFacing displaySlot)
{
	facing = displaySlot;

	// 한 번이라도 불렸으면 이 액터의 flipX는 이제 슬롯이 정한다.
	facingDriven = true;
}

const DirectionalClipSlot* AnimInstance::ResolveSlot(const std::string& logicalName) const
{
	if (clipsDirty)
	{
		FinalizeClips();
	}

	auto it = directionalClips.find(logicalName);

	if (it == directionalClips.end())
	{
		return nullptr;
	}

	return &it->second.slots[ToFacingIndex(facing)];
}

void AnimInstance::FinalizeClips() const
{
	directionalClips.clear();
	clipsDirty = false;

	// 1) 논리 이름별로 모은다. 선언된 슬롯에 그대로 꽂는다.
	for (const auto& entry : clipMap)
	{
		const std::shared_ptr<const AnimationClip>& clip = entry.second;

		if (nullptr == clip)
		{
			continue;
		}

		DirectionalClipSet& set = directionalClips[clip->GetLogicalName()];

		EFacing slotFacing = EFacing::Up;

		if (!ToFacingSlot(clip->GetFacingSpec(), slotFacing))
		{
			// 방향 없는 클립. 같은 이름에 두 장이 오면 어느 쪽이 이길지가
			// unordered_map 순회 순서에 달리게 된다 - 데이터 실수로 본다.
			ASSERT_CRASH(nullptr == set.omniClip);

			set.omniClip = clip;

			continue;
		}

		const int slot = ToFacingIndex(slotFacing);

		// 같은 슬롯이 두 번. 로더가 등록 이름으로 이미 걸러주지만,
		// AddClip을 직접 부르는 경로(코드로 만든 클립)도 있어서 여기서 한 번 더 본다.
		ASSERT_CRASH(!set.authored[slot]);

		set.slots[slot].clip = clip;
		set.slots[slot].mirrored = false;
		set.authored[slot] = true;
	}

	// 2) 세트마다 빈 슬롯을 채운다.
	for (auto& entry : directionalClips)
	{
		DirectionalClipSet& set = entry.second;

		// 방향 없는 클립이 있으면 그것으로 끝이다.
		//
		// 방향 선언과 섞어 쓰면 무엇이 이겨야 하는지 규칙이 없다. 데이터 실수다.
		if (nullptr != set.omniClip)
		{
			for (int index = 0; index < FacingCount; ++index)
			{
				ASSERT_CRASH(!set.authored[index]);

				set.slots[index].clip = set.omniClip;

				// 아트는 오른쪽을 보고 그려져 있다는 관습(flipX==false가 그려진 방향).
				// 그래서 왼쪽 슬롯만 뒤집는다 - 이 규칙이 SetFlipX(inputDirection.x < 0)로
				// 손수 뒤집던 예전 동작을 그대로 재현한다.
				set.slots[index].mirrored = (static_cast<EFacing>(index) == EFacing::Left);
			}

			continue;
		}

		// 측면끼리 미러. ★ 진짜로 선언된 슬롯에서만 ★
		// 폴백으로 들어온 정면 그림을 다시 뒤집으면 얼굴이 반대편에 붙는다.
		const int leftSlot = ToFacingIndex(EFacing::Left);
		const int rightSlot = ToFacingIndex(EFacing::Right);

		if (!set.authored[leftSlot] && set.authored[rightSlot])
		{
			set.slots[leftSlot].clip = set.slots[rightSlot].clip;
			set.slots[leftSlot].mirrored = true;
		}
		else if (!set.authored[rightSlot] && set.authored[leftSlot])
		{
			set.slots[rightSlot].clip = set.slots[leftSlot].clip;
			set.slots[rightSlot].mirrored = true;
		}

		// 앞뒤 폴백. 뒤집지 않는다 - 앞모습으로 뒷모습을 때우는 것이지 거울이 아니다.
		const int upSlot = ToFacingIndex(EFacing::Up);
		const int downSlot = ToFacingIndex(EFacing::Down);

		if (nullptr == set.slots[upSlot].clip && nullptr != set.slots[downSlot].clip)
		{
			set.slots[upSlot] = set.slots[downSlot];
			set.slots[upSlot].mirrored = false;
		}
		else if (nullptr == set.slots[downSlot].clip && nullptr != set.slots[upSlot].clip)
		{
			set.slots[downSlot] = set.slots[upSlot];
			set.slots[downSlot].mirrored = false;
		}

		// 남은 자리는 채워진 아무 슬롯으로. 축이 다른 폴백이라 뒤집지 않는다.
		const DirectionalClipSlot* anyFilled = nullptr;

		for (int index = 0; index < FacingCount; ++index)
		{
			const int candidate = ToFacingIndex(FallbackSourceOrder[index]);

			if (nullptr != set.slots[candidate].clip)
			{
				anyFilled = &set.slots[candidate];

				break;
			}
		}

		// 클립이 하나도 없는 세트는 만들어지지 않는다(위 루프가 최소 한 장은 넣는다).
		ASSERT_CRASH(nullptr != anyFilled);

		for (int index = 0; index < FacingCount; ++index)
		{
			if (nullptr != set.slots[index].clip)
			{
				continue;
			}

			set.slots[index].clip = anyFilled->clip;
			set.slots[index].mirrored = false;
		}

		// 여기까지 왔으면 네 슬롯이 전부 차 있어야 한다. 런타임에 빈 슬롯 분기가 없는 근거다.
		for (int index = 0; index < FacingCount; ++index)
		{
			ASSERT_CRASH(nullptr != set.slots[index].clip);
		}
	}
}

void AnimInstance::Tick(float deltaTime)
{
	// 노티파이 큐는 프레임 단위 수명이다. 매 틱 여기서 비운다.
	notifyQueue.clear();

	TickLayer(baseLayer, deltaTime);
	TickLayer(overlayLayer, deltaTime);

	// 재생이 다 끝난 뒤에 수집한다 - BaseLayer의 현재 상태가 정해져야
	// Overlay를 내보낼지 말지 판단할 수 있기 때문이다.
	CollectNotifies(baseLayer, false);

	// 화면에서 가려진 Overlay는 이벤트도 내지 않는다.
	// 구르기 중에 하체 Walk 클립이 녹음으로 돌아도 발소리가 나면 안 된다.
	if (AllowsOverlay())
	{
		CollectNotifies(overlayLayer, true);
	}

	Composite();
}

void AnimInstance::TickLayer(AnimLayer& layer, float deltaTime)
{
	// 프레임 경계를 여기서 긋는다.
	//
	// AnimationPlayer::Tick 안에서 비우지 않는 이유 - 바로 아래에서 Play가 클립을 바꾸면
	// Reset이 "0번 진입"을 기록하는데, Tick이 맨 앞에서 비우면 그게 지워진다.
	// 상태 머신이 비어 있어 아래에서 얼리 아웃하더라도 비우기는 반드시 일어나야 한다.
	layer.player.ClearFrameEvents();

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
		// 상태는 논리 이름만 안다. 어느 방향 그림을 쓸지는 여기서 정해진다.
		const DirectionalClipSlot* slot = ResolveSlot(currentState->clipName);

		if (nullptr != slot)
		{
			// 상태는 그대로인데 클립 포인터만 달라졌다 = 방향만 바뀐 것.
			// Play를 쓰면 0프레임으로 되감겨서 걷는 중에 커서를 돌릴 때마다 걸음이 튄다.
			if (currentState->clipName == layer.playingLogicalClip
				&& slot->clip != layer.player.GetClip())
			{
				layer.player.RetargetClip(slot->clip);
			}
			else
			{
				layer.player.Play(slot->clip);
			}

			// 방향을 쓰는 액터에서 flipX는 게임플레이 입력이 아니라 "고른 슬롯의 결과"다.
			//
			// BaseLayer만 정한다 - 반전은 합성이 끝난 뒤 한 번뿐이라
			// Overlay가 따로 정할 자리가 없다(정하게 두면 마지막에 틱된 쪽이 이긴다).
			//
			// 공개 SetFlipX가 아니라 멤버에 직접 넣는다. SetFlipX는 지난 프레임 버퍼를
			// 즉시 뒤집는데, 바로 뒤 Composite()가 어차피 처음부터 다시 만든다.
			if (facingDriven && (&layer == &baseLayer))
			{
				flipX = slot->mirrored;
			}
		}

		layer.playingLogicalClip = currentState->clipName;
	}

	// 3) 시간 전진.
	layer.player.Tick(deltaTime);
	layer.stateTime += deltaTime;
}

bool AnimInstance::AllowsOverlay() const
{
	// canBlend가 false인 BaseLayer 상태(구르기/사망 등)는 Overlay를 통째로 가린다.
	// 상태가 아예 없으면(상태 머신을 안 쓰는 액터) 막을 이유가 없으므로 허용한다.
	const AnimState* baseState = baseLayer.stateMachine.GetCurrentState();

	return (nullptr == baseState) || baseState->canBlend;
}

void AnimInstance::CollectNotifies(const AnimLayer& layer, bool isOverlay)
{
	const AnimationClip* clip = layer.player.GetClip().get();

	// 얼리 아웃 - 노티파이가 하나도 없는 클립이 대부분이다.
	if (nullptr == clip || !clip->HasNotifies())
	{
		return;
	}

	const std::vector<AnimNotify>& notifies = clip->GetNotifies();
	const std::vector<int>& framesEntered = layer.player.GetFramesEnteredThisTick();
	const bool hasJustFinished = layer.player.HasJustFinished();

	for (const AnimNotify& notify : notifies)
	{
		if (notify.fireOnFinish)
		{
			if (!hasJustFinished)
			{
				continue;
			}

			notifyQueue.emplace_back(AnimNotifyEvent{ notify.name, clip->GetName(), isOverlay });

			continue;
		}

		// 이번 틱에 그 프레임으로 진입했는지 본다.
		// 한 틱에 여러 장을 건너뛰었다면 목록에 전부 들어있으므로 중간 것도 놓치지 않는다.
		for (int enteredFrame : framesEntered)
		{
			if (enteredFrame != notify.frameIndex)
			{
				continue;
			}

			notifyQueue.emplace_back(AnimNotifyEvent{ notify.name, clip->GetName(), isOverlay });
		}
	}
}

bool AnimInstance::HasNotify(const std::string& name) const
{
	for (const AnimNotifyEvent& notifyEvent : notifyQueue)
	{
		if (notifyEvent.name == name)
		{
			return true;
		}
	}

	return false;
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
	//
	// 노티파이 수집도 정확히 같은 판단을 쓴다(AnimInstance::Tick 참고).
	// 두 곳이 따로 조건을 들고 있으면 한쪽만 바뀌었을 때 화면과 이벤트가 어긋난다.
	if (AllowsOverlay())
	{
		const Sprite* overlaySprite = overlayLayer.player.GetCurrentSprite();
		const AnimState* overlayState = overlayLayer.stateMachine.GetCurrentState();

		if (nullptr != overlaySprite && !overlaySprite->IsEmpty() && nullptr != overlayState)
		{
			// 높이는 캐릭터별로 고정이라 그대로 같아야 한다(가변은 폭만).
			ASSERT_CRASH(overlaySprite->GetHeight() == compositeHeight);

			const AnimationClip* overlayClip = overlayLayer.player.GetClip().get();

			const float overlayPivotX = (nullptr != overlayClip)
				? overlayClip->GetPivotX() : AnimationClip::GetDefaultPivotX(overlaySprite->GetWidth());

			// 세로 피벗은 여전히 일치해야 한다 - 높이가 고정이라 어긋나면 아트 정렬 실수다.
			if (nullptr != overlayClip)
			{
				ASSERT_CRASH(overlayClip->GetPivotY() == compositePivotY);
			}

			// 피벗이 가리키는 지점(발밑 등)이 같은 세계 좌표를 가리키도록 BaseLayer 좌표계로 옮긴다.
			// BaseLayer가 Overlay보다 넓은 클립(Attack 등)이면 이 오프셋만큼 안쪽으로 들어가서
			// 자리 잡고, Overlay가 닿지 않는 양 끝은 처음에 복사해 둔 BaseLayer 그림이 그대로 남는다.
			//
			// 정수 칸으로 안 맞아떨어지면(너비 홀짝이 서로 다름) 가장 가까운 칸으로 반올림한다 -
			// 최대 반 칸 오차가 생길 수 있지만 크래시시키지 않는다.
			const int columnOffset =
				static_cast<int>(::floorf((compositePivotX - overlayPivotX) + 0.5f));

			for (int row = 0; row < compositeHeight; ++row)
			{
				// 이 상태가 담당하지 않는 행은 BaseLayer 그림을 그대로 둔다.
				if (!overlayState->region.Contains(row))
				{
					continue;
				}

				for (int overlayCol = 0; overlayCol < overlaySprite->GetWidth(); ++overlayCol)
				{
					const int baseCol = overlayCol + columnOffset;

					// BaseLayer 캔버스 밖으로 나가면 잘라낸다 - Overlay가 BaseLayer보다
					// 넓어서 양쪽으로 넘치는 경우, 넘친 열은 그냥 버려진다.
					if (baseCol < 0 || baseCol >= compositeWidth)
					{
						continue;
					}

					const char symbol = overlaySprite->GetPixel(row, overlayCol);

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

					// 쓰기는 compositeBuffer(=BaseLayer)의 stride를 써야 한다.
					// overlaySprite::GetPixelIndex를 그대로 쓰면 폭이 다를 때 엉뚱한 칸에 쓰게 된다.
					compositeBuffer[row * (compositeWidth + 1) + baseCol] = symbol;
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
