#pragma once

#include "Math/Vector2.h"

NAME_SPACE_BEGIN(Craft)

class ScreenBuffer
{
public:
	ScreenBuffer(const Vector2& screenSize);
	~ScreenBuffer();

	// 콘솔 초기화 - 화면 지우기
	void Clear() const;

	void Draw(const CHAR_INFO* const charInfo) const;

	// Getter
	inline HANDLE GetBuffer() const { return buffer; }

	// 콘솔 화면 펴버 핸들
	HANDLE buffer = nullptr;

	// 화면 크기
	Vector2 size;
};

NAME_SPACE_END