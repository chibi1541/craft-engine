#include "pch.h"
#include "ScreenBuffer.h"
#include "Math/Palette.h"
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

	CONSOLE_FONT_INFOEX fontInfo = {};
	fontInfo.cbSize = sizeof(CONSOLE_FONT_INFOEX);   // 반드시 설정 - 빠뜨리면 무조건 실패
	fontInfo.nFont = 0;
	fontInfo.dwFontSize = { 8, 8 };                  // 가로 8px, 세로 8px (정사각 셀)
	fontInfo.FontFamily = FF_DONTCARE;
	fontInfo.FontWeight = FW_NORMAL;
	wcscpy_s(fontInfo.FaceName, L"Terminal");

	BOOL result = SetCurrentConsoleFontEx(buffer, FALSE, &fontInfo);
	ASSERT_CRASH(result == TRUE);

	// 16색 슬롯의 RGB를 이 버퍼에 적용
	ApplyPalette();

	// 콘솔 창은 물리적 화면보다 클 수 없다.
	// 요청한 크기가 현재 폰트 기준으로 화면을 넘으면 들어가는 크기로 줄인다.
	// (여기서 안 줄이면 아래 SetConsoleWindowInfo가 실패한다)
	// 화면 해상도/배율/폰트에 따라 달라지는 값이라, 설정값을 그대로 믿으면 안 됨.
	COORD largest = GetLargestConsoleWindowSize(buffer);

	if (size.x > largest.X)
	{
		size.x = largest.X;
	}

	if (size.y > largest.Y)
	{
		size.y = largest.Y;
	}

	// 콘솔 창은 스크린 버퍼보다 클 수 없음.
	// 그래서 창을 최소로 줄여 놓고 -> 버퍼를 목표 크기로 키우고 -> 창을 목표 크기로 맞춘다.
	// (창을 먼저 키우면 기본 버퍼 크기를 넘어서면서 SetConsoleWindowInfo가 실패함)
	SMALL_RECT minimalRect = { 0, 0, 1, 1 };
	result = SetConsoleWindowInfo(buffer, TRUE, &minimalRect);
	ASSERT_CRASH(result == TRUE);

	// 화면 버퍼 크기 설정
	result = SetConsoleScreenBufferSize(buffer, size);
	ASSERT_CRASH(result == TRUE);

	// 화면 창 크기 설정
	// 콘솔 창의 Rect 설정(top, left, right, bottom)
	SMALL_RECT rect = { 0, 0, static_cast<short>(size.x - 1), static_cast<short>(size.y - 1) };
	result = SetConsoleWindowInfo(buffer, TRUE, &rect);
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

void ScreenBuffer::ApplyPalette() const
{
	CONSOLE_SCREEN_BUFFER_INFOEX info = {};
	info.cbSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);   // 빠뜨리면 무조건 실패

	BOOL result = GetConsoleScreenBufferInfoEx(buffer, &info);
	ASSERT_CRASH(result == TRUE);

	// 16개 슬롯의 RGB를 교체.
	// Color enum 값이 곧 이 배열의 인덱스다.
	const COLORREF* palette = Palette::GetTable();
	for (int i = 0; i < Palette::ColorCount; ++i)
	{
		info.ColorTable[i] = palette[i];
	}

	// 이 API의 알려진 함정:
	// Get으로 받은 srWindow의 Right/Bottom이 실제보다 1 작게 나온다.
	// 그대로 Set에 넘기면 호출할 때마다 창이 한 칸씩 줄어든다.
	info.srWindow.Right += 1;
	info.srWindow.Bottom += 1;

	result = SetConsoleScreenBufferInfoEx(buffer, &info);
	ASSERT_CRASH(result == TRUE);
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

	// WriteConsoleOutputA는 호출 1회당 64KB 공유 힙을 사용한다.
	// CHAR_INFO가 4바이트라 한 번에 약 16,384셀이 상한이고,
	// MSDN도 "힙 사용량에 따라 최대 크기가 달라진다"고 해서 여유를 둬야 한다.
	// 그래서 화면을 가로 띠(band)로 나눠서 여러 번 나눠 그린다.
	const int maxCellsPerCall = 8192;	// 32KB - 한도의 절반만 사용

	int bandHeight = maxCellsPerCall / size.x;
	if (bandHeight < 1)
	{
		// 폭 자체가 8192를 넘는 극단적인 경우 - 한 줄씩이라도 그린다.
		bandHeight = 1;
	}

	for (int top = 0; top < size.y; top += bandHeight)
	{
		// 이번 띠의 마지막 줄 (마지막 띠는 화면 끝에서 잘림)
		const int next = top + bandHeight;
		const int bottom = (next < size.y ? next : size.y) - 1;

		// 배열은 행 우선(row-major)이라 한 띠는 메모리상 연속 구간이다.
		// 띠 시작 행의 포인터를 넘기고, 그 띠만큼의 크기를 알려준다.
		const CHAR_INFO* const bandStart = charInfo + (top * size.x);
		COORD bandSize = { static_cast<short>(size.x), static_cast<short>(bottom - top + 1) };

		// 화면에서 이 띠가 그려질 영역
		SMALL_RECT rect = {
			/*left*/	0,
			/*top*/		static_cast<short>(top),
			/*right*/	static_cast<short>(size.x - 1),
			/*bottom*/	static_cast<short>(bottom)
		};

		// 콘솔에 CHAR_INFO 타입으로 글자 쓰는 함수
		BOOL result = WriteConsoleOutputA(
			buffer,
			bandStart,
			bandSize,
			Vector2::Zero,
			&rect
		);

		ASSERT_CRASH(result == TRUE);
	}
}


NAME_SPACE_END