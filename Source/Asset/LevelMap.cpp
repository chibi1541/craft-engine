#include "pch.h"
#include "Asset/LevelMap.h"

#include <cstdarg>
#include <cstdio>

NAME_SPACE_BEGIN(Craft)

namespace
{
	// 셀 하나가 가질 수 있는 값인지.
	//
	// SymbolPalette::Contains('.')는 일부러 false다(투명은 색이 아니라서 표에 없다).
	// 그래서 투명 기호를 따로 허용해야 한다. 이걸 빠뜨리면 의도적으로 뚫어놓은
	// 구멍이 있는 레벨이 로드 단계에서 거부된다.
	bool IsLegalCell(char symbol)
	{
		return symbol == SymbolPalette::TransparentSymbol || SymbolPalette::Contains(symbol);
	}
}

void LevelMap::ReportError(const char* format, ...) const
{
	char message[512] = {};

	va_list args;
	va_start(args, format);
	::vsnprintf(message, sizeof(message) - 1, format, args);
	va_end(args);

	char line[640] = {};
	::snprintf(line, sizeof(line) - 1, "[LevelMap] %s : %s\n",
		levelId.empty() ? "(unknown)" : levelId.c_str(), message);

	// printf가 아니라 OutputDebugString인 이유 - 게임이 콘솔 버퍼를 점유하고 있어서
	// 표준 출력은 화면에 나오지 않는다. AssetManager::CheckOrCrash도 같은 이유다.
	::OutputDebugStringA(line);
}

bool LevelMap::AppendRow(int rowIndex, const WCHAR* line)
{
	// WCHAR* -> char 변환에 FileUtils::Convert를 쓰지 않는 이유.
	//
	// 데이터가 ASCII 보장이라, 직접 순회하면 검증과 축소를 한 번에 하면서
	// 틀린 지점의 (행, 열)을 그대로 남길 수 있다.
	// Convert는 행마다 wstring 복사 + WideCharToMultiByte 호출이 생기는 데다
	// 어디가 틀렸는지를 잃어버린다.
	int column = 0;

	for (; line[column] != 0; ++column)
	{
		if (column >= width)
		{
			ReportError("row %d is longer than width %d", rowIndex, width);
			return false;
		}

		const WCHAR wide = line[column];

		if (wide < 32 || wide > 126)
		{
			ReportError("row %d col %d : non-ascii character (U+%04X)",
				rowIndex, column, static_cast<unsigned int>(wide));
			return false;
		}

		const char symbol = static_cast<char>(wide);

		if (IsLegalCell(symbol) == false)
		{
			ReportError("row %d col %d : unknown symbol '%c' (0x%02X). SymbolPalette를 확인할 것",
				rowIndex, column, symbol, static_cast<unsigned char>(symbol));
			return false;
		}

		cells.push_back(symbol);
	}

	if (column != width)
	{
		ReportError("row %d length %d, expected %d", rowIndex, column, width);
		return false;
	}

	return true;
}

bool LevelMap::ParseFromXml(XmlNode& root)
{
	// levelId는 조회 키가 아니라 진단용이지만, 비어 있으면 오류 메시지가 쓸모없어진다.
	const WCHAR* wideLevelId = root.GetStringAttr(L"levelId", L"");

	if (wideLevelId[0] == 0)
	{
		ReportError("levelId attribute is missing");
		return false;
	}

	// 한 글자씩 좁힌다. assign(begin, end)로 넘기면 컴파일러가 C4244를 낸다
	// (wchar_t -> char 축소). ASCII만 허용한다는 것을 여기서 명시적으로 검사한다.
	levelId.clear();

	for (int index = 0; wideLevelId[index] != 0; ++index)
	{
		const WCHAR wide = wideLevelId[index];

		if (wide < 32 || wide > 126)
		{
			ReportError("levelId contains a non-ascii character (U+%04X)",
				static_cast<unsigned int>(wide));
			return false;
		}

		levelId.push_back(static_cast<char>(wide));
	}

	width = root.GetInt32Attr(L"width");
	height = root.GetInt32Attr(L"height");

	if (width <= 0 || height <= 0)
	{
		ReportError("invalid header : width=%d height=%d", width, height);
		return false;
	}

	// 서버의 Level 파서와 같은 수준으로 엄격하게 본다.
	// 행이 하나만 짧아도 그 아래 전체가 한 칸씩 밀려서, 통과시키면
	// "왜 지형이 대각선으로 어긋나지"를 렌더러에서 찾게 된다.
	std::vector<XmlNode> rows = root.FindChildren(L"Row");

	if (static_cast<int>(rows.size()) != height)
	{
		ReportError("row count %d, expected %d", static_cast<int>(rows.size()), height);
		return false;
	}

	cells.clear();
	cells.reserve(static_cast<size_t>(width) * height);

	for (int rowIndex = 0; rowIndex < height; ++rowIndex)
	{
		if (AppendRow(rowIndex, rows[rowIndex].GetStringValue()) == false)
		{
			return false;
		}
	}

	// 위 검사가 전부 통과했으면 반드시 성립하지만, 인덱싱이 이 항등식에
	// 통째로 기대고 있어서 한 번 더 확인한다.
	if (cells.size() != static_cast<size_t>(width) * height)
	{
		ReportError("cell count %d, expected %d",
			static_cast<int>(cells.size()), width * height);
		return false;
	}

	return true;
}

LevelMap LevelMap::LoadFromFile(const WCHAR* path)
{
	LevelMap levelMap;

	XmlParser parser;
	XmlNode root;

	if (parser.ParseFromFile(path, OUT root) == false || root.IsValid() == false)
	{
		::OutputDebugStringA("[LevelMap] failed to parse level xml\n");
		return LevelMap();
	}

	if (levelMap.ParseFromXml(root) == false)
	{
		// 반쯤 채워진 격자를 돌려주면 인덱싱이 어긋난 채로 그려진다.
		// 빈 격자로 확실히 비우고, 이유는 위에서 이미 남겼다.
		return LevelMap();
	}

	return levelMap;
}

NAME_SPACE_END
