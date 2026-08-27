#include "pch.h"
#include "AnimParameters.h"

NAME_SPACE_BEGIN(Craft)

void AnimParameters::SetFloat(const std::string& name, float value)
{
	values[name] = value;
}

void AnimParameters::SetBool(const std::string& name, bool value)
{
	values[name] = value ? 1.0f : 0.0f;
}

void AnimParameters::SetInt(const std::string& name, int value)
{
	values[name] = static_cast<float>(value);
}

float AnimParameters::GetFloat(const std::string& name, float defaultValue) const
{
	auto it = values.find(name);

	if (it == values.end())
	{
		return defaultValue;
	}

	return it->second;
}

bool AnimParameters::GetBool(const std::string& name, bool defaultValue) const
{
	auto it = values.find(name);

	if (it == values.end())
	{
		return defaultValue;
	}

	// 0이 아니면 참. SetBool이 넣은 값은 정확히 0.0f / 1.0f다.
	return it->second != 0.0f;
}

int AnimParameters::GetInt(const std::string& name, int defaultValue) const
{
	auto it = values.find(name);

	if (it == values.end())
	{
		return defaultValue;
	}

	return static_cast<int>(it->second);
}

bool AnimParameters::Contains(const std::string& name) const
{
	return values.find(name) != values.end();
}

NAME_SPACE_END
