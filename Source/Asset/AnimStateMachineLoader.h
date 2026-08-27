#pragma once

#include "Utils/EngineMacro.h"
#include "Utils/Types.h"
#include "Animation/AnimInstance.h"

NAME_SPACE_BEGIN(Craft)

// 상태 머신 정의를 읽어서 AnimInstance에 레이어와 상태 머신을 채우는 로더.
//
// 두 가지 포맷을 받는다.
//   *.fsm.xml  손으로 쓰는 XML. 상태 두어 개짜리 간단한 FSM은 이쪽이 빠르다.
//   *.canvas   Obsidian 캔버스(JSON). 그래프를 눈으로 보며 편집한다.
//
// 둘은 표기만 다를 뿐 만들어내는 결과가 같다.
// 캔버스를 중간 XML로 변환하지 않고 곧바로 읽는 이유는,
// 어차피 매 실행마다 JSON을 읽어야 해서 중간 파일이 아무것도 아껴주지 않기 때문이다.
// (저작 포맷과 런타임 포맷을 나누는 건 옳지만 그건 빌드 타임에 할 일이다)
//
// -------------------------------------------------------------------------
// XML 형식 (Assets/*.fsm.xml)
// -------------------------------------------------------------------------
//
//   <AnimStateMachine>
//       <Parameters>
//           <Parameter name="speed" value="0" />
//       </Parameters>
//       <Layer name="Base" rows="all">
//           <States entry="Idle">
//               <State name="Idle" clip="Idle" />
//               <State name="Walk" clip="Walk" />
//           </States>
//           <Transitions>
//               <Transition from="Idle" to="Walk">
//                   <Condition test="speed gt 0" />
//               </Transition>
//           </Transitions>
//       </Layer>
//   </AnimStateMachine>
//
//   Parameter : 조건에서 쓸 파라미터 선언 + 기본값. 선언하지 않은 이름을 조건에 쓰면 크래시한다.
//               (오타를 실행 중이 아니라 로드 시점에 잡기 위한 장치)
//   Layer     : rows 생략 또는 "all"이면 전체. "0-3"이면 0~3행만 담당.
//               쓴 순서가 합성 순서이고 뒤에 쓴 레이어가 위에 덮인다.
//               blend 생략 또는 "replace"면 담당 행을 통째로 가져간다(투명도 그대로 반영).
//               "overlay"면 불투명한 칸만 덮어써서 투명한 칸으로 아래가 비친다.
//   State     : clip은 이미 AnimInstance에 등록된 클립 이름이어야 한다.
//               따라서 클립을 먼저 로드하고 이걸 호출해야 한다.
//               empty="true"면 아무것도 출력하지 않는 상태가 되고 clip은 무시된다.
//               이때 그 레이어는 합성에서 건너뛰어져 아래 레이어가 그대로 비친다.
//               (상체 레이어의 "공격 안 하는 중" 상태가 이것)
//   Transition: from을 비우면 Any State. 쓴 순서가 곧 우선순위다.
//   Condition : "파라미터 연산자 값" 3토큰. 여러 개면 전부 참이어야 전이(AND).
//               연산자는 기호(> >= == != < <=)와 단어(gt ge eq ne lt le)를 모두 받는다.
//               XML 속성값에는 '<'를 그대로 못 넣으므로 단어형이 편하다.
//
// -------------------------------------------------------------------------
// 캔버스 형식 (Assets/**/*.canvas)
// -------------------------------------------------------------------------
//
// Obsidian 캔버스의 JSON 스키마는 건드리지 않는다.
// 노드 텍스트와 엣지 라벨 안의 표기 규약만 정해서 읽는다.
// 그래야 Obsidian을 평범한 편집기로 계속 쓸 수 있다.
//
//   그룹 라벨    "Upper [layer=0] [rows=all]"
//                그룹 하나가 레이어 하나. [layer=N]이 작을수록 아래(바탕).
//                생략 시 0. 같은 N이 둘 이상이면 순서가 모호해서 크래시.
//                [rows=..] 생략 시 전체. 그룹이 하나도 없으면 전체를 Base 레이어 하나로 본다.
//                [blend=overlay]를 붙이면 불투명한 칸만 덮는다. 생략하면 replace.
//
//   노드 텍스트  "# Idle [entry]"  +  "clip: Idle"
//                첫 줄이 상태 이름 + [태그]들, 이후 줄은 "키: 값".
//                Obsidian이 마크다운으로 렌더하므로 줄 앞의 # - * 는 떼어내고 읽는다.
//                태그 : [entry] 시작 상태, [empty] 아무것도 출력하지 않는 상태
//                clip: 생략 시 상태 이름을 클립 이름으로 쓴다.
//                예약 이름 : Any(Any State 의사 노드), Parameters(파라미터 선언 노드)
//
//   엣지 라벨    "[0] IsDead == true"        선두 [N]은 우선순위(작을수록 먼저)
//                "IsAttack == true && speed gt 0"   && 로 AND
//                (빈 라벨)                   무조건 전이
//
//   소속 판정    노드 중심점이 들어가는 가장 작은 그룹.
//                완전 포함이 아니라 중심점이라 노드가 살짝 삐져나가도 유지된다.
//
// 주의 - Obsidian은 팬/줌만 해도 파일을 다시 쓰면서 nodes/edges 배열 순서를 뒤섞는다.
// 그래서 배열 순서에 의존하지 않는다. 레이어는 [layer=N], 상태는 이름,
// 전이는 [N] 다음 (from, to, label) 순으로 정렬해서 결과를 항상 같게 만든다.
// XML과 달리 캔버스에서 전이 우선순위를 제어하는 방법은 [N] 태그뿐이다.
class CRAFT_API AnimStateMachineLoader
{
public:
	// 확장자를 보고 포맷을 고른다. .canvas면 Obsidian 그래프, 아니면 XML.
	// 호출부는 어느 포맷인지 몰라도 된다.
	static int LoadIntoInstance(const WCHAR* path, AnimInstance& outInstance);

	// 포맷을 명시해서 부르고 싶을 때.
	// 둘 다 채운 레이어 수를 반환하고, 파일이 없거나 파싱에 실패하면 0이다.
	// (애니메이션 하나 때문에 게임이 아예 안 뜨는 상황을 막기 위함 - Palette와 같은 방침)
	static int LoadXmlIntoInstance(const WCHAR* path, AnimInstance& outInstance);
	static int LoadCanvasIntoInstance(const WCHAR* path, AnimInstance& outInstance);

private:
	AnimStateMachineLoader() = delete;
};

NAME_SPACE_END
