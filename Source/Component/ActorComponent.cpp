#include "pch.h"
#include "ActorComponent.h"

NAME_SPACE_BEGIN(Craft)

void ActorComponent::BeginPlay()
{
	// 중복 초기화를 막기 위한 플래그.
	// 파생 클래스는 super::BeginPlay()를 호출해야 이 처리가 된다.
	hasBeganPlay = true;
}

void ActorComponent::Tick(float deltaTime)
{
}

void ActorComponent::Draw()
{
}

NAME_SPACE_END
