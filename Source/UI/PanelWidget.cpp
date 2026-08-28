#include "pch.h"
#include "UI/PanelWidget.h"

NAME_SPACE_BEGIN(Craft)

namespace UI
{
	std::unique_ptr<PanelSlot> PanelWidget::CreateSlot()
	{
		return std::make_unique<PanelSlot>();
	}

	PanelSlot* PanelWidget::AddChild(const std::shared_ptr<Widget>& child)
	{
		if (nullptr == child)
		{
			return nullptr;
		}

		// 받을 수 있는 개수를 넘으면 데이터가 잘못된 것이다.
		// 조용히 무시하면 "왜 두 번째 자식이 안 보이지"를 한참 찾게 된다.
		const int maxChildCount = GetMaxChildCount();
		ASSERT_CRASH(maxChildCount <= 0 || GetChildCount() < maxChildCount);

		// 이미 다른 부모에 붙어 있으면 먼저 떼어낸다.
		// 안 그러면 한 위젯이 두 부모의 목록에 들어가서 두 번 그려지고,
		// parent가 나중에 붙인 쪽만 가리켜 배치가 어긋난다.
		if (std::shared_ptr<PanelWidget> previousParent = child->GetParent())
		{
			previousParent->RemoveChild(child);
		}

		std::unique_ptr<PanelSlot> newSlot = CreateSlot();
		newSlot->content = child;

		PanelSlot* slotPointer = newSlot.get();
		slots.emplace_back(std::move(newSlot));

		// weak_from_this는 이 패널이 이미 shared_ptr로 감싸져 있어야 유효하다.
		// Widget::Create를 통해 만들었다면 항상 그렇다.
		child->SetParent(
			std::static_pointer_cast<PanelWidget>(shared_from_this()));

		// 이미 화면에 올라가 있는 패널에 붙었다면, 자식도 지금 올라간 것이다.
		// 안 그러면 나중에 추가된 창은 NativeConstruct를 못 받아 입력을 등록하지 못한다.
		if (IsConstructed() && !child->IsConstructed())
		{
			child->NativeConstruct();
		}

		Invalidate();

		return slotPointer;
	}

	bool PanelWidget::RemoveChild(const std::shared_ptr<Widget>& child)
	{
		if (nullptr == child)
		{
			return false;
		}

		for (auto iterator = slots.begin(); iterator != slots.end(); ++iterator)
		{
			if ((*iterator)->content != child)
			{
				continue;
			}

			// 떼어내는 것은 화면에서 내려가는 것이다.
			// 입력 등록을 여기서 풀지 않으면 안 보이는 창이 계속 키를 먹는다.
			if (child->IsConstructed())
			{
				child->NativeDestruct();
			}

			child->SetParent(std::weak_ptr<PanelWidget>());
			slots.erase(iterator);

			Invalidate();

			return true;
		}

		return false;
	}

	void PanelWidget::ClearChildren()
	{
		for (const std::unique_ptr<PanelSlot>& slot : slots)
		{
			if (nullptr == slot->content)
			{
				continue;
			}

			if (slot->content->IsConstructed())
			{
				slot->content->NativeDestruct();
			}

			slot->content->SetParent(std::weak_ptr<PanelWidget>());
		}

		slots.clear();

		Invalidate();
	}

	std::shared_ptr<Widget> PanelWidget::GetChildAt(int index) const
	{
		if (index < 0 || index >= GetChildCount())
		{
			return nullptr;
		}

		return slots[index]->content;
	}

	PanelSlot* PanelWidget::GetSlotAt(int index) const
	{
		if (index < 0 || index >= GetChildCount())
		{
			return nullptr;
		}

		return slots[index].get();
	}

	void PanelWidget::NativeConstruct()
	{
		super::NativeConstruct();

		for (const std::unique_ptr<PanelSlot>& slot : slots)
		{
			// 숨김/접힘과 무관하게 전달한다.
			// 안 보이는 창도 화면에 올라가 있는 것은 사실이고,
			// 다시 보이게 됐을 때 입력을 받으려면 등록은 되어 있어야 한다.
			if (nullptr != slot->content && !slot->content->IsConstructed())
			{
				slot->content->NativeConstruct();
			}
		}
	}

	void PanelWidget::NativeDestruct()
	{
		for (const std::unique_ptr<PanelSlot>& slot : slots)
		{
			if (nullptr != slot->content && slot->content->IsConstructed())
			{
				slot->content->NativeDestruct();
			}
		}

		super::NativeDestruct();
	}

	void PanelWidget::Tick(float deltaTime)
	{
		super::Tick(deltaTime);

		for (const std::unique_ptr<PanelSlot>& slot : slots)
		{
			const std::shared_ptr<Widget>& child = slot->content;

			// 접힌 위젯은 아예 없는 것으로 친다.
			// 숨김(Hidden)은 안 보일 뿐 살아있으므로 계속 갱신한다.
			if (nullptr == child || child->GetVisibility() == EVisibility::Collapsed)
			{
				continue;
			}

			child->Tick(deltaTime);
		}
	}

	int PanelWidget::OnPaint(const Geometry& allottedGeometry, const PaintContext& context) const
	{
		int maxLayerId = context.layerId;

		// 자식이 부모 밖으로 나가지 못하게, 넘겨줄 클리핑 범위를 여기서 좁힌다.
		// 위젯이 문자열을 직접 잘라내지 않고 이 사각형을 Renderer에 그대로 넘긴다.
		const Rect childCullingRect =
			context.cullingRect.Intersect(allottedGeometry.absoluteRect);

		// 완전히 가려졌으면 자식을 그릴 필요가 없다.
		if (childCullingRect.IsEmpty())
		{
			return maxLayerId;
		}

		for (const std::unique_ptr<PanelSlot>& slot : slots)
		{
			const std::shared_ptr<Widget>& child = slot->content;

			if (nullptr == child || !child->IsVisible())
			{
				continue;
			}

			// 배열 순서대로 그린다. 뒤에 있는 자식이 앞의 자식 위에 겹친다.
			PaintContext childContext(childCullingRect, maxLayerId);

			const int childLayerId = child->OnPaint(child->GetCachedGeometry(), childContext);

			if (childLayerId > maxLayerId)
			{
				maxLayerId = childLayerId;
			}
		}

		return maxLayerId;
	}
}

NAME_SPACE_END
