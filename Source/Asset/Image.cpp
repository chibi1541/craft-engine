#include "pch.h"
#include "image.h"

NAME_SPACE_BEGIN(Craft)

Image::Image(const WCHAR* image, uint16 raw, uint16 col)
	:_image(image), _raw(raw), _col(col)
{
	
}

NAME_SPACE_END