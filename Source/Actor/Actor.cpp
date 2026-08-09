#include "pch.h"
#include "Actor.h"
#include "Engine/Engine.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

Actor::Actor(const std::wstring& image, const Vector2& position, Color color)
	: image(image), position(position), color(color), width(static_cast<int>(image.size()))
{

}

Actor::~Actor()
{

}

void Actor::BeginPlay()
{
}

void Actor::Tick(float deltaTime)
{
}

void Actor::Draw()
{
	if (!IsActive())
		return;

	Renderer::Get().Submit(image, position, color, sortingOrder);
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


