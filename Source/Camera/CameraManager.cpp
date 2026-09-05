#include "pch.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraComponent.h"
#include "Actor/Actor.h"
#include "Level/Level.h"
#include "Math/ViewTransform.h"

#include <cmath>

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 각도를 [0, 360) 로 정규화.
	float Normalize360(float degrees)
	{
		degrees = ::fmodf(degrees, 360.0f);

		if (degrees < 0.0f)
		{
			degrees += 360.0f;
		}

		return degrees;
	}

	// 각도 델타를 (-180, 180] 로 정규화. 270°->0° 가 -270 이 아니라 +90 이 되게 한다.
	float ShortestDelta(float fromDeg, float toDeg)
	{
		float delta = ::fmodf(toDeg - fromDeg, 360.0f);

		if (delta > 180.0f)
		{
			delta -= 360.0f;
		}
		else if (delta <= -180.0f)
		{
			delta += 360.0f;
		}

		return delta;
	}

	int CeilToInt(float v)
	{
		return static_cast<int>(::ceilf(v));
	}
}

CameraManager* CameraManager::instance = nullptr;

CameraManager::CameraManager(const Vector2& viewSize)
	: viewSize(viewSize)
{
	ASSERT_CRASH(!instance);
	instance = this;

	// 활성 카메라가 없을 때 ViewWorldToScreen(world) == world 가 되도록 중심을 화면 중앙에.
	viewCenterWorld = Vector2(viewSize.x / 2, viewSize.y / 2);
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

void CameraManager::BlendViewRotationTo(int quarterTurns, float blendTime)
{
	const int normalizedTurns = ((quarterTurns % 4) + 4) % 4;
	const float targetAngle = normalizedTurns * 90.0f;

	targetQuarterTurns = normalizedTurns;

	if (blendTime <= 0.0f)
	{
		// 즉시 스냅도 "정착"이다. 보간 완료 경로와 같이 알려야
		// 카메라 전환/승계로 각도가 바뀐 것을 놓치지 않는다.
		++viewRotationVersion;

		// 즉시 스냅.
		viewAngleDegrees = Normalize360(targetAngle);
		blendDuration = 0.0f;
		blendElapsed = 0.0f;
		blendDelta = 0.0f;
		return;
	}

	blendStartAngle = viewAngleDegrees;
	blendDelta = ShortestDelta(viewAngleDegrees, targetAngle);
	blendElapsed = 0.0f;
	blendDuration = blendTime;
}

void CameraManager::SyncRotationToActiveCamera(const std::shared_ptr<CameraComponent>& active)
{
	const CameraComponent* raw = active.get();

	if (raw == lastSyncedActive)
	{
		return;
	}

	// 카메라 전환/승계 - 새 카메라의 목표 각도로 즉시 맞춘다(전환은 즉시가 기존 동작).
	lastSyncedActive = raw;
	BlendViewRotationTo(active->GetViewQuarterTurns(), 0.0f);
}

void CameraManager::Tick(float deltaTime)
{
	PruneExpired();

	std::shared_ptr<CameraComponent> active = activeCamera.lock();

	// 활성 카메라가 파괴되는 프레임에 이미 승계를 끝낸다.
	if (!IsCameraUsable(active))
	{
		PromoteFallbackCamera();
		active = activeCamera.lock();
	}

	// 활성 카메라 없음 -> 중심/각도/보간 전부 갱신 스킵(마지막 상태 유지 = 동결).
	if (!IsCameraUsable(active))
	{
		return;
	}

	// 전환/승계면 새 카메라 각도로 즉시 맞춤.
	SyncRotationToActiveCamera(active);

	// 보간 진행(선형). u가 정확히 1.0에 도달하므로 수렴 데드밴드가 필요 없다.
	if (blendDuration > 0.0f)
	{
		blendElapsed += deltaTime;

		float u = blendElapsed / blendDuration;

		if (u >= 1.0f)
		{
			// 정확 스냅 후 정수 경로로 복귀.
			viewAngleDegrees = Normalize360(targetQuarterTurns * 90.0f);
			blendDuration = 0.0f;

			// 회전이 여기서 정착한다. 보는 쪽(Level::Draw)이 이번 프레임에 알아챈다.
			++viewRotationVersion;
		}
		else
		{
			if (u < 0.0f)
			{
				u = 0.0f;
			}

			viewAngleDegrees = blendStartAngle + (blendDelta * u);
		}
	}

	viewCenterWorld = ClampViewCenter(active->GetViewCenter());
}

void CameraManager::Reset()
{
	cameras.clear();
	activeCamera.reset();

	// 다음 활성 카메라를 전환으로 인식하도록.
	lastSyncedActive = nullptr;

	// 뷰(중심/각도/보간)는 일부러 건드리지 않는다.
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

Vector2 CameraManager::GetRotatedViewExtent() const
{
	if (blendDuration <= 0.0f)
	{
		// 정지 - 정수. k 홀수면 가로/세로 스왑.
		return (targetQuarterTurns % 2 == 0)
			? viewSize
			: Vector2(viewSize.y, viewSize.x);
	}

	// 보간 중 - 현재 각도의 실제 AABB. 양 끝(0/90)에서 정수 parity 값과 일치한다.
	const float radians = viewAngleDegrees * (3.14159265358979323846f / 180.0f);
	const float ca = ::fabsf(::cosf(radians));
	const float sa = ::fabsf(::sinf(radians));

	return Vector2(
		CeilToInt(viewSize.x * ca + viewSize.y * sa),
		CeilToInt(viewSize.x * sa + viewSize.y * ca));
}

Rect CameraManager::GetViewWorldBounds(int margin) const
{
	// 회전된 뷰가 월드에서 실제로 덮는 AABB. 클램프가 쓰는 바로 그 계산이다.
	// (정지면 정수 parity 스왑, 보간 중이면 현재 각도의 실제 크기)
	const Vector2 extent = GetRotatedViewExtent();

	const Vector2 origin(
		viewCenterWorld.x - (extent.x / 2) - margin,
		viewCenterWorld.y - (extent.y / 2) - margin);

	return Rect(origin, Vector2(extent.x + margin * 2, extent.y + margin * 2));
}

Vector2 CameraManager::ClampViewCenter(const Vector2& desiredCenter) const
{
	// 경계는 활성 카메라의 오너 액터가 속한 레벨에서 읽는다(Engine::Get() 없이).
	const std::shared_ptr<CameraComponent> active = activeCamera.lock();
	const std::shared_ptr<Actor> ownerActor = active ? active->GetOwner() : nullptr;
	const std::shared_ptr<Level> level = ownerActor ? ownerActor->GetOwner() : nullptr;

	if (nullptr == level)
	{
		return desiredCenter;
	}

	const Rect bounds = level->GetWorldBounds();

	// 비어 있으면 클램프하지 않는다 = 무한 월드. 한 축만 0이어도 전체가 empty다.
	if (bounds.IsEmpty())
	{
		return desiredCenter;
	}

	// 회전된 뷰의 월드 AABB 좌상단을 클램프한 뒤 중심으로 되돌린다.
	// 기존 ClampAxis(top-left 기준)를 그대로 재사용하려는 것.
	const Vector2 aabbExtent = GetRotatedViewExtent();
	const Vector2 aabbHalf(aabbExtent.x / 2, aabbExtent.y / 2);

	const Vector2 desiredOrigin = desiredCenter - aabbHalf;

	const int clampedOriginX = ClampAxis(desiredOrigin.x, aabbExtent.x, bounds.GetLeft(), bounds.size.x);
	const int clampedOriginY = ClampAxis(desiredOrigin.y, aabbExtent.y, bounds.GetTop(), bounds.size.y);

	return Vector2(clampedOriginX + aabbHalf.x, clampedOriginY + aabbHalf.y);
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

Vector2 CameraManager::WorldToScreen(const Vector2& world) const
{
	const Vector2 half(viewSize.x / 2, viewSize.y / 2);

	if (blendDuration > 0.0f)
	{
		const float radians = viewAngleDegrees * (3.14159265358979323846f / 180.0f);
		return ViewWorldToScreenF(world, viewCenterWorld, half, ::cosf(radians), ::sinf(radians));
	}

	return ViewWorldToScreen(world, viewCenterWorld, half, targetQuarterTurns);
}

Vector2 CameraManager::ScreenToWorld(const Vector2& screen) const
{
	const Vector2 half(viewSize.x / 2, viewSize.y / 2);

	if (blendDuration > 0.0f)
	{
		const float radians = viewAngleDegrees * (3.14159265358979323846f / 180.0f);
		return ViewScreenToWorldF(screen, viewCenterWorld, half, ::cosf(radians), ::sinf(radians));
	}

	return ViewScreenToWorld(screen, viewCenterWorld, half, targetQuarterTurns);
}

NAME_SPACE_END
