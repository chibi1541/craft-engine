#include "pch.h"
#include "Asset/PropDataAsset.h"
#include "Utils/FileUtils.h"

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 못 찾았을 때 돌려줄 빈 문자열.
	// 참조를 반환하는 함수라 지역 변수를 쓸 수 없다.
	const std::wstring emptyPath;
}

bool PropDataAsset::LoadFromXml(XmlNode& root)
{
	XmlNode listNode = root.FindChild(L"PropSets");

	// LevelData의 Levels와 같은 이유로 이 목록은 선택 항목이 아니다.
	// 프롭 묶음이 하나도 없는 PropData는 존재할 이유가 없고,
	// 허용하면 "이름을 못 찾는" 증상으로만 뒤늦게 드러난다.
	if (!listNode.IsValid())
	{
		return false;
	}

	for (XmlNode& itemNode : listNode.FindChildren(L"PropSet"))
	{
		const std::string name = FileUtils::Convert(std::wstring(itemNode.GetStringAttr(L"name", L"")));
		const std::wstring path = itemNode.GetStringAttr(L"path", L"");

		// 이름이나 경로가 빠진 항목은 데이터 실수다.
		if (name.empty() || path.empty())
		{
			return false;
		}

		// 같은 이름이 두 번 나오면 어느 쪽이 이기는지 데이터만 보고 알 수 없다.
		if (propSetPaths.find(name) != propSetPaths.end())
		{
			return false;
		}

		propSetPaths.emplace(name, path);
	}

	return !propSetPaths.empty();
}

const std::wstring& PropDataAsset::FindPropSetPath(const std::string& name) const
{
	auto it = propSetPaths.find(name);

	if (it == propSetPaths.end())
	{
		return emptyPath;
	}

	return it->second;
}

NAME_SPACE_END
