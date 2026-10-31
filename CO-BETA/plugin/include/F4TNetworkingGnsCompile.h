#pragma once

#include "F4TNetworkingGnsInternal.h"

#define F4T_GNS_WINSOCK_REDIRECT 1
#include "F4TGnsWinsockShim.h"

#undef recv
#undef closesocket
#undef socket

#define socket(a_addressFamily, a_type, a_protocol) \
	::F4T::NetworkingGnsInternal::SocketForValidation((a_addressFamily), (a_type), (a_protocol))
#define closesocket(a_socket) \
	::F4T::NetworkingGnsInternal::CloseSocketForValidation((a_socket))
#define recv(a_socket, a_buffer, a_length, a_flags) \
	::F4T::NetworkingGnsInternal::RecvWithPositionCorrection((a_socket), (a_buffer), (a_length), (a_flags))
