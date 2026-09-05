#include "pch.h"
#include "Asset/PropSpriteLoader.h"
#include "Asset/PixelMapText.h"

NAME_SPACE_BEGIN(Craft)

namespace
{
	// "5.5,15" 를 피벗 좌표로 나눈다. 형식이 틀리면 false.
	// SpriteAnimationLoader의 같은 이름 함수와 같은 규칙이다.
	bool ParsePivot(const std::string& text, OUT float& outX, OUT float& outY)
	{
		const size_t commaPos = text.find(',');

		if (commaPos == std::string::npos)
		{
			return false;
		}

		const std::string xText = text.substr(0, commaPos);
		const std::string yText = text.substr(commaPos + 1);

		if (xText.empty() || yText.empty())
		{
			return false;
		}

		outX = static_cast<float>(::atof(xText.c_str()));
		outY = static_cast<float>(::atof(yText.c_str()));

		return true;
	}

	// Left <-> Right 폴백에서 뒤집을 축. XML의 mirror 속성에서 온다.
	enum class EMirrorMode
	{
		None,
		X,      // 가로만. 세로축이 "높이"인 그림(서 있는 울타리/기둥/묘비).
		XY,     // 둘 다. 세로축이 "벽 방향"인 그림(문처럼 개구부 전체를 담은 것).
	};

	EMirrorMode ParseMirrorMode(const std::string& text)
	{
		if (text == "none")
		{
			return EMirrorMode::None;
		}

		if (text == "xy")
		{
			return EMirrorMode::XY;
		}

		// 빈 문자열과 "x" 모두 기본값. 대부분의 프롭이 서 있는 그림이다.
		return EMirrorMode::X;
	}

	// 픽셀맵을 뒤집는다.
	//
	// Sprite가 "width칸 + '\n'"이 height번 반복되는 정규화 형태를 보장하므로
	// (Asset/Sprite.h의 저장 형식 불변식) 행 단위로 재조립하면 된다.
	Sprite MirrorSprite(const Sprite& source, bool flipX, bool flipY)
	{
		const int width = source.GetWidth();
		const int height = source.GetHeight();

		std::string flipped;
		flipped.reserve(source.GetPixelMap().size());

		for (int row = 0; row < height; ++row)
		{
			const int sourceRow = flipY ? (height - 1 - row) : row;

			for (int column = 0; column < width; ++column)
			{
				const int sourceColumn = flipX ? (width - 1 - column) : column;

				flipped.push_back(source.GetPixel(sourceRow, sourceColumn));
			}

			flipped.push_back('\n');
		}

		return Sprite(flipped);
	}

	// 폴백 상대. 반대편을 먼저 본다.
	EFacing GetFallbackPartner(EFacing facing)
	{
		switch (facing)
		{
		case EFacing::Up:    return EFacing::Down;
		case EFacing::Down:  return EFacing::Up;
		case EFacing::Left:  return EFacing::Right;
		default:             return EFacing::Left;
		}
	}
}

PropSpriteSet PropSpriteLoader::LoadFromFile(const WCHAR* path)
{
	PropSpriteSet props;

	// 파일이 없으면 여기서 끝낸다.
	// XmlParser는 없는 파일에서 예외를 던질 수 있고 엔진은 예외를 쓰지 않는다.
	const fs::path filePath{ path };

	if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
	{
		return props;
	}

	XmlParser parser;
	XmlNode root;

	if (!parser.ParseFromFile(path, root))
	{
		return props;
	}

	if (!root.IsValid())
	{
		return props;
	}

	// 이 파일의 아트가 그려진 타일 격자. 기본 피벗이 여기 걸린다.
	const int tileSize = root.GetInt32Attr(L"tileSize", DefaultTileSize);

	ASSERT_CRASH(tileSize > 0);

	for (XmlNode& propNode : root.FindChildren(L"Prop"))
	{
		const std::string name = FileUtils::Convert(propNode.GetStringAttr(L"name", L""));

		// 이름이 없으면 찾을 수 없으니 건너뛴다.
		if (name.empty())
		{
			continue;
		}

		// 같은 이름이 두 번 나오면 어느 쪽이 이기는지 데이터만 보고 알 수 없다.
		// LevelDataAsset이 중복 이름을 거부하는 것과 같은 이유다.
		if (props.find(name) != props.end())
		{
			continue;
		}

		PropSprite prop;
		prop.name = name;

		if (!LoadProp(propNode, tileSize, prop))
		{
			continue;
		}

		props.emplace(name, std::move(prop));
	}

	return props;
}

