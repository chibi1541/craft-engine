#include "pch.h"
#include "UI/Widget.h"
#include "UI/PanelWidget.h"

NAME_SPACE_BEGIN(Craft)

namespace UI
{
	Vector2 Widget::ComputeDesiredSize()
	{
		// 내용이 없는 기본 위젯은 여백만큼만 필요하다.
		// 자식이나 텍스트가 있는 위젯은 이걸 재정의해서 실제 크기를 계산한다.
		cachedDesiredSize = Vector2(padding.GetTotalWidth(), padding.GetTotalHeight());
		return cachedDesiredSize;
	}

	void Widget::ArrangeChildren(const Geometry& allottedGeometry)
	{
		// 자식이 없으면 받은 영역을 그대로 자기 자리로 삼는다.
		// 패널만 이 함수를 재정의해서 자식들에게 영역을 쪼개 준다.
		cachedGeometry = allottedGeometry;
	}

	void Widget::Invalidate()
	{
		// 지금은 UISystem이 매 프레임 배치를 다시 하므로 할 일이 없다.
		// 더티 플래그를 켜는 시점에 여기만 채우면 되도록 호출부를 미리 만들어 둔다.
	}

	void Widget::NativeConstruct()
	{
		isConstructed = true;
	}

	void Widget::NativeDestruct()
	{
		isConstructed = false;
	}

	void Widget::Tick(float deltaTime)
	{
		// 기본 위젯은 시간에 따라 변하는 것이 없다.
	}

	int Widget::OnPaint(const Geometry& allottedGeometry, const PaintContext& context) const
	{
		// 기본 위젯은 그리는 것이 없다.
		return context.layerId;
	}

	void Widget::SetVisibility(EVisibility newVisibility)
	{
		if (visibility == newVisibility)
		{
			return;
		}

		visibility = newVisibility;

		// Collapsed는 자리를 내놓으므로 형제들의 배치까지 달라진다.
		Invalidate();
	}

	void Widget::SetPadding(const Margin& newPadding)
	{
		padding = newPadding;
		Invalidate();
	}

	void Widget::SetHorizontalAlignment(EHorizontalAlignment alignment)
	{
		horizontalAlignment = alignment;
		Invalidate();
	}

	void Widget::SetVerticalAlignment(EVerticalAlignment alignment)
	{
		verticalAlignment = alignment;
		Invalidate();
	}
}

NAME_SPACE_END
