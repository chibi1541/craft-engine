#pragma once

#include "Math/Vector2.h"
#include "Math/Color.h"
#include "Core/CraftObject.h"

NAME_SPACE_BEGIN(Craft)

class Level;

class CRAFT_API Actor : public CraftObject
{
	TYPE_DECLARATIONS(Actor, CraftObject)

public:
	Actor( const std::wstring& image = L"", const Vector2& position = Vector2::Zero, Color color = Color::White);
	virtual ~Actor();

	// 게임 플레이 이벤트 함수.
	virtual void BeginPlay();
	virtual void Tick(float deltaTime);
	virtual void Draw();

	// 액터 제거 함수.
	void Destroy();

	// 게임(엔진) 종료 함수.
	void QuitGame();

	// getter/setter
	inline bool HasBeganPlay() const { return hasBeganPlay; }
	inline bool IsActive() const { return isActive && !hadExpired; }
	inline bool HasExpired() const { return hadExpired; }
	// ownership(약참조하던 객체를 shared_ptr로 만들어서 반환)
	inline std::shared_ptr<Level> GetOwner() const { return owner.lock(); }
	inline void SetOwner(std::weak_ptr<Level> newOwner) { owner = newOwner; }

	inline Vector2 GetPosition() const { return position; }
 	virtual void SetPosition(const Vector2& newPosition);

protected:
	// BeginPlay
	bool hasBeganPlay = false;

	// 액터 활성화 여부 플래그
	bool isActive = true;

	// 삭제 요청 여부 플래그
	// 쓰레드 분리?
	bool hadExpired = false;

	// ownership
	// 순환 참조 문제를 없애기 위해서 weak_ptr로 참조
	// 그래서 사용할 때는 유효한지 확인해야 함
	std::weak_ptr<Level> owner;

	// 실제 화면에 그릴 글자
	std::wstring image;

	// 글자 색상
	Color color = Color::White;

	// 글자 길이
	int width = 0;

	int sortingOrder = 0;

	Vector2 position;
};

NAME_SPACE_END