#pragma once

#include "Utils/EngineMacro.h"
#include "Utils/Types.h"
#include "Animation/AnimParameters.h"
#include "Animation/AnimStateMachine.h"
#include "Animation/AnimationPlayer.h"
#include "Asset/AnimationClip.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

NAME_SPACE_BEGIN(Craft)

// 레이어가 담당하는 행 범위. [startRow, endRow] 양끝 포함.
//
// 본이 없는 2D 픽셀에서 언리얼의 Layered blend per bone에 대응하는 것이 행 마스크다.
// 8x8 캐릭터면 0~3행이 상체, 4~7행이 하체쯤 된다.
struct CRAFT_API AnimLayerMask
{
	int startRow = 0;

	// -1이면 스프라이트 끝까지(= 전체).
	int endRow = -1;

	bool Contains(int row) const;
};

// 레이어가 자기 담당 행을 어떻게 합칠지.
enum class AnimBlendMode
{
	// 마스크 행을 통째로 소유한다. 투명한 칸까지 그대로 반영돼서 아래 레이어를 지운다.
	// 신체 부위 분리(상체는 공격, 하체는 걷기)가 이쪽이다.
	// 언리얼의 Layered blend per bone과 같은 의미.
	Replace,

	// 불투명한 칸만 덮어쓴다. 투명한 칸으로는 아래 레이어가 비친다.
	// 전신 위에 이펙트를 얹는 용도.
	//
	// 주의 - 이 모드로는 픽셀을 지울 수 없다.
	// 아래 레이어에 있는 것을 없애는 동작(다리를 드느라 다리 픽셀을 빼는 등)은
	// 아래 레이어가 그대로 남아서 화면상 아무 일도 일어나지 않는다.
	Overlay,
};

// 상태 머신 하나와 그 재생 상태를 묶은 단위.
// 레이어를 여러 개 두고 합성하면 "상체는 공격, 하체는 걷기"가 된다.
class CRAFT_API AnimLayer
{
public:
	std::string name;

	AnimLayerMask mask;

	AnimBlendMode blendMode = AnimBlendMode::Replace;

	AnimStateMachine stateMachine;

	AnimationPlayer player;

	// 현재 상태에 머문 시간(초). 전이 조건 stateTime의 값이 된다.
	float stateTime = 0.0f;
};

// 애니메이션 계층의 주체. (언리얼의 UAnimInstance)
//
// 게임플레이 -> 파라미터 -> 전이 -> 클립 진행 -> 프레임 산출로 이어지는 단방향 파이프라인의
// 한가운데 있다. 애니메이션이 게임플레이를 거꾸로 건드리는 경로는 없다.
//
// SpriteAnimatorComponent는 이걸 소유해서 매 틱 Tick()을 돌리고,
// Draw에서 GetCurrentPixelMap()이 내놓은 결과만 화면에 제출한다.
class CRAFT_API AnimInstance
{
public:
	AnimInstance() = default;
	~AnimInstance() = default;

	// 복사 금지.
	//
	// 실용적인 이유 - 레이어를 unique_ptr로 담고 있어서 애초에 복사가 안 된다.
	// 명시하지 않으면 CRAFT_API(dllexport)가 암시적 복사 대입까지 만들어내려 하다가
	// 컴파일 에러가 난다. dllexport는 암시 멤버를 전부 실체화하기 때문이다.
	//
	// 의미적인 이유 - AnimInstance는 특정 액터의 "지금 재생 상태"다.
	// 통째로 복사해서 쓸 물건이 아니다.
	AnimInstance(const AnimInstance&) = delete;
	AnimInstance& operator=(const AnimInstance&) = delete;

	// 게임플레이가 값을 넣는 창구.
	inline AnimParameters& GetParameters() { return parameters; }
	inline const AnimParameters& GetParameters() const { return parameters; }

	// 클립 등록. 키는 clip->GetName().
	void AddClip(const std::shared_ptr<const AnimationClip>& clip);
	std::shared_ptr<const AnimationClip> FindClip(const std::string& name) const;
	inline int GetClipCount() const { return static_cast<int>(clipMap.size()); }
	inline bool HasClip(const std::string& name) const { return clipMap.find(name) != clipMap.end(); }

	// 레이어 추가. 추가한 순서가 곧 합성 순서이고, 뒤에 넣은 레이어가 위에 덮인다.
	AnimLayer& AddLayer(
		const std::string& name,
		const AnimLayerMask& mask,
		AnimBlendMode blendMode = AnimBlendMode::Replace
	);
	AnimLayer* FindLayer(const std::string& name);
	AnimLayer* GetLayer(int index);
	inline int GetLayerCount() const { return static_cast<int>(layers.size()); }

	// 평가 -> 재생 -> 합성. 매 틱 한 번.
	void Tick(float deltaTime);

	// 이번 프레임에 그릴 픽셀맵(합성 결과). 그릴 게 없으면 빈 문자열.
	inline const std::string& GetCurrentPixelMap() const { return compositeBuffer; }

	// 좌우 반전. 아트가 그려진 방향이 false다.
	//
	// 합성이 끝난 뒤 결과를 한 번만 뒤집는다. 레이어별로 뒤집지 않는 이유는
	// 마스크가 행 기준이라 가로 반전과 무관해서 결과가 같고, 한 번이면 충분하기 때문이다.
	// Renderer도 건드릴 필요가 없다.
	void SetFlipX(bool newFlipX);
	inline bool GetFlipX() const { return flipX; }

	// 합성 결과 안에서 액터 위치에 놓여야 할 칸(셀 단위, 반올림 완료).
	// 반전 상태를 반영한다. 그릴 게 없으면 Vector2::Zero.
	//
	// 화면 좌표로 옮기는 건 호출자 몫이다 - 확대 배율을 아는 쪽이 곱해야 한다.
	//   좌상단 = 액터 위치 + offset - (피벗셀.x * scaleX, 피벗셀.y * scaleY)
	Vector2 GetCurrentPivotCell() const;

private:
	// 레이어 하나의 상태를 갱신한다(전이 평가 -> 클립 적용 -> 시간 전진).
	void TickLayer(AnimLayer& layer, float deltaTime);

	// 레이어들의 현재 프레임을 마스크대로 겹쳐서 compositeBuffer를 만든다.
	void Composite();

private:
	std::unordered_map<std::string, std::shared_ptr<const AnimationClip>> clipMap;

	AnimParameters parameters;

	// 순서 = 합성 순서. AnimLayer는 덩치가 있고 참조를 돌려주므로
	// vector 재할당에도 주소가 안 바뀌도록 unique_ptr로 담는다.
	std::vector<std::unique_ptr<AnimLayer>> layers;

	// 매 프레임 문자열을 새로 만들지 않도록 재사용하는 합성 버퍼.
	std::string compositeBuffer;

	// 합성 결과의 크기와 피벗. Composite()가 채운다.
	int compositeWidth = 0;
	int compositeHeight = 0;
	float compositePivotX = 0.0f;
	float compositePivotY = 0.0f;

	bool flipX = false;
};

NAME_SPACE_END
