#pragma once

#include "ServerProtocol.h"
#include "ServerSessionTypes.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server::FurnitureRelay
{
	std::vector<OutboundFrame> HandleFurnitureUse(
		const std::unordered_map<std::uint32_t, SessionTypes::Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		SessionTypes::Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats);
	std::vector<OutboundFrame> HandleSleepUse(
		const std::unordered_map<std::uint32_t, SessionTypes::Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		SessionTypes::Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats);
	std::optional<OutboundFrame> SleepSnapshot(std::uint32_t a_connectionId, const SessionTypes::Session& a_recipient, const SessionTypes::Session& a_subject);
}
