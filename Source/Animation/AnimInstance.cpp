#include "pch.h"
#include "AnimInstance.h"
#include "Math/SymbolPalette.h"
#include <algorithm>
#include <cmath>

NAME_SPACE_BEGIN(Craft)

bool AnimLayerMask::Contains(int row) const
{
	if (row < startRow)
	{
		return false;
	}

	// endRow가 -1이면 끝까지 담당한다.
	if (endRow >= 0 && row > endRow)
	{
		return false;
	}

	return true;
}

void AnimInstance::AddClip(const std::shared_ptr<const AnimationClip>& clip)
{
	// 이름으로 찾을 수 없는 클립은 상태가 지목할 방법이 없으므로 데이터 실수로 본다.
	ASSERT_CRASH(nullptr != clip);
	ASSERT_CRASH(!clip->GetName().empty());

	clipMap[clip->GetName()] = clip;
}

std::shared_ptr<const AnimationClip> AnimInstance::FindClip(const std::string& name) const
{
	auto it = clipMap.find(name);

	if (it == clipMap.end())
	{
		return nullptr;
	}

	return it->second;
}

AnimLayer& AnimInstance::AddLayer(const std::string& name, const AnimLayerMask& mask, AnimBlendMode blendMode)
{
	std::unique_ptr<AnimLayer> newLayer = std::make_unique<AnimLayer>();

	newLayer->name = name;
	newLayer->mask = mask;
	newLayer->blendMode = blendMode;

	layers.emplace_back(std::move(newLayer));

	return *layers.back();
}

AnimLayer* AnimInstance::FindLayer(const std::string& name)
{
	for (const std::unique_ptr<AnimLayer>& layer : layers)
	{
		if (layer->name == name)
		{
			return layer.get();
		}
	}

	return nullptr;
}

AnimLayer* AnimInstance::GetLayer(int index)
{
	if (index < 0 || index >= static_cast<int>(layers.size()))
	{
		return nullptr;
	}

	return layers[index].get();
}

void AnimInstance::Tick(float deltaTime)
{
	for (const std::unique_ptr<AnimLayer>& layer : layers)
	{
		TickLayer(*layer, deltaTime);
	}

	Composite();
}

void AnimInstance::TickLayer(AnimLayer& layer, float deltaTime)
{
	// 1) 이 레이어의 지금 상황을 모아 조건 평가용 컨텍스트를 만든다.
	//    animTime/animFinished/stateTime은 레이어마다 다르므로 공용 블랙보드에 넣지 않는다.
	AnimEvalContext context;
	context.parameters = &parameters;
	context.animTime = layer.player.GetNormalizedTime();
	context.animFinished = layer.player.HasFinished();
	context.stateTime = layer.stateTime;

	// 2) 전이 평가.
	//    시간 전진(3)보다 먼저 하는 게 중요하다.
	//    그래야 논루프 클립이 끝난 프레임에 곧바로 다음 상태의 그림이 나오고,
	//    마지막 프레임이 한 틱 더 남아 보이지 않는다.
	if (layer.stateMachine.Evaluate(context))
	{
		layer.stateTime = 0.0f;
	}

	// 현재 상태의 클립을 적용한다.
	// 매 틱 무조건 호출해도 되는 이유 - AnimationPlayer::Play()는 같은 클립이면 무시한다.
	// 덕분에 "처음 진입할 때 한 번만 재생" 같은 특수 처리가 필요 없다.
	const AnimState* currentState = layer.stateMachine.GetCurrentState();

	if (nullptr != currentState)
	{
		layer.player.Play(FindClip(currentState->clipName));
	}

	// 3) 시간 전진.
	layer.player.Tick(deltaTime);
	layer.stateTime += deltaTime;
}

