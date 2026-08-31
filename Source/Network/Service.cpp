#include "pch.h"
#include "Service.h"
#include "Session.h"
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "SocketUtils.h"

NAME_SPACE_BEGIN(Craft)

Service::Service(NetAddress address, SessionFactory sessionFactory)
{
	_session = sessionFactory(address);
}

Service::~Service()
{
}

void Service::CloseService()
{
	// TODO : CloseService의 타이밍 정립(이게 어떤 타이밍인지 아직 못 정함)
	_session->Disconnect(L"CloseService");
}

void Service::HandleRecv()
{
	int32 recvLen = ::recv(_session->GetSocket(), reinterpret_cast<char*>(_session->_recvBuffer->WritePos()), _session->_recvBuffer->FreeSize(), 0);
	if (recvLen == 0)
	{
		// 연결 해제
		_session->OnDisconnected();
		return;
	}

	if(recvLen == SOCKET_ERROR)
	{
		int32 errorCode = ::WSAGetLastError();

		_session->HandleError(errorCode);

		return;
	}

	_session->ProcessRecv(recvLen);
}

void Service::HandleSend()
{
	// ::send()의 반환 값은 보낸 데이터의 크기
	// 블로킹 모드 -> 모든 데이터 다 보냄
	// 논블로킹 모드 -> 일부만 보낼 수가 있음 (상대방 수신 버퍼 상황에 따라)
	// 일부만 보냈을 경우는 sendBytes가 0보다 클 것이고 
	// 루프를 돌아 다시 들어왔을 때 남은 부분을 보내는 처리를 진행 함
	int32 sendLen = ::send(_session->GetSocket(), reinterpret_cast<char*>(_session->_sendBuffer->Buffer()), _session->_sendBuffer->BufferRemainSize(), 0);
	if (sendLen == SOCKET_ERROR)
	{
		int32 errorCode = ::WSAGetLastError();

		_session->HandleError(errorCode);

		return;
	}

	_session->ProcessSend(sendLen);
}

NetAddress Service::GetAddress() const
{
	return _session->GetAddress();
}

ServerService::ServerService(NetAddress address, SessionFactory sessionFactory) 
	: Service(address, sessionFactory)
{
	
}

ServerService::~ServerService()
{
	
}

bool ServerService::Start()
{
	if (CanStart() == false)
		return false;

	if(_session->Connect() == false)
		return false;

	_isRunning = true;
	return true;
}

void ServerService::Run()
{
	// TODO : 루프 종료 조건 설정
	while(_isRunning)
	{
		// SocketSet 초기화
		FD_ZERO(&_readSet);
		FD_ZERO(&_writeSet);

		// 소켓 등록
		FD_SET(_session->GetSocket(), &_readSet);

		// 보낼 데이터가 있다면 send 이벤트도 받을 수 있도록 등록
		if(_session->ReadyForSend() > 0)
			FD_SET(_session->GetSocket(), &_writeSet);

		// select 함수 마지막에 들어가는 옵션
		// 이 옵션을 설정해 주면 select가 무한 대기를 하지 않고 설정된 시간 초 만큼만 대기
		timeval timeout;
		// 시간(초 단위)
		//timeout.tv_sec;
		// 시간(ms 단위)
		timeout.tv_usec = 10;

		int32 retVal = ::select(0, &_readSet, &_writeSet, nullptr, &timeout);
		if (retVal == SOCKET_ERROR)
		{
			int32 error = ::WSAGetLastError();
			// TODO : 에러 코드 처리

			_isRunning = false;
			break;

		}
		
		// 타임아웃, 준비된 소켓 없음
		if (retVal == 0)
			continue;


		if (FD_ISSET(_session->GetSocket(), &_readSet))
			HandleRecv();

		if (_isRunning && FD_ISSET(_session->GetSocket(), &_writeSet))
			HandleSend();

	}
}

void ServerService::CloseService()
{
	
}

NAME_SPACE_END

