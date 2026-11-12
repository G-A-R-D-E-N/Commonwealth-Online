#pragma once

#include "AdminCommandDispatcher.h"
#include "ServerProtocol.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	enum class FactionAdminOp
	{
		Disband,
		Promote,
		Kick
	};

	using FactionFrameSender = std::function<void(std::vector<OutboundFrame>&)>;

	inline AdminActionResult RunFactionAdminAction(ProtocolHost& a_protocol, FactionAdminOp a_op, std::string_view a_factionId, std::string_view a_profileId, double a_unixTimeSeconds, const FactionFrameSender& a_send)
	{
		std::string failure;
		std::vector<OutboundFrame> frames;
		const char* verb = "disband";
		const char* done = "Disbanded faction ";
		switch (a_op) {
		case FactionAdminOp::Disband:
			frames = a_protocol.AdminDisbandFaction(a_factionId, a_unixTimeSeconds, failure);
			break;
		case FactionAdminOp::Promote:
			verb = "promote";
			done = "Promoted a new leader in faction ";
			frames = a_protocol.AdminPromoteFaction(a_factionId, a_profileId, a_unixTimeSeconds, failure);
			break;
		case FactionAdminOp::Kick:
			verb = "kick";
			done = "Kicked a member from faction ";
			frames = a_protocol.AdminKickFaction(a_factionId, a_profileId, a_unixTimeSeconds, failure);
			break;
		}
		const std::string factionId{ a_factionId };
		if (failure == "persistenceUnavailable") {
			return AdminActionResult{ false, std::string{ "Faction " } + verb + " could not be saved.", nullptr };
		}
		if (failure == "leaderKick") {
			return AdminActionResult{ false, "Faction " + factionId + " leader cannot be kicked; promote someone else or disband.", nullptr };
		}
		if (failure == "alreadyLeader") {
			return AdminActionResult{ false, "Profile " + std::string{ a_profileId } + " already leads faction " + factionId + ".", nullptr };
		}
		if (failure == "invalidTarget") {
			return AdminActionResult{ false, "Profile " + std::string{ a_profileId } + " is not a valid target in faction " + factionId + ".", nullptr };
		}
		if (failure == "factionsDisabled") {
			return AdminActionResult{ false, "Factions are not enabled on this server.", nullptr };
		}
		if (failure == "factionNotFound") {
			return AdminActionResult{ false, "No faction with id " + factionId + ".", nullptr };
		}
		if (!failure.empty()) {
			return AdminActionResult{ false, std::string{ "Faction " } + verb + " failed: " + failure + ".", nullptr };
		}
		a_send(frames);
		nlohmann::json data{ { "factionId", factionId } };
		if (a_op != FactionAdminOp::Disband) {
			data["profileId"] = std::string{ a_profileId };
		}
		return AdminActionResult{ true, done + factionId + ".", data };
	}
}
