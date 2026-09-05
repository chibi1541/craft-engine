#include "pch.h"
#include "Asset/LevelLayout.h"
#include "Utils/FileUtils.h"

#include <cstdarg>
#include <cstdio>

NAME_SPACE_BEGIN(Craft)

void LevelLayout::ReportError(const char* format, ...) const
{
	char message[512] = {};

	va_list args;
	va_start(args, format);
	::vsnprintf(message, sizeof(message) - 1, format, args);
	va_end(args);

	char line[640] = {};
	::snprintf(line, sizeof(line) - 1, "[LevelLayout] %s : %s\n",
		levelId.empty() ? "(unknown)" : levelId.c_str(), message);

	// printf가 아니라 OutputDebugString인 이유 - 게임이 콘솔 버퍼를 점유하고 있어서
	// 표준 출력은 화면에 나오지 않는다. LevelMap::ReportError와 같다.
	::OutputDebugStringA(line);
}

bool LevelLayout::ParseFromXml(XmlNode& root)
{
	// levelId는 조회 키가 아니라 진단용이지만, 비어 있으면 오류 메시지가 쓸모없어진다.
	levelId = FileUtils::Convert(std::wstring(root.GetStringAttr(L"levelId", L"")));

	if (levelId.empty())
	{
		ReportError("levelId attribute is missing");
		return false;
	}

	propSetName = FileUtils::Convert(std::wstring(root.GetStringAttr(L"propSet", L"")));

	if (propSetName.empty())
	{
		ReportError("propSet attribute is missing");
		return false;
	}

	// 0이면 레벨이 자기 기본값을 유지한다. 음수는 데이터 실수다.
	tileSize = root.GetInt32Attr(L"tileSize", 0);

	if (tileSize < 0)
	{
		ReportError("invalid tileSize : %d", tileSize);
		return false;
	}

	std::vector<XmlNode> propNodes = root.FindChildren(L"Prop");

	placements.clear();
	placements.reserve(propNodes.size());

	for (size_t index = 0; index < propNodes.size(); ++index)
	{
		XmlNode& propNode = propNodes[index];

		Placement placement;
		placement.name = FileUtils::Convert(std::wstring(propNode.GetStringAttr(L"name", L"")));

		if (placement.name.empty())
		{
			ReportError("prop %d has no name", static_cast<int>(index));
			return false;
		}

		// x/y는 타일 좌상단 셀 좌표다. 기본값을 0으로 두면 좌표를 빠뜨린 항목이
		// 조용히 원점에 쌓이므로, 속성이 실제로 있는지 본다.
		const WCHAR* xText = propNode.GetStringAttr(L"x", L"");
		const WCHAR* yText = propNode.GetStringAttr(L"y", L"");

		if (xText[0] == 0 || yText[0] == 0)
		{
			ReportError("prop %d (%s) is missing x or y",
				static_cast<int>(index), placement.name.c_str());
			return false;
		}

		placement.tileX = propNode.GetInt32Attr(L"x");
		placement.tileY = propNode.GetInt32Attr(L"y");

		const std::string facingText =
			FileUtils::Convert(std::wstring(propNode.GetStringAttr(L"facing", L"Up")));

		bool parsedFacing = false;
		placement.facing = ParseFacing(facingText, EFacing::Up, &parsedFacing);

		// facing 오타를 Up으로 조용히 흘려보내면 엉뚱한 그림이 나오고,
		// 그 원인이 데이터라는 것을 화면만 봐서는 알 수 없다.
		if (parsedFacing == false)
		{
			ReportError("prop %d (%s) has an unknown facing : %s",
				static_cast<int>(index), placement.name.c_str(), facingText.c_str());
			return false;
		}

		placements.emplace_back(std::move(placement));
	}

	if (placements.empty())
	{
		ReportError("no Prop entries");
		return false;
	}

	return true;
}

LevelLayout LevelLayout::LoadFromFile(const WCHAR* path)
{
	LevelLayout layout;

	XmlParser parser;
	XmlNode root;

	if (parser.ParseFromFile(path, OUT root) == false || root.IsValid() == false)
	{
		::OutputDebugStringA("[LevelLayout] failed to parse layout xml\n");
		return LevelLayout();
	}

	if (layout.ParseFromXml(root) == false)
	{
		// 반쯤 읽힌 배치를 돌려주면 레벨이 일부만 세워진 채로 뜬다.
		// 빈 배치로 확실히 비우고, 이유는 위에서 이미 남겼다.
		return LevelLayout();
	}

	return layout;
}

NAME_SPACE_END
