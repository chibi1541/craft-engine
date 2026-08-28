#include "pch.h"
#include "UI/UISystem.h"
#include "UI/UserWidget.h"
#include "Render/Renderer.h"
#include "Render/RenderLayer.h"

#include <algorithm>

NAME_SPACE_BEGIN(Craft)

namespace UI
{
	UISystem* UISystem::instance = nullptr;

	UISystem::UISystem()
	{
		ASSERT_CRASH(!instance);
		instance = this;
	}

	UISystem::~UISystem()
	{
		// 남아 있는 위젯에게 화면에서 내려간다는 것을 알린다.
		//
		// instance를 지우기 전에 해야 한다. NativeDestruct 안에서 UISystem::Get()을
		// 부르는 위젯이 있을 수 있기 때문이다.
		//
		// 엔진의 멤버 파괴 순서상 UISystem은 renderer/inputSystem보다 먼저 죽으므로,
		// 여기서 입력 등록을 푸는 것은 안전하다.
		for (const ViewportEntry& entry : entries)
		{
			if (nullptr != entry.widget && entry.widget->IsConstructed())
			{
				entry.widget->NativeDestruct();
			}
		}

		entries.clear();
		pendingAddEntries.clear();
		pendingRemoveWidgets.clear();

		instance = nullptr;
	}

	UISystem& UISystem::Get()
	{
		ASSERT_CRASH(instance);
		return *instance;
	}

	bool UISystem::HasInstance()
	{
		return instance != nullptr;
	}

	void UISystem::AddToViewport(const std::shared_ptr<Widget>& widget, int zOrder, bool persistent)
	{
		if (nullptr == widget)
		{
			return;
		}

		// 위에 겹치는 창일수록 입력도 먼저 받아야 하므로 zOrder를 우선순위에 더한다.
		// 그 합이 System 밴드를 넘으면 디버그 콘솔 같은 것보다 먼저 처리되어 버린다.
		ASSERT_CRASH(zOrder >= 0 && zOrder < InputPriority::System - InputPriority::UI);

		if (const std::shared_ptr<UserWidget> userWidget = Cast<UserWidget>(widget))
		{
			userWidget->SetInputPriority(InputPriority::UI + zOrder);
		}

		ViewportEntry entry;
		entry.widget = widget;
		entry.zOrder = zOrder;
		entry.persistent = persistent;

		pendingAddEntries.emplace_back(std::move(entry));
	}

	void UISystem::RemoveFromViewport(const std::shared_ptr<Widget>& widget)
	{
		if (nullptr == widget)
		{
			return;
		}

		pendingRemoveWidgets.emplace_back(widget);
	}

	void UISystem::ClearNonPersistent()
	{
		for (const ViewportEntry& entry : entries)
		{
			if (!entry.persistent)
			{
				pendingRemoveWidgets.emplace_back(entry.widget);
			}
		}

		// 아직 올라가지도 않은 것 중 레벨에 딸린 것도 취소한다.
		// 안 그러면 새 레벨에서 이전 레벨의 UI가 뒤늦게 나타난다.
		for (auto iterator = pendingAddEntries.begin(); iterator != pendingAddEntries.end();)
		{
			if (iterator->persistent)
			{
				++iterator;
				continue;
			}

			iterator = pendingAddEntries.erase(iterator);
		}
	}

	void UISystem::ProcessPendingWidgets()
	{
		// 제거를 먼저 처리한다.
		// 같은 프레임에 내렸다 다시 올린 위젯이 사라지지 않게 하려면 이 순서여야 한다.
		for (const std::shared_ptr<Widget>& widget : pendingRemoveWidgets)
		{
			for (auto iterator = entries.begin(); iterator != entries.end(); ++iterator)
			{
				if (iterator->widget != widget)
				{
					continue;
				}

				// 화면에서 내려가는 순간을 알려준다.
				// UserWidget은 여기서 입력 등록을 푼다.
				if (widget->IsConstructed())
				{
					widget->NativeDestruct();
				}

				entries.erase(iterator);
				break;
			}
		}

		pendingRemoveWidgets.clear();

		if (!pendingAddEntries.empty())
		{
			for (ViewportEntry& entry : pendingAddEntries)
			{
				// 화면에 올라간 순간을 알려준다.
				// UserWidget은 여기서 입력을 등록한다.
				//
				// 이번 프레임의 디스패치는 이미 끝났으므로, 방금 올라온 창은
				// 다음 프레임부터 입력을 받는다. 이건 결함이 아니라 필요한 성질이다.
				// (Enter로 연 메뉴가 같은 Enter를 또 먹는 일이 없다)
				if (nullptr != entry.widget && !entry.widget->IsConstructed())
				{
					entry.widget->NativeConstruct();
				}

				entries.emplace_back(std::move(entry));
			}

			pendingAddEntries.clear();
			needsSort = true;
		}

		if (needsSort)
		{
			SortEntries();
			needsSort = false;
		}
	}

