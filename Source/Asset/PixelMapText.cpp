#include "pch.h"
#include "Asset/PixelMapText.h"
#include "Math/SymbolPalette.h"

NAME_SPACE_BEGIN(Craft)

std::string NormalizePixelMapText(const std::string& rawText)
{
	std::string result;
	result.reserve(rawText.size());

	size_t lineStart = 0;

	while (lineStart <= rawText.size())
	{
		const size_t newlinePos = rawText.find('\n', lineStart);
		const size_t lineEnd = (newlinePos == std::string::npos) ? rawText.size() : newlinePos;

		size_t begin = lineStart;
		size_t end = lineEnd;

		while (begin < end && (rawText[begin] == ' ' || rawText[begin] == '\t' || rawText[begin] == '\r'))
		{
			++begin;
		}

		while (end > begin && (rawText[end - 1] == ' ' || rawText[end - 1] == '\t' || rawText[end - 1] == '\r'))
		{
			--end;
		}

		// 빈 줄은 버린다(여는 태그 다음 줄, 닫는 태그 앞 줄 등).
		if (end > begin)
		{
			for (size_t index = begin; index < end; ++index)
			{
				const char symbol = rawText[index];

				// 팔레트에 없는 기호는 픽셀맵 오타다.
				ASSERT_CRASH(symbol == SymbolPalette::TransparentSymbol || SymbolPalette::Contains(symbol));
			}

			result.append(rawText, begin, end - begin);
			result.push_back('\n');
		}

		if (newlinePos == std::string::npos)
		{
			break;
		}

		lineStart = newlinePos + 1;
	}

	return result;
}

NAME_SPACE_END
