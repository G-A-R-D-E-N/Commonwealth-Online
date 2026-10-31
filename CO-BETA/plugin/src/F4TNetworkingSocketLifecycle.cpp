#include "pch.h"

#include "F4TNetworkingSocketLifecycle.h"

namespace F4T::Networking::Detail
{
	SOCKET g_socket = INVALID_SOCKET;
	SOCKET g_socketPendingClose = INVALID_SOCKET;
	std::mutex g_socketMutex;
	std::mutex g_sendMutex;
	std::atomic_bool g_receiveLoopActive{ false };
	thread_local bool g_isReceiveThread = false;

	DetachedSocket DetachSocketLocked()
	{
		DetachedSocket detached;
		if (g_socket == INVALID_SOCKET) {
			return detached;
		}
		detached.toShutdown = g_socket;
		g_socket = INVALID_SOCKET;
		if (g_isReceiveThread || !g_receiveLoopActive.load(std::memory_order_acquire)) {
			detached.toClose = detached.toShutdown;
		} else {
			g_socketPendingClose = detached.toShutdown;
		}
		return detached;
	}

	void FinishDetachedSocket(const DetachedSocket& a_detached)
	{
		if (a_detached.toShutdown == INVALID_SOCKET) {
			return;
		}
		shutdown(a_detached.toShutdown, SD_BOTH);
		if (a_detached.toClose != INVALID_SOCKET) {
			closesocket(a_detached.toClose);
		}
	}

	void CloseSocket()
	{
		DetachedSocket detached;
		{
			const std::scoped_lock lock(g_sendMutex, g_socketMutex);
			detached = DetachSocketLocked();
		}
		FinishDetachedSocket(detached);
	}

	void ClosePendingSocket()
	{
		SOCKET toClose = INVALID_SOCKET;
		{
			const std::scoped_lock lock(g_socketMutex);
			toClose = g_socketPendingClose;
			g_socketPendingClose = INVALID_SOCKET;
		}
		if (toClose != INVALID_SOCKET) {
			closesocket(toClose);
		}
	}

	SOCKET GetSocket()
	{
		const std::scoped_lock lock(g_socketMutex);
		return g_socket;
	}
}
