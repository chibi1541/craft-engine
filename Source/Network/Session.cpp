#include "pch.h"
#include "Session.h"
#include "SocketUtils.h"
#include "RecvBuffer.h"
#include "SendBuffer.h"


NAME_SPACE_BEGIN(Craft)


Session::Session(NetAddress address) : _netAddress(address)
{
	_socket = SocketUtils::CreateSocket();
	_recvBuffer = std::make_unique<RecvBuffer>(RECV_BUFFER_SIZE);
	_sendBuffer = std::make_unique<SendBuffer>();

}

Session::~Session()
{
	SocketUtils::Close(_socket);
}

bool Session::Connect()
{
	if (IsConnected())
		return false;

	if (SocketUtils::SetReuseAddress(_socket, true) == false)
		return false;

	if (SocketUtils::BindAnyAddress(_socket, /*비어있는 아무 포트나 할당*/0) == false)
		return false;

	SOCKADDR_IN sockAddr = _netAddress.GetSockAddr();
	DWORD numOfBytes = 0;

	if (SOCKET_ERROR == ::connect(_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr)))
	{
		int32 errorCode = ::WSAGetLastError();
		if (errorCode == WSAEWOULDBLOCK)
		{
			// TODO : Debug Output
			return false;
		}
	}

	_connected.store(true);
	OnConnected();
	return true;
}

void Session::Disconnect(const WCHAR* cause)
{
	if (_connected.exchange(false) == false)
	{
		// 이미 false 상태인 경우
		return;
	}

	// TODO : Debug Output

	if(SOCKET_ERROR == ::shutdown(_socket, SD_SEND))
	{
		OnDisconnected();
		return;
	}

	// 종료처리는 Recv에서 0을 받으면서 완료
}

int32 Session::ReadyForSend()
{
	return _sendBuffer->ConsumeBuffer();
}

void Session::RegisterSend(void* buffer, int32 size)
{
	_sendBuffer->PushSendQueue(buffer,size);
}

void Session::HandleError(int32 errorCode)
{
	// TODO : 소켓 에러 처리
	switch (errorCode)
	{
	case WSAECONNRESET:
	case WSAECONNABORTED:
		Disconnect(L"HandleError");
		break;
	default:
		//LOG_WARN(L"Session HandleError : %d", errorCode);
		break;
	}

}

void Session::ProcessRecv(int32 numOfBytes)
{
	_recvBuffer->OnRead(numOfBytes);

	OnRecv(_recvBuffer->ReadPos(), numOfBytes);
}

void Session::ProcessSend(int32 numOfBytes)
{
	_sendBuffer->ProcessBuffer(numOfBytes);

	OnSend(numOfBytes);
}

NAME_SPACE_END
