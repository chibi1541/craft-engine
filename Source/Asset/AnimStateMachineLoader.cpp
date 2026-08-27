#include "pch.h"
#include "AnimStateMachineLoader.h"
#include <algorithm>

NAME_SPACE_BEGIN(Craft)

// ===========================================================================
// 두 포맷이 함께 쓰는 헬퍼
// ===========================================================================
namespace
{
	using json = nlohmann::json;

	// 데이터 실수를 잡을 때 원인을 남기고 죽는다.
	//
	// ASSERT_CRASH는 널 포인터에 쓰기라 메시지가 없다.
	// 상태 머신 정의는 실패 종류가 많아서 어디가 틀렸는지 모르면 찾기 어렵다.
	// OutputDebugStringA는 VS 출력 창에 뜬다.
	// (게임이 콘솔 화면 버퍼를 점유하고 있어서 printf는 눈에 안 보인다)
	void CheckOrCrash(bool condition, const std::string& message)
	{
		if (condition)
		{
			return;
		}

		::OutputDebugStringA("[AnimStateMachineLoader] ");
		::OutputDebugStringA(message.c_str());
		::OutputDebugStringA("\n");

		ASSERT_CRASH(false);
	}

	std::string ToNarrow(const WCHAR* text)
	{
		return FileUtils::Convert(std::wstring(text));
	}

