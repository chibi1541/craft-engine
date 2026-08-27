#pragma once

#include "Utils/EngineMacro.h"

#include "Asset/Sprite.h"
#include <string>
#include <vector>

NAME_SPACE_BEGIN(Craft)

// TODO : 프레임 별 이벤트(람다 방식) 실행 구조 추가

// 하나의 동작을 이루는 스프라이트 시퀀스. (언리얼의 AnimSequence에 해당)
//
// "무엇을 그릴지"만 들고 있고 "지금 몇 번째 프레임인지"는 갖지 않는다.
// 재생 위치는 AnimationPlayer가 따로 관리한다.
// 이렇게 나눠야 여러 액터가 같은 클립을 동시에, 서로 다른 시점으로 재생할 수 있다.
//
// 그래서 클립은 항상 shared_ptr<const AnimationClip>로 공유해서 쓴다.
// 추후에 AssetManager가 본체를 관리할 예정
//
// ★ 프레임 크기 불변식 ★
// 한 클립 안의 모든 프레임은 크기가 같아야 한다. 생성자가 검사한다.
// 피벗 기본값이 "프레임 크기"에서 나오고, 합성기도 이 전제 위에서 동작한다.
class CRAFT_API AnimationClip
{
public:
	AnimationClip() = default;

	// 피벗을 프레임 크기에서 자동으로 정한다. (가운데 맨 아래 = 발밑)
	// framesPerSecond: 초당 몇 장을 넘길지. isLooping: 끝에서 처음으로 돌아갈지.
	AnimationClip(
		const std::string& name,
		const std::vector<Sprite>& frames,
		float framesPerSecond = 12.0f,
		bool isLooping = true
	);

	// 피벗을 직접 지정한다.
	AnimationClip(
		const std::string& name,
		const std::vector<Sprite>& frames,
		float framesPerSecond,
		bool isLooping,
		float pivotX,
		float pivotY
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

	// 모든 프레임이 같은 크기이므로 클립 단위로 물어볼 수 있다.
	inline int GetWidth() const { return width; }
	inline int GetHeight() const { return height; }

	// 액터 위치에 놓일 스프라이트 안의 점. 셀 단위이고 실수다.
	//
	// 실수인 이유 - 짝수 너비의 가운데는 정수로 떨어지지 않는다(8칸이면 3.5).
	// 좌우 반전은 박스 중심 (W-1)/2 축으로 미러링하는 것이라,
	// 피벗이 그 값과 정확히 같아야 뒤집어도 그림이 제자리에 남는다.
	// 3으로 반올림해 저장하면 뒤집을 때마다 한 칸씩 튄다.
	inline float GetPivotX() const { return pivotX; }
	inline float GetPivotY() const { return pivotY; }

	// 기본 피벗 - 가운데 맨 아래.
	static float GetDefaultPivotX(int width) { return (width - 1) * 0.5f; }
	static float GetDefaultPivotY(int height) { return static_cast<float>(height - 1); }

private:
	// 프레임 크기가 모두 같은지 확인하고 width/height를 채운다.
	void ValidateFrames();

private:
	// 클립 식별자. SpriteAnimatorComponent가 이 이름을 키로 클립을 등록한다.
	// 나중에 상태(AnimState)가 재생할 클립을 지목할 때도 이 이름을 쓴다.
	std::string name;

	std::vector<Sprite> frames;

	// 모든 프레임의 공통 크기.
	int width = 0;
	int height = 0;

	float pivotX = 0.0f;
	float pivotY = 0.0f;

	// fps가 아니라 "한 장당 지속 시간"으로 저장한다.
	// 매 틱 나눗셈을 반복하지 않도록 생성 시점에 한 번만 역수를 구해둔다.
	// TODO : 프레임마다 다른 지속 시간이 필요해지면 vector<float>로 확장.
	float frameDuration = 1.0f / 12.0f;

	bool isLooping = true;
};

NAME_SPACE_END
