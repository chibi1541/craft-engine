#include "pch.h"
#include "AnimationPlayer.h"

NAME_SPACE_BEGIN(Craft)

void AnimationPlayer::Play(const std::shared_ptr<const AnimationClip>& newClip, bool forceRestart)
{
	// 같은 클립을 다시 지정한 경우는 무시한다.
	// 상태 머신이 매 틱 같은 클립을 지정해도 재생이 끊기지 않게 하는 장치.
	if (clip == newClip && !forceRestart)
	{
		return;
	}

	clip = newClip;

	Reset();
}

void AnimationPlayer::Tick(float deltaTime)
{
	// 얼리 아웃.
	// 클립이 없거나, 한 장짜리라 넘길 게 없거나, 논루프 재생이 이미 끝났으면 할 일이 없다.
	if (nullptr == clip)
	{
		return;
	}

	if (clip->GetFrameCount() <= 1 || hasFinished)
	{
		return;
	}

	elapsedTime += deltaTime * playRate;

	const float frameDuration = clip->GetFrameDuration();

	// if가 아니라 while인 이유.
	// 프레임 지속 시간이 한 틱보다 짧으면(빠른 애니 or 프레임 드랍) 한 틱에 여러 장을 넘겨야 한다.
	// 남은 시간을 빼면서 도는 방식이라 프레임레이트가 달라져도 재생 속도가 유지된다.
	// (TestActor의 moveAmount 누적 처리와 같은 패턴)
	while (elapsedTime >= frameDuration)
	{
		elapsedTime -= frameDuration;
		++currentFrameIndex;

		// 아직 클립 안쪽이면 계속 진행.
		if (currentFrameIndex < clip->GetFrameCount())
		{
			// 건너뛴 프레임을 하나도 빠뜨리지 않고 기록한다.
			// 저프레임에서 이 루프가 여러 번 도는데, 마지막 것만 남기면
			// 중간 프레임에 걸린 노티파이(타격 판정 등)가 통째로 씹힌다.
			framesEnteredThisTick.emplace_back(currentFrameIndex);

			continue;
		}

		if (clip->IsLooping())
		{
			currentFrameIndex = 0;

			// 한 바퀴 돌아 0번으로 돌아온 것도 "진입"이다. 매 바퀴 다시 울려야 한다.
			framesEnteredThisTick.emplace_back(currentFrameIndex);

			continue;
		}

		// 논루프 클립은 마지막 장에서 멈춘다.
		// 마지막 장에는 이미 이전 반복에서 진입했으므로 여기서 또 기록하지 않는다.
		currentFrameIndex = clip->GetFrameCount() - 1;
		hasFinished = true;
		hasJustFinished = true;
		elapsedTime = 0.0f;

		break;
	}
}

void AnimationPlayer::ClearFrameEvents()
{
	framesEnteredThisTick.clear();
	hasJustFinished = false;
}

void AnimationPlayer::Reset()
{
	elapsedTime = 0.0f;
	currentFrameIndex = 0;
	hasFinished = false;

	// 0번 프레임에 "진입"한 것으로 친다.
	// 그래야 클립을 처음 틀거나 다시 틀 때 0번 노티파이가 울린다.
	// (Tick의 while 루프는 1번 프레임부터 기록하므로 여기서 안 넣으면 0번은 영영 안 울린다)
	framesEnteredThisTick.emplace_back(0);
}

const Sprite* AnimationPlayer::GetCurrentSprite() const
{
	// 예외 처리 - 재생할 게 없으면 그릴 것도 없다.
	if (nullptr == clip || clip->GetFrameCount() <= 0)
	{
		return nullptr;
	}

	return &clip->GetFrame(currentFrameIndex);
}

float AnimationPlayer::GetNormalizedTime() const
{
	// 예외 처리.
	if (nullptr == clip || clip->GetFrameCount() <= 0)
	{
		return 0.0f;
	}

	const float duration = clip->GetDuration();

	if (duration <= 0.0f)
	{
		return 0.0f;
	}

	// 지나간 프레임들 + 현재 프레임에서 흐른 시간.
	const float playedTime = (currentFrameIndex * clip->GetFrameDuration()) + elapsedTime;

	return playedTime / duration;
}

NAME_SPACE_END
