#include "pch.h"
#include "UI/Border.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

namespace UI
{
	void Border::SetContent(const std::shared_ptr<Widget>& content)
	{
		// 기존 자식을 떼고 새로 붙인다.
		// AddChild가 개수 초과를 크래시로 잡으므로 먼저 비워야 한다.
		ClearChildren();

		if (nullptr != content)
		{
			AddChild(content);
		}
	}

	void Border::SetBackgroundColor(std::optional<Color> newBackgroundColor)
	{
		backgroundColor = newBackgroundColor;
	}

	void Border::SetShowBorder(bool show)
	{
		if (showBorder == show)
		{
			return;
		}

		showBorder = show;

		// 테두리가 사방 1칸을 먹으므로 자식이 쓸 수 있는 크기가 달라진다.
		Invalidate();
	}

	Vector2 Border::ComputeDesiredSize()
	{
		// 자식이 원하는 크기에 내 여백과 테두리를 더한 것이 내가 필요한 크기다.
		Vector2 contentSize = Vector2::Zero;

		if (const std::shared_ptr<Widget> content = GetContent())
		{
			if (content->TakesSpace())
			{
				contentSize = content->ComputeDesiredSize();

				if (const PanelSlot* slot = GetSlotAt(0))
				{
					contentSize.x += slot->GetPadding().GetTotalWidth();
					contentSize.y += slot->GetPadding().GetTotalHeight();
				}
			}
		}

		const int borderThickness = GetBorderThickness();

		cachedDesiredSize = Vector2(
			contentSize.x + padding.GetTotalWidth() + (borderThickness * 2),
			contentSize.y + padding.GetTotalHeight() + (borderThickness * 2));

		return cachedDesiredSize;
	}

	void Border::ArrangeChildren(const Geometry& allottedGeometry)
	{
		cachedGeometry = allottedGeometry;

		const std::shared_ptr<Widget> content = GetContent();

		if (nullptr == content || !content->TakesSpace())
		{
			return;
		}

		const PanelSlot* slot = GetSlotAt(0);
		const int borderThickness = GetBorderThickness();

		// 테두리와 내 여백을 뺀 나머지가 자식이 쓸 수 있는 영역이다.
		const Rect& outerRect = allottedGeometry.absoluteRect;

		const Rect innerRect(
			Vector2(
				outerRect.position.x + padding.left + borderThickness,
				outerRect.position.y + padding.top + borderThickness),
			Vector2(
				outerRect.size.x - padding.GetTotalWidth() - (borderThickness * 2),
				outerRect.size.y - padding.GetTotalHeight() - (borderThickness * 2)));

		if (innerRect.IsEmpty())
		{
			// 놓을 자리가 없으면 빈 영역을 준다.
			// 자식이 자기 배치를 갱신해야 이전 프레임의 자리에 남지 않는다.
			content->ArrangeChildren(Geometry(Rect(innerRect.position, Vector2::Zero)));
			return;
		}

		// 자식이 원하는 크기를 슬롯 정렬 규칙에 따라 안쪽 영역에 놓는다.
		const Vector2 contentDesiredSize = content->GetDesiredSize();

		const Rect contentRect = AlignInRect(
			innerRect,
			contentDesiredSize,
			slot ? slot->GetHorizontalAlignment() : EHorizontalAlignment::Fill,
			slot ? slot->GetVerticalAlignment() : EVerticalAlignment::Fill,
			slot ? slot->GetPadding() : Margin());

		content->ArrangeChildren(Geometry(contentRect));
	}

	void Border::PaintBackground(const Rect& rect, const Rect& clipRect, int layerId) const
	{
		Renderer& renderer = Renderer::Get();

		// 배경을 공백 문자 + 배경색으로 칠한다.
		//
		// SubmitPixels가 아니라 Submit을 쓰는 이유:
		// SubmitPixels는 기호 -> 색 팔레트를 요구하는데 UI는 팔레트 기호를 쓰지 않는다.
		// 공백 + backgroundColor면 같은 결과가 나오고 경로도 하나로 유지된다.
		if (backgroundColor.has_value())
		{
			const std::string blankLine(rect.size.x, ' ');

			for (int y = 0; y < rect.size.y; ++y)
			{
				renderer.Submit(
					blankLine,
					Vector2(rect.position.x, rect.position.y + y),
					Color::White,
					layerId,
					backgroundColor,
					clipRect);
			}
		}

		if (!showBorder)
		{
			return;
		}

		// 테두리를 그릴 수 없을 만큼 작으면 건너뛴다.
		if (rect.size.x < 2 || rect.size.y < 2)
		{
			return;
		}

		// ASCII만 쓴다. WriteConsoleOutputA는 시스템 코드 페이지로 해석하므로
		// CP437 박스 문자는 환경에 따라 다른 글자가 나온다.
		const int innerWidth = rect.size.x - 2;

		std::string horizontalLine;
		horizontalLine.reserve(rect.size.x);
		horizontalLine += '+';
		horizontalLine.append(innerWidth, '-');
		horizontalLine += '+';

		// 위/아래 변.
		renderer.Submit(horizontalLine, rect.position, borderColor, layerId, backgroundColor, clipRect);
		renderer.Submit(
			horizontalLine,
			Vector2(rect.position.x, rect.GetBottom()),
			borderColor,
			layerId,
			backgroundColor,
			clipRect);

		// 좌/우 변. 모서리 줄은 위에서 이미 그렸으므로 사이만 채운다.
		const std::string verticalCharacter(1, '|');

		for (int y = rect.position.y + 1; y < rect.GetBottom(); ++y)
		{
			renderer.Submit(verticalCharacter, Vector2(rect.position.x, y),
				borderColor, layerId, backgroundColor, clipRect);

			renderer.Submit(verticalCharacter, Vector2(rect.GetRight(), y),
				borderColor, layerId, backgroundColor, clipRect);
		}
	}

	int Border::OnPaint(const Geometry& allottedGeometry, const PaintContext& context) const
	{
		const Rect& rect = allottedGeometry.absoluteRect;

		if (rect.IsEmpty())
		{
			return context.layerId;
		}

		const Rect clipRect = context.cullingRect.Intersect(rect);

		if (clipRect.IsEmpty())
		{
			return context.layerId;
		}

		// 배경을 먼저 깔고 자식을 그 위에 그린다.
		// Renderer의 z 테스트가 "값이 같으면 나중 것이 위"라서 순서만으로 겹침이 정해진다.
		PaintBackground(rect, clipRect, context.layerId);

		return super::OnPaint(allottedGeometry, context);
	}
}

NAME_SPACE_END