void AnimInstance::Composite()
{
	// TODO : 모든 Layer를 뒤덮을 수 있는 로직이 필요
	// 구르기 혹은 게임 오버 애니메이션처럼 블랜딩 없이 단일 애니메이션만 출력되야 하는 경우 로직 단순화를 위해

	compositeBuffer.clear();

	// 얼리 아웃 - 레이어가 없으면 그릴 것도 없다.
	if (layers.empty())
	{
		return;
	}

	compositeWidth = 0;
	compositeHeight = 0;

	// 결과 캔버스의 크기와 피벗을 정한다. 그릴 프레임이 있는 첫 레이어를 기준으로 삼는다.
	const Sprite* sizeSource = nullptr;
	const AnimationClip* pivotSource = nullptr;

	for (const std::unique_ptr<AnimLayer>& layer : layers)
	{
		const Sprite* sprite = layer->player.GetCurrentSprite();

		if (nullptr != sprite && !sprite->IsEmpty())
		{
			sizeSource = sprite;
			pivotSource = layer->player.GetClip().get();

			break;
		}
	}

	// 어느 레이어도 그릴 게 없으면 이번 프레임은 아무것도 그리지 않는다.
	if (nullptr == sizeSource)
	{
		return;
	}

	const int width = sizeSource->GetWidth();
	const int height = sizeSource->GetHeight();

	compositeWidth = width;
	compositeHeight = height;
	compositePivotX = (nullptr != pivotSource) ? pivotSource->GetPivotX() : AnimationClip::GetDefaultPivotX(width);
	compositePivotY = (nullptr != pivotSource) ? pivotSource->GetPivotY() : AnimationClip::GetDefaultPivotY(height);

	// 전부 투명인 빈 캔버스에서 시작한다.
	//
	// 예전에는 첫 레이어를 통째로 복사해서 바탕으로 깔았는데,
	// 그러면 그 레이어만 마스크를 거치지 않는 예외가 생긴다.
	// 평소에는 layer 0이 rows="all"이라 드러나지 않다가,
	// layer 0이 비는 순간 그 위 레이어가 바탕으로 승격되면서 마스크가 통째로 무시됐다.
	// (상체 전용 레이어가 전신으로 그려지는 버그)
	//
	// 빈 캔버스에서 시작하면 모든 레이어가 예외 없이 마스크를 통과한다.
	// 8x8 기준 64칸이라 통째로 복사하던 것과 비용 차이는 없다.
	compositeBuffer.assign(static_cast<size_t>(height) * (width + 1), SymbolPalette::TransparentSymbol);

	// Sprite와 같은 "width칸 + '\n'" 형태로 맞춘다.
	for (int row = 0; row < height; ++row)
	{
		compositeBuffer[static_cast<size_t>(row) * (width + 1) + width] = '\n';
	}

	for (const std::unique_ptr<AnimLayer>& layerPtr : layers)
	{
		const AnimLayer& layer = *layerPtr;

		const Sprite* sprite = layer.player.GetCurrentSprite();

		if (nullptr == sprite || sprite->IsEmpty())
		{
			continue;
		}

		// 같은 캐릭터의 레이어끼리는 크기가 같아야 합성이 성립한다.
		// 다르면 어느 칸을 어디에 겹칠지 정할 방법이 없으므로 데이터 실수로 본다.
		// TODO : 가로 가변 폭을 지원하면 이 가로 검사를 풀고
		//        캔버스 폭을 각 레이어의 [-pivotX, width-pivotX) 합집합으로 잡는다.
		ASSERT_CRASH(sprite->GetWidth() == width);
		ASSERT_CRASH(sprite->GetHeight() == height);

		// 크기가 같은데 피벗이 다르면 레이어를 어긋나게 겹쳐야 한다.
		// 그건 가변 폭 단계의 일이므로 지금은 데이터 실수로 본다.
		const AnimationClip* clip = layer.player.GetClip().get();

		if (nullptr != clip)
		{
			ASSERT_CRASH(clip->GetPivotX() == compositePivotX);
			ASSERT_CRASH(clip->GetPivotY() == compositePivotY);
		}

		for (int row = 0; row < height; ++row)
		{
			// 이 레이어가 담당하지 않는 행은 아래 레이어 그림을 그대로 둔다.
			if (!layer.mask.Contains(row))
			{
				continue;
			}

			for (int col = 0; col < width; ++col)
			{
				const char symbol = sprite->GetPixel(row, col);

				// Overlay는 불투명한 칸만 덮는다. 투명한 칸으로는 아래 레이어가 비친다.
				//
				// Replace는 투명한 칸까지 그대로 써서 마스크 행을 통째로 가져간다.
				// 이게 없으면 아래 레이어에서 픽셀을 빼는 동작(다리를 드는 등)이
				// 아래 레이어에 남아있는 픽셀에 가려져 화면에 나타나지 않는다.
				if (layer.blendMode == AnimBlendMode::Overlay
					&& symbol == SymbolPalette::TransparentSymbol)
				{
					continue;
				}

				compositeBuffer[sprite->GetPixelIndex(row, col)] = symbol;
			}
		}
	}

	// 좌우 반전은 합성이 다 끝난 뒤 한 번만 한다.
	// 행 단위로 뒤집는 것이라 마스크(행 기준)와는 서로 간섭하지 않는다.
	if (!flipX)
	{
		return;
	}

	for (int row = 0; row < height; ++row)
	{
		// 줄 끝의 '\n'은 건드리지 않고 [0, width)만 뒤집는다.
		std::string::iterator lineBegin = compositeBuffer.begin() + (static_cast<size_t>(row) * (width + 1));

		std::reverse(lineBegin, lineBegin + width);
	}
}

void AnimInstance::SetFlipX(bool newFlipX)
{
	if (flipX == newFlipX)
	{
		return;
	}

	flipX = newFlipX;

	// 이번 프레임 합성은 이미 끝났을 수 있다.
	// 다음 Tick까지 기다리지 않고 지금 결과를 뒤집어 둔다.
	if (compositeBuffer.empty() || compositeWidth <= 0)
	{
		return;
	}

	for (int row = 0; row < compositeHeight; ++row)
	{
		std::string::iterator lineBegin =
			compositeBuffer.begin() + (static_cast<size_t>(row) * (compositeWidth + 1));

		std::reverse(lineBegin, lineBegin + compositeWidth);
	}
}

Vector2 AnimInstance::GetCurrentPivotCell() const
{
	// 얼리 아웃 - 그릴 게 없으면 기준점도 없다.
	if (compositeBuffer.empty() || compositeWidth <= 0)
	{
		return Vector2::Zero;
	}

	// 행을 뒤집으면 열 c가 (W-1-c)로 가므로 피벗도 같이 옮겨간다.
	//
	// 피벗이 박스 중심 (W-1)/2와 같으면 뒤집은 값이 자기 자신이라 좌상단이 그대로다.
	// 짝수 너비에서 피벗을 정수로 반올림해 저장했다면 이 두 값이 1 차이가 나고,
	// 방향을 바꿀 때마다 그림이 한 칸씩 튄다. 피벗을 실수로 들고 있는 이유가 이것이다.
	const float pivotX = flipX ? ((compositeWidth - 1) - compositePivotX) : compositePivotX;

	// 반올림은 여기서 한 번만 한다. 정상/반전 양쪽에 같은 함수를 써야
	// 피벗이 박스 중심일 때 두 결과가 정확히 같아진다.
	return Vector2(
		static_cast<int>(::floorf(pivotX + 0.5f)),
		static_cast<int>(::floorf(compositePivotY + 0.5f))
	);
}

NAME_SPACE_END
