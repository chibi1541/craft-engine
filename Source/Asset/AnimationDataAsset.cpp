#include "pch.h"
#include "AnimationDataAsset.h"
#include "Utils/FileUtils.h"

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 못 찾았을 때 돌려줄 빈 문자열.
	// 참조를 반환하는 함수라 지역 변수를 쓸 수 없다.
	const std::wstring emptyPath;
}

bool AnimationDataAsset::LoadPathMap(
	XmlNode& root,
	const WCHAR* listTag,
	const WCHAR* itemTag,
	std::unordered_map<std::string, std::wstring>& outMap)
{
	XmlNode listNode = root.FindChild(listTag);

	// 목록 자체가 없는 건 허용한다.
	// (상태 머신을 하나도 안 쓰는 프로젝트도 있을 수 있다)
	if (!listNode.IsValid())
	{
		return true;
	}

	for (XmlNode& itemNode : listNode.FindChildren(itemTag))
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
		if (outMap.find(name) != outMap.end())
		{
			return false;
		}

		outMap.emplace(name, path);
	}

	return true;
}

bool AnimationDataAsset::LoadFromXml(XmlNode& root)
{
	if (!LoadPathMap(root, L"Clips", L"Clip", clipPaths))
	{
		return false;
	}

	if (!LoadPathMap(root, L"StateMachines", L"StateMachine", stateMachinePaths))
	{
		return false;
	}

	// 클립이 하나도 없으면 이 애셋으로 할 수 있는 게 없다.
	return !clipPaths.empty();
}

const std::wstring& AnimationDataAsset::FindClipPath(const std::string& name) const
{
	auto it = clipPaths.find(name);

	return (it != clipPaths.end()) ? it->second : emptyPath;
}

const std::wstring& AnimationDataAsset::FindStateMachinePath(const std::string& name) const
{
	auto it = stateMachinePaths.find(name);

	return (it != stateMachinePaths.end()) ? it->second : emptyPath;
}

NAME_SPACE_END
