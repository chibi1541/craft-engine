#include "pch.h"
#include "AssetManager.h"

NAME_SPACE_BEGIN(Craft)

AssetManager* AssetManager::instance = nullptr;

AssetManager::AssetManager()
{
	ASSERT_CRASH(!instance);
	instance = this;
}

AssetManager::~AssetManager()
{
	instance = nullptr;
}

AssetManager& AssetManager::Get()
{
	ASSERT_CRASH(instance);
	return *instance;
}

void AssetManager::Tick(float deltaTime)
{
	for (auto& pair : caches)
	{
		pair.second->Tick(deltaTime, unloadThreshold);
	}
}

NAME_SPACE_END
