#include "pch.h"
#include "SpriteAnimationLoader.h"
#include "Math/SymbolPalette.h"

NAME_SPACE_BEGIN(Craft)

namespace
{
	// "3.5,7" 을 피벗 좌표로 나눈다. 형식이 틀리면 false.
	// x가 실수인 이유는 짝수 너비의 가운데가 정수로 떨어지지 않기 때문이다(8칸이면 3.5).
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
}

// TODO : AssetManager 쪽으로 기능 이전
std::vector<std::shared_ptr<const AnimationClip>> SpriteAnimationLoader::LoadFromFile(const WCHAR* path)
{
	std::vector<std::shared_ptr<const AnimationClip>> clips;

	// 예외 처리 - 파일이 없으면 여기서 끝낸다.
	// FileUtils::ReadFile은 fs::file_size로 시작해서 없는 파일이면 예외를 던진다.
	const fs::path filePath{ path };

	if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
	{
		return clips;
	}

	XmlParser parser;
	XmlNode root;

	if (!parser.ParseFromFile(path, root))
	{
		return clips;
	}

	if (!root.IsValid())
	{
		return clips;
	}

	for (XmlNode& clipNode : root.FindChildren(L"Clip"))
	{
		const std::string name = FileUtils::Convert(clipNode.GetStringAttr(L"name", L""));

		// 이름이 없으면 PlayClip으로 찾을 수 없으니 건너뛴다.
		if (name.empty())
		{
			continue;
		}

		const float framesPerSecond = clipNode.GetFloatAttr(L"fps", 12.0f);
		const bool isLooping = clipNode.GetBoolAttr(L"loop", true);

		std::vector<Sprite> frames;

		for (XmlNode& frameNode : clipNode.FindChildren(L"Frame"))
		{
			const std::string pixelMap =
				NormalizePixelMap(FileUtils::Convert(frameNode.GetStringValue(L"")));

			// 빈 프레임은 건너뛴다.
			if (pixelMap.empty())
			{
				continue;
			}

			// Sprite 생성자가 줄 길이가 어긋난 픽셀맵을 잡아준다.
			frames.emplace_back(Sprite(pixelMap));
		}

		// 프레임이 하나도 없는 클립은 만들지 않는다.
		if (frames.empty())
		{
			continue;
		}

		// fps가 0 이하면 AnimationClip 생성자에서 크래시하므로 여기서 미리 거른다.
		ASSERT_CRASH(framesPerSecond > 0.0f);

		// width/height는 선언이지 정의가 아니다. 진실의 원천은 여전히 픽셀맵이고,
		// 여기서는 대조만 한다. 선언이 픽셀맵을 이기게 하면 조용히 그림이 왜곡된다.
		//
		// 이 검사가 잡아주는 것 - Sprite 생성자는 "줄 길이가 서로 다른" 경우만 본다.
		// 8x8이어야 할 클립에서 줄 하나가 통째로 빠져 8x7이 된 건 못 잡는다.
		const int declaredWidth = clipNode.GetInt32Attr(L"width", 0);
		const int declaredHeight = clipNode.GetInt32Attr(L"height", 0);

		if (declaredWidth > 0)
		{
			ASSERT_CRASH(frames[0].GetWidth() == declaredWidth);
		}

		if (declaredHeight > 0)
		{
			ASSERT_CRASH(frames[0].GetHeight() == declaredHeight);
		}

		// pivot="x,y". 생략하면 프레임 크기에서 가운데 맨 아래(발밑)로 정해진다.
		const std::string pivotText = FileUtils::Convert(clipNode.GetStringAttr(L"pivot", L""));

		if (pivotText.empty())
		{
			clips.emplace_back(std::make_shared<const AnimationClip>(name, frames, framesPerSecond, isLooping));

			continue;
		}

		float pivotX = 0.0f;
		float pivotY = 0.0f;

		const bool hasParsedPivot = ParsePivot(pivotText, pivotX, pivotY);

		// 형식이 틀린 피벗을 0,0으로 넘겨버리면 캐릭터가 엉뚱한 곳에 붙는다.
		ASSERT_CRASH(hasParsedPivot);

		clips.emplace_back(std::make_shared<const AnimationClip>(
			name, frames, framesPerSecond, isLooping, pivotX, pivotY));
	}

	return clips;
}

std::string SpriteAnimationLoader::NormalizePixelMap(const std::string& rawText)
{
	std::string result;
	result.reserve(rawText.size());

	size_t lineStart = 0;

	while (lineStart <= rawText.size())
	{
		const size_t newlinePos = rawText.find('\n', lineStart);
		const size_t lineEnd = (newlinePos == std::string::npos) ? rawText.size() : newlinePos;

		// 줄 앞뒤 공백/탭/'\r'을 버린다.
		// 픽셀맵은 투명을 '.'으로 쓰기 때문에 공백을 지워도 그림이 망가지지 않고,
		// 덕분에 XML을 자유롭게 들여쓸 수 있다.
		size_t begin = lineStart;
		size_t end = lineEnd;

		while (begin < end && (rawText[begin] == ' ' || rawText[begin] == '\t' || rawText[begin] == '\r'))
		{
			++begin;
		}

		while (end > begin && (rawText[end - 1] == ' ' || rawText[end - 1] == '\t' || rawText[end - 1] == '\r'))
		{
			--end;
		}

		// 빈 줄은 버린다(여는 태그 다음 줄, 닫는 태그 앞 줄 등).
		if (end > begin)
		{
			for (size_t index = begin; index < end; ++index)
			{
				const char symbol = rawText[index];

				// 팔레트에 없는 기호는 픽셀맵 오타다.
				// Renderer까지 흘려보내지 말고 데이터를 읽는 여기서 잡는다.
				ASSERT_CRASH(symbol == SymbolPalette::TransparentSymbol || SymbolPalette::Contains(symbol));
			}

			result.append(rawText, begin, end - begin);
			result.push_back('\n');
		}

		if (newlinePos == std::string::npos)
		{
			break;
		}

		lineStart = newlinePos + 1;
	}

	return result;
}

NAME_SPACE_END
