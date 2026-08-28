#include "pch.h"
#include "MemoryPool.h"

NAME_SPACE_BEGIN(Craft)

MemoryPool::MemoryPool(int32 allocSize)
	: _allocSize(allocSize)
{
	::InitializeSListHead(&_header);
}

MemoryPool::~MemoryPool()
{
	// 메모리 풀 해제
	while (MemoryHeader* memory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&_header)))
	{
		::_aligned_free(memory);
	}
}

void MemoryPool::Push(MemoryHeader* ptr)
{
	// 메모리 사이즈 초기화
	ptr->allocSize = 0;

	// 풀에 반환
	::InterlockedPushEntrySList(&_header, static_cast<PSLIST_ENTRY>(ptr));

	// 원자적으로 카운트 낮춤
	_useCount.fetch_sub(1);
	// 여유 공간이니까 카운트를 높임
	_reserveCount.fetch_add(1);
}

MemoryHeader* MemoryPool::Pop()
{
	// 헤드에서 사용 가능한 영역을 Pop
	MemoryHeader* memory = static_cast<MemoryHeader*>(::InterlockedPopEntrySList(&_header));

	// 이 경우는 메모리가 없는 경우 -> 새로 할당해서 풀을 늘려야 함
	if (memory == nullptr)
	{
		// 정렬된 메모리가 중요하므로 _aligned_malloc로 할당을 요청
		memory = reinterpret_cast<MemoryHeader*>(::_aligned_malloc(_allocSize, SLIST_ALIGNMENT));
	}
	else
	{
		ASSERT_CRASH(memory->allocSize == 0);
		_reserveCount.fetch_sub(1);
	}

	_useCount.fetch_add(1);

	return memory;
}

NAME_SPACE_END
