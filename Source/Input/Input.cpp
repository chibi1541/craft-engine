#include "pch.h"
#include "Input.h"
#include <cassert>
#include <Windows.h>

namespace Craft
{
	// static 변수 초기화.
	Input* Input::instance = nullptr;

	Input::Input()
	{
		// 시작할 때 instance 값은 null이어야 함.
		assert(!instance && "instance should be null here.");
		instance = this;

		// 콘솔 입력 버퍼의 핸들을 가져옴.
		inputHandle = GetStdHandle(STD_INPUT_HANDLE);

		// 콘솔 입력 핸들을 가져오지 못했으면 종료.
		if (inputHandle == INVALID_HANDLE_VALUE || inputHandle == nullptr)
		{
			return;
		}

		// 현재 콘솔 입력 모드를 가져옴.
		if (GetConsoleMode(inputHandle, &originalConsoleMode))
		{
			// 변경할 콘솔 입력 모드 설정.
			DWORD inputMode = originalConsoleMode;

			// 마우스 이벤트 입력 활성화.
			inputMode |= ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT;

			// 빠른 편집 모드가 활성화되어 있으면 마우스 입력이
			// 콘솔의 드래그 선택 기능으로 처리되므로 비활성화.
			inputMode &= ~ENABLE_QUICK_EDIT_MODE;

			// 변경한 콘솔 입력 모드를 적용하고 성공 여부 저장.
			shouldRestoreConsoleMode = SetConsoleMode(inputHandle, inputMode) != FALSE;
		}
	}

	Input::~Input()
	{
		// 입력 모드 변경에 성공했으면 기존 콘솔 입력 모드로 복구.
		if (shouldRestoreConsoleMode)
		{
			SetConsoleMode(inputHandle, originalConsoleMode);
		}

		// 전역 접근 변수 정리.
		instance = nullptr;
	}

	bool Input::GetKeyDown(int keyCode) const
	{
		return keyStates[keyCode].pressedThisFrame;
	}

	bool Input::GetKeyUp(int keyCode) const
	{
		return keyStates[keyCode].releasedThisFrame;
	}

	bool Input::GetKey(int keyCode) const
	{
		return keyStates[keyCode].isKeyDown;
	}

	bool Input::HasKeyActivity(int keyCode) const
	{
		const KeyState& state = keyStates[keyCode];

		return state.isKeyDown
			|| state.pressedThisFrame
			|| state.releasedThisFrame;
	}

	Input& Input::Get()
	{
		// 여기에서 instance는 null이면 안됨.
		assert(instance && "instance should not be null here");
		return *instance;
	}

	void Input::UpdateKeyState(int keyCode, bool isKeyDown)
	{
		KeyState& state = keyStates[keyCode];

		// 상태가 그대로면 전이가 아니므로 아무것도 기록하지 않는다.
		// 자동 반복 이벤트와 마우스 이동 이벤트가 여기서 걸러진다.
		if (state.isKeyDown == isKeyDown)
		{
			return;
		}

		// 실제로 바뀐 순간에만 전이를 기록.
		// 한 프레임 안에서 눌렀다 떼면 두 플래그가 모두 켜진 채로 남는다.
		if (isKeyDown)
		{
			state.pressedThisFrame = true;
		}
		else
		{
			state.releasedThisFrame = true;
		}

		state.isKeyDown = isKeyDown;
	}

	void Input::SetScreenCellSize(const Vector2& newScreenCellSize)
	{
		screenCellSize = newScreenCellSize;
	}

