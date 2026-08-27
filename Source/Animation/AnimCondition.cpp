#include "pch.h"
#include "AnimCondition.h"
#include <vector>

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 내장 파라미터 이름. 컨텍스트에서 값을 가져온다.
	const char* animTimeName = "animTime";
	const char* animFinishedName = "animFinished";
	const char* stateTimeName = "stateTime";

	// 공백(스페이스/탭/개행)으로 잘라서 토큰 목록을 만든다.
	std::vector<std::string> Tokenize(const std::string& text)
	{
		std::vector<std::string> tokens;

		size_t index = 0;

		while (index < text.size())
		{
			// 공백 건너뛰기.
			while (index < text.size() && (text[index] == ' ' || text[index] == '\t'
				|| text[index] == '\r' || text[index] == '\n'))
			{
				++index;
			}

			if (index >= text.size())
			{
				break;
			}

			const size_t tokenStart = index;

			while (index < text.size() && text[index] != ' ' && text[index] != '\t'
				&& text[index] != '\r' && text[index] != '\n')
			{
				++index;
			}

			tokens.emplace_back(text.substr(tokenStart, index - tokenStart));
		}

		return tokens;
	}

	// 연산자 토큰을 해석한다. 기호형과 단어형을 모두 받는다.
	bool ParseCompareOp(const std::string& token, OUT AnimCompareOp& outOp)
	{
		if (token == "==" || token == "eq") { outOp = AnimCompareOp::Equal;        return true; }
		if (token == "!=" || token == "ne") { outOp = AnimCompareOp::NotEqual;     return true; }
		if (token == ">=" || token == "ge") { outOp = AnimCompareOp::GreaterEqual; return true; }
		if (token == "<=" || token == "le") { outOp = AnimCompareOp::LessEqual;    return true; }
		if (token == ">"  || token == "gt") { outOp = AnimCompareOp::Greater;      return true; }
		if (token == "<"  || token == "lt") { outOp = AnimCompareOp::Less;         return true; }

		return false;
	}

	// 값 토큰을 float으로 바꾼다. true/false도 받는다.
	bool ParseValue(const std::string& token, OUT float& outValue)
	{
		if (token == "true")
		{
			outValue = 1.0f;

			return true;
		}

		if (token == "false")
		{
			outValue = 0.0f;

			return true;
		}

		// 숫자 파싱. 토큰 전체를 다 먹었을 때만 성공으로 본다.
		// ("0abc" 같은 걸 0으로 조용히 받아들이면 오타를 놓친다)
		char* parseEnd = nullptr;
		const float parsed = ::strtof(token.c_str(), &parseEnd);

		if (parseEnd == token.c_str() || *parseEnd != '\0')
		{
			return false;
		}

		outValue = parsed;

		return true;
	}
}

bool AnimCondition::IsBuiltInParameter(const std::string& name)
{
	return name == animTimeName || name == animFinishedName || name == stateTimeName;
}

bool AnimCondition::Parse(const std::string& text, OUT AnimCondition& outCondition)
{
	const std::vector<std::string> tokens = Tokenize(text);

	// 1토큰 축약: "animFinished" -> "animFinished == true"
	if (tokens.size() == 1)
	{
		outCondition.parameterName = tokens[0];
		outCondition.op = AnimCompareOp::Equal;
		outCondition.value = 1.0f;

		return true;
	}

	if (tokens.size() != 3)
	{
		return false;
	}

	AnimCompareOp parsedOp = AnimCompareOp::Equal;

	if (!ParseCompareOp(tokens[1], parsedOp))
	{
		return false;
	}

	float parsedValue = 0.0f;

	if (!ParseValue(tokens[2], parsedValue))
	{
		return false;
	}

	outCondition.parameterName = tokens[0];
	outCondition.op = parsedOp;
	outCondition.value = parsedValue;

	return true;
}

bool AnimCondition::Evaluate(const AnimEvalContext& context) const
{
	// 왼쪽 값을 구한다. 내장 이름이면 컨텍스트에서, 아니면 블랙보드에서.
	float left = 0.0f;

	if (parameterName == animTimeName)
	{
		left = context.animTime;
	}
	else if (parameterName == animFinishedName)
	{
		left = context.animFinished ? 1.0f : 0.0f;
	}
	else if (parameterName == stateTimeName)
	{
		left = context.stateTime;
	}
	else if (nullptr != context.parameters)
	{
		// 선언되지 않은 파라미터는 로더가 걸러내므로 여기까지 오면 있는 값이다.
		left = context.parameters->GetFloat(parameterName, 0.0f);
	}

	switch (op)
	{
	case AnimCompareOp::Equal:			return left == value;
	case AnimCompareOp::NotEqual:		return left != value;
	case AnimCompareOp::Greater:		return left > value;
	case AnimCompareOp::GreaterEqual:	return left >= value;
	case AnimCompareOp::Less:			return left < value;
	case AnimCompareOp::LessEqual:		return left <= value;
	}

	return false;
}

NAME_SPACE_END
