#include "pch.h"
#include "JobQueue.h"

NAME_SPACE_BEGIN(Craft)

void JobQueue::Push(shared_ptr<Job> job)
{
	// 기존 서버에 있는 JobQueue 와 대대적인 변경이 필요한 부분
	// 기존에 잡을 밀어넣은 쓰레드가 Job을 밀어넣을 때 JobCount가 0이면 해당 JobQueue를 점유하고 일감을 처리
	// 근데 이러면 일감을 밀어넣는 쓰레드가 소비까지 하는 일이 생김 -> 이거 조금 위험한데;;
	// 서버의 경우 쓰레드가 역할 분담이 없이 JobQueue를 상속하는 대상만 하나의 쓰레드가 점유해서 처리하는 것에 목적을 뒀지만
	// 클라는 Job을 밀어 넣는 쓰레드(패킷 쓰레드)와 소비하는 쓰레드(메인 게임 쓰레드)가 따로 나뉨
	// 그래서 Push는 딱 일감만 밀어 넣고 나가는 걸 생각해야 함

	// _jobs는 LockQueue
	_jobs.Push(job);

	// 서버 쪽에서 이후에 있는 실행 처리에 대한 고민은 하지 않는다(소비를 할 주체가 정해져있다)
}

void JobQueue::Execute()
{
	// job을 소비
	// 한번에 작업이 몰리는 상황이 발생하면 증분적으로 처리하는 로직을 추가
	while (true)
	{
		vector<shared_ptr<Job>> jobs;
		// 여기서 lock을 걸로 queue에 있는 모든 일감을 복사해 옴
		_jobs.PopAll(OUT jobs);

		const int32 jobCount = static_cast<int32>(jobs.size());
		for (int32 id = 0; id < jobCount; ++id)
		{
			jobs[id]->Execute();
		}

		// 처리한 만큼 JobCount를 줄임
		// 일단 혹시 일감이 늘어도 여기서 마무리 -> 다음 Tick에 소모
		_jobCount.fetch_sub(jobCount);
	}
}



NAME_SPACE_END
