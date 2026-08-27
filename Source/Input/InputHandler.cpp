#include "pch.h"
#include "InputHandler.h"
#include "InputSystem.h"

NAME_SPACE_BEGIN(Craft)

std::shared_ptr<InputHandler> InputHandler::Create()
{
	// 생성자가 private이라 make_shared를 쓸 수 없으므로 new로 만든다.
	// 소유권은 바로 shared_ptr로 넘어간다.
	return std::shared_ptr<InputHandler>(new InputHandler());
}

InputHandler::~InputHandler()
{
	// 소멸자에서는 shared_from_this를 쓸 수 없으므로 주소로 해제한다.
	Unregister();
}

void InputHandler::Register()
{
	// 검증 - 이미 등록됐거나 시스템이 아직/이미 없으면 건너뛰기.
	if (isRegistered || !InputSystem::HasInstance())
	{
		return;
	}

	InputSystem::Get().RegisterHandler(weak_from_this());
	isRegistered = true;
}

void InputHandler::Unregister()
{
	// 검증 - 등록되어 있지 않거나 시스템이 이미 사라졌으면 건너뛰기.
	// 엔진 종료 시 InputSystem이 먼저 죽을 수 있다.
	if (!isRegistered || !InputSystem::HasInstance())
	{
		return;
	}

	InputSystem::Get().UnregisterHandler(this);
	isRegistered = false;
}

void InputHandler::BindKey(int keyCode, EInputEvent event, InputCallback callback, bool consume)
{
	// 예외 처리 - 빈 콜백이나 범위를 벗어난 키는 등록하지 않는다.
	if (!callback || keyCode < 0 || keyCode >= 256)
	{
		return;
	}

	Binding binding;
	binding.keyCode = keyCode;
	binding.event = event;
	binding.callback = std::move(callback);
	binding.consume = consume;

	bindings.emplace_back(std::move(binding));
}

void InputHandler::ClearBindings()
{
	bindings.clear();
}

void InputHandler::ClearBindings(int keyCode)
{
	for (size_t ix = bindings.size(); ix > 0; --ix)
	{
		if (bindings[ix - 1].keyCode == keyCode)
		{
			bindings.erase(bindings.begin() + (ix - 1));
		}
	}
}

void InputHandler::SetPriority(int newPriority)
{
	// 값이 그대로면 정렬을 다시 할 이유가 없다.
	if (priority == newPriority)
	{
		return;
	}

	priority = newPriority;

	// 등록되어 있을 때만 정렬이 의미가 있다.
	if (isRegistered && InputSystem::HasInstance())
	{
		InputSystem::Get().RequestSort();
	}
}

bool InputHandler::IsEffectivelyEnabled() const
{
	if (!isEnabled)
	{
		return false;
	}

	// 훅이 없으면 통과. 있으면 소유자에게 물어본다.
	return enabledCheck ? enabledCheck() : true;
}

bool InputHandler::HandleInput(int keyCode, EInputEvent event, bool& outConsumed)
{
	// 이 키/이벤트에 걸린 바인딩이 하나라도 있었는지 여부.
	bool hasBinding = false;

	// 콜백 안에서 바인딩이 추가/제거될 수 있으므로 인덱스로 순회하고,
	// 시작 시점의 크기를 스냅샷으로 잡아둔다.
	const size_t bindingCount = bindings.size();

	for (size_t ix = 0; ix < bindingCount && ix < bindings.size(); ++ix)
	{
		// 참조로 잡으면 콜백이 vector를 재할당했을 때 무효화되므로 값으로 복사한다.
		const Binding binding = bindings[ix];

		if (binding.keyCode != keyCode || binding.event != event)
		{
			continue;
		}

		hasBinding = true;

		if (binding.consume)
		{
			outConsumed = true;
		}

		binding.callback();
	}

	return hasBinding;
}

NAME_SPACE_END
