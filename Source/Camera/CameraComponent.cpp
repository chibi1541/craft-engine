#include "pch.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraManager.h"
#include "Actor/Actor.h"

NAME_SPACE_BEGIN(Craft)

CameraComponent::~CameraComponent()
{
	// 주소로 해제한다. 소멸자에서는 shared_from_this를 쓸 수 없다.
	// (~InputHandler가 주소로 Unregister하는 것과 같은 이유)
	//
	// mainLevel이 Engine의 첫 멤버라 가장 나중에 파괴되므로, 이 소멸자는
	// CameraManager가 이미 죽은 뒤에 돈다. HasInstance() 가드가 필수다.
	// 소멸자 경로에서 Engine::Get()을 부르면 종료 중에 엔진이 부활하므로 금지.
	if (CameraManager::HasInstance())
	{
		CameraManager::Get().UnregisterCamera(this);
	}
}

void CameraComponent::BeginPlay()
{
	super::BeginPlay();

	if (!registered && CameraManager::HasInstance())
	{
		CameraManager::Get().RegisterCamera(weak_from_this());
		registered = true;
	}
}

Vector2 CameraComponent::GetViewCenter() const
{
	// ownership - 액터가 사라졌으면 기준점을 알 수 없다.
	const std::shared_ptr<Actor> ownerActor = GetOwner();
	const Vector2 base = ownerActor ? ownerActor->GetPosition() : Vector2::Zero;

	return base + offset;
}

NAME_SPACE_END
