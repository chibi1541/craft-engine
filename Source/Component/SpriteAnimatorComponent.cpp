#include "pch.h"
#include "SpriteAnimatorComponent.h"
#include "Asset/SpriteAnimationLoader.h"
#include "Asset/AnimStateMachineLoader.h"
#include "Actor/Actor.h"
#include "Render/Renderer.h"

NAME_SPACE_BEGIN(Craft)

void SpriteAnimatorComponent::Tick(float deltaTime)
{
	super::Tick(deltaTime);

	// 시간을 밀어주는 건 여기서만 한다.
	// Draw 체인에는 deltaTime이 없기 때문에 Draw에서는 결과만 읽어간다.
	animInstance.Tick(deltaTime);
}

void SpriteAnimatorComponent::Draw()
{
	// 얼리 아웃 - 이번 프레임에 그릴 게 없는 경우.
	const std::string& pixelMap = animInstance.GetCurrentPixelMap();

	if (pixelMap.empty())
	{
		return;
	}

	// ownership - 액터가 이미 사라졌으면 그릴 위치를 알 수 없다.
	std::shared_ptr<Actor> ownerActor = GetOwner();

	if (nullptr == ownerActor)
	{
		return;
	}

	// 액터 위치에 놓이는 건 스프라이트의 좌상단이 아니라 피벗(기본값은 발밑)이다.
	//
	// 피벗은 스프라이트 셀 단위라 화면 칸으로 환산해야 한다.
	// SubmitPixels가 픽셀 하나를 scaleX칸씩 늘려 그리므로 배율을 곱해야
	// 확대해도 발밑이 액터 위치에 붙어 있다.
	const Vector2 pivotCell = animInstance.GetCurrentPivotCell();
	const Vector2 pivotOffset(pivotCell.x * scaleX, pivotCell.y * scaleY);

	Renderer::Get().SubmitPixels(
		pixelMap,
		SymbolPalette::GetTable(),
		ownerActor->GetPosition() + offset - pivotOffset,
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
		animInstance.AddClip(clip);
	}

	return static_cast<int>(loadedClips.size());
}

int SpriteAnimatorComponent::LoadStateMachineFromFile(const WCHAR* path)
{
	return AnimStateMachineLoader::LoadIntoInstance(path, animInstance);
}

void SpriteAnimatorComponent::AddClip(const std::shared_ptr<const AnimationClip>& clip)
{
	animInstance.AddClip(clip);
}

bool SpriteAnimatorComponent::PlayClip(const std::string& name, bool forceRestart)
{
	std::shared_ptr<const AnimationClip> clip = animInstance.FindClip(name);

	// 등록되지 않은 이름. 크래시 대신 false를 돌려준다.
	if (nullptr == clip)
	{
		return false;
	}

	// BaseLayer는 항상 존재한다. 상태 머신을 안 쓰는 액터도 이 경로로 클립 하나만 직접 튼다.
	// 같은 클립이면 Play()가 알아서 무시한다.
	animInstance.GetBaseLayer().player.Play(clip, forceRestart);

	return true;
}

NAME_SPACE_END
