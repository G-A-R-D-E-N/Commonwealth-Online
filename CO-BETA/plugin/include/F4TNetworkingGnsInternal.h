#pragma once

#include "F4TPowerArmorSync.h"
#include "F4TRemotePlayerState.h"

#include <WinSock2.h>

#include <cstdint>
#include <span>
#include <string>

namespace F4T::NetworkingGnsInternal
{
	SOCKET SocketForValidation(int a_addressFamily, int a_type, int a_protocol) noexcept;
	int CloseSocketForValidation(SOCKET a_socket) noexcept;
	int RecvWithPositionCorrection(SOCKET a_socket, char* a_buffer, int a_length, int a_flags) noexcept;
}

namespace F4T::Networking
{
	bool ConnectAndValidateServerLegacy(const char* a_host, std::uint16_t a_port, std::string& a_error);
}