bool PropSpriteLoader::LoadProp(XmlNode& propNode, int tileSize, OUT PropSprite& outProp)
{
	// tileSpan은 타일 영역의 "벽 방향" 길이다(깊이는 언제나 1타일). 0 이하는 데이터 실수.
	const int tileSpan = propNode.GetInt32Attr(L"tileSpan", 1);

	ASSERT_CRASH(tileSpan > 0);

	outProp.tileSpan = tileSpan;
	outProp.tileSize = tileSize;

	bool hasSlot[FacingCount] = { false, false, false, false };

	// 슬롯별 반전 방식. 폴백할 때 "원본" 슬롯의 값을 쓴다.
	EMirrorMode mirrorModes[FacingCount] =
	{
		EMirrorMode::X, EMirrorMode::X, EMirrorMode::X, EMirrorMode::X
	};

	for (XmlNode& spriteNode : propNode.FindChildren(L"Sprite"))
	{
		const std::string dirText = FileUtils::Convert(spriteNode.GetStringAttr(L"dir", L""));

		bool parsedDir = false;
		const EFacing facing = ParseFacing(dirText, EFacing::Up, &parsedDir);

		// dir 오타를 Up으로 조용히 흘려보내면 엉뚱한 슬롯을 덮어쓴다.
		ASSERT_CRASH(parsedDir);

		const std::string pixelMap =
			NormalizePixelMapText(FileUtils::Convert(spriteNode.GetStringValue(L"")));

		// 빈 스프라이트는 건너뛴다.
		if (pixelMap.empty())
		{
			continue;
		}

		const int slot = ToFacingIndex(facing);

		// 같은 방향이 두 번 나오면 데이터 실수다.
		ASSERT_CRASH(!hasSlot[slot]);

		// Sprite 생성자가 줄 길이가 어긋난 픽셀맵을 잡아준다.
		outProp.sprites[slot] = Sprite(pixelMap);
		hasSlot[slot] = true;

		const Sprite& sprite = outProp.sprites[slot];

		// width/height는 선언이지 정의가 아니다. 진실의 원천은 여전히 픽셀맵이고
		// 여기서는 대조만 한다. 선언이 픽셀맵을 이기게 하면 조용히 그림이 왜곡된다.
		const int declaredWidth = spriteNode.GetInt32Attr(L"width", 0);
		const int declaredHeight = spriteNode.GetInt32Attr(L"height", 0);

		if (declaredWidth > 0)
		{
			ASSERT_CRASH(sprite.GetWidth() == declaredWidth);
		}

		if (declaredHeight > 0)
		{
			ASSERT_CRASH(sprite.GetHeight() == declaredHeight);
		}

		mirrorModes[slot] = ParseMirrorMode(
			FileUtils::Convert(spriteNode.GetStringAttr(L"mirror", L"")));

		// pivot="x,y". 생략하면 슬롯과 무관한 기본값.
		const std::string pivotText = FileUtils::Convert(spriteNode.GetStringAttr(L"pivot", L""));

		if (pivotText.empty())
		{
			outProp.pivotX[slot] = PropSprite::GetDefaultPivotX(sprite.GetWidth());
			outProp.pivotY[slot] = PropSprite::GetDefaultPivotY(sprite.GetHeight(), tileSize);
		}
		else
		{
			float pivotX = 0.0f;
			float pivotY = 0.0f;

			const bool hasParsedPivot = ParsePivot(pivotText, pivotX, pivotY);

			// 형식이 틀린 피벗을 0,0으로 넘겨버리면 프롭이 엉뚱한 곳에 붙는다.
			ASSERT_CRASH(hasParsedPivot);

			outProp.pivotX[slot] = pivotX;
			outProp.pivotY[slot] = pivotY;
		}
	}

	// 스프라이트가 하나도 없는 프롭은 등록하지 않는다. 폴백할 원본이 없다.
	bool hasAny = false;

	for (int index = 0; index < FacingCount; ++index)
	{
		hasAny = hasAny || hasSlot[index];
	}

	if (!hasAny)
	{
		return false;
	}

	ResolveFallbacks(outProp, hasSlot, mirrorModes);

	return true;
}

