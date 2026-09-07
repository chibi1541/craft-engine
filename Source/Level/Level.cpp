#include "pch.h"
#include "Level.h"
#include "Actor/Actor.h"
#include "Camera/CameraManager.h"
#include "Math/ViewTransform.h"
#include "Input/Input.h"
#include "Math/SymbolPalette.h"
#include "Render/RenderLayer.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 기준점에 찍을 한 칸짜리 픽셀맵. 'R'은 SymbolPalette의 Red다.
	const std::string pivotMarker = "R";
}

bool Level::showPivotDebug = false;

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
	// 디버그 표시 토글. GetKeyDown이라 누르고 있어도 한 번만 뒤집힌다.
	//
	// 입력을 여기서 직접 읽는 이유 - 이건 게임플레이 입력이 아니라 개발용 스위치라서,
	// 액터에 InputComponent를 달아 바인딩할 대상이 아니다.
	if (Input::Get().GetKeyDown('P'))
	{
		showPivotDebug = !showPivotDebug;
	}

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
	// 카메라가 없으면 컬링할 기준도, 정렬할 축도 없다.
	// (엔진 종료 순서나 카메라 없는 테스트 레벨에서 실제로 일어난다)
	const bool hasCamera = CameraManager::HasInstance();

	Rect cullBounds;
	int quarterTurns = 0;

	if (hasCamera)
	{
		const CameraManager& camera = CameraManager::Get();

		quarterTurns = camera.GetViewQuarterTurns();

		// 뷰 회전이 정착했으면 이번 프레임에 한 번만 알린다.
		//
		// 매 프레임 카메라를 읽는 대신 정수 하나를 비교한다. 정적 액터는 이때만
		// 그릴 방향을 다시 고르므로, 보간 중에는 그림이 바뀌지 않는다
		// (회전 도중에 그림이 바뀌면 오브젝트가 튀는 것처럼 보인다).
		//
		// 컬링된 액터에게도 알려야 한다. 알림을 놓친 채 화면에 들어오면
		// 다음 회전까지 낡은 방향으로 그려진다. 그래서 이 순회는 컬링과 분리돼 있다.
		const int rotationVersion = camera.GetViewRotationVersion();

		if (rotationVersion != lastViewRotationVersion)
		{
			lastViewRotationVersion = rotationVersion;

			for (const std::shared_ptr<Actor>& actor : actorList)
			{
				actor->OnViewRotationChanged(quarterTurns);
			}
		}

		// 프레임당 한 번만 구한다. 액터마다 부르면 회전 보간 중에 삼각함수가 쏟아진다.
		cullBounds = camera.GetViewWorldBounds(cullMargin);
	}

	const bool shouldCull = hasCamera && viewCullingEnabled && !cullBounds.IsEmpty();

	for (const std::shared_ptr<Actor>& actor : actorList)
	{
		// 검증 - 활성화되지 않았으면 건너뛰기.
		if (!actor->IsActive())
		{
			continue;
		}

		// 경계와 기준점 한 점을 비교한다.
		//
		// 액터의 그림 크기를 보지 않는 이유 - 액터마다 크기를 물어보려면
		// 스프라이트를 뒤져야 하고, 그 비용이 컬링으로 아낀 것을 도로 까먹는다.
		// 대신 경계를 cullMargin만큼 넓혀서 큰 그림도 미리 들어오게 한다.
		if (shouldCull && !cullBounds.Contains(actor->GetPosition()))
		{
			if(actor->IsShouldDraw() == false)
				continue;
		}

		// 위치 기반 Z-order. 화면에서 아래에 있는 액터가 앞에 보인다.
		//
		// 깊이 키가 Rotate90(위치, k).y 인 이유:
		// 화면 세로 좌표는 Rotate90(위치 - 중심, k).y + half.y 인데 Rotate90은 선형이라
		// 중심/half 항은 모든 액터에 공통인 상수다. 상수를 빼도 정렬 순서는 그대로이므로
		// 카메라 중심을 참조할 필요가 없다. 풀어 쓰면
		//   k=0 -> +y   k=1 -> +x   k=2 -> -y   k=3 -> -x
		// 180/270°의 부호 반전을 빠뜨리면 반대편에서 볼 때 앞뒤가 정확히 뒤집힌다.
		if (actor->UsesDepthSorting())
		{
			const Vector2 rotated = Rotate90(actor->GetPosition(), quarterTurns);

			actor->SetSortingOrder(RenderLayer::WorldActorDepth + rotated.y);
		}

		// Draw 이벤트 호출.
		actor->Draw();

		// 기준점 표시. 액터를 그린 "뒤"에 올려야 그림에 가려지지 않는다.
		//
		// SubmitPixelsWorld를 쓰는 이유 - 액터 스프라이트와 완전히 같은 좌표 변환을
		// 타야 한다. 여기서 다른 경로로 계산하면 점과 그림이 각자 틀려서
		// 무엇을 믿어야 할지 알 수 없게 된다.
		// 피벗 오프셋은 주지 않는다. 기준점 그 자체를 찍는 것이 목적이다.
		if (showPivotDebug)
		{
			Renderer::Get().SubmitPixelsWorld(
				pivotMarker,
				SymbolPalette::GetTable(),
				actor->GetPosition(),
				RenderLayer::WorldUI,
				SymbolPalette::TransparentSymbol);
		}
	}

	// 켜져 있다는 것을 화면으로 알린다. 점이 안 보일 때
	// 모드가 꺼진 건지 기준점이 화면 밖인지 구분되어야 한다.
	if (showPivotDebug)
	{
		Renderer::Get().Submit("[P] pivot", Vector2(0, 1), Color::Red, RenderLayer::UI);
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
