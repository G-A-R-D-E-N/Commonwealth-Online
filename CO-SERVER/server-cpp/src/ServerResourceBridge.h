#pragma once

#include "ServerProtocol.h"
#include "ServerSessionTypes.h"
#include "ServerStateBag.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	std::vector<OutboundFrame> ResourceViewOpenFrames(
		StateBagSessions& a_sessions,
		std::uint32_t a_playerId,
		std::string_view a_resource,
		std::string_view a_view,
		std::string& a_failure);

	std::vector<OutboundFrame> ResourceViewCloseFrames(
		StateBagSessions& a_sessions,
		std::uint32_t a_playerId,
		std::string_view a_resource);

	bool IsResourceViewOpen(
		const StateBagSessions& a_sessions,
		std::uint32_t a_playerId,
		std::string_view a_resource);

	std::vector<OutboundFrame> HandleResourceMessage(
		SessionTypes::Session& a_session,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats,
		const ResourceMessageHandler& a_handler);
}
