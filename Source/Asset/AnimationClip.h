#pragma once

#include "Utils/EngineMacro.h"

#include "Asset/Sprite.h"
#include <string>
#include <vector>

NAME_SPACE_BEGIN(Craft)

// 하나의 동작을 이루는 스프라이트 시퀀스. (언리얼의 AnimSequence에 해당)
//
// "무엇을 그릴지"만 들고 있고 "지금 몇 번째 프레임인지"는 갖지 않는다.
// 재생 위치는 AnimationPlayer가 따로 관리한다.
// 이렇게 나눠야 여러 액터가 같은 클립을 동시에, 서로 다른 시점으로 재생할 수 있다.
//
// 그래서 클립은 항상 shared_ptr<const AnimationClip>로 공유해서 쓴다.
class CRAFT_API AnimationClip
{
public:
	AnimationClip() = default;

	// framesPerSecond: 초당 몇 장을 넘길지. isLooping: 끝에서 처음으로 돌아갈지.
	AnimationClip(
		const std::string& name,
		const std::vector<Sprite>& frames,
		float framesPerSecond = 12.0f,
		bool isLooping = true
	);

	~AnimationClip() = default;

	// 프레임 하나를 가져온다. 범위를 벗어나면 양 끝으로 잘라서(clamp) 반환한다.
	const Sprite& GetFrame(int index) const;

	inline int GetFrameCount() const { return static_cast<int>(frames.size()); }

	// 프레임 한 장이 화면에 머무는 시간(초).
	inline float GetFrameDuration() const { return frameDuration; }

	// 클립 전체를 한 번 재생하는 데 걸리는 시간(초).
	inline float GetDuration() const { return frameDuration * GetFrameCount(); }

	inline bool IsLooping() const { return isLooping; }
	inline const std::string& GetName() const { return name; }

private:
	// 클립 식별자. SpriteAnimatorComponent가 이 이름을 키로 클립을 등록한다.
	// 나중에 상태(AnimState)가 재생할 클립을 지목할 때도 이 이름을 쓴다.
	std::string name;

	std::vector<Sprite> frames;

	// fps가 아니라 "한 장당 지속 시간"으로 저장한다.
	// 매 틱 나눗셈을 반복하지 않도록 생성 시점에 한 번만 역수를 구해둔다.
	// TODO : 프레임마다 다른 지속 시간이 필요해지면 vector<float>로 확장.
	float frameDuration = 1.0f / 12.0f;

	bool isLooping = true;
};

NAME_SPACE_END
