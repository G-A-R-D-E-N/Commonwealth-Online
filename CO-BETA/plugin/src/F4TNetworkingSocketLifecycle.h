#pragma once

#include <WinSock2.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

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

	bool WaitForSocketWritable(SOCKET a_socket, std::chrono::milliseconds a_timeout);
	bool SetNonBlocking(SOCKET a_socket);
	bool TryFinishNonBlockingConnect(SOCKET a_socket);
	std::string TrimAsciiWhitespace(std::string_view a_text);
	bool NormalizeConnectEndpoint(std::string& a_host, std::uint16_t& a_port);
	bool TryConnectNonBlocking(SOCKET a_socket, const sockaddr* a_address, int a_addressLength);
}