	void UISystem::SortEntries()
	{
		// zOrder 오름차순. 먼저 그린 것이 아래에 깔린다.
		//
		// stable_sort를 쓰는 이유는 같은 zOrder끼리는 올린 순서를 지키기 위해서다.
		// 그래야 "나중에 연 창이 위에 뜬다"는 당연한 동작이 나온다.
		std::stable_sort(entries.begin(), entries.end(),
			[](const ViewportEntry& left, const ViewportEntry& right)
			{
				return left.zOrder < right.zOrder;
			});
	}

	Rect UISystem::GetViewportRect() const
	{
		// 엔진 설정값이 아니라 렌더러가 실제로 잡은 크기를 쓴다.
		// 콘솔은 요청한 크기를 못 잡아줄 때가 많고, 설정값으로 배치하면
		// 화면 밖에 그리게 된다.
		return Rect(Vector2::Zero, Renderer::Get().GetScreenSize());
	}

	void UISystem::Tick(float deltaTime)
	{
		// 순회 도중 목록이 바뀌지 않는다는 보장이 필요하다.
		// 추가/제거는 전부 지연되므로 여기서 목록이 흔들릴 일은 없지만,
		// 위젯이 자기 자식을 바꾸는 건 자유다(그건 이 목록과 무관하다).
		for (const ViewportEntry& entry : entries)
		{
			if (nullptr == entry.widget)
			{
				continue;
			}

			// 접힌 위젯은 없는 것으로 친다.
			if (entry.widget->GetVisibility() == EVisibility::Collapsed)
			{
				continue;
			}

			entry.widget->Tick(deltaTime);
		}
	}

	void UISystem::Paint()
	{
		if (entries.empty())
		{
			return;
		}

		const Rect viewportRect = GetViewportRect();

		if (viewportRect.IsEmpty())
		{
			return;
		}

		for (const ViewportEntry& entry : entries)
		{
			const std::shared_ptr<Widget>& widget = entry.widget;

			if (nullptr == widget || !widget->IsVisible())
			{
				continue;
			}

			// 1패스: 자식 -> 부모 방향으로 필요한 크기를 계산한다.
			widget->ComputeDesiredSize();

			// 2패스: 부모 -> 자식 방향으로 자리를 나눠준다.
			//
			// 루트 위젯은 뷰포트 전체를 받되, 자기 정렬 설정에 따라 그 안에 놓인다.
			// 기본값이 Fill이라 창을 화면 크기로 만들고 싶지 않다면
			// 정렬을 바꾸거나 크기를 강제하는 위젯으로 감싸야 한다.
			const Rect rootRect = AlignInRect(
				viewportRect,
				widget->GetDesiredSize(),
				widget->GetHorizontalAlignment(),
				widget->GetVerticalAlignment());

			widget->ArrangeChildren(Geometry(rootRect));

			// 3패스: 그린다.
			//
			// 트리 전체가 같은 정렬 순서 값을 쓴다.
			// Renderer의 z 테스트가 "값이 같으면 나중에 제출한 것이 위"이므로,
			// 깊이 우선으로 제출하는 것만으로 부모 -> 자식, 형제는 배열 순서대로 겹친다.
			// 창끼리의 앞뒤는 entries가 zOrder로 정렬돼 있어서 이 루프 순서가 해결한다.
			const PaintContext context(viewportRect, RenderLayer::UI);

			widget->OnPaint(widget->GetCachedGeometry(), context);
		}
	}
}

NAME_SPACE_END
