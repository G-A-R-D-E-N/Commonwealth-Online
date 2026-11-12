#pragma once

#include "F4TSteamTunnel.h"

namespace F4T::Steam
{
	[[nodiscard]] Transport::UdpSocketApi MakeWinsockLoopbackUdp();
}
