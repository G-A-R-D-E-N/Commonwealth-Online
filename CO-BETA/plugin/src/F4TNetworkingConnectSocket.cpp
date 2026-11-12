#include "pch.h"

#include "F4TConnectionPolicy.h"
#include "F4TNetworkingSocketLifecycle.h"

#include <WinSock2.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace F4T::Networking::Detail
{
	constexpr auto kConnectTimeoutMilliseconds = 3000;

	bool WaitForSocketWritable(SOCKET a_socket, std::chrono::milliseconds a_timeout)
	{
		fd_set writeSet{};
		FD_ZERO(std::addressof(writeSet));
		FD_SET(a_socket, std::addressof(writeSet));

		timeval timeout{};
		timeout.tv_sec = static_cast<long>(a_timeout.count() / 1000);
		timeout.tv_usec = static_cast<long>((a_timeout.count() % 1000) * 1000);
		return select(0, nullptr, std::addressof(writeSet), nullptr, std::addressof(timeout)) > 0;
	}

	bool SetNonBlocking(SOCKET a_socket)
	{
		u_long nonBlocking = 1;
		return ioctlsocket(a_socket, FIONBIO, std::addressof(nonBlocking)) == 0;
	}

	bool TryFinishNonBlockingConnect(SOCKET a_socket)
	{
		fd_set writeSet{};
		FD_ZERO(std::addressof(writeSet));
		FD_SET(a_socket, std::addressof(writeSet));

		timeval timeout{};
		timeout.tv_sec = kConnectTimeoutMilliseconds / 1000;
		timeout.tv_usec = (kConnectTimeoutMilliseconds % 1000) * 1000;

		const auto selectResult = select(0, nullptr, std::addressof(writeSet), nullptr, std::addressof(timeout));
		if (selectResult <= 0) {
			return false;
		}

		int socketError = 0;
		int socketErrorSize = sizeof(socketError);
		if (getsockopt(
				a_socket,
				SOL_SOCKET,
				SO_ERROR,
				reinterpret_cast<char*>(std::addressof(socketError)),
				std::addressof(socketErrorSize)) != 0) {
			return false;
		}

		return socketError == 0;
	}

	std::string TrimAsciiWhitespace(std::string_view a_text)
	{
		while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t' || a_text.front() == '\r' ||
		                           a_text.front() == '\n')) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r' ||
		                          a_text.back() == '\n')) {
			a_text.remove_suffix(1);
		}
		return std::string{ a_text };
	}

	bool NormalizeConnectEndpoint(std::string& a_host, std::uint16_t& a_port)
	{
		a_host = TrimAsciiWhitespace(a_host);
		return F4T::ConnectionPolicy::NormalizeConnectEndpoint(a_host, a_port);
	}

	bool TryConnectNonBlocking(SOCKET a_socket, const sockaddr* a_address, int a_addressLength)
	{
		const auto connectResult = connect(a_socket, a_address, a_addressLength);
		return connectResult == 0 ||
		       (WSAGetLastError() == WSAEWOULDBLOCK && TryFinishNonBlockingConnect(a_socket));
	}
}