	// 앞뒤 공백/탭/개행 제거.
	std::string Trim(const std::string& text)
	{
		size_t begin = 0;
		size_t end = text.size();

		while (begin < end && (text[begin] == ' ' || text[begin] == '\t'
			|| text[begin] == '\r' || text[begin] == '\n'))
		{
			++begin;
		}

		while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'
			|| text[end - 1] == '\r' || text[end - 1] == '\n'))
		{
			--end;
		}

		return text.substr(begin, end - begin);
	}

	// "0" / "0-3" / "all" / "" 을 행 마스크로 바꾼다.
	AnimLayerMask ParseRowRange(const std::string& text)
	{
		AnimLayerMask mask;

		// 생략했거나 "all"이면 전체를 담당한다.
		if (text.empty() || text == "all")
		{
			mask.startRow = 0;
			mask.endRow = -1;

			return mask;
		}

		const size_t dashPos = text.find('-');

		if (dashPos == std::string::npos)
		{
			// 한 줄짜리 마스크. "3" -> 3행만.
			mask.startRow = ::atoi(text.c_str());
			mask.endRow = mask.startRow;

			return mask;
		}

		mask.startRow = ::atoi(text.substr(0, dashPos).c_str());
		mask.endRow = ::atoi(text.substr(dashPos + 1).c_str());

		// 뒤집힌 범위는 데이터 실수.
		CheckOrCrash(mask.startRow >= 0 && mask.endRow >= mask.startRow,
			"rows range is inverted or negative: " + text);

		return mask;
	}

	// "true" / "false" / "" 를 bool로 바꾼다. 생략(빈 문자열)이면 defaultValue.
	// AnimState::canBlend가 기본값 true인 것과 짝을 이룬다 - blend를 안 쓰면 항상 true다.
	bool ParseBool(const std::string& text, bool defaultValue)
	{
		if (text.empty())
		{
			return defaultValue;
		}

		if (text == "true")
		{
			return true;
		}

		if (text == "false")
		{
			return false;
		}

		CheckOrCrash(false, "expected 'true' or 'false', got '" + text + "'");

		return defaultValue;
	}

	// 파라미터 기본값을 읽는다. true/false도 받는다.
	float ParseParameterValue(const std::string& text)
	{
		if (text == "true")
		{
			return 1.0f;
		}

		if (text == "false" || text.empty())
		{
			return 0.0f;
		}

		return static_cast<float>(::atof(text.c_str()));
	}

	// 조건 문자열 하나를 검증해서 전이에 붙인다.
	void AddConditionChecked(const std::string& test, const AnimInstance& instance, AnimTransition& outTransition)
	{
		AnimCondition condition;

		// 형식이 틀린 조건은 실행 중에 조용히 거짓이 되어 원인을 찾기 어렵다.
		// 로드 시점에 잡는다.
		const bool hasParsed = AnimCondition::Parse(test, condition);
		CheckOrCrash(hasParsed, "condition syntax error: '" + test + "'");

		// 선언되지 않은 파라미터를 쓰면 항상 0으로 평가돼서 전이가 안 도는데,
		// 그 원인이 오타라는 걸 실행 중에 알아채기 어렵다. 여기서 막는다.
		CheckOrCrash(AnimCondition::IsBuiltInParameter(condition.parameterName)
			|| instance.GetParameters().Contains(condition.parameterName),
			"undeclared parameter '" + condition.parameterName + "' in condition '" + test + "'");

		outTransition.conditions.emplace_back(condition);
	}

	// 상태 하나를 검증해서 레이어에 넣는다.
	//
	// layerRoleName은 "Base" 또는 "Overlay" - 에러 메시지용.
	// isBaseLayer가 true인데 hasRowsDeclared도 true면 크래시한다.
	// Base는 항상 전신을 담당해서 rows가 의미 없는 자리인데, 실수로 적었을 가능성이 높다.
	// isEmptyState면 클립을 틀지 않는 상태가 된다.
	void AddStateChecked(
		AnimLayer& outLayer,
		const char* layerRoleName,
		bool isBaseLayer,
		AnimState state,
		bool isEmptyState,
		bool hasRowsDeclared,
		const AnimInstance& instance)
	{
		CheckOrCrash(!state.name.empty(), "state has no name");

		CheckOrCrash(!outLayer.stateMachine.HasState(state.name),
			"duplicate state name '" + state.name + "' in layer '" + layerRoleName + "'");

		CheckOrCrash(!(isBaseLayer && hasRowsDeclared),
			"state '" + state.name + "' in layer 'Base' declares rows, but Base always covers "
			"the whole sprite - rows only means something on Overlay");

		if (isEmptyState)
		{
			// 아무것도 출력하지 않는 상태.
			// Overlay에서는 합성이 통째로 건너뛰어져서 Base가 그대로 비친다.
			// (언리얼로 치면 base pose로 블렌드 아웃하는 자리)
			state.clipName.clear();
		}
		else
		{
			// 등록되지 않은 클립을 지목하면 아무것도 안 그려진다.
			// 그런데 그 화면은 empty 상태와 구분이 안 되기 때문에,
			// 오타인지 의도인지는 여기서 갈라놓아야 한다.
			// (클립을 먼저 로드했는지까지 여기서 걸러진다)
			CheckOrCrash(instance.HasClip(state.clipName),
				"state '" + state.name + "' points at unknown clip '" + state.clipName
				+ "' (load clips first, or mark the state empty)");
		}

		outLayer.stateMachine.AddState(state);
	}

	// 전이 하나를 검증해서 레이어에 넣는다.
	void AddTransitionChecked(AnimLayer& outLayer, const char* layerRoleName, const AnimTransition& transition)
	{
		// from은 비어있어도 된다(Any State). to는 반드시 있어야 한다.
		CheckOrCrash(outLayer.stateMachine.HasState(transition.toStateName),
			"transition target state '" + transition.toStateName + "' not found in layer '" + layerRoleName + "'");

		if (!transition.fromStateName.empty())
		{
			CheckOrCrash(outLayer.stateMachine.HasState(transition.fromStateName),
				"transition source state '" + transition.fromStateName + "' not found in layer '"
				+ layerRoleName + "'");
		}

		outLayer.stateMachine.AddTransition(transition);
	}
}

// ===========================================================================
// XML 경로
// ===========================================================================
namespace
{
	// <Parameters> 선언을 읽어 블랙보드에 기본값을 넣는다.
	void LoadXmlParameters(XmlNode& root, AnimInstance& outInstance)
	{
		XmlNode parametersNode = root.FindChild(L"Parameters");

		if (!parametersNode.IsValid())
		{
			return;
		}

		for (XmlNode& parameterNode : parametersNode.FindChildren(L"Parameter"))
		{
			const std::string name = ToNarrow(parameterNode.GetStringAttr(L"name", L""));

			// 이름 없는 선언은 쓸모가 없다.
			CheckOrCrash(!name.empty(), "<Parameter> has no name attribute");

			const std::string value = ToNarrow(parameterNode.GetStringAttr(L"value", L"0"));

			outInstance.GetParameters().SetFloat(name, ParseParameterValue(value));
		}
	}

