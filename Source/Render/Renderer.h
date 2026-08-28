#pragma once

#include "Math/Vector2.h"
#include "Math/Color.h"
#include "Math/Rect.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>

NAME_SPACE_BEGIN(Craft)

class ScreenBuffer;

// 그리기 기능을 전담하는 전문 객체.
class CRAFT_API Renderer
{
	// 프레임(이미지) 데이터 구조체
	struct Frame
	{
		Frame(int bufferCount);
		~Frame();

		// 프레임 초기화 함수. backgroundColor로 화면 전체를 채운다.
		void Clear(const Vector2& screenSize, Color backgroundColor);

		// 화면에 그릴 2차원 배열 문자값
		std::unique_ptr<CHAR_INFO[]> charInfoArray;
	
		// 그리기 정렬 값 이차원 배열.
		std::unique_ptr<int[]> sortingOrderArray;
	};

	// 화면에 그릴 데이터를 명령 단위로 저장하기 위한 구조체.
	struct RenderCommand
	{
		// 화면에 그릴 문자값.
		std::string image;

		// 셀별 배경색(픽셀 렌더링용). 비어있으면 기존 텍스트+전경색 경로, 채워지면 배경색 픽셀 경로.
		// nullopt인 칸은 투명(건너뜀)으로 취급.
		std::vector<std::optional<Color>> pixelColors;

		// 위치.
		Vector2 position = Vector2::Zero;

		// 색상.
		Color color = Color::White;

		// 텍스트 경로에서 글자 뒤에 깔 배경색.
		//
		// 비어있으면 기존 동작(속성을 전경색으로 통째로 덮음)이라 배경이 검게 남는다.
		// UI는 패널 배경 위에 글자를 올리므로 배경/전경을 한 명령으로 합쳐야 한다.
		// (배경을 먼저 그리고 글자를 나중에 그리면, 글자 명령이 셀 속성을 통째로
		//  덮어써서 그 칸만 배경색이 사라진다)
		std::optional<Color> backgroundColor;

		// 이 명령이 그릴 수 있는 화면 영역.
		//
		// 비어있으면 화면 전체. UI 위젯이 부모 경계를 넘어가지 않도록 자를 때 쓴다.
		// 위젯이 문자열을 직접 잘라서 제출하지 않는 이유:
		//  - SubmitPixels는 픽셀맵 문자열 전체를 받으므로 부분 제출이 안 된다
		//  - scaleX/scaleY가 걸리면 자르기 계산이 배율과 얽혀 틀리기 쉽다
		//  - 매 프레임 문자열을 새로 할당하게 된다
		std::optional<Rect> clipRect;

		// 그리기 정렬 순서. 값이 크면 우선순위가 높음.
		int sortingOrder = -1;
	};

	// 문자열을 '\n' 기준으로 줄 단위로 나눠서 각 줄과 줄 오프셋(0부터 증가)을 콜백으로 전달.
	// CRLF 대응(줄 끝 \r 제거)도 여기서 처리. Submit()과 SubmitPixels()가 공유.
	// 템플릿 멤버 함수라 정의를 여기(헤더)에 둬야 함 - cpp로 분리 불가.
	template <typename Func>
	void ForEachLine(const std::string& text, Func&& func)
	{
		if (text.empty())
		{
			return;
		}

		size_t lineStart = 0;
		int lineOffset = 0;

		while (lineStart <= text.size())
		{
			const size_t newlinePos = text.find('\n', lineStart);
			const size_t lineEnd = (newlinePos == std::string::npos) ? text.size() : newlinePos;

			std::string line = text.substr(lineStart, lineEnd - lineStart);

			// CRLF 대응: 줄 끝에 남은 \r 제거
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}

			if (!line.empty())
			{
				func(line, lineOffset);
			}

			if (newlinePos == std::string::npos)
			{
				break;
			}

			lineStart = newlinePos + 1;
			++lineOffset;
		}
	}

public:
	Renderer(const Vector2& screenSize);
	~Renderer();

	// 화면에 그릴 데이터를 제출(전달)하는 함수.
	//
	// backgroundColor: 글자 뒤에 깔 배경색. 생략하면 배경이 검게 남는다(기존 동작).
	// clipRect: 그릴 수 있는 화면 영역. 생략하면 화면 전체.
	void Submit(
		const std::string& image,
		const Vector2& position,
		Color color = Color::White,
		int sortingOrder = 0,
		std::optional<Color> backgroundColor = std::nullopt,
		std::optional<Rect> clipRect = std::nullopt
	);

	// 팔레트 기호 문자열을 배경색 픽셀로 그리는 함수.
	// pixelMap: '\n'으로 줄 구분된 기호 문자열. palette: 기호 -> 배경색.
	// transparentSymbol에 해당하는 칸은 건너뜀(투명).
	// scaleX/scaleY: 픽셀 하나를 콘솔 셀 몇 칸/몇 줄로 그릴지.
	//   콘솔 셀은 폰트에 따라 정사각이 아니므로 이걸로 보정한다.
	//   셀이 세로로 길면 scaleX를 키우고, 가로로 길면 scaleY를 키운다.
	//   스프라이트를 크게 그릴 때도 같이 쓴다.
	void SubmitPixels(
		const std::string& pixelMap,
		const std::unordered_map<char, Color>& palette,
		const Vector2& position,
		int sortingOrder = 0,
		char transparentSymbol = '.',
		int scaleX = 1,
		int scaleY = 1,
		std::optional<Rect> clipRect = std::nullopt
	);

	// 실제로 잡힌 화면 크기(콘솔 셀 개수).
	//
	// Engine의 설정값(Setting.txt)이 아니라 ScreenBuffer가 클램프한 뒤의 값이다.
	// 콘솔은 화면 해상도/폰트에 따라 요청한 크기를 못 잡아주므로
	// 레이아웃과 마우스 좌표는 반드시 이 값을 기준으로 해야 한다.
	inline Vector2 GetScreenSize() const { return screenSize; }

	// 매 프레임 화면을 지울 때 채울 배경색.
	// 아무것도 안 그려진 칸은 이 색으로 남는다.
	inline void SetClearColor(Color color) { clearColor = color; }
	inline Color GetClearColor() const { return clearColor; }

	// Draw 이벤트 함수 - Engine에서 호출.
	void Draw();

	// 전역 접근 함수.
	static Renderer& Get();

private:
	// 그리기 작업을 시작할 때 프레임(화면)을 지우는 함수.
	void Clear();

	// 전달 받은 렌더 명령을 활용해 화면을 그리는 함수.
	void DrawRenderQueue();

	// 그린 결과를 화면에 표시하는 함수.
	void Present();

	const ScreenBuffer* const GetCurrentBuffer() const;

private:
	// 전역 접근이 가능하도록 변수 선언.
	static Renderer* instance;

	// 이번 프레임에 그릴 렌더 명령을 모아두는 배열.
	// 큐(Queue)처럼 사용.
	std::vector<RenderCommand> renderQueue;

	// 화면 크기
	Vector2 screenSize;

	// 화면 지우기 색(배경색)
	Color clearColor = Color::Black;

	// 글자/그리기 순서 2차원 배열을 관리하는 프레임 객체
	std::unique_ptr<Frame> frame;

	// 이중 버퍼링 구현을 위한 화면 버퍼 2개
	std::unique_ptr<ScreenBuffer> screenBufferArray[2];

	// 버퍼 인덱스
	int currentBufferIndex = 0;
};

NAME_SPACE_END

