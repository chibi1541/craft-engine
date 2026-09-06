#include "pch.h"
#include "ScreenBuffer.h"
#include "Math/Palette.h"
#include <iostream>
#include <cstdio>

NAME_SPACE_BEGIN(Craft)

ScreenBuffer::ScreenBuffer(const Vector2& screenSize, const ConsoleFontDesc& fontDesc)
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

	// ★ 폰트/크기 설정보다 먼저 이 버퍼를 활성화해야 한다 ★
	//
	// 폰트는 스크린 버퍼가 아니라 "콘솔 창"의 속성이다. 비활성 버퍼에
	// SetCurrentConsoleFontEx를 부르면 TRUE를 반환하면서 아무 일도 일어나지 않는다.
	// (실측: 비활성 상태에서 8x8을 요청해도 셀은 콘솔 기본값 8x16 그대로,
	//        활성화한 뒤 같은 호출을 하면 8x8이 그대로 적용됨)
	// 이걸 놓치면 폰트 이름이든 크기든 무엇을 넣어도 화면이 전혀 안 바뀐다.
	BOOL activated = SetConsoleActiveScreenBuffer(buffer);
	ASSERT_CRASH(activated == TRUE);

	// 폰트를 적용하고 "실제로 적용된" 셀 크기를 확정한다.
	// 최대 칸 수가 이 값에 따라 정해지므로 크기 계산보다 먼저다.
	cellPixelSize = ApplyFont(fontDesc);

	// 화면에 들어가는 크기로 줄여서 버퍼/창을 잡는다.
	ResizeToFit();

	// 16색 슬롯의 RGB를 이 버퍼에 적용.
	//
	// 크기를 확정한 "뒤"에 부른다. ApplyPalette은 내부에서 srWindow를 건드리므로
	// 리사이즈 도중에 끼어들면 창 상태가 어긋난다.
	ApplyPalette();

	// 직접 만든 콘솔의 커서 끄기
	CONSOLE_CURSOR_INFO info;
	BOOL result = GetConsoleCursorInfo(buffer, &info);
	ASSERT_CRASH(result == TRUE);

	// 커서 안보이게 설정
	info.bVisible = FALSE;
	result = SetConsoleCursorInfo(buffer, &info);
	ASSERT_CRASH(result == TRUE);
}

Vector2 ScreenBuffer::ApplyFont(const ConsoleFontDesc& fontDesc) const
{
	CONSOLE_FONT_INFOEX fontInfo = {};
	fontInfo.cbSize = sizeof(CONSOLE_FONT_INFOEX);   // 반드시 설정 - 빠뜨리면 무조건 실패
	fontInfo.nFont = 0;
	fontInfo.dwFontSize = {
		static_cast<SHORT>(fontDesc.width),
		static_cast<SHORT>(fontDesc.height)
	};
	fontInfo.FontWeight = FW_NORMAL;

	// FF_DONTCARE(0)로 둔다.
	//
	// 실측 결과 래스터/TrueType 모두 이 값에서 정상 동작한다. 반대로
	// FF_MODERN|TMPF_TRUETYPE(54)를 넣으면 래스터 "Terminal" 8x8 요청이
	// 6x8로 뭉개진다. 폰트 종류는 FaceName으로 결정하게 두는 편이 낫다.
	fontInfo.FontFamily = FF_DONTCARE;

	wcscpy_s(fontInfo.FaceName, fontDesc.faceName);

	// bMaximumWindow는 FALSE.
	// TRUE는 "최대 창 크기 기준으로 폰트를 잡으라"는 뜻이라, 뒤이어 부르는
	// GetLargestConsoleWindowSize / SetConsoleWindowInfo와 기준이 어긋난다.
	BOOL result = SetCurrentConsoleFontEx(buffer, FALSE, &fontInfo);
	ASSERT_CRASH(result == TRUE);

	// ★ 이 되읽기가 핵심 ★
	// SetCurrentConsoleFontEx는 요청한 폰트/크기가 그대로 적용되지 않고 다른 것으로
	// 대체돼도 TRUE를 반환한다. 되읽지 않으면 "8x8을 요청했으니 8x8이겠지"라고
	// 착각한 채로 최대 칸 수 계산이 전부 어긋난다.
	CONSOLE_FONT_INFOEX appliedInfo = {};
	appliedInfo.cbSize = sizeof(CONSOLE_FONT_INFOEX);

	result = GetCurrentConsoleFontEx(buffer, FALSE, &appliedInfo);
	ASSERT_CRASH(result == TRUE);

	const Vector2 applied(appliedInfo.dwFontSize.X, appliedInfo.dwFontSize.Y);

	// 항상 남긴다. 조용히 넘어가면 폰트가 대체됐는지 알 방법이 없고,
	// 대체되지 않았다는 사실도 똑같이 확인할 수 있어야 한다.
	// (게임이 콘솔 화면 버퍼를 점유해서 printf는 안 보이고, OutputDebugStringA가 VS 출력 창에 뜬다)
	char message[256];
	sprintf_s(message,
		"[ScreenBuffer] font requested (%d, %d) -> applied (%d, %d)%s\n",
		fontDesc.width, fontDesc.height, applied.x, applied.y,
		(applied.y != fontDesc.height) ? "  *** 높이가 요청과 다름 - 줄 수가 예상과 달라진다 ***" : "");

	::OutputDebugStringA(message);

	return applied;
}

