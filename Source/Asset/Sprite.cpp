#include "pch.h"
#include "Sprite.h"

NAME_SPACE_BEGIN(Craft)

Sprite::Sprite(const std::string& pixelMap)
	: pixelMap(pixelMap)
{
	// 예외 처리 - 빈 스프라이트는 크기 0으로 둔다(그리기 단계에서 걸러짐).
	if (pixelMap.empty())
	{
		return;
	}

	// 줄을 세는 방식은 Renderer::ForEachLine()과 똑같이 맞춰야 한다.
	// (거기서 빈 줄은 건너뛰고 줄 끝 '\r'은 버리므로, 여기서도 동일하게 처리)
	// Renderer::ForEachLine은 private 템플릿 멤버라 밖에서 재사용할 수 없어서 같은 규칙을 반복한다.
	size_t lineStart = 0;

	while (lineStart <= pixelMap.size())
	{
		const size_t newlinePos = pixelMap.find('\n', lineStart);
		const size_t lineEnd = (newlinePos == std::string::npos) ? pixelMap.size() : newlinePos;

		size_t lineLength = lineEnd - lineStart;

		// CRLF 대응: 줄 끝에 남은 '\r'은 그려지지 않으므로 길이에서 뺀다.
		if (lineLength > 0 && pixelMap[lineEnd - 1] == '\r')
		{
			--lineLength;
		}

		if (lineLength > 0)
		{
			const int currentWidth = static_cast<int>(lineLength);

			if (width == 0)
			{
				// 첫 줄의 길이가 이 스프라이트의 기준 너비가 된다.
				width = currentWidth;
			}
			else
			{
				// SubmitPixels는 각 줄을 좌측 정렬로 그대로 그리기 때문에,
				// 줄 길이가 어긋나면 조용히 삐뚤어진 그림이 된다.
				// 데이터 작성 실수는 여기서 바로 잡는다.
				ASSERT_CRASH(currentWidth == width);
			}

			++height;
		}

		if (newlinePos == std::string::npos)
		{
			break;
		}

		lineStart = newlinePos + 1;
	}
}

NAME_SPACE_END