	// <States> 를 읽어 상태 머신에 채운다. entry 상태 이름을 반환.
	std::string LoadXmlStates(
		XmlNode& layerNode,
		AnimLayer& outLayer,
		const char* layerRoleName,
		bool isBaseLayer,
		const AnimInstance& instance)
	{
		XmlNode statesNode = layerNode.FindChild(L"States");

		// 상태가 없는 레이어는 아무것도 못 한다.
		CheckOrCrash(statesNode.IsValid(), "layer '" + std::string(layerRoleName) + "' has no <States>");

		for (XmlNode& stateNode : statesNode.FindChildren(L"State"))
		{
			AnimState state;
			state.name = ToNarrow(stateNode.GetStringAttr(L"name", L""));
			state.clipName = ToNarrow(stateNode.GetStringAttr(L"clip", L""));

			const std::string rowsText = ToNarrow(stateNode.GetStringAttr(L"rows", L""));
			state.region = ParseRowRange(rowsText);
			state.canBlend = ParseBool(ToNarrow(stateNode.GetStringAttr(L"blend", L"")), true);

			AddStateChecked(outLayer, layerRoleName, isBaseLayer, state,
				stateNode.GetBoolAttr(L"empty", false), !rowsText.empty(), instance);
		}

		CheckOrCrash(!outLayer.stateMachine.IsEmpty(), "layer '" + std::string(layerRoleName) + "' has no states");

		return ToNarrow(statesNode.GetStringAttr(L"entry", L""));
	}

	// <Transitions> 를 읽어 상태 머신에 채운다.
	void LoadXmlTransitions(
		XmlNode& layerNode,
		AnimLayer& outLayer,
		const char* layerRoleName,
		const AnimInstance& instance)
	{
		XmlNode transitionsNode = layerNode.FindChild(L"Transitions");

		// 전이가 없는 레이어도 있을 수 있다(상태 하나만 계속 재생).
		if (!transitionsNode.IsValid())
		{
			return;
		}

		for (XmlNode& transitionNode : transitionsNode.FindChildren(L"Transition"))
		{
			AnimTransition transition;
			transition.fromStateName = ToNarrow(transitionNode.GetStringAttr(L"from", L""));
			transition.toStateName = ToNarrow(transitionNode.GetStringAttr(L"to", L""));

			for (XmlNode& conditionNode : transitionNode.FindChildren(L"Condition"))
			{
				AddConditionChecked(ToNarrow(conditionNode.GetStringAttr(L"test", L"")), instance, transition);
			}

			AddTransitionChecked(outLayer, layerRoleName, transition);
		}
	}

	// <Layer name="Base"|"Overlay"> 하나를 읽어 해당 AnimLayer를 채운다.
	void LoadXmlLayer(XmlNode& layerNode, AnimLayer& outLayer, const char* layerRoleName,
		bool isBaseLayer, AnimInstance& outInstance)
	{
		const std::string entryStateName =
			LoadXmlStates(layerNode, outLayer, layerRoleName, isBaseLayer, outInstance);

		LoadXmlTransitions(layerNode, outLayer, layerRoleName, outInstance);

		// entry를 생략하면 첫 번째 상태부터 시작한다(AnimStateMachine::AddState가 처리).
		if (entryStateName.empty())
		{
			return;
		}

		const bool hasEntryState = outLayer.stateMachine.SetEntryState(entryStateName);
		CheckOrCrash(hasEntryState,
			"entry state '" + entryStateName + "' not found in layer '" + layerRoleName + "'");
	}
}

int AnimStateMachineLoader::LoadXmlIntoInstance(const WCHAR* path, AnimInstance& outInstance)
{
	// 예외 처리 - 파일이 없으면 여기서 끝낸다.
	// FileUtils::ReadFile은 fs::file_size로 시작해서 없는 파일이면 예외를 던진다.
	const fs::path filePath{ path };

	if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
	{
		return 0;
	}

	XmlParser parser;
	XmlNode root;

	if (!parser.ParseFromFile(path, root))
	{
		return 0;
	}

	if (!root.IsValid())
	{
		return 0;
	}

	// 조건 검증이 파라미터 선언에 기대므로 파라미터를 먼저 읽는다.
	LoadXmlParameters(root, outInstance);

	// 레이어는 정확히 "Base" 1개(필수) + "Overlay" 0~1개만 허용한다.
	bool hasBase = false;
	bool hasOverlay = false;
	int loadedLayerCount = 0;

	for (XmlNode& layerNode : root.FindChildren(L"Layer"))
	{
		const std::string name = ToNarrow(layerNode.GetStringAttr(L"name", L""));

		CheckOrCrash(name == "Base" || name == "Overlay",
			"<Layer name=\"" + name + "\"> is not allowed - only \"Base\" and \"Overlay\" exist");

		if (name == "Base")
		{
			CheckOrCrash(!hasBase, "more than one <Layer name=\"Base\">");
			hasBase = true;

			LoadXmlLayer(layerNode, outInstance.GetBaseLayer(), "Base", true, outInstance);
		}
		else
		{
			CheckOrCrash(!hasOverlay, "more than one <Layer name=\"Overlay\">");
			hasOverlay = true;

			LoadXmlLayer(layerNode, outInstance.GetOverlayLayer(), "Overlay", false, outInstance);
		}

		++loadedLayerCount;
	}

	CheckOrCrash(hasBase, "<AnimStateMachine> has no <Layer name=\"Base\">");

	return loadedLayerCount;
}

