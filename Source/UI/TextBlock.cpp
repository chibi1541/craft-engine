#include "pch.h"
#include "UI/TextBlock.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

namespace UI
{
	namespace
	{
		// 여러 줄 문자열이 차지할 칸 수를 센다.
		//
		// 줄 나눔 규칙을 Renderer::ForEachLine과 똑같이 맞춰야 한다.
		// 여기서 잰 크기로 자리를 잡아 두고 실제로는 다르게 그려지면
		// 글자가 배경 밖으로 삐져나간다.
		Vector2 MeasureText(const std::string& text)
		{
			if (text.empty())
			{
				return Vector2::Zero;
			}

			int maxLineLength = 0;
			int currentLineLength = 0;
			int lineCount = 1;

			for (const char character : text)
			{
				if (character == '\n')
				{
					if (currentLineLength > maxLineLength)
					{
						maxLineLength = currentLineLength;
					}

					currentLineLength = 0;
					++lineCount;
					continue;
				}

				// Renderer가 줄 끝의 '\r'을 떼므로 폭에 세지 않는다.
				if (character == '\r')
				{
					continue;
				}

				++currentLineLength;
			}

			if (currentLineLength > maxLineLength)
			{
				maxLineLength = currentLineLength;
			}

			return Vector2(maxLineLength, lineCount);
		}
	}

	TextBlock::TextBlock(const std::string& text, Color color)
		: text(text), color(color)
	{
		// 글자는 자기 크기만큼만 차지하는 것이 자연스럽다.
		// 기본값인 Fill을 쓰면 빈 칸까지 자기 영역으로 잡아서
		// 가운데 정렬 같은 게 의도대로 안 나온다.
		horizontalAlignment = EHorizontalAlignment::Left;
		verticalAlignment = EVerticalAlignment::Top;
	}

	void TextBlock::SetText(const std::string& newText)
	{
		if (text == newText)
		{
			return;
		}

		text = newText;

		// 글자 수가 바뀌면 필요한 칸 수도 바뀐다.
		Invalidate();
	}

	Vector2 TextBlock::ComputeDesiredSize()
	{
		const Vector2 textSize = MeasureText(text);

		cachedDesiredSize = Vector2(
			textSize.x + padding.GetTotalWidth(),
			textSize.y + padding.GetTotalHeight());

		return cachedDesiredSize;
	}

	int TextBlock::OnPaint(const Geometry& allottedGeometry, const PaintContext& context) const
	{
		if (text.empty())
		{
			return context.layerId;
		}

		const Rect& rect = allottedGeometry.absoluteRect;

		if (rect.IsEmpty())
		{
			return context.layerId;
		}

		// 배정받은 영역 안쪽(여백을 뺀 자리)부터 그린다.
		const Vector2 drawPosition(
			rect.position.x + padding.left,
			rect.position.y + padding.top);

		// 부모가 준 클리핑 범위와 내 영역을 겹쳐서 넘긴다.
		// 잘라내는 일은 Renderer가 한다 - 여기서 문자열을 substr하면
		// 매 프레임 문자열을 새로 만들게 되고, 여러 줄 처리까지 다시 짜야 한다.
		const Rect clipRect = context.cullingRect.Intersect(rect);

		if (clipRect.IsEmpty())
		{
			return context.layerId;
		}

		Renderer::Get().Submit(
			text,
			drawPosition,
			color,
			context.layerId,
			backgroundColor,
			clipRect);

		return context.layerId;
	}
}

NAME_SPACE_END
