#pragma once

#include "ServerProtocol.h"
#include "ServerSessionTypes.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server::ShotReplayRelay
{
	struct ExplosionFriendlyFire
	{
		bool friendlyFire{ true };
		bool factionFriendlyFire{};
		std::function<bool(const std::string&, const std::string&)> sameFaction;
	};

	std::vector<OutboundFrame> HandleShotReplay(
		const std::unordered_map<std::uint32_t, SessionTypes::Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		SessionTypes::Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats);
	std::vector<OutboundFrame> HandleExplosionSpawn(
		const std::unordered_map<std::uint32_t, SessionTypes::Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		SessionTypes::Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats,
		const ExplosionFriendlyFire& a_friendlyFire = {});
}
