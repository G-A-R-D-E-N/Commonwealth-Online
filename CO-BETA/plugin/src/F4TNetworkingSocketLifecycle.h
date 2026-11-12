#pragma once

#include <WinSock2.h>

#include <atomic>
#include <mutex>

namespace F4T::Networking::Detail
{
	extern SOCKET g_socket;
	extern SOCKET g_socketPendingClose;
	extern std::mutex g_socketMutex;
	extern std::mutex g_sendMutex;
	extern std::atomic_bool g_receiveLoopActive;
	extern thread_local bool g_isReceiveThread;

	struct DetachedSocket
	{
		SOCKET toShutdown = INVALID_SOCKET;
		SOCKET toClose = INVALID_SOCKET;
	};

	DetachedSocket DetachSocketLocked();
	void FinishDetachedSocket(const DetachedSocket& a_detached);
	void CloseSocket();
	void ClosePendingSocket();
	SOCKET GetSocket();
}
