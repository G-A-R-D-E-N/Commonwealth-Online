#include "ServerFurnitureRelay.h"

#include "F4TFurnitureSync.h"
#include "F4TInterestScope.h"
#include "F4TPacketRateLimit.h"
#include "F4TSleepBedIdentity.h"
#include "F4TWorkshopProtocol.h"
#include "ServerOutboundFrames.h"
#include "ServerScopeKeys.h"

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
			return ScopeKeys::InterestScopeOf(a_movement);
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
		if (!a_sender.compatibilityCapabilities.Has(F4T::FurnitureSync::kCapabilityName) || !action) {
			a_stats.packetsRejected++;
			return {};
		}
		if (!scope) {
			if (*action == F4T::FurnitureSync::Action::Exit && a_sender.lastSeatPacket && (a_sender.visibilityPaused || a_sender.resumeAwaitingTransform)) {
				a_sender.lastSeatPacket.reset();
				return {};
			}
			a_stats.packetsRejected++;
			return {};
		}
		const bool clearingSeat = *action == F4T::FurnitureSync::Action::Exit && a_sender.lastSeatPacket.has_value();
		if (!clearingSeat && !F4T::PacketRateLimit::TakeToken(a_sender.furnitureUses, a_monotonicSeconds, kFurnitureUseRefillPerSecond, kFurnitureUseBurst)) {
			a_stats.packetsRejected++;
			return {};
		}
		const nlohmann::json packet{
			{ "type", "furnitureUse" },
			{ "playerId", a_sender.playerId },
			{ "action", std::string{ F4T::FurnitureSync::ActionName(*action) } }
		};
		if (*action == F4T::FurnitureSync::Action::Enter) a_sender.lastSeatPacket = packet;
		else a_sender.lastSeatPacket.reset();
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

	std::optional<OutboundFrame> SeatSnapshot(std::uint32_t a_connectionId, const Session& a_recipient, const Session& a_subject)
	{
		if (!a_recipient.active || !a_subject.active || a_recipient.playerId == a_subject.playerId ||
			!a_recipient.compatibilityCapabilities.Has(F4T::FurnitureSync::kCapabilityName) || !a_subject.lastSeatPacket) return std::nullopt;
		return ReliableFrame(a_connectionId, *a_subject.lastSeatPacket);
	}

	std::optional<OutboundFrame> SeatClear(std::uint32_t a_connectionId, const Session& a_recipient, std::uint32_t a_subjectPlayerId)
	{
		if (!a_recipient.compatibilityCapabilities.Has(F4T::FurnitureSync::kCapabilityName)) return std::nullopt;
		return ReliableFrame(a_connectionId, nlohmann::json{
			{ "type", "furnitureUse" },
			{ "playerId", a_subjectPlayerId },
			{ "action", std::string{ F4T::FurnitureSync::ActionName(F4T::FurnitureSync::Action::Exit) } } });
	}

	std::optional<OutboundFrame> SleepResync(std::uint32_t a_connectionId, const Session& a_recipient, std::uint32_t a_subjectPlayerId, const Session* a_subject)
	{
		if (!a_recipient.active || !a_recipient.compatibilityCapabilities.Has(F4T::SleepBedIdentity::kCapabilityName) || a_recipient.playerId == a_subjectPlayerId) {
			return std::nullopt;
		}
		const auto subjectScope = a_subject ? ScopeFromMovement(a_subject->lastMovement) : std::nullopt;
		if (subjectScope && a_subject->active && a_subject->lastSleepPacket &&
			F4T::InterestScope::StatesShareInterest(*subjectScope, ScopeFromMovement(a_recipient.lastMovement))) {
			return ReliableFrame(a_connectionId, *a_subject->lastSleepPacket);
		}
		auto packet = F4T::SleepBedIdentity::MakeSleepExit();
		packet["playerId"] = a_subjectPlayerId;
		return ReliableFrame(a_connectionId, packet);
	}
}
