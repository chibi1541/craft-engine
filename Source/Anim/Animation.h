#pragma once

NAME_SPACE_BEGIN(Craft)

class Image;

class CRAFT_API Animation
{
public:
	void AddFrameImage(const WCHAR* frameImage, int raw, int col);
	int GetAnimIndex() const {return _frameImage.size(); }
	void SetDuration(float duration) {_duration = duration;}
	float GetDuration() const {return _duration;}
	float GetSingleDuration() const;
	const Image* GetAnim(uint16 index) const;

private:
	std::vector<Image*>	_frameImage;
	float _duration = 0.f;
};

NAME_SPACE_END