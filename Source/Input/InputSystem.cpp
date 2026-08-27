#include "pch.h"
#include "InputSystem.h"
#include "Input/Input.h"

#include <algorithm>

NAME_SPACE_BEGIN(Craft)

// static 변수 초기화.
InputSystem* InputSystem::instance = nullptr;

InputSystem::InputSystem()
{
	// 시작할 때 instance 값은 null이어야 함.
	ASSERT_CRASH(instance == nullptr);
	instance = this;
}

InputSystem::~InputSystem()
{
	// 전역 접근 변수 정리.
	instance = nullptr;
}

InputSystem& InputSystem::Get()
{
	// 여기에서 instance는 null이면 안됨.
	ASSERT_CRASH(instance != nullptr);
	return *instance;
}

bool InputSystem::HasInstance()
{
	return instance != nullptr;
}

void InputSystem::RegisterHandler(const std::weak_ptr<InputHandler>& handler)
{
	// 예외 처리 - 이미 죽은 핸들러는 등록하지 않는다.
	std::shared_ptr<InputHandler> lockedHandler = handler.lock();

	if (nullptr == lockedHandler)
	{
		return;
	}

	HandlerEntry entry;
	entry.handler = handler;
	entry.rawHandler = lockedHandler.get();

	handlers.emplace_back(std::move(entry));

	// 새로 들어왔으니 우선순위 순서를 다시 잡아야 한다.
	needsSort = true;
}

void InputSystem::UnregisterHandler(const InputHandler* handler)
{
	for (size_t ix = 0; ix < handlers.size(); ++ix)
	{
		if (handlers[ix].rawHandler != handler)
		{
			continue;
		}

		handlers.erase(handlers.begin() + ix);
		break;
	}
}

void InputSystem::RefreshHandlers()
{
	// 소유자가 사라진 항목 정리.
	// 액터가 Destroy되면 컴포넌트와 함께 핸들러도 사라지므로
	// 별도의 해제 호출 없이 여기에서 빠진다.
	for (size_t ix = handlers.size(); ix > 0; --ix)
	{
		if (handlers[ix - 1].handler.expired())
		{
			handlers.erase(handlers.begin() + (ix - 1));
		}
	}

	if (!needsSort)
	{
		return;
	}

	// 우선순위 내림차순. 같은 값이면 등록 순서를 유지해야 하므로 stable_sort.
	// 위에서 만료된 항목을 걸렀으므로 여기서의 lock은 실패하지 않는다.
	std::stable_sort(handlers.begin(), handlers.end(),
		[](const HandlerEntry& lhs, const HandlerEntry& rhs)
		{
			const std::shared_ptr<InputHandler> lhsHandler = lhs.handler.lock();
			const std::shared_ptr<InputHandler> rhsHandler = rhs.handler.lock();

			const int lhsPriority = lhsHandler ? lhsHandler->GetPriority() : INT_MIN;
			const int rhsPriority = rhsHandler ? rhsHandler->GetPriority() : INT_MIN;

			return lhsPriority > rhsPriority;
		});

	needsSort = false;
}

void InputSystem::DispatchInput()
{
	RefreshHandlers();

	// 이번 프레임에 처리할 핸들러를 미리 잠가둔다.
	// 콜백이 액터를 파괴하거나 새 컴포넌트를 붙여도 순회가 흔들리지 않는다.
	dispatchCache.clear();

	for (const HandlerEntry& entry : handlers)
	{
		std::shared_ptr<InputHandler> lockedHandler = entry.handler.lock();

		if (nullptr == lockedHandler)
		{
			continue;
		}

		dispatchCache.emplace_back(std::move(lockedHandler));
	}

	const Input& input = Input::Get();

	for (int keyCode = 0; keyCode < KeyCount; ++keyCode)
	{
		// 이번 프레임에 아무 일도 없었던 키는 건너뛴다.
		if (!input.HasKeyActivity(keyCode))
		{
			continue;
		}

		// 이벤트 순서는 Pressed -> Held -> Released로 고정한다.
		//
		// 한 프레임 안에서 눌렀다 뗀 경우 isKeyDown이 false라 Held만 건너뛰고
		// Pressed와 Released가 순서대로 발생한다.
		if (input.GetKeyDown(keyCode))
		{
			RouteEvent(keyCode, EInputEvent::Pressed);
		}

		if (input.GetKey(keyCode))
		{
			RouteEvent(keyCode, EInputEvent::Held);
		}

		if (input.GetKeyUp(keyCode))
		{
			RouteEvent(keyCode, EInputEvent::Released);

			// 키를 뗐으므로 소유권을 놓는다.
			// Released를 보낸 다음에 풀어야 소비한 쪽이 짝을 받는다.
			keyOwners[keyCode].reset();
		}
	}

	// 다음 프레임까지 핸들러를 붙들고 있을 이유가 없다.
	dispatchCache.clear();
}

void InputSystem::RouteEvent(int keyCode, EInputEvent event)
{
	// 이 키를 이미 소유한 핸들러가 있으면 그쪽으로만 보낸다.
	std::shared_ptr<InputHandler> owner = keyOwners[keyCode].lock();

	if (nullptr != owner)
	{
		if (owner->IsEffectivelyEnabled())
		{
			bool consumed = false;
			owner->HandleInput(keyCode, event, consumed);
			return;
		}

		// 소유자가 비활성이 됐으면 소유권을 놓고 아래로 흘려보낸다.
		// (메뉴가 닫히는 등)
		keyOwners[keyCode].reset();
	}

	// 우선순위가 높은 쪽부터 순회.
	for (const std::shared_ptr<InputHandler>& handler : dispatchCache)
	{
		if (!handler->IsEffectivelyEnabled())
		{
			continue;
		}

		bool consumed = false;

		// 바인딩이 없으면 아무 일도 일어나지 않는다.
		handler->HandleInput(keyCode, event, consumed);

		// 모달은 바인딩이 없는 키까지 막는다.
		if (handler->IsBlockAllInput())
		{
			consumed = true;
		}

		if (!consumed)
		{
			continue;
		}

		// 이 키를 뗄 때까지 이 핸들러가 소유한다.
		keyOwners[keyCode] = handler;
		return;
	}
}

NAME_SPACE_END