void PropSpriteLoader::ResolveFallbacks(
	OUT PropSprite& outProp,
	const bool (&hasSlot)[FacingCount],
	const void* mirrorModesRaw)
{
	// 채워진 슬롯 아무거나 하나. 마지막 폴백에 쓴다.
	int anyFilled = -1;

	for (int index = 0; index < FacingCount; ++index)
	{
		if (hasSlot[index])
		{
			anyFilled = index;
			break;
		}
	}

	// 호출부가 "하나라도 있다"를 이미 확인했다.
	ASSERT_CRASH(anyFilled >= 0);

	const EMirrorMode* mirrorModes = static_cast<const EMirrorMode*>(mirrorModesRaw);

	for (int index = 0; index < FacingCount; ++index)
	{
		if (hasSlot[index])
		{
			continue;
		}

		// 반대편을 먼저 본다. 정면 그림은 뒤에서 봐도 정면이고,
		// 측면 그림은 반대쪽에서 봐도 측면이다(원본이 좌우 2장을 따로 주지 않는 이상).
		const int partner = ToFacingIndex(GetFallbackPartner(static_cast<EFacing>(index)));
		const int source = hasSlot[partner] ? partner : anyFilled;

		// 좌우끼리 물려받을 때는 그림을 뒤집는다.
		//
		// 측면 아트가 한 장뿐인데 그대로 복사하면 서쪽 벽과 동쪽 벽이 같은 그림이 되어
		// 반대편에서 봐도 거울이 아니다. 실제 아트가 전부 좌우 비대칭이라 눈에 띈다.
		//
		// ★ 어느 축을 뒤집을지는 데이터가 정한다 (@mirror) ★
		// Left <-> Right는 거울이 아니라 수직축 180도 회전이라 월드 두 축이 모두 뒤집힌다.
		// 이미지에서 어느 축이 뒤집히는지는 그 그림의 세로축이 무엇을 뜻하느냐에 달렸다 -
		// 서 있는 울타리는 세로축이 높이라 가로만, 문처럼 개구부 전체를 담은 그림은
		// 세로축이 벽 방향이라 둘 다 뒤집어야 한다. 그림만 봐서는 알 수 없어서
		// 규칙으로 유도하지 않고 작가가 적는다.
		//
		// 앞뒤(Up <-> Down)는 뒤집지 않는다. 뒷면 아트가 없어 정면으로 때우는 것뿐이다.
		// 축이 다른 폴백(정면 -> 측면)도 마찬가지다.
		const bool sideToSide =
			IsSideFacing(static_cast<EFacing>(index)) && IsSideFacing(static_cast<EFacing>(source));

		const EMirrorMode mode = sideToSide ? mirrorModes[source] : EMirrorMode::None;

		const bool flipX = (mode == EMirrorMode::X || mode == EMirrorMode::XY);
		const bool flipY = (mode == EMirrorMode::XY);

		outProp.sprites[index] = (flipX || flipY)
			? MirrorSprite(outProp.sprites[source], flipX, flipY)
			: outProp.sprites[source];

		// 피벗은 원본 슬롯 값을 물려받되, 뒤집은 축은 같이 뒤집어야 그림이 안 어긋난다.
		const int width = outProp.sprites[index].GetWidth();
		const int height = outProp.sprites[index].GetHeight();

		outProp.pivotX[index] = flipX
			? (width - 1) - outProp.pivotX[source]
			: outProp.pivotX[source];

		outProp.pivotY[index] = flipY
			? (height - 1) - outProp.pivotY[source]
			: outProp.pivotY[source];
	}
}

NAME_SPACE_END
