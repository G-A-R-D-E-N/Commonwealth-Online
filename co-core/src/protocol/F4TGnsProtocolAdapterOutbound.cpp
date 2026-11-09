#include "F4TGnsProtocolAdapterImpl.h"

namespace F4T::GnsProtocolAdapter
{
	std::vector<OutboundMessage> Session::Impl::AdaptTransform(Json packet)
	{
		Json equippedItems = Json::array();
		if (const auto iterator = packet.find("equippedItems"); iterator != packet.end() && iterator->is_array()) {
			equippedItems = *iterator;
		}

		if (const auto iterator = packet.find("appearance"); iterator != packet.end() && iterator->is_object()) {
			lastLocalAppearance = *iterator;
		}
		if (const auto characterName = ReadString(packet, "characterName"); characterName && !characterName->empty()) {
			lastLocalCharacterName = *characterName;
		}

		Json actionEvents = Json::array();
		if (const auto iterator = packet.find("actionEvents"); iterator != packet.end() && iterator->is_array()) {
			actionEvents = *iterator;
		}

		Json durable = Json::object();
		durable["equippedItems"] = equippedItems;
		if (const auto iterator = packet.find("powerArmor"); iterator != packet.end() && iterator->is_object()) {
			durable["powerArmor"] = *iterator;
		}
		if (lastLocalAppearance) {
			durable["appearance"] = *lastLocalAppearance;
		}
		if (lastLocalCharacterName && !lastLocalCharacterName->empty()) {
			durable["characterName"] = *lastLocalCharacterName;
		}
		const auto durableDump = durable.dump();

		for (const char* field : { "equippedItems", "appearance", "actionEvents", "characterName", "powerArmor" }) {
			packet.erase(field);
		}

		if (movementEpoch != 0) {
			packet["movementEpoch"] = movementEpoch;
			packet["movementSequence"] = ++outboundMovementSequence;
		}
		if (awaitingVisibilityResume) {
			const auto cellId = ReadString(packet, "cellId").value_or("");
			const auto worldspaceId = ReadString(packet, "worldspaceId").value_or("");
			auto movementType = ReadString(packet, "movementType").value_or("normal");
			const bool scopeChanged = resumeMovementType && (cellId != resumeCellId || worldspaceId != resumeWorldspaceId);
			if (scopeChanged && movementType == "normal") {
				movementType = worldspaceId != resumeWorldspaceId ? "worldspace_change" : "cell_change";
			}
			if (!resumeMovementType || scopeChanged || movementType != "normal") {
				resumeMovementType = movementType;
				++resumeTransitionId;
				resumeCellId = cellId;
				resumeWorldspaceId = worldspaceId;
			} else {
				packet["resumeRetry"] = true;
			}
			packet["movementType"] = *resumeMovementType;
			packet["resumeTransitionId"] = resumeTransitionId;
		}
		std::vector<OutboundMessage> messages;
		messages.push_back({ packet.dump(), awaitingVisibilityResume ? Delivery::ReliableOrdered : Delivery::UnreliableSequenced });
		const auto now = std::chrono::steady_clock::now();
		const bool durableChanged = durableDump != lastLocalDurableDump;
		const bool stickyDue = resumeStatePending || durableChanged || now - lastStickySendAt >= kStickyPlayerStateResendInterval;
		if (stickyDue || !actionEvents.empty()) {
			Json playerState{
				{ "type", "playerState" },
				{ "actionEvents", std::move(actionEvents) }
			};
			if (stickyDue) {
				MergeStickyPlayerState(playerState, durable);
				lastStickySendAt = now;
				lastLocalDurableDump = durableDump;
				resumeStatePending = false;
			}
			if (awaitingVisibilityResume) {
				messages.insert(messages.begin(), { playerState.dump(), Delivery::ReliableOrdered });
			} else {
				messages.push_back({ playerState.dump(), Delivery::ReliableOrdered });
			}
		}
		return messages;
	}

	std::vector<OutboundMessage> Session::Impl::AdaptNpcState(const Json& packet)
	{
		const auto npcs = packet.find("npcs");
		if (npcs == packet.end() || !npcs->is_array() || localPlayerId == 0) {
			return {};
		}
		std::unordered_map<ScopeKey, Json, ScopeKeyHash> groups;
		if (npcs->empty()) { if (const auto scope = ReadAuthorityScope(packet)) groups[*scope] = Json::array(); }
		for (const auto& npc : *npcs) {
			if (!npc.is_object()) {
				continue;
			}
			const auto scope = ReadNpcScope(npc);
			if (!scope) {
				continue;
			}
			auto& group = groups[*scope];
			if (!group.is_array()) {
				group = Json::array();
			}
			group.push_back(npc);
		}

		std::vector<OutboundMessage> messages;
		for (auto& [scope, group] : groups) {
			const auto authority = npcAuthority.find(scope);
			if (authority == npcAuthority.end() ||
				authority->second.playerId != localPlayerId ||
				authority->second.epoch == 0) {
				continue;
			}

			Json scopedPacket = packet;
			scopedPacket["npcs"] = std::move(group);
			scopedPacket["authorityEpoch"] = authority->second.epoch;
			scopedPacket["authorityCellId"] = scope.cellId;
			scopedPacket["authorityWorldspaceId"] = scope.worldspaceId;
			messages.push_back({ scopedPacket.dump(), Delivery::UnreliableSequenced });
		}
		return messages;
	}

	std::vector<OutboundMessage> Session::Impl::AdaptOutbound(std::string_view a_legacyPayload)
	{
		const auto text = TrimLegacyPayload(a_legacyPayload);
		if (text.empty() || !ready) {
			return {};
		}

		Json packet;
		try {
			packet = Json::parse(text);
		} catch (...) {
			return { { text, Delivery::ReliableOrdered } };
		}
		if (!packet.is_object()) {
			return { { text, Delivery::ReliableOrdered } };
		}

		const auto type = ReadString(packet, "type");
		if (type && *type == "visibilityPause") {
			if (!visibilityPauseSupported || !ReadBoolean(packet, "paused")) {
				return {};
			}
			packet["movementEpoch"] = ++movementEpoch;
			outboundMovementSequence = 0;
			awaitingVisibilityResume = !*ReadBoolean(packet, "paused");
			resumeStatePending = awaitingVisibilityResume;
			resumeMovementType.reset();
			resumeTransitionId = 0;
			return { { packet.dump(), Delivery::ReliableOrdered } };
		}
		if (type && *type == "transform") {
			return AdaptTransform(std::move(packet));
		}
		if (type && *type == "npcState") {
			return AdaptNpcState(packet);
		}
		return { { packet.dump(), Delivery::ReliableOrdered } };
	}

}
