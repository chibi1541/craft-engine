#include "pch.h"
#include "Engine.h"
#include "Level/Level.h"
#include "Input/Input.h"
#include "Input/InputSystem.h"
#include "Render/Renderer.h"
#include "Camera/CameraManager.h"
#include "Math/Palette.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetTypes.h"
#include "Asset/SpriteAnimationLoader.h"
#include "Math/SymbolPalette.h"
#include "Thread/ThreadManager.h"
#include "Job/JobQueue.h"
#include "UI/UISystem.h"

#include <memory>

NAME_SPACE_BEGIN(Craft)

Engine* Engine::instance = nullptr;

Engine::Engine()
{
	ASSERT_CRASH(instance == nullptr);
	instance = this;

	// 다른 쓰레드 -> 게임 쓰레드 작업 큐.
	//
	// 생성자 맨 앞이다. 아래에서 애셋 워커가 뜨고, 컨텐츠 코드가 네트워크 쓰레드를
	// 띄우기도 한다. 그 어느 쪽보다 먼저 존재해야 첫 잡을 흘리지 않는다.
	gameThreadQueue = std::make_shared<JobQueue>();

	// 엔진 설정 로드
	LoadEngineSetting();

	// 팔레트 로드.
	// ScreenBuffer 생성자가 팔레트를 읽으므로 Renderer보다 먼저 불러야 한다.
	// 실패해도 기본 팔레트로 동작하므로 반환값은 확인하지 않는다.
	Palette::LoadFromFile(L"../Config/Palette.xml");

	// 입력 객체 생성
	input = std::make_unique<Input>();

	// 입력 디스패처 생성.
	// 디스패치할 때 Input의 상태를 읽으므로 반드시 input 다음에 만든다.
	inputSystem = std::make_unique<InputSystem>();

	// 랜더러 객체 생성
	renderer = std::make_unique<Renderer>(Vector2(setting.width, setting.height));

	// 카메라 매니저 생성.
	// 뷰 크기로 렌더러가 실제로 잡은 화면 크기를 쓰므로 renderer 다음이어야 한다.
	cameraManager = std::make_unique<CameraManager>(renderer->GetScreenSize());

	// UI 시스템 생성.
	// 위젯 배치가 화면 크기를 필요로 하므로 renderer 다음이어야 한다.
	uiSystem = std::make_unique<UI::UISystem>();

	// 애셋 매니저 생성 + 타입별 로더 등록.
	assetManager = std::make_unique<AssetManager>();
	assetManager->RegisterLoader<AnimationClipSet>(
		[](const WCHAR* path)
		{
			return std::make_shared<const AnimationClipSet>(SpriteAnimationLoader::LoadFromFile(path));
		});

	// 워커를 띄우기 전에 메인에서 한 번 만들어 둔다.
	// 애셋 로드 경로에서 유일한 지연 초기화 정적(함수 안 static)이라,
	// 여기서 미리 만들어 두면 여러 워커가 동시에 처음 만나는 상황 자체가 없어진다.
	SymbolPalette::GetTable();

	// 애셋 파싱용 워커 기동.
	// 지금 애셋 수로는 2개면 충분하고, 늘려도 디스크가 병목이라 이득이 없다.
	threadManager = std::make_unique<ThreadManager>();

	for (int i = 0; i < 2; ++i)
	{
		threadManager->Launch([this]() { assetManager->WorkerLoop(); });
	}
}

