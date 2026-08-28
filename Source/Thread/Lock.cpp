#include "pch.h"
#include "Lock.h"
#include "TLS.h"
#include <thread>

NAME_SPACE_BEGIN(Craft)

void Lock::WriteLock(const char* name)
{
	// 이미 해당 쓰레드가 Write Lock을 점유한 경우
	// 예전에 여기에 16비트 밀어주는 처리를 빼먹어서 재귀적으로 Write Lock을 점유하는 로직이 문제가 발생했음
	const uint32 lockThreadId = (_lockFlag.load() & WRITE_THREAD_MASK) >> 16;
	if (lockThreadId == LThreadId)
	{
		// WriteLock 카운트를 증가해서 재귀적으로 Lock을 점유하도록 허용
		_writeCount++;
		return;
	}

	// Lock 점유를 위해 무한 SpinLock이 발생하지 않도록 시간을 체크해서 TimeOut을 발생 시킴
	// TODO : 특정 상황에 lock 점유가 몰리지 않는지를 체크하기 위해 락 획득까지 걸리는 Tick을 기록해 두는 기능을 만들어도 좋을 듯
	const uint64 beginTick = ::GetTickCount64();
	// 아무도 Lock(Write + Read)을 점유하고 있지 않을 때 경합을 진행(Compare And Swap 방식)
	const uint32 desired = ((LThreadId << 16) & WRITE_THREAD_MASK);
	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; ++spinCount)
		{
			// expected를 매번 빈 플레그 값으로 초기화 해주는 것이 필수!!!!
			// compare_exchange_strong 계산 후에 expected 값은 _lockFlag 값으로 바뀌어 버림
			// 예전에 이 처리를 해주지 않아서 접근한 모든 쓰레드가 Lock을 획득하는 세기말 상황이 발생함
			uint32 expected = EMPTY_FLAG;
			if (_lockFlag.compare_exchange_strong(OUT expected, desired))
			{
				// 여기에 들어왔다는 것은 -> 비교 당시 _lockFlag 값이 expected와 같음 + _lockFlag은 현재 desired와 같은 값(쓰레드 Id를 기록)이 됨을 의미
				// write lock을 점유했으므로 count을 증가시킴
				_writeCount++;
				return;
			}
		}

		// lock을 시도하기 위해 경과한 시간이 AQUIRE_TIMEOUT_TICK를 초과
		if (::GetTickCount64() - beginTick >= AQUIRE_TIMEOUT_TICK)
			CRASH("LOCK_TIMEOUT");

		// MAX_SPIN_COUNT 만큼 lock 획득 시도했는데 아직 TimeOut 시간이 안지났다면
		// 쓰레드를 잠깐 대기 상태로 쉬게 함
		this_thread::yield();
	}
}

void Lock::WriteUnlock(const char* name)
{
	// Write Lock을 해제하려고 하는데 Read Lock을 잡고 있는 상태라면 해제 할 수 없도록 함
	// write lock을 획득한 상태에서 read lock을 잡았다면
	// read Lock을 해제하고 write Lock을 해제해야 함
	if ((_lockFlag.load() & READ_COUNT_MASK) != 0)
		CRASH("INVALIDE_UNLOCK_ORDER");

	// 재귀적으로 획득한 lock이 모두 해제되는 상황에 lock을 반환
	const int32 lockCount = --_writeCount;
	if (lockCount == 0)
		_lockFlag.store(EMPTY_FLAG);
}

void Lock::ReadLock(const char* name)
{

	// 이미 write lock을 점유한 상태라면 (다른 쓰레드의 접근이 불가능하니)
	const uint32 lockThreadId = (_lockFlag.load() & WRITE_THREAD_MASK) >> 16;
	if (lockThreadId == LThreadId)
	{
		// 묻지도 따지지도 않고 Read Lock 카운트를 하나 올려줌
		_lockFlag.fetch_add(1);
		return;
	}

	// write lock을 경합하여 점유
	const uint64 beginTick = ::GetTickCount64();
	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; ++spinCount)
		{
			// read lock 점유가 가능한 상태 : write lock을 점유한 쓰레드가 없고, read lock을 점유한 쓰레드만 있는 상태
			// 즉, write flag 값은 0이고 read flag만 있는 상태
			// read count만 있도록 마스크 체킹함
			uint32 expected = (_lockFlag.load() & READ_COUNT_MASK);
			if (_lockFlag.compare_exchange_strong(OUT expected, expected + 1))
			{
				// 여기 들어오면서 카운트를 늘렸기 때문에 별도의 처리를 하지 않음
				return;
			}
		}

		// lock을 시도하기 위해 경과한 시간이 AQUIRE_TIMEOUT_TICK를 초과
		if (::GetTickCount64() - beginTick >= AQUIRE_TIMEOUT_TICK)
			CRASH("LOCK_TIMEOUT");

		// MAX_SPIN_COUNT 만큼 lock 획득 시도했는데 아직 TimeOut 시간이 안지났다면
		// 쓰레드를 잠깐 대기 상태로 쉬게 함
		this_thread::yield();
	}

}

void Lock::ReadUnlock(const char* name)
{
	// read lock의 경우 lock을 해제하는데 별도의 체크 조건이 없음
	// 카운트가 어긋낫을 때만 크래쉬
	// fetch_sub는 직전 값을 반환하므로 0을 반환하면 현재 카운트는 -1이라는 것
	if (_lockFlag.fetch_sub(1) == 0)
		CRASH("MULTIPLE_UNLOCK");
}


NAME_SPACE_END