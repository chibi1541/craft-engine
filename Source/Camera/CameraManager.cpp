#include "pch.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraComponent.h"
#include "Actor/Actor.h"
#include "Level/Level.h"

NAME_SPACE_BEGIN(Craft)

CameraManager* CameraManager::instance = nullptr;

CameraManager::CameraManager(const Vector2& viewSize)
	: viewSize(viewSize)
{
	ASSERT_CRASH(!instance);
	instance = this;
}

CameraManager::~CameraManager()
{
	instance = nullptr;
}

CameraManager& CameraManager::Get()
{
	ASSERT_CRASH(instance);
	return *instance;
}

bool CameraManager::HasInstance()
{
	return instance != nullptr;
}

void CameraManager::RegisterCamera(const std::weak_ptr<CameraComponent>& camera)
{
	const std::shared_ptr<CameraComponent> locked = camera.lock();

	if (nullptr == locked)
	{
		return;
	}

	// 이미 들어와 있으면 중복 추가하지 않는다.
	for (const std::weak_ptr<CameraComponent>& entry : cameras)
	{
		if (entry.lock() == locked)
		{
			return;
		}
	}

	cameras.emplace_back(camera);

	// 활성 카메라가 없고 이 카메라가 autoActivate면 바로 활성으로 삼는다.
	if (!IsCameraUsable(activeCamera.lock()) && locked->IsAutoActivate())
	{
		activeCamera = camera;
	}
}

void CameraManager::UnregisterCamera(const CameraComponent* camera)
{
	for (size_t ix = 0; ix < cameras.size(); ++ix)
	{
		if (cameras[ix].lock().get() != camera)
		{
			continue;
		}

		cameras.erase(cameras.begin() + ix);
		break;
	}

	// 활성 카메라가 방금 빠졌으면 승계한다.
	if (!IsCameraUsable(activeCamera.lock()))
	{
		PromoteFallbackCamera();
	}
}

void CameraManager::SetActiveCamera(const std::shared_ptr<CameraComponent>& camera)
{
	if (nullptr == camera)
	{
		return;
	}

	activeCamera = camera;
}

void CameraManager::Tick(float /*deltaTime*/)
{
	PruneExpired();

	std::shared_ptr<CameraComponent> active = activeCamera.lock();

	// 활성 카메라가 파괴되는 프레임에 이미 승계를 끝낸다.
	// Actor::Destroy()는 hadExpired만 세우고 실제 파괴는 프레임 끝이므로,
	// IsActive() 검사가 그보다 한 프레임 먼저 무효화를 잡아낸다.
	if (!IsCameraUsable(active))
	{
		PromoteFallbackCamera();
		active = activeCamera.lock();
	}

	// 활성 카메라 없음 -> viewOrigin 갱신을 건너뛴다(마지막 원점 유지 = 동결).
	// 한 번도 없었으면 Zero로 남아 기존 동작과 100% 호환된다.
	if (!IsCameraUsable(active))
	{
		return;
	}

	const Vector2 center = active->GetViewCenter();

	// 뷰 중심이 화면 가운데에 오도록 좌상단을 잡는다.
	// 정수 나눗셈이라 홀수 크기에서 오른쪽/아래가 한 칸 더 넓다(의도된 고정).
	const Vector2 desiredOrigin(
		center.x - (viewSize.x / 2),
		center.y - (viewSize.y / 2));

	viewOrigin = ClampToWorldBounds(desiredOrigin);
}

void CameraManager::Reset()
{
	cameras.clear();
	activeCamera.reset();

	// viewOrigin은 일부러 건드리지 않는다.
}

void CameraManager::PruneExpired()
{
	for (size_t ix = cameras.size(); ix > 0; --ix)
	{
		if (cameras[ix - 1].expired())
		{
			cameras.erase(cameras.begin() + (ix - 1));
		}
	}
}

bool CameraManager::IsCameraUsable(const std::shared_ptr<CameraComponent>& camera)
{
	if (nullptr == camera || !camera->IsActive())
	{
		return false;
	}

	const std::shared_ptr<Actor> ownerActor = camera->GetOwner();

	return nullptr != ownerActor && ownerActor->IsActive();
}

void CameraManager::PromoteFallbackCamera()
{
	// 가장 최근에 등록된 것부터 훑는다.
	for (auto iterator = cameras.rbegin(); iterator != cameras.rend(); ++iterator)
	{
		const std::shared_ptr<CameraComponent> candidate = iterator->lock();

		if (IsCameraUsable(candidate) && candidate->IsAutoActivate())
		{
			activeCamera = candidate;
			return;
		}
	}

	// 승계할 카메라가 없다 -> 동결.
	activeCamera.reset();
}

Vector2 CameraManager::ClampToWorldBounds(const Vector2& desiredOrigin) const
{
	// 경계는 활성 카메라의 오너 액터가 속한 레벨에서 읽는다.
	// (소멸자 경로가 아니므로 안전하지만, Engine::Get() 없이 끝내는 편이 깔끔하다)
	const std::shared_ptr<CameraComponent> active = activeCamera.lock();
	const std::shared_ptr<Actor> ownerActor = active ? active->GetOwner() : nullptr;
	const std::shared_ptr<Level> level = ownerActor ? ownerActor->GetOwner() : nullptr;

	if (nullptr == level)
	{
		return desiredOrigin;
	}

	const Rect bounds = level->GetWorldBounds();

	// 비어 있으면 클램프하지 않는다 = 무한 월드, 기존 동작 유지.
	// Rect::IsEmpty()는 한 축만 0이어도 전체가 empty이므로,
	// "가로만 경계"는 지원하지 않는다 - 두 축 다 있거나 둘 다 없다.
	if (bounds.IsEmpty())
	{
		return desiredOrigin;
	}

	return Vector2(
		ClampAxis(desiredOrigin.x, viewSize.x, bounds.GetLeft(), bounds.size.x),
		ClampAxis(desiredOrigin.y, viewSize.y, bounds.GetTop(), bounds.size.y));
}

int CameraManager::ClampAxis(int origin, int viewExtent, int boundsMin, int boundsExtent)
{
	// 뷰가 경계보다 크거나 같다 -> 중앙 정렬 고정.
	// 이 분기가 없으면 maxOrigin < minOrigin으로 구간이 역전되어
	// 뒤에 쓴 대입이 이기고 한 칸 어긋난다.
	if (viewExtent >= boundsExtent)
	{
		// 정수 나눗셈이라 좌/상 쪽으로 반 칸 치우친다(한쪽 고정이 의도).
		return boundsMin - ((viewExtent - boundsExtent) / 2);
	}

	const int minOrigin = boundsMin;

	// maxOrigin = GetRight() - viewExtent + 1.
	// +1을 잊으면 경계의 마지막 열이 영영 안 보인다.
	const int maxOrigin = boundsMin + boundsExtent - viewExtent;

	if (origin < minOrigin)
	{
		return minOrigin;
	}

	if (origin > maxOrigin)
	{
		return maxOrigin;
	}

	return origin;
}

NAME_SPACE_END
