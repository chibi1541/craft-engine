#include "pch.h"
#include "Sprite.h"

NAME_SPACE_BEGIN(Craft)

Sprite::Sprite(const std::string& sourcePixelMap)
{
	// 예외 처리 - 빈 스프라이트는 크기 0으로 둔다(그리기 단계에서 걸러짐).
	if (sourcePixelMap.empty())
	{
		return;
	}

	// 줄을 나누는 규칙은 Renderer::ForEachLine()과 똑같이 맞춘다.
	// (빈 줄은 건너뛰고 줄 끝 '\r'은 버린다)
	// Renderer::ForEachLine은 private 템플릿 멤버라 밖에서 재사용할 수 없어서 같은 규칙을 반복한다.
	//
	// 여기서 입력을 그대로 두지 않고 다시 조립하는 이유는 헤더에 적은 저장 형식 불변식 때문이다.
	// "width칸 + '\n'"이 정확히 반복되어야 (row, col) 인덱싱이 성립한다.
	pixelMap.reserve(sourcePixelMap.size() + 1);

	size_t lineStart = 0;

	while (lineStart <= sourcePixelMap.size())
	{
		const size_t newlinePos = sourcePixelMap.find('\n', lineStart);
		const size_t lineEnd = (newlinePos == std::string::npos) ? sourcePixelMap.size() : newlinePos;

		size_t lineLength = lineEnd - lineStart;

		// CRLF 대응: 줄 끝에 남은 '\r'은 그려지지 않으므로 버린다.
		if (lineLength > 0 && sourcePixelMap[lineEnd - 1] == '\r')
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

			pixelMap.append(sourcePixelMap, lineStart, lineLength);
			pixelMap.push_back('\n');

			++height;
		}

		if (newlinePos == std::string::npos)
		{
			break;
		}

		lineStart = newlinePos + 1;
	}
}

char Sprite::GetPixel(int row, int col) const
{
	ASSERT_CRASH(row >= 0 && row < height);
	ASSERT_CRASH(col >= 0 && col < width);

	return pixelMap[GetPixelIndex(row, col)];
}

NAME_SPACE_END
