#pragma once

#include "F4TConnectFailure.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::GnsWinsockShim
{
	using ConnectFailure = F4T::Networking::ConnectFailure;
	using ConnectFailureKind = F4T::Networking::ConnectFailureKind;

	struct NpcAuthorityAssignment
	{
		std::uint32_t playerId{};
		std::uint64_t epoch{};
	};

	SOCKET Socket(int a_addressFamily, int a_type, int a_protocol) noexcept;
	int Connect(SOCKET a_socket, const sockaddr* a_address, int a_addressLength) noexcept;
	int IoctlSocket(SOCKET a_socket, long a_command, u_long* a_argument) noexcept;
	int Send(SOCKET a_socket, const char* a_buffer, int a_length, int a_flags) noexcept;
	int Recv(SOCKET a_socket, char* a_buffer, int a_length, int a_flags) noexcept;
	int Shutdown(SOCKET a_socket, int a_how) noexcept;
	int CloseSocket(SOCKET a_socket) noexcept;
	int LastError() noexcept;
	std::optional<F4T::Networking::ConnectFailure> TakeConnectFailure(SOCKET a_socket) noexcept;
	std::optional<F4T::Networking::ConnectFailure> TakeCurrentThreadConnectFailure() noexcept;
	std::optional<NpcAuthorityAssignment> GetNpcAuthorityForScope(
		std::string_view a_cellId,
		std::string_view a_worldspaceId) noexcept;
}

#if defined(F4T_GNS_WINSOCK_REDIRECT)
#define socket(a_addressFamily, a_type, a_protocol) \
	::F4T::GnsWinsockShim::Socket((a_addressFamily), (a_type), (a_protocol))
#define connect(a_socket, a_address, a_addressLength) \
	::F4T::GnsWinsockShim::Connect((a_socket), (a_address), (a_addressLength))
#define ioctlsocket(a_socket, a_command, a_argument) \
	::F4T::GnsWinsockShim::IoctlSocket((a_socket), (a_command), (a_argument))
#define send(a_socket, a_buffer, a_length, a_flags) \
	::F4T::GnsWinsockShim::Send((a_socket), (a_buffer), (a_length), (a_flags))
#define recv(a_socket, a_buffer, a_length, a_flags) \
	::F4T::GnsWinsockShim::Recv((a_socket), (a_buffer), (a_length), (a_flags))
#define closesocket(a_socket) ::F4T::GnsWinsockShim::CloseSocket((a_socket))
#define shutdown(a_socket, a_how) ::F4T::GnsWinsockShim::Shutdown((a_socket), (a_how))
#define WSAGetLastError() ::F4T::GnsWinsockShim::LastError()
#endif
