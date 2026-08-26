#pragma once

#include "Utils/EngineMacro.h"

#include <string>

NAME_SPACE_BEGIN(Craft)

// 애니메이션 한 장(프레임)에 해당하는 그림 데이터.
//
// Renderer::SubmitPixels()가 받는 형식을 그대로 들고 있다.
// 즉 '\n'으로 줄이 구분된 "기호 문자열"이고, 기호 -> 색 변환은
// 여기가 아니라 상위(SpriteAnimatorComponent)의 팔레트가 담당한다.
// 그림과 색을 분리해두면 같은 스프라이트를 다른 팔레트로 재활용할 수 있다.
class CRAFT_API Sprite
{
public:
	Sprite() = default;

	// explicit: 문자열이 실수로 Sprite로 암시 변환되는 걸 막는다.
	// (AnimationClip이 vector<Sprite>를 받기 때문에 실수하기 쉬운 자리)
	explicit Sprite(const std::string& pixelMap);

	~Sprite() = default;

	// Renderer::SubmitPixels()에 그대로 넘기는 값.
	inline const std::string& GetPixelMap() const { return pixelMap; }

	inline int GetWidth() const { return width; }
	inline int GetHeight() const { return height; }

	inline bool IsEmpty() const { return pixelMap.empty(); }

private:
	// '\n'으로 줄이 구분된 기호 문자열.
	std::string pixelMap;

	// 한 줄의 길이(칸 수). 모든 줄이 같은 길이라고 가정한다.
	int width = 0;

	// 줄 수.
	int height = 0;
};

NAME_SPACE_END