void ScreenBuffer::ResizeToFit()
{
	// 콘솔 창은 물리적 화면보다 클 수 없다.
	// 최대 칸 수는 "화면 픽셀 / 셀 픽셀"이라 폰트 크기에 따라 달라진다.
	// 화면 해상도/배율/폰트에 따라 달라지는 값이라, 설정값을 그대로 믿으면 안 됨.
	const COORD largest = GetLargestConsoleWindowSize(buffer);
	const Vector2 requested = size;

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
	BOOL result = SetConsoleWindowInfo(buffer, TRUE, &minimalRect);
	ASSERT_CRASH(result == TRUE);

	// largest가 실제로 잡히는 크기보다 큰 값을 알려주는 경우가 있다(테두리/타이틀바 등).
	// 그때는 한 줄씩 줄여가며 잡히는 크기를 찾는다.
	// 여기서 크래쉬시키면 화면이 작은 다른 PC에서는 실행 자체가 불가능해진다.
	const int maxResizeAttempts = 32;

	for (int attempt = 0; attempt < maxResizeAttempts; ++attempt)
	{
		result = SetConsoleScreenBufferSize(buffer, size);

		if (result == TRUE)
		{
			// 콘솔 창의 Rect 설정(left, top, right, bottom)
			SMALL_RECT rect = { 0, 0, static_cast<short>(size.x - 1), static_cast<short>(size.y - 1) };
			result = SetConsoleWindowInfo(buffer, TRUE, &rect);

			if (result == TRUE)
			{
				break;
			}
		}

		// 세로가 먼저 막히므로 세로부터 줄인다. 세로가 바닥나면 가로를 줄인다.
		if (size.y > 1)
		{
			--size.y;
		}
		else if (size.x > 1)
		{
			--size.x;
		}
		else
		{
			break;
		}
	}

	// 끝내 못 잡았으면 콘솔이 실제로 갖고 있는 창 크기를 그대로 받아들인다.
	// size가 실제 콘솔과 어긋나면 그리기가 화면 밖으로 나가기 때문에,
	// "size는 항상 실제 크기"라는 불변식을 여기서 지켜준다.
	//
	// 성공한 경우에는 되읽지 않는다. 방금 우리가 넣은 값이 곧 실제 크기이고,
	// ApplyPalette의 주석대로 Ex 계열 Get은 srWindow를 1 작게 돌려주는 함정이 있어
	// 굳이 되읽으면 멀쩡한 크기가 한 칸씩 줄어든다.
	if (result != TRUE)
	{
		CONSOLE_SCREEN_BUFFER_INFO info = {};

		if (GetConsoleScreenBufferInfo(buffer, &info) == TRUE)
		{
			size.x = info.srWindow.Right - info.srWindow.Left + 1;
			size.y = info.srWindow.Bottom - info.srWindow.Top + 1;
		}
	}

	// 여기도 항상 남긴다. 지금까지 클램프가 완전히 조용해서
	// "height = 150을 넣었는데 왜 85줄인지" 알 수가 없었다.
	char message[256];
	sprintf_s(message,
		"[ScreenBuffer] size requested (%d, %d) -> actual (%d, %d), largest (%d, %d), cell (%d, %d)px\n",
		requested.x, requested.y, size.x, size.y,
		largest.X, largest.Y, cellPixelSize.x, cellPixelSize.y);

	::OutputDebugStringA(message);
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