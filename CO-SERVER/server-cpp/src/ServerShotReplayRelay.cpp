#include "ServerShotReplayRelay.h"

#include "F4TInterestScope.h"
#include "F4TPacketRateLimit.h"
#include "F4TQuestProtocol.h"
#include "F4TShotReplay.h"
#include "ServerExplosionFormAllowlist.h"
#include "ServerOutboundFrames.h"
#include "ServerScopeKeys.h"

#include <optional>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server::ShotReplayRelay
{
	namespace
	{
		using SessionTypes::Session;
		using SessionTypes::kExplosionSpawnBurst;
		using SessionTypes::kExplosionSpawnRefillPerSecond;
		using SessionTypes::kShotReplayBurst;
		using SessionTypes::kShotReplayRefillPerSecond;

		constexpr double kExplosionSenderRadius = 6500.0;

		bool NearSender(const std::optional<SessionTypes::StoredMovementState>& a_movement, const F4T::ShotReplay::ExplosionSpawn& a_spawn)
		{
			if (!a_movement) {
				return false;
			}
			const double dx = a_spawn.x - a_movement->x;
			const double dy = a_spawn.y - a_movement->y;
			const double dz = a_spawn.z - a_movement->z;
			return dx * dx + dy * dy + dz * dz <= kExplosionSenderRadius * kExplosionSenderRadius;
		}

		bool IsKnownExplosionForm(std::string_view a_plugin, std::uint32_t a_localId)
		{
			for (const auto& entry : ExplosionFormAllowlist::kEntries) {
				if (entry.plugin == a_plugin && entry.localId == a_localId) {
					return true;
				}
			}
			return false;
		}

		std::optional<F4T::InterestScope::StateScope> ScopeFromMovement(const std::optional<SessionTypes::StoredMovementState>& a_movement)
		{
			if (!a_movement) {
				return std::nullopt;
			}
			return ScopeKeys::InterestScopeOf(a_movement);
		}

		bool FriendlyFireAllows(const Session& a_sender, const Session& a_recipient, const ExplosionFriendlyFire& a_rules)
		{
			if (!a_rules.friendlyFire) {
				return false;
			}
			if (a_rules.factionFriendlyFire || !a_rules.sameFaction) {
				return true;
			}
			return !a_rules.sameFaction(a_sender.profileId, a_recipient.profileId);
		}

		std::vector<OutboundFrame> RelayToInterested(
			const std::unordered_map<std::uint32_t, Session>& a_sessions,
			std::uint32_t a_senderConnectionId,
			const Session& a_sender,
			const nlohmann::json& a_packet,
			std::string_view a_capabilityName,
			const F4T::InterestScope::StateScope& a_scope,
			const ExplosionFriendlyFire* a_friendlyFire)
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : a_sessions) {
				if (connectionId == a_senderConnectionId || !recipient.active || recipient.playerId == a_sender.playerId ||
					!recipient.compatibilityCapabilities.Has(a_capabilityName)) {
					continue;
				}
				if (a_friendlyFire && !FriendlyFireAllows(a_sender, recipient, *a_friendlyFire)) {
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
		return RelayToInterested(a_sessions, a_senderConnectionId, a_sender, packet, F4T::ShotReplay::kCapabilityName, *scope, nullptr);
	}

	std::vector<OutboundFrame> HandleExplosionSpawn(
		const std::unordered_map<std::uint32_t, Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats,
		const ExplosionFriendlyFire& a_friendlyFire)
	{
		const auto spawn = F4T::ShotReplay::ParseExplosionSpawn(a_packet);
		const auto scope = ScopeFromMovement(a_sender.lastMovement);
		if (!a_sender.compatibilityCapabilities.Has(F4T::ShotReplay::kExplosionCapabilityName) || !spawn || !scope ||
			!NearSender(a_sender.lastMovement, *spawn) ||
			!F4T::PacketRateLimit::TakeToken(a_sender.explosionSpawns, a_monotonicSeconds, kExplosionSpawnRefillPerSecond, kExplosionSpawnBurst)) {
			a_stats.packetsRejected++;
			return {};
		}
		if (!IsKnownExplosionForm(spawn->formKey.plugin, spawn->formKey.localId)) {
			a_stats.packetsRejected++;
			a_stats.explosionFormsRejected++;
			return {};
		}
		const nlohmann::json packet{
			{ "type", "explosionSpawn" },
			{ "playerId", a_sender.playerId },
			{ "formKey", F4T::QuestProtocol::FormatFormKey(spawn->formKey) },
			{ "x", spawn->x },
			{ "y", spawn->y },
			{ "z", spawn->z }
		};
		return RelayToInterested(a_sessions, a_senderConnectionId, a_sender, packet, F4T::ShotReplay::kExplosionCapabilityName, *scope, &a_friendlyFire);
	}
}
