#include "pch.h"
#include "Actor.h"
#include "Engine/Engine.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

Actor::Actor(const std::string& image, const Vector2& position, Color color)
	: image(image), position(position), color(color), width(static_cast<int>(image.size()))
{

}

Actor::~Actor()
{

}

void Actor::BeginPlay()
{
	// Level::BeginPlay()는 HasBeganPlay()로 중복 호출을 거른다.
	// 이 플래그를 여기서 세우지 않으면 매 프레임 BeginPlay가 다시 불려서
	// 컴포넌트가 계속 추가되는 문제가 생긴다.
	hasBeganPlay = true;

	for (const std::shared_ptr<ActorComponent>& component : componentList)
	{
		// 검증 - 이미 BeginPlay 처리된 경우 건너뛰기.
		if (component->HasBeganPlay())
		{
			continue;
		}

		component->BeginPlay();
	}
}

void Actor::Tick(float deltaTime)
{
	// BeginPlay에서 추가된 컴포넌트까지 이번 프레임부터 갱신된다.
	for (const std::shared_ptr<ActorComponent>& component : componentList)
	{
		// 검증 - 활성화되지 않았으면 건너뛰기.
		if (!component->IsActive())
		{
			continue;
		}

		component->Tick(deltaTime);
	}
}

void Actor::Draw()
{
	if (!IsActive())
		return;

	// 글자 액터인 경우에만 문자열을 제출한다.
	// 스프라이트만 쓰는 액터는 image가 비어있다.
	if (!image.empty())
	{
		Renderer::Get().Submit(image, position, color, sortingOrder);
	}

	// 그리기를 담당하는 컴포넌트(예: SpriteAnimatorComponent)에 전달.
	for (const std::shared_ptr<ActorComponent>& component : componentList)
	{
		// 검증 - 활성화되지 않았으면 건너뛰기.
		if (!component->IsActive())
		{
			continue;
		}

		component->Draw();
	}
}

void Actor::Destroy()
{
	hadExpired = true;
}

void Actor::QuitGame()
{
	Engine::Get().Quit();
}

void Actor::SetPosition(const Vector2& newPosition)
{
	if (position == newPosition)
		return;

	position = newPosition;
}


NAME_SPACE_END
