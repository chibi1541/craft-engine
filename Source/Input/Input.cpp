#include "pch.h"
#include "Input.h"
#include <cassert>
#include <Windows.h>

NAME_SPACE_BEGIN(Craft)

Input* Input::instance = nullptr;

Input::Input()
{
	// 시작할 때 instance 값은 null이어야 함
	ASSERT_CRASH(instance == nullptr);
	instance = this;
}

bool Input::GetKeyDown(int KeyCode) const
{
	return keyStates[KeyCode].wasKeyDown == false && keyStates[KeyCode].isKeyDown;
}

bool Input::GetKeyUp(int KeyCode) const
{
	return keyStates[KeyCode].wasKeyDown && keyStates[KeyCode].isKeyDown == false;
}

bool Input::GetKey(int KeyCode) const
{
	return keyStates[KeyCode].isKeyDown && keyStates[KeyCode].wasKeyDown;
}

Input& Input::Get()
{
	// 여기에서 instance는 nullptr이면 안됨
	ASSERT_CRASH(instance != nullptr);
	return *instance;
}

void Input::ProcessInput()
{
	// 현재 프레임에 키 입력이 발생했는지 확인
	for (int ix = 0; ix < KeyCount; ++ix)
	{
		// 키눌림 여부 저장
		// 0x8000은 최상위 비트만 1 => [1000 0000][0000 0000][0000 0000][0000 0000]
		keyStates[ix].isKeyDown = ((GetAsyncKeyState(ix) & 0x8000) != 0);

	}

}

void Input::SavePreviousStates()
{
	// 이전 프레임 값 캐싱
	for (KeyState& state : keyStates)
	{
		state.wasKeyDown = state.isKeyDown;
	}

}

NAME_SPACE_END
