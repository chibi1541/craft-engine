#include "pch.h"
#include "InputComponent.h"
#include "Actor/Actor.h"

NAME_SPACE_BEGIN(Craft)

InputComponent::InputComponent()
	: handler(InputHandler::Create())
{
}

void InputComponent::BeginPlay()
{
	super::BeginPlay();

	// 컴포넌트나 소유 액터가 비활성/파괴 상태면 입력을 받지 않는다.
	//
	// InputHandler는 Actor를 모르기 때문에(그래야 나중에 UI가 Actor 없이 쓸 수 있다)
	// 액터 쪽 사정을 물어보는 방법만 여기서 넣어준다.
	//
	// 람다가 캡처하는 this의 수명은 안전하다.
	// handler는 이 컴포넌트의 멤버라서 컴포넌트보다 오래 살 수 없고,
	// 컴포넌트가 죽으면 handler도 죽어서 InputSystem의 약참조가 만료된다.
	handler->SetEnabledCheck(
		[this]()
		{
			if (!IsActive())
			{
				return false;
			}

			// ownership(약참조하던 객체를 shared_ptr로 만들어서 확인)
			std::shared_ptr<Actor> ownerActor = GetOwner();

			return nullptr != ownerActor && ownerActor->IsActive();
		});

	handler->Register();
}

void InputComponent::BindKey(int keyCode, EInputEvent event,
	InputHandler::InputCallback callback, bool consume)
{
	handler->BindKey(keyCode, event, std::move(callback), consume);
}

void InputComponent::ClearBindings()
{
	handler->ClearBindings();
}

NAME_SPACE_END