Engine::~Engine()
{
	// 멤버는 선언 역순으로 파괴된다. threadManager가 assetManager보다 먼저 죽는데,
	// ThreadManager 소멸자가 Join()을 부르므로 여기서 워커를 먼저 멈춰두지 않으면
	// 무한 루프인 워커를 기다리며 그대로 멈춰 선다.
	//
	// Run()이 정상 종료되면 Shutdown()에서 이미 처리되지만,
	// Run()을 안 타고 파괴되는 경우까지 덮기 위해 여기서도 보장한다.
	Shutdown();

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

		// 프레임 시간 계산
		QueryPerformanceCounter(&counter);
		current = counter.QuadPart;
		float deltaTime = static_cast<float>(current - previous) / static_cast<float>(frequency.QuadPart);

		// 고정 프레임
		if (deltaTime >= oneFrameTime)
		{
			// 1. 입력 처리.
			//
			// 반드시 프레임 게이트 안에서 호출해야 한다.
			// 밖에 두면 대기하는 동안 콘솔 입력 버퍼를 수백 번 드레인하게 되어
			// 프레임 단위 전이 플래그가 실제 프레임 경계와 어긋난다.
			// 대기 중에는 콘솔 입력 버퍼가 이벤트를 대신 보관하므로 유실되지 않는다.
			ProcessInput();

			// 프레임 수 측정
			UpdateFps(deltaTime);

			// 2. 게임 이벤트 호출
			OnInitialized();

			// 아마 플레그를 둬서 한번만 처리하는 방식으로 구현하겠지...
			BeginPlay();

			// 3. 다른 쓰레드에서 넘어온 작업 처리(주로 서버에서 도착한 패킷).
			//
			// DispatchInput 앞인 것이 중요하다.
			// 서버가 확정한 상태를 먼저 반영하고, 그 위에 이번 프레임의 로컬 입력을 얹는다.
			// 순서가 뒤집히면 이번 프레임의 예측이 지난 프레임 상태 위에 얹히고,
			// 서버 보정이 항상 한 프레임 늦게 적용된다.
			//
			// BeginPlay 뒤인 이유 - 이번 프레임에 막 올라온 액터도 잡의 대상이 될 수 있다.
			PumpGameThreadJobs();

			// 4. 입력 이벤트 전달.
			// BeginPlay에서 막 등록된 InputComponent도 이번 프레임부터 입력을 받는다.
			DispatchInput();

			Tick(deltaTime);

			// 화면 그리기.
			Draw();

			// 이 위까지 호출이 완료되면 프레임 처리 완료됨.
			
			// 레벨 전환 처리
			if (nullptr != nextLevel)
			{
				if (nullptr != mainLevel)
				{
					// 레벨에 딸린 UI(HUD 등)를 함께 내린다.
					// 안 그러면 이전 레벨의 창이 새 레벨로 그대로 넘어온다.
					// 메인 메뉴처럼 persistent로 올린 것만 남는다.
					//
					// 이전 레벨이 있을 때만 하는 것이 중요하다.
					// 첫 레벨을 올리는 건 "교체"가 아니라 최초 적재다.
					// 여기서 무조건 지우면 Run() 전에 올려둔 UI가
					// 첫 프레임에 통째로 사라진다.
					if (uiSystem)
					{
						uiSystem->ClearNonPersistent();
					}

					// 등록 카메라와 활성 카메라를 비운다(viewOrigin은 유지).
					if (cameraManager)
					{
						cameraManager->Reset();
					}

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

			// 추가 or 제거 요청된 위젯 정리.
			//
			// 액터와 같은 자리에서 같은 방식으로 처리한다.
			// 프레임 도중에 위젯이 사라지지 않으므로, 입력 콜백 안에서 창을 닫아도
			// 그 프레임의 남은 입력 처리가 죽은 핸들러를 만나는 일이 없다.
			if (uiSystem)
			{
				uiSystem->ProcessPendingWidgets();
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

void Engine::RunOnGameThread(std::function<void()> job)
{
	// 어느 쓰레드에서 불려도 안전하다. LockQueue가 락을 잡는다.
	// 밀어 넣기만 하고 바로 돌아온다 - 미는 쓰레드가 소비까지 하지 않는다.
	gameThreadQueue->DoAsync(std::move(job));
}

void Engine::AddShutdownHandler(std::function<void()> handler)
{
	shutdownHandlers.emplace_back(std::move(handler));
}

void Engine::PumpGameThreadJobs()
{
	// 쌓인 잡을 전부 소비한다.
	//
	// 잡 안에서 다시 RunOnGameThread를 불러도 이번 프레임에 다시 돌지 않는다.
	// Execute()가 PopAll로 스냅샷을 뜬 뒤 그것만 돌기 때문에 새 잡은 다음 프레임 몫이다.
	// (한 프레임 안에서 무한히 도는 일이 없다)
	gameThreadQueue->Execute();
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

void Engine::DispatchInput()
{
	ASSERT_CRASH(inputSystem != nullptr);
	if (inputSystem == nullptr)
	{
		return;
	}

	inputSystem->DispatchInput();
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

int Engine::GetWidth() const
{
	// 렌더러가 실제로 잡은 크기가 진실이다. 설정값은 "요청"일 뿐이다.
	return renderer ? renderer->GetScreenSize().x : setting.width;
}

int Engine::GetHeight() const
{
	return renderer ? renderer->GetScreenSize().y : setting.height;
}

void Engine::Tick(float deltaTime)
{
	// 레벨 유무와 무관하게 애셋 유휴 정리는 항상 돈다.
	if (assetManager)
	{
		assetManager->Tick(deltaTime);
	}

	// 레벨이 없어도 엔진은 계속 돈다.
	// 레벨 없이 메인 메뉴만 띄우는 상태가 있기 때문에,
	// 여기서 통째로 얼리 아웃하면 그런 화면이 아예 동작하지 않는다.
	if (mainLevel)
	{
		mainLevel->Tick(deltaTime);
	}

	// 액터가 움직인 뒤 카메라가 따라가고, 그 결과를 같은 프레임의 UI가 읽는다.
	// 그래서 mainLevel->Tick 뒤, uiSystem->Tick 앞이다.
	if (cameraManager)
	{
		cameraManager->Tick(deltaTime);
	}

	// UI는 레벨과 무관하게 돈다. 레벨 없이 메뉴만 떠 있는 상태가 있기 때문이다.
	// 레벨 다음인 이유는 게임플레이가 이번 프레임에 바꾼 값(체력 등)을
	// UI가 같은 프레임에 읽게 하기 위해서다.
	if (uiSystem)
	{
		uiSystem->Tick(deltaTime);
	}
}

void Engine::Draw()
{
	// 이번 프레임의 뷰를 확정한다.
	// 반드시 액터/UI 제출보다 먼저 - World 제출이 이 값으로 월드->화면 변환을 한다.
	if (renderer && cameraManager)
	{
		if (cameraManager->IsRotationBlending())
		{
			renderer->SetViewInterpolated(
				cameraManager->GetViewCenterWorld(),
				cameraManager->GetViewAngleDegrees());
		}
		else
		{
			renderer->SetView(
				cameraManager->GetViewCenterWorld(),
				cameraManager->GetViewQuarterTurns());
		}
	}

	// 여기서 레벨에 속해있는 액터 객체가 rendercommand에 드로우콜을 등록
	if (mainLevel)
	{
		mainLevel->Draw();
	}

	// 렌더러가 없으면 제출할 곳이 없다.
	if (!renderer)
		return;

	// UI는 액터 뒤에 제출한다. 월드의 무엇에도 가려지지 않아야 한다.
	// 정렬 순서(RenderLayer::UI)로도 액터보다 위지만, 제출 순서도 맞춰 둔다.
	if (uiSystem)
	{
		uiSystem->Paint();
	}

	// 프레임 수 표시.
	// 어떤 액터에도 가려지지 않도록 정렬 순서를 최대로 준다.
	if (showFps)
	{
		char text[32] = {};
		sprintf_s(text, "FPS %.1f", currentFps);

		renderer->Submit(text, Vector2::Zero, Color::White, INT_MAX);
	}

	renderer->Draw();
}

void Engine::UpdateFps(float deltaTime)
{
	++fpsFrameCount;
	fpsElapsedTime += deltaTime;

	// 갱신 주기. 너무 짧으면 숫자가 흔들려서 읽기 힘들다.
	const float updateInterval = 0.5f;

	if (fpsElapsedTime < updateInterval)
	{
		return;
	}

	// 모아둔 프레임 수를 경과 시간으로 나눠서 초당 프레임 수를 구한다.
	currentFps = static_cast<float>(fpsFrameCount) / fpsElapsedTime;

	fpsFrameCount = 0;
	fpsElapsedTime = 0.0f;
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
	// 순서가 중요하다.
	// 워커 루프는 무한 루프라서 정지 신호를 먼저 주지 않으면
	// Join()이 영영 돌아오지 않는다(= 종료가 안 됨).
	if (assetManager)
	{
		assetManager->StopWorkers();
	}

	// 엔진이 만들지 않은 쓰레드에 정지 신호를 보낸다(네트워크 쓰레드 등).
	// 애셋 워커와 같은 이유로 Join()보다 먼저다.
	for (const std::function<void()>& handler : shutdownHandlers)
	{
		if (handler)
		{
			handler();
		}
	}

	if (threadManager)
	{
		threadManager->Join();
	}
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