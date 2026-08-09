#include "pch.h"
#include "AnimationClip.h"
#include "Animation.h"

NAME_SPACE_BEGIN(Craft)

AnimationClip::AnimationClip(const Animation* targetAnim)
	: _targetAnim(targetAnim)
{
	
}

AnimationClip::~AnimationClip()
{
	if (_targetAnim)
	{
		delete _targetAnim;
	}
}

void AnimationClip::Play(bool IsLoop, float frameRate)
{
	_curIndex = 0;
	_isLoop = IsLoop;
	_frameRate = frameRate;
	_elapsedTime = 0.f;
}

void AnimationClip::End()
{
}

void AnimationClip::Update(float deltaTime)
{
	_elapsedTime += deltaTime;
	if(_elapsedTime >= _targetAnim->GetDuration())
	{
		if(_isLoop)
		{
			_elapsedTime = 0.f;
		}
		else
		{
			End();
			return;
		}
	}

	_curIndex = static_cast<uint16>(_elapsedTime / _targetAnim->GetSingleDuration());
}

NAME_SPACE_END