#include "pch.h"
#include "SymbolPalette.h"

NAME_SPACE_BEGIN(Craft)

const std::unordered_map<char, Color>& SymbolPalette::GetTable()
{
	// 기호는 색 이름에서 연상되는 글자로 골랐고, 같은 램프 안에서 겹치지 않게 나눴다.
	// 어두운 쪽은 다른 단어의 머리글자를 쓴다(Forest, naVy, Crimson 등).
	static const std::unordered_map<char, Color> table = {
		// 무채색 램프
		{ 'K', Color::Black },			// blacK
		{ 'D', Color::DarkGray },		// Dark
		{ 'L', Color::Gray },			// Light gray
		{ 'B', Color::White },			// Bright

		// 녹색 램프 (풀, 나뭇잎)
		{ 'F', Color::DarkGreen },		// Forest
		{ 'G', Color::Green },			// Green

		// 갈색 램프 (흙, 나무, 가죽, 피부)
		{ 'N', Color::DarkBrown },		// browN
		{ 'W', Color::Brown },			// Wood
		{ 'T', Color::Tan },			// Tan

		// 적색 램프 (적, 피, HP)
		{ 'C', Color::DarkRed },		// Crimson
		{ 'R', Color::Red },			// Red

		// 난색 (불, 금화, 발사체)
		{ 'O', Color::Orange },			// Orange
		{ 'Y', Color::Yellow },			// Yellow

		// 청색 램프 (물, 얼음, 마법)
		{ 'V', Color::DarkBlue },		// naVy
		{ 'U', Color::Blue },			// blUe

		// 마법, 레어 등급
		{ 'P', Color::Purple },			// Purple
	};

	return table;
}

const std::unordered_map<char, Color>& SymbolPalette::GetSolidTable(Color color)
{
	// 색깔별 캐시. 플래시에 쓰는 색은 사실상 White 하나라 항목이 몇 개 안 된다.
	static std::unordered_map<int, std::unordered_map<char, Color>> cache;

	auto it = cache.find(static_cast<int>(color));
	if (it != cache.end())
	{
		return it->second;
	}

	std::unordered_map<char, Color> solid;
	for (const auto& pair : GetTable())
	{
		solid[pair.first] = color;
	}

	return cache.emplace(static_cast<int>(color), std::move(solid)).first->second;
}

bool SymbolPalette::Contains(char symbol)
{
	return GetTable().find(symbol) != GetTable().end();
}

Color SymbolPalette::Get(char symbol)
{
	const std::unordered_map<char, Color>& table = GetTable();

	auto it = table.find(symbol);

	// 표에 없는 기호 - 픽셀맵 오타 등을 즉시 확인.
	ASSERT_CRASH(it != table.end());

	return it->second;
}

NAME_SPACE_END
