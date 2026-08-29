#pragma once

#include "Utils/EngineMacro.h"
#include "Math/Vector2.h"
#include "Math/Rect.h"

#include <memory>
#include <vector>

NAME_SPACE_BEGIN(Craft)

class CameraComponent;

// 활성 카메라를 추적하고 매 프레임 뷰 원점(뷰 좌상단의 월드 좌표)을 계산하는 싱글턴.
// 언리얼의 APlayerCameraManager 자리다.
//
// Engine이 unique_ptr로 소유하며, 선언은 renderer 뒤 / uiSystem 앞이다.
//  - 생성: 화면 크기를 알아야 하므로 renderer 다음.
//  - 파괴: 역순이라 uiSystem이 먼저 죽어 카메라를 안전하게 참조하고 내려간다.
//
// 이 단계의 viewOrigin은 정수 Vector2다. lag가 없으면 float은 순수 낭비이고
// 반올림 버그 표면만 늘린다. float 상태는 나중에 lag/데드존과 함께 들어온다.
class CRAFT_API CameraManager
{
	friend class Engine;

public:
	explicit CameraManager(const Vector2& viewSize);
	~CameraManager();

	// 싱글턴이므로 복사 금지.
	CameraManager(const CameraManager&) = delete;
	CameraManager& operator=(const CameraManager&) = delete;

	// 전역 접근.
	static CameraManager& Get();

	// 엔진 종료 순서 때문에 매니저가 이미 사라졌을 수 있다.
	// CameraComponent 소멸자 등에서는 Get 전에 이걸로 확인해야 한다.
	static bool HasInstance();

	// CameraComponent가 스스로 호출한다.
	void RegisterCamera(const std::weak_ptr<CameraComponent>& camera);
	void UnregisterCamera(const CameraComponent* camera);

	// 활성 카메라 교체. 언리얼 SetViewTarget 대응.
	void SetActiveCamera(const std::shared_ptr<CameraComponent>& camera);
	std::shared_ptr<CameraComponent> GetActiveCamera() const { return activeCamera.lock(); }

	// 매 프레임 뷰 원점을 갱신한다. Engine이 mainLevel->Tick 뒤, uiSystem->Tick 앞에서 부른다.
	void Tick(float deltaTime);

	// 레벨 교체 시 등록 목록과 활성 카메라를 비운다.
	// viewOrigin은 유지한다 - Zero로 되돌리면 레벨 교체마다 화면이 월드 원점으로 튄다.
	void Reset();

	// 뷰 좌상단의 월드 좌표.
	Vector2 GetViewOrigin() const { return viewOrigin; }
	Vector2 GetViewSize() const { return viewSize; }
	Rect    GetViewRect() const { return Rect(viewOrigin, viewSize); }

	// 좌표 변환. 월드 부착 위젯(체력바 등)이 쓴다.
	Vector2 WorldToScreen(const Vector2& world) const { return world - viewOrigin; }
	Vector2 ScreenToWorld(const Vector2& screen) const { return screen + viewOrigin; }

private:
	// 만료된 weak_ptr 항목을 목록에서 제거한다.
	void PruneExpired();

	// 카메라가 살아있고 활성이며 오너 액터도 활성인지.
	// InputComponent::BeginPlay의 SetEnabledCheck 람다와 동일한 검사다.
	static bool IsCameraUsable(const std::shared_ptr<CameraComponent>& camera);

	// 활성 카메라가 무효화됐을 때, 남은 카메라 중 autoActivate가 켜진 것을
	// 가장 최근 등록 순으로 승격한다. 없으면 활성 카메라 없음(동결).
	void PromoteFallbackCamera();

	// 원하는 뷰 원점을 레벨 경계 안으로 클램프한다. 경계가 비어 있으면 그대로 통과.
	Vector2 ClampToWorldBounds(const Vector2& desiredOrigin) const;

	// 한 축의 클램프.
	//   viewExtent >= boundsExtent -> 중앙 정렬 고정(분기하지 않으면 구간이 역전된다)
	//   그 외                       -> [boundsMin, boundsMin + boundsExtent - viewExtent] 로 클램프
	// maxOrigin의 +1(= boundsExtent - viewExtent)을 잊으면 끝 열이 영영 안 보인다.
	static int ClampAxis(int origin, int viewExtent, int boundsMin, int boundsExtent);

private:
	static CameraManager* instance;

	// 등록된 카메라들. 등록 순서를 유지한다(승계가 최근 등록 우선이라).
	std::vector<std::weak_ptr<CameraComponent>> cameras;

	// 현재 화면을 비추는 카메라.
	std::weak_ptr<CameraComponent> activeCamera;

	// 뷰 크기(콘솔 셀 개수). Renderer가 실제로 잡은 화면 크기.
	Vector2 viewSize;

	// 뷰 좌상단의 월드 좌표. 카메라가 없으면 갱신을 건너뛴다(마지막 값 유지 = 동결).
	Vector2 viewOrigin = Vector2::Zero;
};

NAME_SPACE_END
