#include "pch.h"
#include "ScreenBuffer.h"
#include <iostream>

NAME_SPACE_BEGIN(Craft)

ScreenBuffer::ScreenBuffer(const Vector2& screenSize)
	: size(screenSize)
{
	// 콘솔 버퍼 핸들 생성
	
	DWORD dwDesiredAccess =		GENERIC_READ | GENERIC_WRITE;
	DWORD dwShareMode =			FILE_SHARE_READ | FILE_SHARE_WRITE;
	DWORD dwFlags =				CONSOLE_TEXTMODE_BUFFER;
	
	buffer = CreateConsoleScreenBuffer(
		dwDesiredAccess,
		dwShareMode,
		NULL, /*다른 프로세스가 핸들을 상속 할 것인지*/
		dwFlags,
		NULL);

	ASSERT_CRASH(buffer != INVALID_HANDLE_VALUE);

	// 화면 창 크기 설정
	// 콘솔 창의 Rect 설정(top, left, right, bottom)
	SMALL_RECT rect = { 0, 0, static_cast<short>(size.x - 1), static_cast<short>(size.y - 1) };
	BOOL result = SetConsoleWindowInfo(buffer, TRUE, &rect);
	ASSERT_CRASH(result == TRUE);

	// 화면 버퍼 크기 설정
	result = SetConsoleScreenBufferSize(buffer, size);
	ASSERT_CRASH(result == TRUE);

	// 직접 만든 콘솔의 커서 끄기
	CONSOLE_CURSOR_INFO info;
	result = GetConsoleCursorInfo(buffer, &info);
	ASSERT_CRASH(result == TRUE);

	// 커서 안보이게 설정
	info.bVisible = FALSE;
	result = SetConsoleCursorInfo(buffer, &info);
	ASSERT_CRASH(result == TRUE);
}

ScreenBuffer::~ScreenBuffer()
{
	// 콘솔 버퍼 핸들 반환
	CloseHandle(buffer);
}

void ScreenBuffer::Clear() const
{
	// 콘솔 전체를 지우는 함수
	// 공백 문자를 화면 전체에 한 번에 설정

	// 화면에 설정된 글자 수
	DWORD outWrittenCount = 0;
	BOOL result = FillConsoleOutputCharacterA(buffer, ' ', size.x * size.y, Vector2::Zero, &outWrittenCount);
	ASSERT_CRASH(result == TRUE);
}

void ScreenBuffer::Draw(const CHAR_INFO* const charInfo) const
{
	// charInfo는 2차원 배열로 사용

	SMALL_RECT rect = {/*left*/0, /*top*/0, /*right*/static_cast<short>(size.x - 1), /*bottom*/static_cast<short>(size.y - 1) };

	// 콘솔에 CHAR_INFO 타입으로 글자 쓰는 함수
	BOOL result = WriteConsoleOutputA(
		buffer,
		charInfo,
		size,
		Vector2::Zero,
		&rect
	);

	ASSERT_CRASH(result == TRUE);
}


NAME_SPACE_END