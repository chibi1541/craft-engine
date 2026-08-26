#include "pch.h"
#include "SpriteAnimatorComponent.h"
#include "Asset/SpriteAnimationLoader.h"
#include "Actor/Actor.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

void SpriteAnimatorComponent::Tick(float deltaTime)
{
	super::Tick(deltaTime);

	// 시간을 밀어주는 건 여기서만 한다.
	// Draw 체인에는 deltaTime이 없기 때문에 Draw에서는 결과만 읽어간다.
	player.Tick(deltaTime);
}

void SpriteAnimatorComponent::Draw()
{
	// 얼리 아웃 - 지금 그릴 프레임이 없는 경우.
	const Sprite* currentSprite = player.GetCurrentSprite();

	if (nullptr == currentSprite || currentSprite->IsEmpty())
	{
		return;
	}

	// ownership - 액터가 이미 사라졌으면 그릴 위치를 알 수 없다.
	std::shared_ptr<Actor> ownerActor = GetOwner();

	if (nullptr == ownerActor)
	{
		return;
	}

	Renderer::Get().SubmitPixels(
		currentSprite->GetPixelMap(),
		SymbolPalette::GetTable(),
		ownerActor->GetPosition() + offset,
		ownerActor->GetSortingOrder(),
		SymbolPalette::TransparentSymbol,
		scaleX,
		scaleY
	);
}

int SpriteAnimatorComponent::LoadClipsFromFile(const WCHAR* path)
{
	const std::vector<std::shared_ptr<const AnimationClip>> loadedClips =
		SpriteAnimationLoader::LoadFromFile(path);

	for (const std::shared_ptr<const AnimationClip>& clip : loadedClips)
	{
		AddClip(clip);
	}

	return static_cast<int>(loadedClips.size());
}

void SpriteAnimatorComponent::AddClip(const std::shared_ptr<const AnimationClip>& clip)
{
	// 빈 클립이나 이름 없는 클립은 이름으로 찾을 수 없으므로 데이터 실수로 본다.
	ASSERT_CRASH(nullptr != clip);
	ASSERT_CRASH(!clip->GetName().empty());

	clipMap[clip->GetName()] = clip;
}

bool SpriteAnimatorComponent::PlayClip(const std::string& name, bool forceRestart)
{
	auto it = clipMap.find(name);

	// 등록되지 않은 이름. 크래시 대신 false를 돌려준다.
	// 상태 머신이 매 틱 호출하게 될 자리라서, 여기서 게임을 죽이면 곤란하다.
	if (it == clipMap.end())
	{
		return false;
	}

	// 같은 클립이면 Play()가 알아서 무시한다.
	player.Play(it->second, forceRestart);

	return true;
}

NAME_SPACE_END
