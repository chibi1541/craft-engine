#pragma once

NAME_SPACE_BEGIN(Craft)

class Animation;

class AnimationClip
{
public:
	AnimationClip(const Animation* targetAnim);
	~AnimationClip();

	void Play(bool IsLoop, float frameRate = 1.f);
	void End();
	void Update(float deltaTime);

private:
	const Animation* _targetAnim =	nullptr;

	uint16 _curIndex =			0;
	bool _isLoop =				false;
	float _frameRate =			1.f;
	float _elapsedTime =		0.f;
};

NAME_SPACE_END