#pragma once

#include "Utils/EngineMacro.h"
#include "Utils/Types.h"
#include "Asset/AnimationClip.h"
#include <memory>
#include <string>
#include <vector>

NAME_SPACE_BEGIN(Craft)

// 스프라이트 애니메이션 정의 XML을 읽어서 AnimationClip 목록으로 만드는 로더.
//
// 파일 형식 (Assets/*.anim.xml):
//
//   <SpriteAnimation>
//       <Clip name="Idle" fps="3" loop="true">
//           <Frame>
//               .DDDDD.D
//               .DLLLL.D
//           </Frame>
//           <Frame> ... </Frame>
//       </Clip>
//   </SpriteAnimation>
//
//   name : 클립 이름. SpriteAnimatorComponent::PlayClip()에 넘기는 그 이름.
//   fps  : 초당 넘길 프레임 수. 생략하면 12.
//   loop : 끝에서 처음으로 돌아갈지. 생략하면 true.
//   Frame: 픽셀맵 한 장. 줄 앞뒤 공백은 버리므로 XML 들여쓰기를 자유롭게 써도 된다.
//          기호는 SymbolPalette의 16개 대문자 + 투명('.')만 쓸 수 있다.
//
// TODO : 지금은 액터가 파일 경로를 직접 들고 있다.
//        애셋 매니저 / 데이터 애셋이 생기면 그쪽에서 캐싱해 나눠주는 구조로 옮긴다.
class CRAFT_API SpriteAnimationLoader
{
public:
	// XML에서 클립 목록을 읽는다.
	// 파일이 없거나 파싱에 실패하면 빈 목록을 반환한다.
	// (애니메이션 하나 때문에 게임이 아예 안 뜨는 상황을 막기 위함 - Palette와 같은 방침)
	static std::vector<std::shared_ptr<const AnimationClip>> LoadFromFile(const WCHAR* path);

private:
	// XML 텍스트를 픽셀맵 문자열로 다듬는다.
	// 줄 앞뒤 공백 제거 + 빈 줄 제거 + '\n'으로 재조립.
	static std::string NormalizePixelMap(const std::string& rawText);

	SpriteAnimationLoader() = delete;
};

NAME_SPACE_END
