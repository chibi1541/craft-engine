#pragma once

NAME_SPACE_BEGIN(Craft)

class CRAFT_API ActorComponent : public CraftObject
{
	TYPE_DECLARATIONS(ActorComponent, CraftObject)

public:
	ActorComponent() = default;
	virtual ~ActorComponent() = default;

	virtual void Tick(float deltaTime);
};

NAME_SPACE_END

