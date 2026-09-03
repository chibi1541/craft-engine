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

		if (!LoadProp(propNode, prop))
		{
			continue;
		}

		props.emplace(name, std::move(prop));
	}

	return props;
}

bool PropSpriteLoader::LoadProp(XmlNode& propNode, OUT PropSprite& outProp)
{
	// tileSpan은 정사각 타일 영역의 한 변이다. 0 이하는 데이터 실수.
	const int tileSpan = propNode.GetInt32Attr(L"tileSpan", 1);

	ASSERT_CRASH(tileSpan > 0);

	outProp.tileSpan = tileSpan;

	bool hasSlot[FacingCount] = { false, false, false, false };

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

		// pivot="x,y". 생략하면 슬롯별 기본값(정면은 발밑, 측면은 이미지 중앙).
		const std::string pivotText = FileUtils::Convert(spriteNode.GetStringAttr(L"pivot", L""));

		if (pivotText.empty())
		{
			outProp.pivotX[slot] = PropSprite::GetDefaultPivotX(sprite.GetWidth());
			outProp.pivotY[slot] = PropSprite::GetDefaultPivotY(sprite.GetHeight(), facing);
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

	ResolveFallbacks(outProp, hasSlot);

	return true;
}

void PropSpriteLoader::ResolveFallbacks(OUT PropSprite& outProp, const bool (&hasSlot)[FacingCount])
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

		outProp.sprites[index] = outProp.sprites[source];

		// 피벗은 원본 슬롯의 값을 그대로 쓰지 않는다.
		//
		// 정면 슬롯에서 측면 슬롯으로 복사하면(FRONT만 있는 프롭) 피벗 규칙이
		// 슬롯마다 다르기 때문에 그림이 타일 영역 밖으로 내려간다.
		// 그림은 물려받되 피벗은 "이 슬롯"의 규칙으로 다시 계산한다.
		//
		// 단, 원본 슬롯이 XML에서 피벗을 직접 지정했다면 그건 작가의 의도이므로
		// 기본값으로 되돌리면 안 된다. 그래서 원본이 기본값을 쓴 경우에만 다시 계산한다.
		const Sprite& sprite = outProp.sprites[index];
		const EFacing sourceFacing = static_cast<EFacing>(source);
		const EFacing targetFacing = static_cast<EFacing>(index);

		const bool sourceUsedDefault =
			outProp.pivotX[source] == PropSprite::GetDefaultPivotX(sprite.GetWidth())
			&& outProp.pivotY[source] == PropSprite::GetDefaultPivotY(sprite.GetHeight(), sourceFacing);

		if (sourceUsedDefault)
		{
			outProp.pivotX[index] = PropSprite::GetDefaultPivotX(sprite.GetWidth());
			outProp.pivotY[index] = PropSprite::GetDefaultPivotY(sprite.GetHeight(), targetFacing);
		}
		else
		{
			outProp.pivotX[index] = outProp.pivotX[source];
			outProp.pivotY[index] = outProp.pivotY[source];
		}
	}
}

NAME_SPACE_END
