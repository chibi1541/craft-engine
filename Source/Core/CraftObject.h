#pragma once
#include "Core/Core.h"
#include <memory>

NAME_SPACE_BEGIN(Craft)

class CRAFT_API CraftObject
{
public:
	~CraftObject() = default;

	// 현재 객체 타입의 ID를 반환하는 함수
	// 순수 가상함수로 만들기 -> 상속하는 계층에 구현을 강제
	// C++ 인터페이스 패턴
	virtual size_t GetType() const = 0;

	// 전달된 다른 타입 ID와 현재 객체(부모 포함)의 타입 비교.
	virtual bool Is(size_t id) const
	{
		// 여기까지 오면 타입이 맞지 않다는 것
		return false;
	}

	// 타입 질문
	// 이 함수를 사용하기 위해서 T 타입이 static TypeId 함수가 있어야 함
	template<typename T>
	bool IsTypeOf() const
	{
		return Is(T::TypeId());
	}
};

// 현변환 함수
// 스마트 포인터 간의 변환 처리
template<typename T, typename U>
std::shared_ptr<T> Cast(const std::shared_ptr<U>& object)
{
	// 예외 처리
	if (nullptr == object)
	{
		return nullptr;
	}

	// object의 실제 타입이 T(또는 T의 파생타입) 인지 확인 후 캐스팅
	if (object->Is(T::TypeId()))
	{
		// static_pointer_cast로 반환
		// 이거 검증은 컴파일 단계에서 하기 때문에
		// 런타임에는 된다고 가정하고 반환함
		return std::static_pointer_cast<T>(object);
	}

	// 타입 검사에 실패하면 null 반환
	return nullptr;
}

// 타입 시스템을 사용하는 클래스에 배치할 매크로
// 반복적인 코드 자동화를 할 때 많이 활용됨.
#define TYPE_DECLARATIONS(Type, ParentType)								\
	using super = ParentType;											\
protected:																\
/*전역 지역 변수의 주소를 활용해 유니크한 id를 반환하는 함수*/					\
static size_t TypeIdClass()												\
	{																	\
		static int runTimeTypeId = 0;									\
		return reinterpret_cast<size_t>(&runTimeTypeId);				\
	}																	\
public:																	\
	static size_t TypeId()												\
	{																	\
		return Type::TypeIdClass();										\
	}																	\
	virtual size_t GetType() const override								\
	{																	\
		return Type::TypeIdClass();										\
	}																	\
virtual bool Is(size_t id) const override								\
	{																	\
		/*현재 계층 비교하고 아니면 재귀적으로 부모 계층 비교*/				\
		return (id ==TypeIdClass()) ? true : ParentType::Is(id);		\
	}																	\

NAME_SPACE_END

