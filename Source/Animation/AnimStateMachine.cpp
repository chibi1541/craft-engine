#include "pch.h"
#include "AnimStateMachine.h"

NAME_SPACE_BEGIN(Craft)

void AnimStateMachine::AddState(const AnimState& state)
{
	states.emplace_back(state);

	// 시작 상태를 따로 지정하지 않아도 첫 번째 상태부터 돌게 해둔다.
	if (entryStateIndex < 0)
	{
		entryStateIndex = 0;
		currentStateIndex = 0;
	}
}

void AnimStateMachine::AddTransition(const AnimTransition& transition)
{
	transitions.emplace_back(transition);
}

bool AnimStateMachine::SetEntryState(const std::string& name)
{
	const int index = FindStateIndex(name);

	if (index < 0)
	{
		return false;
	}

	entryStateIndex = index;
	currentStateIndex = index;

	return true;
}

bool AnimStateMachine::Evaluate(const AnimEvalContext& context)
{
	// 얼리 아웃 - 상태가 없으면 판단할 것도 없다.
	if (states.empty())
	{
		return false;
	}

	// 아직 현재 상태가 없으면 시작 상태로.
	if (currentStateIndex < 0)
	{
		currentStateIndex = (entryStateIndex >= 0) ? entryStateIndex : 0;

		return true;
	}

	bool hasChanged = false;

	// 전이한 직후 새 상태에서 또 전이할 조건이 참일 수 있어서 반복한다.
	// (Idle -> Attack -> Recover 처럼 한 틱에 여러 칸 건너뛰는 경우)
	// 다만 무한 연쇄를 막기 위해 횟수를 제한한다.
	// TODO : 상태 전이 흐름을 눈으로 확인 할 수 있도록 디버그 기능 추가
	for (int step = 0; step < MaxTransitionsPerTick; ++step)
	{
		const AnimTransition* firedTransition = FindFirstMatchingTransition(context);

		if (nullptr == firedTransition)
		{
			break;
		}

		const int targetIndex = FindStateIndex(firedTransition->toStateName);

		// 로더가 대상 상태의 존재를 검증하므로 정상 데이터면 여기 걸리지 않는다.
		if (targetIndex < 0)
		{
			break;
		}

		currentStateIndex = targetIndex;
		hasChanged = true;
	}

	return hasChanged;
}

const AnimState* AnimStateMachine::GetCurrentState() const
{
	if (currentStateIndex < 0 || currentStateIndex >= static_cast<int>(states.size()))
	{
		return nullptr;
	}

	return &states[currentStateIndex];
}

void AnimStateMachine::Reset()
{
	currentStateIndex = (entryStateIndex >= 0) ? entryStateIndex : (states.empty() ? -1 : 0);
}

bool AnimStateMachine::HasState(const std::string& name) const
{
	return FindStateIndex(name) >= 0;
}

const AnimState* AnimStateMachine::GetState(int index) const
{
	if (index < 0 || index >= static_cast<int>(states.size()))
	{
		return nullptr;
	}

	return &states[index];
}

const AnimTransition* AnimStateMachine::GetTransition(int index) const
{
	if (index < 0 || index >= static_cast<int>(transitions.size()))
	{
		return nullptr;
	}

	return &transitions[index];
}

int AnimStateMachine::FindStateIndex(const std::string& name) const
{
	for (int index = 0; index < static_cast<int>(states.size()); ++index)
	{
		if (states[index].name == name)
		{
			return index;
		}
	}

	return -1;
}

const AnimTransition* AnimStateMachine::FindFirstMatchingTransition(const AnimEvalContext& context) const
{
	const AnimState* currentState = GetCurrentState();

	if (nullptr == currentState)
	{
		return nullptr;
	}

	for (const AnimTransition& transition : transitions)
	{
		// 출발 상태가 비어있으면 Any State. 아니면 현재 상태와 같아야 한다.
		if (!transition.fromStateName.empty() && transition.fromStateName != currentState->name)
		{
			continue;
		}

		// 이미 그 상태다. Any State 전이가 매 틱 자기 자신으로 발동하는 걸 막는다.
		if (transition.toStateName == currentState->name)
		{
			continue;
		}

		// 조건이 전부 참이어야 한다(AND). 조건이 없으면 무조건 전이.
		bool allConditionsMet = true;

		for (const AnimCondition& condition : transition.conditions)
		{
			if (condition.Evaluate(context))
			{
				continue;
			}

			allConditionsMet = false;

			break;
		}

		if (allConditionsMet)
		{
			return &transition;
		}
	}

	return nullptr;
}

NAME_SPACE_END