// ===========================================================================
// 캔버스(Obsidian .canvas, JSON) 경로
// ===========================================================================
namespace
{
	// 캔버스에서 예약된 노드 이름.
	const char* anyStateNodeName = "Any";
	const char* parametersNodeName = "Parameters";

	// [N] 태그가 없는 전이의 우선순위. 태그가 붙은 것보다 항상 뒤로 간다.
	enum { defaultTransitionPriority = 1000000, };

	// nodes 배열의 항목 하나.
	struct CanvasNode
	{
		std::string id;
		std::string text;      // text 노드의 본문
		std::string label;     // group 노드의 라벨
		bool isGroup = false;

		double x = 0.0;
		double y = 0.0;
		double width = 0.0;
		double height = 0.0;

		double GetCenterX() const { return x + (width * 0.5); }
		double GetCenterY() const { return y + (height * 0.5); }
		double GetArea() const { return width * height; }

		bool Contains(const CanvasNode& other) const
		{
			const double centerX = other.GetCenterX();
			const double centerY = other.GetCenterY();

			return centerX >= x && centerX <= (x + width)
				&& centerY >= y && centerY <= (y + height);
		}
	};

	// 노드 텍스트에서 뽑아낸 것.
	struct CanvasNodeText
	{
		std::string name;
		std::string clipName;
		bool hasEntryTag = false;
		bool hasEmptyTag = false;

		// Overlay 전용. rows 태그를 실제로 썼는지 - Base에 잘못 붙였는지 검증할 때 쓴다.
		bool hasRowsTag = false;
		AnimLayerMask region;
		bool canBlend = true;
	};

	// 캔버스에서 만들어질 레이어 하나. Base 또는 Overlay 둘 중 하나에 대응한다.
	struct CanvasLayer
	{
		bool isBase = false;
		const CanvasNode* group = nullptr;   // nullptr이면 그룹 없는 기본 레이어(=Base로 간주)
		AnimLayer* layer = nullptr;          // GetBaseLayer()/GetOverlayLayer() 결과
		std::string entryStateName;

		const char* RoleName() const { return isBase ? "Base" : "Overlay"; }
	};

	// 캔버스에서 만들어질 전이 하나.
	struct CanvasTransition
	{
		int layerIndex = 0;
		int priority = defaultTransitionPriority;
		std::string sortKey;                 // 우선순위가 같을 때 순서를 고정하기 위한 키
		AnimTransition transition;
	};

	// 줄 앞의 마크다운 장식(# - *)과 공백을 떼어낸다.
	// Obsidian이 노드 텍스트를 마크다운으로 렌더하기 때문에 붙는 것들이다.
	std::string StripMarkdown(const std::string& line)
	{
		size_t begin = 0;

		while (begin < line.size()
			&& (line[begin] == '#' || line[begin] == '-' || line[begin] == '*'
				|| line[begin] == ' ' || line[begin] == '\t'))
		{
			++begin;
		}

		return Trim(line.substr(begin));
	}

	// 문자열을 줄 단위로 나눈다. 빈 줄은 버린다.
	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;

		size_t lineStart = 0;

		while (lineStart <= text.size())
		{
			const size_t newlinePos = text.find('\n', lineStart);
			const size_t lineEnd = (newlinePos == std::string::npos) ? text.size() : newlinePos;

			const std::string line = StripMarkdown(text.substr(lineStart, lineEnd - lineStart));

			if (!line.empty())
			{
				lines.emplace_back(line);
			}

			if (newlinePos == std::string::npos)
			{
				break;
			}

			lineStart = newlinePos + 1;
		}

