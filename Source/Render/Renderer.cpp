#include "pch.h"
#include "Renderer.h"
#include "ScreenBuffer.h"
#include "Math/ViewTransform.h"

#include <cmath>

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

void Renderer::Frame::Clear(const Vector2& screenSize, Color backgroundColor)
{
	// 이중 루프를 순회하면서 값 초기화
	const int width = screenSize.x;
	const int height = screenSize.y;

	// 배경색은 전경색 비트를 배경색 자리로 옮겨서 사용.
	// 공백 문자 + 배경색이라 화면 전체가 그 색으로 칠해진다.
	const WORD backgroundAttribute = ToBackgroundAttribute(backgroundColor);

	for (int y = 0; y < height;++y)
	{
		for (int x = 0; x < width; ++x)
		{
			const int index = (y * width) + x;

			CHAR_INFO& info = charInfoArray[index];
			info.Char.AsciiChar = ' ';
			info.Attributes = backgroundAttribute;

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

	// 이중 버퍼 구현을 위한 콘솔 버퍼 생성 및 초기화.
	// 요청한 크기가 화면에 안 들어가면 ScreenBuffer가 줄여서 잡으므로,
	// 먼저 하나를 만들어서 실제로 잡힌 크기를 확정한 뒤 나머지를 맞춘다.
	// (Frame과 ScreenBuffer의 크기가 어긋나면 그리기가 화면 밖으로 나간다)
	screenBufferArray[0] = std::make_unique<ScreenBuffer>(screenSize);
	this->screenSize = screenBufferArray[0]->GetSize();
	screenBufferArray[0]->Clear();

	screenBufferArray[1] = std::make_unique<ScreenBuffer>(this->screenSize);
	screenBufferArray[1]->Clear();

	const int bufferCount = this->screenSize.x * this->screenSize.y;
	frame = std::make_unique<Frame>(bufferCount);

	// 생성 후 프레임 지우기
	frame->Clear(this->screenSize, clearColor);

	// 뷰 중심 기본값 = 화면 중앙. 활성 카메라가 없으면 이 값이 유지되어
	// ViewWorldToScreen(world) == world 가 된다(회전 이전 동작과 동일).
	viewCenterWorld = Vector2(this->screenSize.x / 2, this->screenSize.y / 2);

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
	const std::string& image,
	const Vector2& position,
	Color color,
	int sortingOrder,
	std::optional<Color> backgroundColor,
	std::optional<Rect> clipRect)
{
	// 개행 문자(\n) 기준으로 줄 단위로 쪼개서 각 줄을 별도의 RenderCommand로 큐에 추가.
	// 이렇게 하면 DrawRenderQueue()의 한 줄 처리 로직(컬링/클리핑/z-order)을
	// 그대로 재사용할 수 있음.
	ForEachLine(image, [&](const std::string& line, int lineOffset)
	{
		// 렌더 명령 생성 및 값 설정.
		RenderCommand command;
		command.image = line;
		command.position = Vector2(position.x, position.y + lineOffset);
		command.color = color;
		command.backgroundColor = backgroundColor;
		command.clipRect = clipRect;
		command.sortingOrder = sortingOrder;

		// 렌더 큐에 명령 추가.
		renderQueue.emplace_back(std::move(command));
	});
}

void Renderer::SubmitPixels(
	const std::string& pixelMap,
	const std::unordered_map<char, Color>& palette,
	const Vector2& position,
	int sortingOrder,
	char transparentSymbol,
	int scaleX,
	int scaleY,
	std::optional<Rect> clipRect)
{
	ASSERT_CRASH(scaleX >= 1 && scaleY >= 1);

	ForEachLine(pixelMap, [&](const std::string& line, int lineOffset)
	{
		// 한 줄의 색상 배열을 가로 배율만큼 늘려서 만든다.
		std::vector<std::optional<Color>> lineColors;
		lineColors.reserve(line.size() * scaleX);

		for (char symbol : line)
		{
			std::optional<Color> cell;

			if (symbol != transparentSymbol)
			{
				auto it = palette.find(symbol);
				// 팔레트에 없는 기호 - 오타 등 즉시 확인
				ASSERT_CRASH(it != palette.end());
				cell = it->second;
			}

			// 픽셀 하나를 가로로 scaleX칸 반복
			for (int repeatX = 0; repeatX < scaleX; ++repeatX)
			{
				lineColors.emplace_back(cell);
			}
		}

		// 컬링/클리핑 로직은 image.length()를 기준으로 동작하므로,
		// pixelColors와 같은 길이의 더미 문자열을 채워 기존 로직과 호환시킴.
		// (실제 그려지는 문자는 DrawRenderQueue()에서 항상 공백으로 덮어씀)
		const std::string dummyImage(lineColors.size(), ' ');

		// 같은 줄을 세로로 scaleY번 반복해서 큐에 넣는다.
		for (int repeatY = 0; repeatY < scaleY; ++repeatY)
		{
			RenderCommand command;
			command.position = Vector2(position.x, position.y + (lineOffset * scaleY) + repeatY);
			command.sortingOrder = sortingOrder;
			command.pixelColors = lineColors;
			command.image = dummyImage;
			command.clipRect = clipRect;

			renderQueue.emplace_back(std::move(command));
		}
	});
}

Vector2 Renderer::ViewToScreen(const Vector2& world) const
{
	const Vector2 half(screenSize.x / 2, screenSize.y / 2);

	return viewInterpolated
		? ViewWorldToScreenF(world, viewCenterWorld, half, viewCos, viewSin)
		: ViewWorldToScreen(world, viewCenterWorld, half, viewQuarterTurns);
}

std::optional<Rect> Renderer::ViewClipToScreen(const std::optional<Rect>& worldClip) const
{
	if (!worldClip.has_value())
	{
		return std::nullopt;
	}

	const Vector2 half(screenSize.x / 2, screenSize.y / 2);

	return viewInterpolated
		? ViewWorldRectToScreenAABBF(*worldClip, viewCenterWorld, half, viewCos, viewSin)
		: ViewWorldRectToScreenAABB(*worldClip, viewCenterWorld, half, viewQuarterTurns);
}

void Renderer::SetView(const Vector2& centerWorld, int quarterTurns)
{
	viewCenterWorld = centerWorld;
	viewQuarterTurns = ((quarterTurns % 4) + 4) % 4;
	viewInterpolated = false;
}

void Renderer::SetViewInterpolated(const Vector2& centerWorld, float angleDegrees)
{
	viewCenterWorld = centerWorld;
	viewInterpolated = true;

	// 프레임당 1회만 계산해서 매 제출마다 다시 부르지 않는다.
	const float radians = angleDegrees * (3.14159265358979323846f / 180.0f);
	viewCos = ::cosf(radians);
	viewSin = ::sinf(radians);
}

void Renderer::SubmitWorld(
	const std::string& image,
	const Vector2& worldPosition,
	Color color,
	int sortingOrder,
	std::optional<Color> backgroundColor,
	std::optional<Rect> clipRect,
	const Vector2& screenOffset)
{
	// 변환은 여기 진입부에서 한 번. clipRect도 position과 같은 공간이므로 함께 옮긴다.
	// screenOffset은 빌보드 오프셋이라 변환 뒤에 더한다(SubmitPixelsWorld와 같은 규칙).
	Submit(image, ViewToScreen(worldPosition) + screenOffset, color, sortingOrder, backgroundColor, ViewClipToScreen(clipRect));
}

void Renderer::SubmitPixelsWorld(
	const std::string& pixelMap,
	const std::unordered_map<char, Color>& palette,
	const Vector2& worldPosition,
	int sortingOrder,
	char transparentSymbol,
	int scaleX,
	int scaleY,
	std::optional<Rect> clipRect,
	const Vector2& screenPixelOffset)
{
	// 피벗 오프셋은 뷰 변환 뒤에 화면 공간에서 더한다(빌보드 - 회전에 따라 돌면 안 됨).
	SubmitPixels(pixelMap, palette, ViewToScreen(worldPosition) + screenPixelOffset,
		sortingOrder, transparentSymbol, scaleX, scaleY, ViewClipToScreen(clipRect));
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
	frame->Clear(screenSize, clearColor);

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

		{
			// 그릴 수 있는 영역 = 화면 전체, 명령에 클립이 있으면 그것과의 교집합.
			//
			// 화면 컬링과 클리핑을 하나의 사각형으로 합쳐두면
			// 아래 컬링/클리핑 계산을 두 벌로 나눠 쓰지 않아도 된다.
			Rect drawableRect(Vector2::Zero, screenSize);

			if (command.clipRect.has_value())
			{
				drawableRect = drawableRect.Intersect(*command.clipRect);

				// 클립 영역이 화면 밖이면 그릴 것이 없다.
				if (drawableRect.IsEmpty())
				{
					continue;
				}
			}

			// y위치가 그릴 수 있는 영역을 벗어났으면 컬링
			if (command.position.y < drawableRect.GetTop() || command.position.y > drawableRect.GetBottom())
			{
				continue;
			}

			// 그리려는 문자열 길이 값.
			const int length = static_cast<int>(command.image.length());

			// 글자의 시작 위치
			const int startX = command.position.x;

			// 글자의 끝 위치
			const int endX = startX + length - 1;

			// x 위치가 그릴 수 있는 영역을 벗어났는지 확인
			if (endX < drawableRect.GetLeft() || startX > drawableRect.GetRight())
			{
				continue;
			}

			// 실제 그릴 글자의 위치 구하기
			// 영역 왼쪽 밖으로 나간 문자를 건너뛰도록 startX를 조절
			const int visibleStart = startX < drawableRect.GetLeft() ? drawableRect.GetLeft() : startX;
			// 영역 오른쪽 밖으로 나가는 문자를 잘라내도록 범위를 좁힘
			const int visibleEnd = endX > drawableRect.GetRight() ? drawableRect.GetRight() : endX;

			// 픽셀(배경색) 렌더 명령인지 여부 - pixelColors가 채워져 있으면 픽셀 경로.
			const bool isPixelCommand = !command.pixelColors.empty();

			// 문자열을 루프 순회하면서 글자를 2차월 배열에 하나씩 기록
			for (int x = visibleStart; x <= visibleEnd; ++x)
			{

				// 문자열에서 글자값을 가져올 때 사용할 인덱스
				const int sourceIndex = x - startX;

				// 글자 2차월 배열의 인덱스
				const int index = (command.position.y * screenSize.x) + x;

				// 투명 픽셀은 z-order도 건드리지 않고 완전히 건너뜀
				if (isPixelCommand && !command.pixelColors[sourceIndex].has_value())
				{
					continue;
				}

				// 정렬 순서를 비교해서 그릴지 말지를 판정.
				if (frame->sortingOrderArray[index] > command.sortingOrder)
				{
					// 이미 그려진 값이 우선 순위가 높음(ZOrder가 더 작음)
					continue;
				}

				if (isPixelCommand)
				{
					// 글자는 공백으로 채우고 배경색만 보이게 함
					frame->charInfoArray[index].Char.AsciiChar = ' ';
					frame->charInfoArray[index].Attributes = ToBackgroundAttribute(*command.pixelColors[sourceIndex]);
				}
				else
				{
					// 2차원 배열에 글자, 속성 설정
					frame->charInfoArray[index].Char.AsciiChar = command.image[sourceIndex];

					// 글자 색상 값 설정.
					//
					// 속성은 [상위 4비트 = 배경색][하위 4비트 = 전경색]이다.
					// 배경색을 안 주면 상위 니블이 0(검정)이 되므로,
					// 패널 배경 위에 글자를 올릴 때는 반드시 배경색을 함께 넘겨야 한다.
					WORD attributes = static_cast<WORD>(command.color);

					if (command.backgroundColor.has_value())
					{
						attributes |= ToBackgroundAttribute(*command.backgroundColor);
					}

					frame->charInfoArray[index].Attributes = attributes;
				}

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

