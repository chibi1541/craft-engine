#pragma once

#include "Utils/EngineMacro.h"
#include "Utils/Types.h"
#include "Component/ActorComponent.h"
#include "Animation/AnimationPlayer.h"
#include "Math/Vector2.h"
#include "Math/SymbolPalette.h"
#include <string>
#include <memory>
#include <unordered_map>

NAME_SPACE_BEGIN(Craft)

// 액터에 스프라이트 애니메이션을 붙이는 컴포넌트.
// (언리얼로 치면 SkeletalMeshComponent가 AnimInstance를 들고 있는 자리)
//
// 클립 목록을 소유하고,
//   Tick : 재생기(AnimationPlayer)의 시간을 밀어준다.
//   Draw : 지금 프레임의 스프라이트를 Renderer::SubmitPixels로 제출한다.
//
// 기호 -> 색 변환표는 SymbolPalette가 공통으로 들고 있으므로 여기서 갖지 않는다.
//
// PlayClip()이 "이름으로 애니를 고르는" API라는 점이 다음 단계와의 연결 고리다.
// 상태 머신을 얹으면 AnimState가 clipName을 들고 있고,
// 매 틱 PlayClip(현재상태.clipName)을 호출하는 것만으로 동작하게 된다.
class CRAFT_API SpriteAnimatorComponent : public ActorComponent
{
	TYPE_DECLARATIONS(SpriteAnimatorComponent, ActorComponent)

public:
	SpriteAnimatorComponent() = default;
	virtual ~SpriteAnimatorComponent() = default;

	virtual void Tick(float deltaTime) override;
	virtual void Draw() override;

	// XML 애니메이션 정의를 읽어서 클립을 한꺼번에 등록한다.
	// 등록된 클립 수를 반환한다(0이면 파일이 없거나 파싱 실패).
	//
	// TODO : 지금은 호출하는 액터가 경로를 직접 들고 있다.
	//        애셋 매니저 / 데이터 애셋이 생기면 이미 만들어진 클립을 받아오는 형태로 바뀐다.
	int LoadClipsFromFile(const WCHAR* path);

	// 클립을 하나 등록한다. 키는 clip->GetName().
	void AddClip(const std::shared_ptr<const AnimationClip>& clip);

	// 등록된 클립을 이름으로 재생한다. 없는 이름이면 false.
	// 이미 그 클립을 재생 중이면 아무 일도 일어나지 않는다(재생이 리셋되지 않음).
	bool PlayClip(const std::string& name, bool forceRestart = false);

	// getter/setter
	// 재생 상태를 직접 다뤄야 할 때(HasFinished, SetPlayRate 등) 쓴다.
	inline AnimationPlayer& GetPlayer() { return player; }
	inline const AnimationPlayer& GetPlayer() const { return player; }

	inline bool HasClip(const std::string& name) const { return clipMap.find(name) != clipMap.end(); }
	inline int GetClipCount() const { return static_cast<int>(clipMap.size()); }

	// 콘솔 셀은 정사각형이 아니라서 비율 보정이 필요하다.
	// 스프라이트를 크게 그릴 때도 같이 쓴다. (Renderer::SubmitPixels 참고)
	inline void SetScale(int newScaleX, int newScaleY)
	{
		ASSERT_CRASH(newScaleX >= 1 && newScaleY >= 1);
		scaleX = newScaleX;
		scaleY = newScaleY;
	}

	// 액터 위치를 기준으로 한 스프라이트의 그리기 오프셋.
	// 액터의 기준점(발밑 등)과 그림의 좌상단을 맞출 때 쓴다.
	inline void SetOffset(const Vector2& newOffset) { offset = newOffset; }
	inline Vector2 GetOffset() const { return offset; }

private:
	// 재생 위치를 관리하는 시간 커서.
	AnimationPlayer player;

	// 이름 -> 클립. 클립 실체는 공유되므로 shared_ptr<const>.
	std::unordered_map<std::string, std::shared_ptr<const AnimationClip>> clipMap;

	Vector2 offset = Vector2::Zero;

	int scaleX = 1;
	int scaleY = 1;
};

NAME_SPACE_END