		return lines;
	}

	// "Idle [entry] [rows=4-7]" 에서 대괄호 안의 태그들을 뽑고, 앞쪽 본문을 반환한다.
	std::string ExtractTags(const std::string& text, OUT std::vector<std::string>& outTags)
	{
		std::string head;
		size_t index = 0;

		while (index < text.size())
		{
			const size_t open = text.find('[', index);

			if (open == std::string::npos)
			{
				head += text.substr(index);

				break;
			}

			// 대괄호 앞까지가 본문이다.
			head += text.substr(index, open - index);

			const size_t close = text.find(']', open);

			if (close == std::string::npos)
			{
				// 닫히지 않은 대괄호는 그냥 본문으로 취급한다.
				head += text.substr(open);

				break;
			}

			outTags.emplace_back(Trim(text.substr(open + 1, close - open - 1)));

			index = close + 1;
		}

		return Trim(head);
	}

	// "key=value" 태그에서 값을 찾는다. 없으면 빈 문자열.
	std::string FindTagValue(const std::vector<std::string>& tags, const std::string& key)
	{
		for (const std::string& tag : tags)
		{
			const size_t equalPos = tag.find('=');

			if (equalPos == std::string::npos)
			{
				continue;
			}

			if (Trim(tag.substr(0, equalPos)) == key)
			{
				return Trim(tag.substr(equalPos + 1));
			}
		}

		return std::string();
	}

	bool HasTag(const std::vector<std::string>& tags, const std::string& key)
	{
		return std::find(tags.begin(), tags.end(), key) != tags.end();
	}

	// 노드 본문을 파싱한다.
	//   # Idle [entry] [rows=4-7] [blend=false]
	//   clip: Idle
	CanvasNodeText ParseNodeText(const std::string& text)
	{
		CanvasNodeText parsed;

		const std::vector<std::string> lines = SplitLines(text);

		if (lines.empty())
		{
			return parsed;
		}

		std::vector<std::string> tags;
		parsed.name = ExtractTags(lines[0], tags);
		parsed.hasEntryTag = HasTag(tags, "entry");
		parsed.hasEmptyTag = HasTag(tags, "empty");

		const std::string rowsTag = FindTagValue(tags, "rows");
		parsed.hasRowsTag = !rowsTag.empty();
		parsed.region = ParseRowRange(rowsTag);
		parsed.canBlend = ParseBool(FindTagValue(tags, "blend"), true);

		// 이후 줄은 "키: 값". 지금은 clip: 만 쓴다.
		for (size_t index = 1; index < lines.size(); ++index)
		{
			const size_t colonPos = lines[index].find(':');

			if (colonPos == std::string::npos)
			{
				continue;
			}

			const std::string key = Trim(lines[index].substr(0, colonPos));
			const std::string value = Trim(lines[index].substr(colonPos + 1));

			if (key == "clip")
			{
				parsed.clipName = value;
			}
		}

		// clip을 생략하면 상태 이름을 클립 이름으로 쓴다.
		if (parsed.clipName.empty())
		{
			parsed.clipName = parsed.name;
		}

		return parsed;
	}

	// 엣지 라벨을 우선순위와 조건 문자열들로 나눈다.
	//   "[0] IsDead == true"  ->  priority 0, ["IsDead == true"]
	//   "a == 1 && b gt 2"    ->  ["a == 1", "b gt 2"]
	int ParseEdgeLabel(const std::string& label, OUT std::vector<std::string>& outConditions)
	{
		std::vector<std::string> tags;
		const std::string body = ExtractTags(label, tags);

		int priority = defaultTransitionPriority;

		for (const std::string& tag : tags)
		{
			// 숫자만 들어있는 태그가 우선순위다.
			if (tag.empty() || tag.find_first_not_of("0123456789") != std::string::npos)
			{
				continue;
			}

			priority = ::atoi(tag.c_str());
		}

		// && 로 나눈다. 조건이 없으면 무조건 전이.
		size_t index = 0;

		while (index <= body.size())
		{
			const size_t andPos = body.find("&&", index);
			const size_t end = (andPos == std::string::npos) ? body.size() : andPos;

			const std::string piece = Trim(body.substr(index, end - index));

			if (!piece.empty())
			{
				outConditions.emplace_back(piece);
			}

			if (andPos == std::string::npos)
			{
				break;
			}

			index = andPos + 2;
		}

		return priority;
	}
}

