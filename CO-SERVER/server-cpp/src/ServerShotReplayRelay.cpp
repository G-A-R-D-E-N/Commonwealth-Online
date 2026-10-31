#include "ServerShotReplayRelay.h"

#include "F4TInterestScope.h"
#include "F4TPacketRateLimit.h"
#include "F4TShotReplay.h"
#include "ServerOutboundFrames.h"

#include <optional>
#include <string>

namespace CommonwealthOnline::Server::ShotReplayRelay
{
	namespace
	{
		using SessionTypes::Session;
		using SessionTypes::kShotReplayBurst;
		using SessionTypes::kShotReplayRefillPerSecond;

		std::optional<F4T::InterestScope::StateScope> ScopeFromMovement(const std::optional<SessionTypes::StoredMovementState>& a_movement)
		{
			if (!a_movement) {
				return std::nullopt;
			}
			return F4T::InterestScope::StateScope{ a_movement->cellId, a_movement->worldspaceId, a_movement->x, a_movement->y };
		}

		std::vector<OutboundFrame> RelayToInterested(
			const std::unordered_map<std::uint32_t, Session>& a_sessions,
			std::uint32_t a_senderConnectionId,
			const Session& a_sender,
			const nlohmann::json& a_packet,
			const F4T::InterestScope::StateScope& a_scope)
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : a_sessions) {
				if (connectionId == a_senderConnectionId || !recipient.active || recipient.playerId == a_sender.playerId ||
					!recipient.compatibilityCapabilities.Has(F4T::ShotReplay::kCapabilityName)) {
					continue;
				}
				if (F4T::InterestScope::StatesShareInterest(a_scope, ScopeFromMovement(recipient.lastMovement))) {
					frames.push_back(ReliableFrame(connectionId, a_packet));
				}
			}
			return frames;
		}
	}

	std::vector<OutboundFrame> HandleShotReplay(
		const std::unordered_map<std::uint32_t, Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats)
	{
		const auto type = a_packet.find("type");
		if (!a_sender.compatibilityCapabilities.Has(F4T::ShotReplay::kCapabilityName) || type == a_packet.end() || !type->is_string()) {
			a_stats.packetsRejected++;
			return {};
		}
		const auto scope = ScopeFromMovement(a_sender.lastMovement);
		if (!scope) {
			a_stats.packetsRejected++;
			return {};
		}
		const auto kind = type->get<std::string>();
		nlohmann::json packet{ { "playerId", a_sender.playerId } };
		if (kind == "projectileAim") {
			const auto direction = F4T::ShotReplay::ParseProjectileAim(a_packet);
			if (!direction || !F4T::PacketRateLimit::TakeToken(a_sender.shotReplays, a_monotonicSeconds, kShotReplayRefillPerSecond, kShotReplayBurst)) {
				a_stats.packetsRejected++;
				return {};
			}
			packet["type"] = "projectileAim";
			packet["direction"] = { { "x", direction->x }, { "y", direction->y }, { "z", direction->z } };
		} else if (kind == "playerAnimEvent") {
			const auto event = F4T::ShotReplay::ParsePlayerAnimEvent(a_packet);
			if (!event || !F4T::PacketRateLimit::TakeToken(a_sender.animEventReplays, a_monotonicSeconds, kShotReplayRefillPerSecond, kShotReplayBurst)) {
				a_stats.packetsRejected++;
				return {};
			}
			packet["type"] = "playerAnimEvent";
			packet["event"] = *event;
		} else {
			a_stats.packetsRejected++;
			return {};
		}
		return RelayToInterested(a_sessions, a_senderConnectionId, a_sender, packet, *scope);
	}
}
