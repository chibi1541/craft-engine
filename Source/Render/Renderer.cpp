#include "pch.h"
#include "Renderer.h"
#include "ScreenBuffer.h"

NAME_SPACE_BEGIN(Craft)

// =========================
//		Render::Frame
// =========================

Renderer::Frame::Frame(int bufferCount)
{
	charInfoArray = std::make_unique<CHAR_INFO[]>(bufferCount);
	sortingOrderArray = std::make_unique<int[]>(bufferCount);
}

Renderer::Frame::~Frame()
{

}

void Renderer::Frame::Clear(const Vector2& screenSize)
{
	// 이중 루프를 순회하면서 값 초기화
	const int width = screenSize.x;
	const int height = screenSize.y;

	for (int y = 0; y < height;++y)
	{
		for (int x = 0; x < width; ++x)
		{
			const int index = (y * width) + x;

			CHAR_INFO& info = charInfoArray[index];
			info.Char.AsciiChar = ' ';
			// 색상 표기를 안함
			info.Attributes = 0;

			sortingOrderArray[index] = -1;
		}
	}
}


Renderer* Renderer::instance = nullptr;

Renderer::Renderer(const Vector2& screenSize) : screenSize(screenSize)
{
	// 어서트.
	ASSERT_CRASH(!instance);
	instance = this;

	const int bufferCount = screenSize.x * screenSize.y;
	frame = std::make_unique<Frame>(bufferCount);

	// 생성 후 프레임 지우기
	frame->Clear(screenSize);

	// 이중 버퍼 구현을 위한 콘솔 버퍼 생성 및 초기화
	screenBufferArray[0] = std::make_unique<ScreenBuffer>(screenSize);
	screenBufferArray[0]->Clear();

	screenBufferArray[1] = std::make_unique<ScreenBuffer>(screenSize);
	screenBufferArray[1]->Clear();

	currentBufferIndex = 0;
	// 화면에 0번 콘솔 버퍼 활성화
	BOOL result = SetConsoleActiveScreenBuffer(screenBufferArray[currentBufferIndex]->GetBuffer());
	ASSERT_CRASH(result == TRUE);
}

Renderer::~Renderer()
{
	instance = nullptr;

	// 콘솔 창 원래대로 복구
	// 표준 핸들로 교체
	SetConsoleActiveScreenBuffer(GetStdHandle(STD_OUTPUT_HANDLE));

}

void Renderer::Submit(
	const std::wstring& image,
	const Vector2& position,
	Color color,
	int sortingOrder)
{
	// 렌더 명령 생성 및 값 설정.
	RenderCommand command;
	command.image = image;
	command.position = position;
	command.color = color;
	command.sortingOrder = sortingOrder;

	// 렌더 큐에 명령 추가.
	renderQueue.emplace_back(command);
}

void Renderer::Draw()
{
	// 화면(이미지/프레임) 지우기.
	Clear();

	// 프레임 그리기.
	DrawRenderQueue();

	// 화면(이미지/프레임) 표시.
	Present();
}

Renderer& Renderer::Get()
{
	// 어서트.
	ASSERT_CRASH(instance);
	return *instance;
}

void Renderer::Clear()
{
	// 프레딤 값 초기화
	frame->Clear(screenSize);

	// 콘솔 버퍼 초기화
	GetCurrentBuffer()->Clear();

}

void Renderer::DrawRenderQueue()
{
	// 렌더 큐를 순회하면서 그리기 명령 실행.
	for (const RenderCommand& command : renderQueue)
	{
		// 그릴 문자 값이 없으면 건너뛰기
		if (command.image.empty())
		{
			continue;
		}



		// TODO : 프로젝트 할 때 복수 문자로 Actor 표현 하려면 컬링 로직 손봐야 함
		{
			// y위치가 화면을 벗어났으면 컬링
			if (command.position.y < 0 || command.position.y >= screenSize.y)
			{
				continue;
			}

			// 그리려는 문자열 길이 값.
			const int length = static_cast<int>(command.image.length());

			// 글자의 시작 위치
			const int startX = command.position.x;

			// 글자의 끝 위치
			const int endX = startX + length - 1;

			// x 위치가 화면을 벗어났는지 확인
			if (endX < 0 || startX >= screenSize.x)
			{
				continue;
			}

			// 실제 그릴 글자의 위치 구하기
			// 0위치의 문자부터 그리게 startX를 조절
			const int visibleStart = startX < 0 ? 0 : startX;
			// 범위를 벗어나는 문자를 잘라내도록 screenSize.x - 1 만큼으로 범위를 좁힘
			const int visibleEnd = endX >= screenSize.x ? screenSize.x - 1 : endX;

			//temp : 개행 처리
			//int lineIndex = 0;
			//int xIdx = 0;

			// 문자열을 루프 순회하면서 글자를 2차월 배열에 하나씩 기록
			for (int x = visibleStart; x <= visibleEnd; ++x)
			{

				// 문자열에서 글자값을 가져올 때 사용할 인덱스
				const int sourceIndex = x - startX;

				// 글자 2차월 배열의 인덱스
				const int index = ((command.position.y /*+ lineIndex*/) * screenSize.x) + x;

				//++xIdx;

				// 정렬 순서를 비교해서 그릴지 말지를 판정.
				if (frame->sortingOrderArray[index] > command.sortingOrder)
				{
					// 이미 그려진 값이 우선 순위가 높음(ZOrder가 더 작음)
					continue;
				}

				// temp : 개행 처리
				//if(command.image[sourceIndex] == L'\n')
				//{
				//	++lineIndex;
				//	xIdx = 0;
				//	continue;
				//}

				// 2차원 배열에 글자, 속성 설정
				frame->charInfoArray[index].Char.UnicodeChar = command.image[sourceIndex];

				// 글자 색상 값 설정
				frame->charInfoArray[index].Attributes = static_cast<DWORD>(command.color);

				// 그리기 우선순위 값 설정
				frame->sortingOrderArray[index] = command.sortingOrder;
			}
		}
	}

	// 앞에서 설정한 2차원 배열을 콘솔에 그리기
	GetCurrentBuffer()->Draw(frame->charInfoArray.get());

	// 렌더큐 비우기.
	renderQueue.clear();

	// 콘솔 색상 복원.
	SetConsoleTextAttribute(GetCurrentBuffer()->GetBuffer(), static_cast<WORD>(Color::White));
}

void Renderer::Present()
{
	// 현재 순번의 콘솔 버퍼를 활성화
	SetConsoleActiveScreenBuffer(GetCurrentBuffer()->GetBuffer());

	// 다음 인덱스로 교체(0 -> 1 -> 0)
	//currentBufferIndex = (currentBufferIndex + 1) % 2;
	// 마법의 공식 -> OneMinus (이건 이중 버퍼를 쓸때만)
	currentBufferIndex = 1 - currentBufferIndex;
}

const ScreenBuffer* const Renderer::GetCurrentBuffer() const
{
	// 스마트 포인터를 raw 포인터로 반환하기 때문에 받는 쪽에서는 아무것도 못하도록 함
	return screenBufferArray[currentBufferIndex].get();
}


NAME_SPACE_END

