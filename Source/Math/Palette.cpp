#include "pch.h"
#include "Palette.h"
#include "Xml/XmlParser.h"

#include <filesystem>

NAME_SPACE_BEGIN(Craft)

// 기본 팔레트. Config/Palette.xml이 없거나 깨져도 이 값으로 동작한다.
// 순서는 Color enum의 슬롯 인덱스와 일치해야 한다.
COLORREF Palette::table[Palette::ColorCount] =
{
	RGB(0,   0,   0),		// 0  Black
	RGB(45,  45,  55),		// 1  DarkGray
	RGB(140, 140, 155),		// 2  Gray
	RGB(245, 245, 255),		// 3  White

	RGB(30,  75,  40),		// 4  DarkGreen
	RGB(75,  150, 60),		// 5  Green

	RGB(60,  40,  30),		// 6  DarkBrown
	RGB(125, 85,  50),		// 7  Brown
	RGB(210, 175, 130),		// 8  Tan

	RGB(120, 30,  35),		// 9  DarkRed
	RGB(210, 60,  50),		// 10 Red

	RGB(240, 140, 50),		// 11 Orange
	RGB(250, 215, 90),		// 12 Yellow

	RGB(35,  55,  120),		// 13 DarkBlue
	RGB(70,  130, 215),		// 14 Blue

	RGB(150, 80,  200),		// 15 Purple
};

bool Palette::LoadFromFile(const WCHAR* path)
{
	// XmlParser는 내부에서 FileUtils::ReadFile -> fs::file_size를 호출하는데,
	// 파일이 없으면 예외를 던진다. 엔진은 예외를 쓰지 않으므로 미리 확인한다.
	if (!std::filesystem::exists(path))
	{
		std::cout << "Palette file not found. Using default palette.\n";
		return false;
	}

	XmlParser parser;
	XmlNode root;

	if (!parser.ParseFromFile(path, OUT root))
	{
		std::cout << "Failed to parse palette file. Using default palette.\n";
		return false;
	}

	if (!root.IsValid())
	{
		std::cout << "Palette file has no root node. Using default palette.\n";
		return false;
	}

	// 읽어들인 값을 바로 table에 쓰지 않고 임시 배열에 모은다.
	// 중간에 잘못된 항목이 있어도 팔레트가 반쯤 덮인 상태로 남지 않도록.
	COLORREF loaded[ColorCount] = {};
	bool filled[ColorCount] = {};

	for (XmlNode& node : root.FindChildren(L"Color"))
	{
		const int32 index = node.GetInt32Attr(L"index", -1);

		// 슬롯 범위를 벗어난 항목은 무시
		if (index < 0 || index >= ColorCount)
		{
			continue;
		}

		const int32 r = node.GetInt32Attr(L"r", 0);
		const int32 g = node.GetInt32Attr(L"g", 0);
		const int32 b = node.GetInt32Attr(L"b", 0);

		loaded[index] = RGB(
			static_cast<BYTE>(r),
			static_cast<BYTE>(g),
			static_cast<BYTE>(b));

		filled[index] = true;
	}

	// 빠진 슬롯이 있으면 기본 팔레트를 유지한다.
	for (int i = 0; i < ColorCount; ++i)
	{
		if (!filled[i])
		{
			std::cout << "Palette file is missing color index " << i << ". Using default palette.\n";
			return false;
		}
	}

	for (int i = 0; i < ColorCount; ++i)
	{
		table[i] = loaded[i];
	}

	return true;
}

COLORREF Palette::Get(Color color)
{
	const int index = static_cast<int>(color);
	ASSERT_CRASH(index >= 0 && index < ColorCount);

	return table[index];
}

const COLORREF* Palette::GetTable()
{
	return table;
}

NAME_SPACE_END