	void Input::PollMousePosition()
	{
		// 화면 크기를 아직 모르면 환산할 수가 없다. 이벤트로 들어온 값을 그대로 둔다.
		if (screenCellSize.x <= 0 || screenCellSize.y <= 0)
		{
			return;
		}

		// 포커스가 없으면 커서가 남의 창 위에 있는 것이다.
		// 그때 읽은 좌표로 조준 방향을 바꾸면 창을 다시 누르는 순간 캐릭터가 홱 돈다.
		if (!HasConsoleFocus())
		{
			return;
		}

		const HWND consoleWindow = GetConsoleWindow();

		if (nullptr == consoleWindow)
		{
			return;
		}

		RECT clientRect = {};

		if (!GetClientRect(consoleWindow, &clientRect))
		{
			return;
		}

		const int clientWidth = clientRect.right - clientRect.left;
		const int clientHeight = clientRect.bottom - clientRect.top;

		// 창이 최소화되면 클라이언트 영역이 0이 된다. 0으로 나누지 않는다.
		if (clientWidth <= 0 || clientHeight <= 0)
		{
			return;
		}

		POINT cursor = {};

		if (!GetCursorPos(&cursor))
		{
			return;
		}

		if (!ScreenToClient(consoleWindow, &cursor))
		{
			return;
		}

		// 창 밖은 가장자리로 자른다.
		//
		// 폰트 크기(8x8)를 상수로 쓰지 않고 클라이언트 영역을 격자 수로 나누는 이유 -
		// 콘솔 창은 DPI 배율을 타서 실제 셀 픽셀 크기가 설정값과 다를 수 있다.
		// 클라이언트 영역 전체가 곧 화면 격자이므로 비례로 계산하면 항상 맞는다.
		//
		// 자르기를 나눗셈 전에 하는 것이 중요하다. 정수 나눗셈은 0쪽으로 잘려서
		// 커서가 창 왼쪽 밖(-1픽셀)에 있어도 0열이 나오고, 그러면 왼쪽 끝과 구분이 안 된다.
		int cursorX = static_cast<int>(cursor.x);
		int cursorY = static_cast<int>(cursor.y);

		cursorX = (cursorX < 0) ? 0 : ((cursorX > clientWidth - 1) ? clientWidth - 1 : cursorX);
		cursorY = (cursorY < 0) ? 0 : ((cursorY > clientHeight - 1) ? clientHeight - 1 : cursorY);

		mousePosition.x = (cursorX * screenCellSize.x) / clientWidth;
		mousePosition.y = (cursorY * screenCellSize.y) / clientHeight;
	}

	bool Input::HasConsoleFocus() const
	{
		return GetConsoleWindow() == GetForegroundWindow();
	}

