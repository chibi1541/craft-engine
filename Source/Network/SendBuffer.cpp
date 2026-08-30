#include "pch.h"
#include "SendBuffer.h"
#include <algorithm>

NAME_SPACE_BEGIN(Craft)

SendBuffer::SendBuffer()
{
}

SendBuffer::~SendBuffer()
{
}

void SendBuffer::ResetQueue()
{	
	// 내부에서만 호출
	// 호출하기 전에는 lock을 걸어야 함

	int32 remainSize = RemainSize();

	if (remainSize == 0)
	{
		_reservedPos = _consumePos = 0;
	}
	else
	{
		// 남아 있는 쓰기 버퍼가 일정 사이즈보다 적은 경우
		if (FreeSize() < BUFFER_RESET_SIZE)
		{
			// 버퍼에 남아있는 데이터를 버퍼 맨 앞으로 복사하고 인덱스 초기화
			::memcpy(_queue.data(), &_queue[_consumePos], remainSize);
			_consumePos = 0;
			_reservedPos = remainSize;
		}
	}
}

void SendBuffer::ResetBuffer()
{
	int32 remainSize = _writePos - _readPos;

	if (remainSize == 0)
	{
		_writePos = _readPos = 0;
	}
	else
	{
		// 남아 있는 쓰기 버퍼가 일정 사이즈보다 적은 경우
		if ((SENDBUFFER_SIZE - _writePos) < BUFFER_RESET_SIZE)
		{
			// 버퍼에 남아있는 데이터를 버퍼 맨 앞으로 복사하고 인덱스 초기화
			::memcpy(_buffer.data(), &_buffer[_readPos], remainSize);
			_readPos = 0;
			_writePos = remainSize;
		}
	}
}

void SendBuffer::PushSendQueue(void* data, int32 size)
{
	// 이 경우는 버퍼에 대한 사이즈 재조정 혹은 패킷 사이즈에 대한 재검토가 필요
	ASSERT_CRASH(SENDBUFFER_SIZE >= size);

	{
		WRITE_LOCK;

		if(FreeSize() < size)
			ResetQueue();

		::memcpy(&_queue[_reservedPos], data, static_cast<size_t>(size));
		_reservedPos += size;
	}

}

void SendBuffer::ProcessBuffer(int32 size)
{
	ASSERT_CRASH(BufferRemainSize() >= size);

	_readPos += size;
	ResetBuffer();
}

int32 SendBuffer::ConsumeBuffer()
{
	WRITE_LOCK;

	int32 remainQSize = RemainSize();

	if(remainQSize != 0)
	{
		if (BufferCapacity() < remainQSize)
			ResetBuffer();

		int32 procSize = std::min(BufferCapacity(), remainQSize);
		::memcpy(&_buffer[_writePos], &_queue[_consumePos], static_cast<size_t>(procSize));
		_writePos += procSize;
		_consumePos += procSize;

		ResetQueue();
	}

	return BufferRemainSize();
}

BufferChunk::BufferChunk()
{
}

BufferChunk::~BufferChunk()
{
}

void BufferChunk::Reset()
{
	_usedSize = 0;
	_open = false;
}

BYTE* BufferChunk::Open(uint32 size)
{
	// 사이즈 상한 체크
	ASSERT_CRASH(SENDBUFFER_CHUNK_SIZE >= size);

	// 오픈 체크
	ASSERT_CRASH(_open == false);

	// 프리 사이즈 체크
	if (FreeSize() < size)
	{
		Reset();
	}

	_open = true;

	return &_buffer[_usedSize];
}

void BufferChunk::Close(uint32 size)
{
	ASSERT_CRASH(_open);
	_open = false;
	_usedSize += size;
}

NAME_SPACE_END


