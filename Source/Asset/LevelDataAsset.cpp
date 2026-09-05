#include "pch.h"
#include "Asset/LevelDataAsset.h"
#include "Utils/FileUtils.h"

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 못 찾았을 때 돌려줄 빈 문자열.
	// 참조를 반환하는 함수라 지역 변수를 쓸 수 없다.
	const std::wstring emptyPath;
}

bool LevelDataAsset::LoadFromXml(XmlNode& root)
{
	XmlNode listNode = root.FindChild(L"Levels");

	// AnimationData의 StateMachines와 달리 이 목록은 선택 항목이 아니다.
	// 레벨이 하나도 없는 LevelData는 존재할 이유가 없고,
	// 허용하면 "레벨 이름을 못 찾는" 증상으로만 뒤늦게 드러난다.
	if (!listNode.IsValid())
	{
		return false;
	}

	for (XmlNode& itemNode : listNode.FindChildren(L"Level"))
	{
		const std::string name = FileUtils::Convert(std::wstring(itemNode.GetStringAttr(L"name", L"")));
		const std::wstring path = itemNode.GetStringAttr(L"path", L"");

		// 이름이나 경로가 빠진 항목은 데이터 실수다.
		// 조용히 건너뛰면 나중에 "이름을 못 찾는" 증상으로만 드러나서 원인을 찾기 어렵다.
		if (name.empty() || path.empty())
		{
			return false;
		}

		// 같은 이름이 두 번 나오면 어느 쪽이 이기는지 데이터만 보고 알 수 없다.
		if (levelPaths.find(name) != levelPaths.end())
		{
			return false;
		}

		levelPaths.emplace(name, path);

		// layout은 선택 항목이다. 없으면 프롭 없는 레벨.
		const std::wstring layoutPath = itemNode.GetStringAttr(L"layout", L"");

		if (layoutPath.empty() == false)
		{
			layoutPaths.emplace(name, layoutPath);
		}
	}

	return !levelPaths.empty();
}

const std::wstring& LevelDataAsset::FindLevelPath(const std::string& name) const
{
	auto it = levelPaths.find(name);

	if (it == levelPaths.end())
	{
		return emptyPath;
	}

	return it->second;
}

const std::wstring& LevelDataAsset::FindLayoutPath(const std::string& name) const
{
	auto it = layoutPaths.find(name);

	if (it == layoutPaths.end())
	{
		return emptyPath;
	}

	return it->second;
}

NAME_SPACE_END
