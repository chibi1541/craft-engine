#include "pch.h"
#include "TLS.h"
#include "ThreadManager.h"

NAME_SPACE_BEGIN(Craft)

ThreadManager::ThreadManager()
{
	// 메인 쓰레드의 TLS 초기화
	InitTLS();
}

ThreadManager::~ThreadManager()
{
	Join();
	DestroyTLS();
}

void ThreadManager::Launch(function<void()> work)
{
	std::lock_guard<std::mutex> guard(_lock);

	_threads.emplace_back(
		// 쓰레드 생성(쓰레드가 실행할 로직)
		std::thread([=]()
			{
				InitTLS();
				work();
				DestroyTLS();
			})
	);
}

void ThreadManager::Join()
{
	for (std::thread& t : _threads)
	{
		if (t.joinable())
		{
			t.join();
		}
	}

	_threads.clear();
}

void ThreadManager::InitTLS()
{
	static atomic<uint32> SThreadId = 1;
	LThreadId = SThreadId.fetch_add(1);

	LBufferChunk.Reset();
}

void ThreadManager::DestroyTLS()
{
}

NAME_SPACE_END