#include "pch.h"
#include "SpriteAnimationLoader.h"
#include "Math/SymbolPalette.h"

NAME_SPACE_BEGIN(Craft)

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

		clips.emplace_back(std::make_shared<const AnimationClip>(name, frames, framesPerSecond, isLooping));
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
