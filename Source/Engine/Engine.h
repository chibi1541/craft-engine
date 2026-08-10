#pragma once

NAME_SPACE_BEGIN(Craft)

class Level;
class Input;
class Renderer;


// dll에서 외부 모듈로 노출시키겠다는 키워드
// 언리얼의 모듈이름_API 키워드와 비슷
class CRAFT_API Engine
{
	// 엔진 설정 (데이터)
	struct Setting
	{
		enum { BUFFER_SIZE = 2048, };

		// 목표 프레임 수
		float framerate = 0.f;

		// 사용할 콘솔 화면 너비
		int width = 0;

		// 사용한 콘솔 화면 높이
		int height = 0;
	};

public:
	Engine();
	virtual ~Engine();

	// 엔진 실행 함수
	void Run();

	// 엔진 종료 함수
	void Quit();

	// 레벨 추가 요청 함수
	template<typename T, typename = std::enable_if_t<std::is_base_of<Level, T>:: value>>
	void AddNewLevel()
	{
		nextLevel = std::make_shared<T>();
	}

	// 전역 인스턴스 반환
	// Engine::Get(); -> Engine의 instance를 반환
	static Engine& Get();

	inline int GetWidth() const { return setting.width; }
	inline int GetHeight() const { return setting.height; }

	std::shared_ptr<Level> GetLevel();

protected:
	// 입력 처리(폴링 방식 vs 이벤트)
	void ProcessInput();

	// 초기화 함수.
	void OnInitialized();


	// 게임 플레이 이벤트 함수
	// 게임 플레이 초기화
	void BeginPlay();

	// 게임 플레이 업데이트
	void Tick(float deltaTime);

	// 레벨 그리기 함수
	void Draw();

	// 프레임 간 입력 값 저장을 위함 함수(입력의 변화를 체크하기 위함)
	void SavePreviousInputState();

	// 엔진 종료 시 리소스 정리
	void Shutdown();

	// 엔진 설정 로드 함수
	void LoadEngineSetting();

protected:
	bool isQuit = false;

	Setting setting;

	// 전역으로 접근이 가능하도록 변수 선언
	// Engine::instance
	static Engine* instance;

	// 메인 레벨
	std::shared_ptr<Level> mainLevel;

	// 추가 요청된 레벨
	std::shared_ptr<Level> nextLevel;

	// 입력 시스템 변수
	std::unique_ptr<Input> input;

	std::unique_ptr<Renderer> renderer;

private:

};


NAME_SPACE_END
