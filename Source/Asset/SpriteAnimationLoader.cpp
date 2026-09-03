#include "pch.h"
#include "SpriteAnimationLoader.h"
#include "Math/SymbolPalette.h"
#include "Asset/PixelMapText.h"

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

	// <Clip> 안의 <Notify>들을 읽어 클립에 등록한다.
	// <Frame>과 태그 이름이 달라서 FindChildren이 알아서 구분한다.
	void LoadNotifies(XmlNode& clipNode, AnimationClip& outClip)
	{
		for (XmlNode& notifyNode : clipNode.FindChildren(L"Notify"))
		{
			AnimNotify notify;
			notify.name = FileUtils::Convert(notifyNode.GetStringAttr(L"name", L""));

			const std::string frameText = FileUtils::Convert(notifyNode.GetStringAttr(L"frame", L""));

			// frame="end"는 프레임이 아니라 "논루프 클립이 끝난 순간"을 뜻한다.
			// 마지막 프레임 노티파이는 그 장에 "들어갈 때" 울려서 한 프레임 빠르다.
			if (frameText == "end")
			{
				notify.fireOnFinish = true;
			}
			else
			{
				// frame을 아예 안 적으면 0번으로 본다(atoi("")가 0).
				notify.frameIndex = ::atoi(frameText.c_str());
			}

			// 이름 없음 / 범위 밖 프레임 / 루프 클립에 end 같은 실수는 AddNotify가 잡는다.
			outClip.AddNotify(notify);
		}
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

		// 노티파이를 넣어야 하므로 일단 비-const로 만든다.
		// 아래에서 shared_ptr<const AnimationClip>로 넘기면(암시 변환) 밖에서는 변경할 수 없다.
		std::shared_ptr<AnimationClip> clip;

		if (pivotText.empty())
		{
			clip = std::make_shared<AnimationClip>(name, frames, framesPerSecond, isLooping);
		}
		else
		{
			float pivotX = 0.0f;
			float pivotY = 0.0f;

			const bool hasParsedPivot = ParsePivot(pivotText, pivotX, pivotY);

			// 형식이 틀린 피벗을 0,0으로 넘겨버리면 캐릭터가 엉뚱한 곳에 붙는다.
			ASSERT_CRASH(hasParsedPivot);

			clip = std::make_shared<AnimationClip>(
				name, frames, framesPerSecond, isLooping, pivotX, pivotY);
		}

		// 노티파이는 클립을 만든 뒤에 넣는다 - 프레임 범위 검증에 frameCount가 필요하다.
		LoadNotifies(clipNode, *clip);

		clips.emplace_back(clip);
	}

	return clips;
}

std::string SpriteAnimationLoader::NormalizePixelMap(const std::string& rawText)
{
	// 실제 처리는 공용 함수에 있다(Asset/PixelMapText.h).
	// 프롭 스프라이트 로더도 같은 규칙을 써야 해서 한 곳으로 옮겼다 -
	// 복사해두면 한쪽만 고쳐졌을 때 조용히 갈라진다.
	return NormalizePixelMapText(rawText);
}

NAME_SPACE_END
