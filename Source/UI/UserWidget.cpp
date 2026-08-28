#include "pch.h"
#include "UI/UserWidget.h"

NAME_SPACE_BEGIN(Craft)

namespace UI
{
	UserWidget::UserWidget()
		: handler(InputHandler::Create())
	{
		// UI 밴드에서 시작한다. 실제 값은 뷰포트에 올릴 때 zOrder만큼 더해진다.
		handler->SetPriority(InputPriority::UI);

		// 여기서 Register를 부르면 안 된다.
		// 아직 shared_ptr로 감싸지기 전이라 자기 수명을 알릴 방법이 없다.
	}

	UserWidget::~UserWidget()
	{
		// 정상 경로라면 NativeDestruct에서 이미 풀렸다.
		// 그래도 여기서 한 번 더 확인하는 이유는, 화면에 올리지 않고 버려진 위젯이나
		// 예외적인 경로로 그냥 소멸하는 경우까지 막기 위해서다.
		//
		// InputHandler의 소멸자도 스스로 Unregister를 하므로 이중 안전망이다.
		if (nullptr != handler)
		{
			handler->ClearBindings();
			handler->Unregister();
		}
	}

	void UserWidget::SetRootWidget(const std::shared_ptr<Widget>& root)
	{
		ClearChildren();

		if (nullptr != root)
		{
			AddChild(root);
		}
	}

	void UserWidget::BindKey(int keyCode, EInputEvent event, InputHandler::InputCallback callback, bool consume)
	{
		handler->BindKey(keyCode, event, std::move(callback), consume);
	}

	void UserWidget::ClearBindings()
	{
		handler->ClearBindings();
	}

	int UserWidget::GetInputPriority() const
	{
		return handler->GetPriority();
	}

	void UserWidget::SetInputPriority(int priority)
	{
		handler->SetPriority(priority);
	}

	void UserWidget::SetBlockAllInput(bool block)
	{
		handler->SetBlockAllInput(block);
	}

	bool UserWidget::IsBlockAllInput() const
	{
		return handler->IsBlockAllInput();
	}

	void UserWidget::NativeConstruct()
	{
		super::NativeConstruct();

		// 입력 시스템에게 "이 핸들러가 아직 유효한가"를 물어볼 방법을 준다.
		//
		// 이게 왜 꼭 필요한지:
		// InputSystem은 디스패치 시작 시점에 핸들러들을 강참조로 복사해 둔다.
		// 그래서 프레임 도중에 위젯이 죽어도 핸들러는 그 프레임 끝까지 살아 있고,
		// 바인딩 콜백이 캡처한 this는 이미 사라진 위젯을 가리킬 수 있다.
		// weak_ptr로 확인해야 그 호출을 막을 수 있다.
		std::weak_ptr<UserWidget> weakSelf =
			std::static_pointer_cast<UserWidget>(shared_from_this());

		handler->SetEnabledCheck(
			[weakSelf]()
			{
				const std::shared_ptr<UserWidget> self = weakSelf.lock();

				if (nullptr == self)
				{
					return false;
				}

				// 숨겨진 창은 입력을 받지 않는다.
				return self->IsVisible();
			});

		handler->Register();
	}

	void UserWidget::NativeDestruct()
	{
		handler->Unregister();

		super::NativeDestruct();
	}

	Vector2 UserWidget::ComputeDesiredSize()
	{
		// 루트 트리가 원하는 크기가 곧 이 화면이 원하는 크기다.
		Vector2 rootSize = Vector2::Zero;

		if (const std::shared_ptr<Widget> root = GetRootWidget())
		{
			if (root->TakesSpace())
			{
				rootSize = root->ComputeDesiredSize();
			}
		}

		cachedDesiredSize = Vector2(
			rootSize.x + padding.GetTotalWidth(),
			rootSize.y + padding.GetTotalHeight());

		return cachedDesiredSize;
	}

	void UserWidget::ArrangeChildren(const Geometry& allottedGeometry)
	{
		cachedGeometry = allottedGeometry;

		const std::shared_ptr<Widget> root = GetRootWidget();

		if (nullptr == root || !root->TakesSpace())
		{
			return;
		}

		// 화면 자체는 배치 규칙을 갖지 않는다. 받은 영역을 루트에게 그대로 넘긴다.
		// 어디에 얼마나 놓일지는 루트(보통 Border나 캔버스)가 정한다.
		const Rect innerRect(
			Vector2(
				allottedGeometry.absoluteRect.position.x + padding.left,
				allottedGeometry.absoluteRect.position.y + padding.top),
			Vector2(
				allottedGeometry.absoluteRect.size.x - padding.GetTotalWidth(),
				allottedGeometry.absoluteRect.size.y - padding.GetTotalHeight()));

		const Rect rootRect = AlignInRect(
			innerRect,
			root->GetDesiredSize(),
			root->GetHorizontalAlignment(),
			root->GetVerticalAlignment());

		root->ArrangeChildren(Geometry(rootRect));
	}
}

NAME_SPACE_END
