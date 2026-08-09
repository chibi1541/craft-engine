#include "pch.h"
#include "Animation.h"
#include "Asset/Image.h"

NAME_SPACE_BEGIN(Craft)

void Animation::AddFrameImage(const WCHAR* frameImage, int raw, int col)
{
	_frameImage.emplace_back(new Image(frameImage, raw, col));
}

float Animation::GetSingleDuration() const
{
	if(_frameImage.size() == 0)
		return 0.0f;

	return _duration / static_cast<float>(_frameImage.size());
}

const Image* Animation::GetAnim(uint16 index) const
{
	ASSERT_CRASH(index < _frameImage.size());

	return _frameImage[index];
}

NAME_SPACE_END


