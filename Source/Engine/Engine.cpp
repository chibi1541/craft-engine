#include "pch.h"
#include "Engine.h"
#include "Level/Level.h"
#include "Input/Input.h"
#include "Render/Renderer.h"

#include <memory>

NAME_SPACE_BEGIN(Craft)

Engine* Engine::instance = nullptr;

Engine::Engine()
{
	ASSERT_CRASH(instance == nullptr);
	instance = this;

	// 엔진 설정 로드
	LoadEngineSetting();

	// 입력 객체 생성
	input = std::make_unique<Input>();

	// 랜더러 객체 생성
	renderer = std::make_unique<Renderer>(Vector2(setting.width, setting.height));

}

Engine::~Engine()
{
	instance = nullptr;
}

void Engine::Run()
{
	// 고해당도 타이머 사용.
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);

	// 현재 시간 읽기.
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);

	int64 current = counter.QuadPart;
	int64 previous = current;

	// 고정 프레임으로 만들기 위한 값
	float oneFrameTime = 1.0f / setting.framerate;

	// 엔진 루프
	while (true)
	{
		// 종료 조건
		if (isQuit)
		{
			break;
		}

		// 프레임 처리
		// 1. 입력 처리
		ProcessInput();

		// 프레임 시간 계산
		QueryPerformanceCounter(&counter);
		current = counter.QuadPart;
		float deltaTime = static_cast<float>(current - previous) / static_cast<float>(frequency.QuadPart);

		// 고정 프레임
		if (deltaTime >= oneFrameTime)
		{
			// 2. 게임 이벤트 호출
			OnInitialized();

			// 아마 플레그를 둬서 한번만 처리하는 방식으로 구현하겠지...
			BeginPlay();

			Tick(deltaTime);

			// 화면 그리기.
			Draw();

			// 이 위까지 호출이 완료되면 프레임 처리 완료됨.
			
			// 레벨 전환 처리
			if (nullptr != nextLevel)
			{
				if (nullptr != mainLevel)
				{
					mainLevel.reset();
				}

				mainLevel = nextLevel;

				// 스마트 포인터 정리
				nextLevel.reset();
			}

			// 추가 or 제거 요청된 액터 정리
			if (mainLevel)
			{
				mainLevel->ProcessAddAndDestoryActors();
			}

			// 이번 프레임의 입력 상태 캐싱
			SavePreviousInputState();

			// 현재 시간 초기화
			QueryPerformanceCounter(&counter);
			previous = counter.QuadPart;
		}

	}

	// 종료 처리 함수 호출
	Shutdown();
}
void Engine::Quit()
{
	// 엔진 종료 플래그 true
	isQuit = true;
}

Engine& Engine::Get()
{
	// 뭐임? 이런 구문 왜 만들어짐?
	// TODO: insert return statement here
	
	// TaskList에 TODO 구문만 모아서 보여줌;; 몰랐다...

	if (instance == nullptr)
	{
		instance = new Engine();
	}

	// && 방식으로 출력하고 싶은 구문 추가도 가능
	// <cassert> 헤더 추가
	//assert(instance && "instance is null");
	ASSERT_CRASH(instance);

	return *instance;
}

std::shared_ptr<Level> Engine::GetLevel()
{
	ASSERT_CRASH(mainLevel || nextLevel);

	return (mainLevel) ? mainLevel : nextLevel;
}

void Engine::ProcessInput()
{
	ASSERT_CRASH(input != nullptr);
	if (input == nullptr)
	{
		return;
	}

	input->ProcessInput();
}

void Engine::OnInitialized()
{
	// 레벨의 초기화 처리

	// 예외 처리
	if (!mainLevel || mainLevel->HasInitialized())
	{
		return;
	}

	// 초기화 이벤트 호출.
	mainLevel->OnInitialized();
}

void Engine::BeginPlay()
{
	if (!mainLevel)
	{
		return;
	}

	// 레벨에 이벤트 전달
	mainLevel->BeginPlay();
}

void Engine::Tick(float deltaTime)
{
	// 상용 엔진의 경우 code style은 얼리 아웃의 경우가 많음
	if (!mainLevel)
	{
		return;
	}

	mainLevel->Tick(deltaTime);
}

void Engine::Draw()
{
	if (!mainLevel)
	{
		return;
	}

	// 여기서 레벨에 속해있는 액터 객체가 rendercommand에 드로우콜을 등록
	mainLevel->Draw();

	if (!renderer)
		return;

	renderer->Draw();
}

void Engine::SavePreviousInputState()
{
	ASSERT_CRASH(input != nullptr);
	if (input == nullptr)
	{
		return;
	}

	input->SavePreviousStates();
}

void Engine::Shutdown()
{

}

void Engine::LoadEngineSetting()
{
	// 파일 열기(개행 문자 처리를 쉽게 하기 위해 텍스트 모드로 열기)
	FILE* file = nullptr;
	fopen_s(&file, "../Config/Setting.txt", "rt");

	// 예외 처리
	if (nullptr == file)
	{
		std::cout << "Failed to open engine setting file. \n";

		// 디버그 모드에서 강제 중단 시키는 기능
		__debugbreak();
		return;
	}

	// 데이터 읽어오기.
	//const int bufferSize = 2048;
	char buffer[Setting::BUFFER_SIZE] = {};

	size_t readSize = fread(buffer, sizeof(char), Setting::BUFFER_SIZE, file);


	// 문자열 파싱
	// 문자열 자르기
	char* context = nullptr;
	char* token = nullptr;

	// 처음 자를 때는 문자열 전체를 첫번째 인자로 받음
	// 잘린 값은 token에 나머지 문자열은 context에 저장
	token = strtok_s(buffer, "\n", &context);

	// 자르는 작업 반복
	while (nullptr != token)
	{
		// 공백 전까지 읽은 문자열을 저장할 변수
		char key[15] = {};

		// 포맷을 지정한 문자열 읽기
		// 공백 문자 전까지 읽어들임(공백은 안들어감)
		sscanf_s(token, "%s", key, 15);

		// 키 값을 비교해서 값 설정
		if (strcmp(key, "framerate") == 0)
		{
			sscanf_s(token, "framerate = %f", &setting.framerate);
		}
		else if (strcmp(key, "width") == 0)
		{
			sscanf_s(token, "width = %d", &setting.width);
		}
		else if (strcmp(key, "height") == 0)
		{
			sscanf_s(token, "height = %d", &setting.height);
		}

		// 나머지 문자열 자르기(개행 문자를 기준으로)
		token = strtok_s(nullptr, "\n", &context);
	}

	fclose(file);
	file = nullptr;
}

NAME_SPACE_END