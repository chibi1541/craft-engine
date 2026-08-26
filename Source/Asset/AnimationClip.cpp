#include "pch.h"
#include "AnimationClip.h"

NAME_SPACE_BEGIN(Craft)

AnimationClip::AnimationClip(
	const std::string& name,
	const std::vector<Sprite>& frames,
	float framesPerSecond,
	bool isLooping)
	: name(name), frames(frames), isLooping(isLooping)
{
	// 0 이하의 fps는 0 나누기가 되므로 데이터 실수로 보고 바로 잡는다.
	ASSERT_CRASH(framesPerSecond > 0.0f);

	frameDuration = 1.0f / framesPerSecond;
}

const Sprite& AnimationClip::GetFrame(int index) const
{
	// 예외 처리 - 프레임이 하나도 없는 클립.
	// 호출자가 매번 null 검사를 하지 않아도 되도록 빈 스프라이트를 돌려준다.
	// (빈 스프라이트는 SpriteAnimatorComponent::Draw()에서 걸러진다)
	if (frames.empty())
	{
		static const Sprite emptySprite;
		return emptySprite;
	}

	// 범위를 벗어난 인덱스는 양 끝으로 잘라낸다.
	if (index < 0)
	{
		index = 0;
	}
	else if (index >= GetFrameCount())
	{
		index = GetFrameCount() - 1;
	}

	return frames[index];
}

NAME_SPACE_END
