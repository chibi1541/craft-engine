#include "pch.h"
#include "Level.h"
#include "Actor/Actor.h"

NAME_SPACE_BEGIN(Craft)

Level::Level()
{
}

Level::~Level()
{
}

void Level::OnInitialized()
{
	// 초기화 됐다고 설정.
	hasInitialized = true;
}

void Level::BeginPlay()
{
	// 액터 초기화 시 1번 호출되는 이벤트.
	for (const std::shared_ptr<Actor>& actor : actorList)
	{
		// 검증 - 이미 BeginPlay 처리된 경우 건너뛰기.
		if (actor->HasBeganPlay())
		{
			continue;
		}

		// BeginPlay 이벤트 호출.
		actor->BeginPlay();
	}
}

void Level::Tick(float deltaTime)
{
	for (const std::shared_ptr<Actor>& actor : actorList)
	{
		// 검증 - 활성화되지 않았으면 건너뛰기.
		//if (actor->IsActive() == false)
		if (!actor->IsActive())
		{
			continue;
		}

		// Tick 이벤트 호출.
		actor->Tick(deltaTime);
	}
}

void Level::Draw()
{
	for (const std::shared_ptr<Actor>& actor : actorList)
	{
		// 검증 - 활성화되지 않았으면 건너뛰기.
		if (!actor->IsActive())
		{
			continue;
		}

		// Draw 이벤트 호출.
		actor->Draw();
	}
}

void Level::ProcessAddAndDestoryActors()
{
	for (auto iterator = actorList.begin(); iterator != actorList.end();)
	{
		// 제거 요청된 액터인지 확인.
		auto actor = *iterator;
		if (actor->HasExpired())
		{
			iterator = actorList.erase(iterator);

			continue;
		}

		++iterator;
	}

	// 추가 처리.
	// 추가 요청된 목록이 없으면 종료.
	if (addRequestedActorList.empty())
	{
		return;
	}

	for (const auto& actor : addRequestedActorList)
	{
		actorList.emplace_back(actor);
	}

	// 추가 처리된 목록 정리.
	addRequestedActorList.clear();
}



NAME_SPACE_END