	void Input::ReconcileMouseButtons()
	{
		// 포커스가 있을 때만 보정한다.
		// GetAsyncKeyState는 창과 무관하게 전역 상태를 읽기 때문에,
		// 포커스가 없을 때 부르면 다른 창에서 누른 버튼이 그대로 들어온다.
		if (!HasConsoleFocus())
		{
			return;
		}

		// 콘솔은 물리적 위치(가장 왼쪽 버튼)를 보고하는데 VK_LBUTTON은 논리적인
		// 주 버튼이라, 사용자가 좌우 버튼을 바꿔 놨으면 둘이 어긋난다.
		// 이벤트 스트림과 싸우지 않도록 여기서 맞춰준다.
		const bool isSwapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;

		const int leftKeyCode = isSwapped ? VK_RBUTTON : VK_LBUTTON;
		const int rightKeyCode = isSwapped ? VK_LBUTTON : VK_RBUTTON;

		// 이미 이벤트로 갱신된 상태와 같으면 UpdateKeyState가 전이로 치지 않으므로
		// 아무 일도 일어나지 않는다. 어긋났을 때만 전이가 기록된다.
		UpdateKeyState(VK_LBUTTON, (GetAsyncKeyState(leftKeyCode) & 0x8000) != 0);
		UpdateKeyState(VK_RBUTTON, (GetAsyncKeyState(rightKeyCode) & 0x8000) != 0);
		UpdateKeyState(VK_MBUTTON, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
	}

	void Input::ProcessInput()
	{
		// 콘솔 입력 핸들이 유효하지 않으면 입력 처리 종료.
		if (inputHandle == INVALID_HANDLE_VALUE || inputHandle == nullptr)
		{
			return;
		}

		// 한 번에 읽어올 콘솔 입력 이벤트 배열.
		INPUT_RECORD records[128] = { };

		// 콘솔 입력 버퍼에 대기 중인 이벤트 수.
		DWORD pendingEventCount = 0;

		// 한 프레임 동안 입력 버퍼에 쌓인 이벤트를 모두 처리.
		while (GetNumberOfConsoleInputEvents(inputHandle, &pendingEventCount)
			&& pendingEventCount > 0)
		{
			// 실제로 읽어온 이벤트 수를 저장할 변수.
			DWORD readEventCount = 0;

			// 배열 크기를 넘지 않도록 한 번에 읽을 이벤트 수 결정.
			const DWORD readCount = pendingEventCount < 128 ? pendingEventCount : 128;

			// 콘솔 입력 버퍼에서 이벤트 읽기.
			if (!ReadConsoleInput(inputHandle, records, readCount, &readEventCount))
			{
				break;
			}

			// 읽어온 입력 이벤트를 순서대로 처리.
			for (DWORD ix = 0; ix < readEventCount; ++ix)
			{
				// 현재 처리할 입력 이벤트.
				const INPUT_RECORD& record = records[ix];

				// 입력 이벤트 종류에 따라 처리.
				switch (record.EventType)
				{
				case KEY_EVENT:
				{
					// 키보드 이벤트 정보 가져오기.
					const KEY_EVENT_RECORD& keyEvent = record.Event.KeyEvent;

					// 입력된 키의 가상 키 코드 가져오기.
					const WORD keyCode = keyEvent.wVirtualKeyCode;

					// 관리하는 키 배열 범위 안에 있는지 확인.
					if (keyCode < KeyCount)
					{
						// 키가 눌렸는지 또는 해제됐는지 현재 상태에 저장.
						UpdateKeyState(keyCode, keyEvent.bKeyDown != FALSE);
					}
					break;
				}

				case MOUSE_EVENT:
				{
					// 마우스 이벤트 정보 가져오기.
					const MOUSE_EVENT_RECORD& mouseEvent = record.Event.MouseEvent;

					// 마우스 포인터의 콘솔 셀 좌표 저장.
					mousePosition.x = mouseEvent.dwMousePosition.X;
					mousePosition.y = mouseEvent.dwMousePosition.Y;

					// 마우스 버튼과 가상 키 코드를 연결하기 위한 구조체.
					const struct MouseButton
					{
						// 키 상태 배열에서 사용할 가상 키 코드.
						int keyCode;

						// 마우스 이벤트에서 버튼 상태를 확인할 비트 값.
						DWORD buttonMask;
					} mouseButtons[] = {
						{ VK_LBUTTON, FROM_LEFT_1ST_BUTTON_PRESSED },
						{ VK_RBUTTON, RIGHTMOST_BUTTON_PRESSED },
						{ VK_MBUTTON, FROM_LEFT_2ND_BUTTON_PRESSED }
					};

					// 왼쪽, 오른쪽, 가운데 마우스 버튼 상태 처리.
					//
					// 마우스 이벤트는 커서를 움직이기만 해도 발생하고 그때마다
					// 눌려 있는 버튼 상태가 그대로 다시 실려 온다.
					// UpdateKeyState가 전이만 기록하므로 Pressed가 중복되지 않고,
					// 이벤트가 더 안 와도 눌린 상태는 그대로 유지된다.
					for (const MouseButton& button : mouseButtons)
					{
						// 버튼이 눌렸는지 비트 연산으로 확인한 후 키 상태에 저장.
						UpdateKeyState(button.keyCode,
							(mouseEvent.dwButtonState & button.buttonMask) != 0);
					}
					break;
				}

				case FOCUS_EVENT:
					// 콘솔 창이 입력 포커스를 잃었는지 확인.
					if (!record.Event.FocusEvent.bSetFocus)
					{
						// 포커스를 잃는 동안 KeyUp 이벤트가 누락되어
						// 키가 계속 눌린 상태로 남는 것을 방지.
						//
						// 그냥 isKeyDown만 지우면 뗌 전이가 기록되지 않아서
						// InputSystem의 키 소유권(래치)이 영원히 안 풀린다.
						// UpdateKeyState로 지워야 Released 이벤트까지 정상 발생한다.
						for (int keyCode = 0; keyCode < KeyCount; ++keyCode)
						{
							UpdateKeyState(keyCode, false);
						}
					}
					break;
				}
			}
		}

		// 이벤트를 다 읽은 뒤 마우스 버튼만 실제 상태로 보정한다.
		// 더블클릭의 두 번째 해제 이벤트가 오지 않아 눌린 채로 남는 것을 여기서 푼다.
		// 키보드는 해제 이벤트가 신뢰할 만하므로 대상이 아니다.
		ReconcileMouseButtons();

		// 커서 위치도 실제 값으로 덮어쓴다. 이벤트보다 뒤인 것이 중요하다 -
		// 이벤트로 들어온 위치는 그 이벤트가 발생한 시점의 값이고, 이쪽이 지금 값이다.
		//
		// 버튼과 달리 위치는 프레임마다 반드시 최신이어야 한다.
		// 조준 방향처럼 매 프레임 각을 다시 재는 쪽에서는, 이벤트가 뜸한 순간마다
		// 방향이 멈췄다가 툭 튀는 것으로 보인다.
		PollMousePosition();
	}

	void Input::SavePreviousStates()
	{
		// 프레임 단위 전이 플래그 정리.
		// 여기가 프레임의 경계다. isKeyDown은 실제 상태이므로 건드리지 않는다.
		for (KeyState& state : keyStates)
		{
			state.pressedThisFrame = false;
			state.releasedThisFrame = false;
		}
	}
}
