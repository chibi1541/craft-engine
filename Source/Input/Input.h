#pragma once


NAME_SPACE_BEGIN(Craft)


class CRAFT_API Input
{
	friend class Engine;

	enum { KeyCount = 256, };

	// 키 입력 상태를 저장하기 위한 구조체
	struct KeyState
	{
		// 현재 프레임 키 입력 여부
		bool isKeyDown = false;

		// 이전 프레임 키 입력 여부
		bool wasKeyDown = false;

	};

public:
	Input();
	~Input() = default;

	// 키 눌림/해제 여부 확인 함수
	// 이전 프레임에 안눌렸다가 이번 프레임에 눌리면 true를 반환
	bool GetKeyDown(int KeyCode) const;

	// 이전 프레임에 눌렸다가 이번 프레임에 안눌리면 true를 반환
	bool GetKeyUp(int KeyCode) const;

	// 현재 프레임에 입력이 눌리면 반복해서 true를 반환하는 함수.
	bool GetKey(int KeyCode) const;

	// 외부에서 접근 할 때 호출하는 함수
	static Input& Get();

private:
	// 언리얼에서는 EngineLoop의 Tick 단계에서 Slate를 통해 이번 프레임에 처리할 입력 값을
	// 모아 놓고 Engine::Tick으로 진입해서 입력을 처리
	
	// 현재 프레임에 특정 키 입력이 발생했는지를 처리하는 함수
	void ProcessInput();

	// 이전 프레임에 키 눌림 상태를 저장하는 함수
	void SavePreviousStates();

private:

	// 가상 키의 수(256가지)
	// 이거 왜 enum으로 안씀?
	//const int keyCount = 256;

	// 키 상태를 관리할 배열
	KeyState keyStates[KeyCount] = {};

	// 전역 접근이 가능하도록 변수 추가.
	static Input* instance;
};


NAME_SPACE_END

