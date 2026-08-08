#pragma once

NAME_SPACE_BEGIN(Craft)

enum class CRAFT_API Color : WORD /*ushort = 65535*/
{
	Red =			FOREGROUND_RED,
	Green =			FOREGROUND_GREEN,
	Blue =			FOREGROUND_BLUE,
	Yellow =		Red | Green,
	Cyan =			Green | Blue,
	Purple =		Red | Blue,
	White =			Red | Green |Blue,
	BrightWhite =	White | FOREGROUND_INTENSITY,
};


NAME_SPACE_END