int AnimStateMachineLoader::LoadCanvasIntoInstance(const WCHAR* path, AnimInstance& outInstance)
{
	// 예외 처리 - 파일이 없으면 여기서 끝낸다.
	const fs::path filePath{ path };

	if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
	{
		return 0;
	}

	const std::vector<BYTE> bytes = FileUtils::ReadFile(path);

	// allow_exceptions = false.
	// 이 엔진에는 try/catch가 한 군데도 없어서 예외 대신 discarded 값을 받는다.
	const json document = json::parse(bytes.begin(), bytes.end(), nullptr, false);

	if (document.is_discarded())
	{
		return 0;
	}

	if (!document.contains("nodes") || !document["nodes"].is_array())
	{
		return 0;
	}

	// -----------------------------------------------------------------------
	// 1) 노드를 그룹과 텍스트로 나눈다.
	// -----------------------------------------------------------------------
	std::vector<CanvasNode> groupNodes;
	std::vector<CanvasNode> textNodes;

	for (const json& item : document["nodes"])
	{
		CanvasNode node;
		node.id = item.value("id", std::string());
		node.x = item.value("x", 0.0);
		node.y = item.value("y", 0.0);
		node.width = item.value("width", 0.0);
		node.height = item.value("height", 0.0);

		const std::string type = item.value("type", std::string());

		if (type == "group")
		{
			node.isGroup = true;
			node.label = item.value("label", std::string());
			groupNodes.emplace_back(node);

			continue;
		}

		if (type != "text")
		{
			// file / link 노드는 그래프와 무관하다. 메모로 쓸 수 있게 그냥 무시한다.
			continue;
		}

		node.text = item.value("text", std::string());
		textNodes.emplace_back(node);
	}

	// -----------------------------------------------------------------------
	// 2) Parameters 노드를 먼저 읽는다. 조건 검증이 여기 기대기 때문이다.
	// -----------------------------------------------------------------------
	bool hasParametersNode = false;

	for (const CanvasNode& node : textNodes)
	{
		const std::vector<std::string> lines = SplitLines(node.text);

		if (lines.empty() || lines[0] != parametersNodeName)
		{
			continue;
		}

		hasParametersNode = true;

		for (size_t index = 1; index < lines.size(); ++index)
		{
			const size_t colonPos = lines[index].find(':');

			CheckOrCrash(colonPos != std::string::npos,
				"Parameters node line is not 'name: value': '" + lines[index] + "'");

			const std::string name = Trim(lines[index].substr(0, colonPos));
			const std::string value = Trim(lines[index].substr(colonPos + 1));

			CheckOrCrash(!name.empty(), "Parameters node has an entry with no name");

			outInstance.GetParameters().SetFloat(name, ParseParameterValue(value));
		}
	}

	// -----------------------------------------------------------------------
	// 3) 그룹을 레이어로 만든다.
	//    그룹 0개 - 전체를 Base 하나로. 1개 - 반드시 "Base". 2개 - 반드시 "Base"+"Overlay".
	//    예전의 [layer=N] 정렬/충돌 검사는 이름 검증으로 대체된다.
	// -----------------------------------------------------------------------
	std::vector<CanvasLayer> canvasLayers;

	if (groupNodes.empty())
	{
		CanvasLayer base;
		base.isBase = true;
		base.layer = &outInstance.GetBaseLayer();
		canvasLayers.emplace_back(base);
	}
	else
	{
		CheckOrCrash(groupNodes.size() <= 2,
			"canvas has " + std::to_string(groupNodes.size())
			+ " layer groups; only \"Base\" and \"Overlay\" are allowed");

		const CanvasNode* baseGroup = nullptr;
		const CanvasNode* overlayGroup = nullptr;

		for (const CanvasNode& group : groupNodes)
		{
			std::vector<std::string> tags;
			const std::string groupName = ExtractTags(group.label, tags);

			if (groupName == "Base")
			{
				CheckOrCrash(nullptr == baseGroup, "more than one group named \"Base\"");
				baseGroup = &group;
			}
			else if (groupName == "Overlay")
			{
				CheckOrCrash(nullptr == overlayGroup, "more than one group named \"Overlay\"");
				overlayGroup = &group;
			}
			else
			{
				CheckOrCrash(false, "layer group named '" + groupName
					+ "' is not allowed - only \"Base\" and \"Overlay\" exist");
			}
		}

		CheckOrCrash(nullptr != baseGroup, "canvas has layer groups but none is named \"Base\"");

		CanvasLayer base;
		base.isBase = true;
		base.group = baseGroup;
		base.layer = &outInstance.GetBaseLayer();
		canvasLayers.emplace_back(base);

		if (nullptr != overlayGroup)
		{
			CanvasLayer overlay;
			overlay.isBase = false;
			overlay.group = overlayGroup;
			overlay.layer = &outInstance.GetOverlayLayer();
			canvasLayers.emplace_back(overlay);
		}
	}

	// -----------------------------------------------------------------------
	// 4) 노드가 어느 레이어에 속하는지 정한다. 중심점이 들어가는 가장 작은 그룹.
	// -----------------------------------------------------------------------
	std::unordered_map<std::string, int> nodeLayerIndexMap;
	std::unordered_map<std::string, CanvasNodeText> nodeTextMap;

	for (const CanvasNode& node : textNodes)
	{
		const CanvasNodeText parsed = ParseNodeText(node.text);

		// Parameters 노드는 상태가 아니고 그룹 소속과도 무관하다.
		if (parsed.name == parametersNodeName)
		{
			continue;
		}

		CheckOrCrash(!parsed.name.empty(), "canvas node has no name: '" + node.text + "'");

		int owningIndex = -1;
		double owningArea = 0.0;

		for (int index = 0; index < static_cast<int>(canvasLayers.size()); ++index)
		{
			const CanvasNode* group = canvasLayers[index].group;

			// 그룹 없는 기본 레이어는 모든 노드를 받는다.
			if (nullptr == group)
			{
				owningIndex = index;

				break;
			}

			if (!group->Contains(node))
			{
				continue;
			}

			// 중첩 그룹이면 안쪽(작은) 것을 고른다.
			if (owningIndex >= 0 && group->GetArea() >= owningArea)
			{
				continue;
			}

			owningIndex = index;
			owningArea = group->GetArea();
		}

		CheckOrCrash(owningIndex >= 0,
			"node '" + parsed.name + "' is outside every layer group");

		nodeLayerIndexMap[node.id] = owningIndex;
		nodeTextMap[node.id] = parsed;
	}

	// -----------------------------------------------------------------------
	// 5) 상태를 채운다. 이름순으로 넣어서 배열 순서에 의존하지 않게 한다.
	// -----------------------------------------------------------------------
	for (int layerIndex = 0; layerIndex < static_cast<int>(canvasLayers.size()); ++layerIndex)
	{
		std::vector<const CanvasNode*> stateNodes;

		for (const CanvasNode& node : textNodes)
		{
			auto it = nodeLayerIndexMap.find(node.id);

			if (it == nodeLayerIndexMap.end() || it->second != layerIndex)
			{
				continue;
			}

			// Any는 상태가 아니라 "어느 상태에서든"을 뜻하는 의사 노드다.
			if (nodeTextMap[node.id].name == anyStateNodeName)
			{
				continue;
			}

			stateNodes.emplace_back(&node);
		}

		std::stable_sort(stateNodes.begin(), stateNodes.end(),
			[&nodeTextMap](const CanvasNode* left, const CanvasNode* right)
			{
				return nodeTextMap[left->id].name < nodeTextMap[right->id].name;
			});

		CanvasLayer& canvasLayer = canvasLayers[layerIndex];

		for (const CanvasNode* node : stateNodes)
		{
			const CanvasNodeText& parsed = nodeTextMap[node->id];

			AnimState state;
			state.name = parsed.name;
			state.clipName = parsed.clipName;
			state.region = parsed.region;
			state.canBlend = parsed.canBlend;

			AddStateChecked(*canvasLayer.layer, canvasLayer.RoleName(), canvasLayer.isBase,
				state, parsed.hasEmptyTag, parsed.hasRowsTag, outInstance);

			if (!parsed.hasEntryTag)
			{
				continue;
			}

			CheckOrCrash(canvasLayer.entryStateName.empty(),
				"layer '" + std::string(canvasLayer.RoleName()) + "' has more than one [entry] node ('"
				+ canvasLayer.entryStateName + "' and '" + parsed.name + "')");

			canvasLayer.entryStateName = parsed.name;
		}

		CheckOrCrash(!canvasLayer.layer->stateMachine.IsEmpty(),
			"layer '" + std::string(canvasLayer.RoleName()) + "' has no states");
	}

	// -----------------------------------------------------------------------
	// 6) 엣지를 전이로 바꾼다.
	// -----------------------------------------------------------------------
	std::vector<CanvasTransition> canvasTransitions;

	if (document.contains("edges") && document["edges"].is_array())
	{
		for (const json& item : document["edges"])
		{
			const std::string fromId = item.value("fromNode", std::string());
			const std::string toId = item.value("toNode", std::string());

			auto fromIt = nodeLayerIndexMap.find(fromId);
			auto toIt = nodeLayerIndexMap.find(toId);

			CheckOrCrash(fromIt != nodeLayerIndexMap.end() && toIt != nodeLayerIndexMap.end(),
				"edge connects a node that is not a state (check for edges to groups or notes)");

			CheckOrCrash(fromIt->second == toIt->second,
				"edge crosses layers: '" + nodeTextMap[fromId].name + "' -> '" + nodeTextMap[toId].name + "'");

			const std::string fromName = nodeTextMap[fromId].name;
			const std::string toName = nodeTextMap[toId].name;

			CheckOrCrash(toName != anyStateNodeName,
				"edge points into the Any node ('" + fromName + "' -> Any); Any can only be a source");

			CanvasTransition canvasTransition;
			canvasTransition.layerIndex = fromIt->second;

			// Any에서 나가는 엣지는 출발 상태가 비어있는 전이(Any State)가 된다.
			canvasTransition.transition.fromStateName = (fromName == anyStateNodeName) ? std::string() : fromName;
			canvasTransition.transition.toStateName = toName;

			const std::string label = item.value("label", std::string());

			std::vector<std::string> conditionTexts;
			canvasTransition.priority = ParseEdgeLabel(label, conditionTexts);

			// Parameters 노드가 없으면 조건에 쓰인 이름을 그대로 선언으로 받아준다.
			// (선언 노드가 있으면 오타 검증이 살아있고, 없으면 편의를 택한다)
			if (!hasParametersNode)
			{
				for (const std::string& text : conditionTexts)
				{
					AnimCondition probe;

					if (!AnimCondition::Parse(text, probe))
					{
						continue;
					}

					if (AnimCondition::IsBuiltInParameter(probe.parameterName)
						|| outInstance.GetParameters().Contains(probe.parameterName))
					{
						continue;
					}

					outInstance.GetParameters().SetFloat(probe.parameterName, 0.0f);
				}
			}

			for (const std::string& text : conditionTexts)
			{
				AddConditionChecked(text, outInstance, canvasTransition.transition);
			}

			// 우선순위가 같을 때도 순서가 흔들리지 않도록 키를 만들어 둔다.
			canvasTransition.sortKey = fromName + '\x01' + toName + '\x01' + label;

			canvasTransitions.emplace_back(canvasTransition);
		}
	}

	// [N]이 작을수록 먼저 검사받는다. 같으면 (from, to, label) 순.
	// 캔버스에서 우선순위를 제어하는 방법은 [N] 태그뿐이다 - 배열 순서는 Obsidian이 바꾼다.
	std::stable_sort(canvasTransitions.begin(), canvasTransitions.end(),
		[](const CanvasTransition& left, const CanvasTransition& right)
		{
			if (left.priority != right.priority)
			{
				return left.priority < right.priority;
			}

			return left.sortKey < right.sortKey;
		});

	for (const CanvasTransition& canvasTransition : canvasTransitions)
	{
		CanvasLayer& target = canvasLayers[canvasTransition.layerIndex];

		AddTransitionChecked(*target.layer, target.RoleName(), canvasTransition.transition);
	}

	// -----------------------------------------------------------------------
	// 7) 시작 상태를 지정한다.
	// -----------------------------------------------------------------------
	for (CanvasLayer& canvasLayer : canvasLayers)
	{
		// [entry]를 생략하면 이름순 첫 상태부터 시작한다.
		if (canvasLayer.entryStateName.empty())
		{
			continue;
		}

		const bool hasEntryState = canvasLayer.layer->stateMachine.SetEntryState(canvasLayer.entryStateName);
		CheckOrCrash(hasEntryState,
			"entry state '" + canvasLayer.entryStateName + "' not found in layer '"
			+ canvasLayer.RoleName() + "'");
	}

	return static_cast<int>(canvasLayers.size());
}

// ===========================================================================
// 진입점 - 확장자로 포맷을 고른다.
// ===========================================================================
int AnimStateMachineLoader::LoadIntoInstance(const WCHAR* path, AnimInstance& outInstance)
{
	const fs::path filePath{ path };

	if (filePath.extension() == L".canvas")
	{
		return LoadCanvasIntoInstance(path, outInstance);
	}

	return LoadXmlIntoInstance(path, outInstance);
}

NAME_SPACE_END
