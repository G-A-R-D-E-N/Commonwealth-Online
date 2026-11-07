#include "ServerFurnitureRelay.h"

#include "F4TFurnitureSync.h"
#include "F4TInterestScope.h"
#include "F4TPacketRateLimit.h"
#include "F4TSleepBedIdentity.h"
#include "F4TWorkshopProtocol.h"
#include "ServerOutboundFrames.h"

#include <optional>
#include <string>

namespace CommonwealthOnline::Server::FurnitureRelay
{
	namespace
	{
		using SessionTypes::Session;
		using SessionTypes::kFurnitureUseBurst;
		using SessionTypes::kFurnitureUseRefillPerSecond;

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
			const F4T::InterestScope::StateScope& a_scope,
			std::string_view a_capability)
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : a_sessions) {
				if (connectionId == a_senderConnectionId || !recipient.active || recipient.playerId == a_sender.playerId ||
					!recipient.compatibilityCapabilities.Has(a_capability)) {
					continue;
				}
				if (F4T::InterestScope::StatesShareInterest(a_scope, ScopeFromMovement(recipient.lastMovement))) {
					frames.push_back(ReliableFrame(connectionId, a_packet));
				}
			}
			return frames;
		}
	}

	std::vector<OutboundFrame> HandleFurnitureUse(
		const std::unordered_map<std::uint32_t, Session>& a_sessions,
		std::uint32_t a_senderConnectionId,
		Session& a_sender,
		const nlohmann::json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats)
	{
		const auto scope = ScopeFromMovement(a_sender.lastMovement);
		const auto action = F4T::FurnitureSync::ParseClientUse(a_packet);
		if (!a_sender.compatibilityCapabilities.Has(F4T::FurnitureSync::kCapabilityName) || !scope || !action ||
			!F4T::PacketRateLimit::TakeToken(a_sender.furnitureUses, a_monotonicSeconds, kFurnitureUseRefillPerSecond, kFurnitureUseBurst)) {
			a_stats.packetsRejected++;
			return {};
		}
		const nlohmann::json packet{
			{ "type", "furnitureUse" },
			{ "playerId", a_sender.playerId },
			{ "action", std::string{ F4T::FurnitureSync::ActionName(*action) } }
		};
		return RelayToInterested(a_sessions, a_senderConnectionId, a_sender, packet, *scope, F4T::FurnitureSync::kCapabilityName);
	}

	std::vector<OutboundFrame> HandleSleepUse(
		const std::unordered_map<std::uint32_t, Session>& a_sessions,
		std::uint32_t a_senderConnectionId, Session& a_sender, const nlohmann::json& a_packet,
		double a_monotonicSeconds, ProtocolStats& a_stats)
	{
		const auto scope = ScopeFromMovement(a_sender.lastMovement);
		const auto action = F4T::FurnitureSync::ParseClientUse(a_packet);
		if (!a_sender.compatibilityCapabilities.Has(F4T::SleepBedIdentity::kCapabilityName) || !action) {
			++a_stats.packetsRejected;
			return {};
		}
		if (!scope) {
			if (*action == F4T::FurnitureSync::Action::Exit && a_sender.lastSleepPacket &&
				(a_sender.visibilityPaused || a_sender.resumeAwaitingTransform)) {
				a_sender.lastSleepPacket.reset();
				return {};
			}
			++a_stats.packetsRejected;
			return {};
		}
		nlohmann::json packet{ { "type", "sleepUse" }, { "playerId", a_sender.playerId }, { "action", std::string{ F4T::FurnitureSync::ActionName(*action) } } };
		if (*action == F4T::FurnitureSync::Action::Enter) {
			const auto bed = a_packet.find("bed");
			const auto marker = a_packet.find("marker");
			const auto identity = bed == a_packet.end() ? std::nullopt : F4T::SleepBedIdentity::ParseBedIdentity(*bed);
			const auto markerId = marker == a_packet.end() ? std::nullopt : F4T::WorkshopProtocol::Codec::ReadU32(*marker);
			if (!identity || !markerId || !F4T::SleepBedIdentity::IsValidMarker(*markerId)) {
				++a_stats.packetsRejected;
				return {};
			}
			if (const auto* form = std::get_if<F4T::QuestProtocol::FormKey>(&*identity)) {
				packet["bed"] = { { "kind", "form" }, { "key", F4T::QuestProtocol::FormatFormKey(*form) } };
			} else {
				const auto& object = std::get<F4T::SleepBedIdentity::WorkshopObject>(*identity);
				packet["bed"] = { { "kind", "workshop" }, { "zone", object.zone }, { "object", object.object } };
			}
			packet["marker"] = *markerId;
		}
		const bool acceptedExit = *action == F4T::FurnitureSync::Action::Exit && a_sender.lastSleepPacket.has_value();
		if (!F4T::PacketRateLimit::TakeToken(a_sender.furnitureUses, a_monotonicSeconds, kFurnitureUseRefillPerSecond, kFurnitureUseBurst) && !acceptedExit) {
			++a_stats.packetsRejected;
			return {};
		}
		if (*action == F4T::FurnitureSync::Action::Enter) a_sender.lastSleepPacket = packet;
		else a_sender.lastSleepPacket.reset();
		return RelayToInterested(a_sessions, a_senderConnectionId, a_sender, packet, *scope, F4T::SleepBedIdentity::kCapabilityName);
	}

	std::optional<OutboundFrame> SleepSnapshot(std::uint32_t a_connectionId, const Session& a_recipient, const Session& a_subject)
	{
		if (!a_recipient.active || !a_subject.active || a_recipient.playerId == a_subject.playerId ||
			!a_recipient.compatibilityCapabilities.Has(F4T::SleepBedIdentity::kCapabilityName) || !a_subject.lastSleepPacket) return std::nullopt;
		return ReliableFrame(a_connectionId, *a_subject.lastSleepPacket);
	}
}
