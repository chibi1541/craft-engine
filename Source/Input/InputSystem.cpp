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

		// 이벤트 순서는 프레임이 끝난 시점의 실제 상태를 보고 정한다.
		//
		// 순서를 Pressed -> Held -> Released로 고정하면, 한 프레임에
		// 눌림/뗌/눌림이 다 들어왔을 때(빠른 더블클릭) 마지막 전이가 눌림인데도
		// Released를 맨 뒤에 보내게 된다. 그러면 핸들러는 떼진 줄 아는데
		// Held는 계속 발생하고, 래치도 엉뚱한 시점에 풀린다.
		const bool isDown = input.GetKey(keyCode);
		const bool wasPressed = input.GetKeyDown(keyCode);
		const bool wasReleased = input.GetKeyUp(keyCode);

		if (isDown)
		{
			// 마지막 전이가 눌림이다.
			// 뗌 플래그도 켜져 있으면 그게 먼저 있었다는 뜻이므로,
			// Released를 먼저 보내 앞선 Pressed와 짝을 맞춘 뒤 새로 누른 것으로 친다.
			if (wasReleased)
			{
				RouteEvent(keyCode, EInputEvent::Released);

				// 키를 뗐으므로 소유권을 놓는다.
				// Released를 보낸 다음에 풀어야 소비한 쪽이 짝을 받는다.
				ReleaseKeyOwnership(keyCode);
			}

			if (wasPressed)
			{
				RouteEvent(keyCode, EInputEvent::Pressed);
			}

			RouteEvent(keyCode, EInputEvent::Held);
		}
		else
		{
			// 마지막 전이가 뗌이거나, 이미 떼진 채로 유지되는 상태.
			if (wasPressed)
			{
				RouteEvent(keyCode, EInputEvent::Pressed);
			}

			if (wasReleased)
			{
				RouteEvent(keyCode, EInputEvent::Released);

				ReleaseKeyOwnership(keyCode);
			}
		}
	}

	// 다음 프레임까지 핸들러를 붙들고 있을 이유가 없다.
	dispatchCache.clear();
}

void InputSystem::ReleaseKeyOwnership(int keyCode)
{
	keyOwners[keyCode].reset();
	keyHasOwner[keyCode] = false;
}

void InputSystem::RouteEvent(int keyCode, EInputEvent event)
{
	// 이 키를 이미 소유한 핸들러가 있으면 그쪽으로만 보낸다.
	if (keyHasOwner[keyCode])
	{
		std::shared_ptr<InputHandler> owner = keyOwners[keyCode].lock();

		if (nullptr != owner && owner->IsEffectivelyEnabled())
		{
			bool consumed = false;
			owner->HandleInput(keyCode, event, consumed);
			return;
		}

		// 소유자가 죽었거나 입력을 못 받는 상태가 됐다.
		//
		// 여기서 아래 우선순위로 흘려보내면 안 된다.
		// Pressed를 못 본 쪽이 Released나 Held만 받게 되기 때문이다.
		//
		// 실제로 이렇게 샌다:
		//   Esc Pressed -> 메뉴가 소비, 소유권 획득
		//   콜백이 메뉴를 닫음(숨김/파괴)
		//   DispatchInput이 곧바로 같은 프레임의 Held를 라우팅
		//   -> 소유자가 사라졌으니 게임플레이가 Esc Held를 받는다
		// 메뉴를 닫는 Esc가 그 프레임의 게임플레이까지 건드리는 셈이다.
		//
		// 그래서 소유권만 놓고(약참조 정리), keyHasOwner는 켜 둔 채
		// 키를 실제로 뗄 때까지 이 키의 이벤트를 전부 삼킨다.
		keyOwners[keyCode].reset();
		return;
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
		keyHasOwner[keyCode] = true;
		return;
	}
}

NAME_SPACE_END
