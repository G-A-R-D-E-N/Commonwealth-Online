#pragma once
#include "AdminCommandDispatcher.h"
#include "ServerProtocol.h"
#include <functional>
#include <vector>

namespace CommonwealthOnline::Server
{
using AdminFrameSender = std::function<void(std::vector<OutboundFrame>&)>;

void DrainAdminMenuActions(ProtocolHost& a_protocol, const AdminCommandDispatcher& a_dispatcher, const AdminFrameSender& a_send);
}
