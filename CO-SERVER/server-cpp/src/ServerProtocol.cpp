#include "ServerProtocolImpl.h"

namespace CommonwealthOnline::Server
{
	std::size_t MaximumBytesForFamily(Family a_family) noexcept
	{
		return a_family == Family::Transform ? kMaximumTransformBytes : kMaximumNpcStateBytes;
	}

	std::size_t MaximumBytesForType(std::string_view a_type) noexcept
	{
		if (a_type == "transform") {
			return kMaximumTransformBytes;
		}
		if (a_type == "npcState") {
			return kMaximumNpcStateBytes;
		}
		if (a_type == "npcLoot") {
			return kMaximumNpcLootBytes;
		}
		if (a_type == "pluginList") {
			return kMaximumPluginListBytes;
		}
		if (a_type == "playerState") {
			return kMaximumPlayerStateBytes;
		}
		if (a_type == "hello") {
			return kMaximumHelloBytes;
		}
		if (a_type == "chatSend") {
			return kMaximumChatSendBytes;
		}
		if (a_type == "inventoryTradeRequest") {
			return kMaximumInventoryTradeBytes;
		}
		return kMaximumSmallPacketBytes;
	}

	Json SessionStatePacket(
		const Session& a_session,
		F4T::Session::LifecycleState a_state,
		double a_unixTimeSeconds)
	{
		return Json{
			{ "type", "sessionState" },
			{ "playerId", a_session.playerId },
			{ "state", F4T::Session::ToWireName(a_state) },
			{ "spawnable", F4T::Session::IsSpawnable(a_state) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	bool IsSpaceChangeMovementType(std::string_view a_movementType) noexcept
	{
		return a_movementType == "cell_change" ||
			a_movementType == "worldspace_change" ||
			a_movementType == "load" ||
			a_movementType == "fast_travel";
	}

	std::optional<Compatibility::SessionCapabilities> NormalizeCompatibilityCapabilities(const Json& a_packet)
	{
		const auto found = a_packet.find("capabilities");
		if (found == a_packet.end()) {
			return Compatibility::SessionCapabilities{};
		}
		if (!found->is_array() || found->size() > 64) {
			return std::nullopt;
		}
		Compatibility::SessionCapabilities result;
		result.values.reserve(found->size());
		std::unordered_set<std::string> unique;
		for (const auto& item : *found) {
			if (!item.is_string()) {
				return std::nullopt;
			}
			const auto value = item.get<std::string>();
			if (value.empty() || value.size() > 64 || !unique.insert(value).second) {
				return std::nullopt;
			}
			for (const unsigned char ch : value) {
				if (ch < 0x20 || ch == 0x7f) {
					return std::nullopt;
				}
			}
			result.values.push_back(value);
		}
		return result;
	}

	std::string NormalizeFormId(std::string_view a_value)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed, 16);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size()) {
			return {};
		}
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", parsed);
		return buffer;
	}

	std::size_t FamilyIndex(Family a_family)
	{
		return a_family == Family::Transform ? 0uz : 1uz;
	}

	std::optional<Family> FamilyForPacketType(std::string_view a_type)
	{
		if (a_type == "transform") {
			return Family::Transform;
		}
		if (a_type == "npcState") {
			return Family::NpcState;
		}
		return std::nullopt;
	}

	std::optional<Json> ParsePacket(std::span<const std::uint8_t> a_payload)
	{
		if (a_payload.empty() || a_payload.size() > kMaximumMessageBytes) {
			return std::nullopt;
		}
		try {
			const auto text = std::string{
				reinterpret_cast<const char*>(a_payload.data()),
				a_payload.size()
			};
			auto packet = Json::parse(text);
			return packet.is_object() ? std::optional{ std::move(packet) } : std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<StoredMovementState> NormalizeTransform(Json& a_packet)
	{
		auto normalized = F4T::TransformValidation::NormalizeTransform(a_packet);
		if (!normalized) {
			return std::nullopt;
		}
		a_packet = std::move(*normalized);
		for (const char* field : { "equippedItems", "appearance", "actionEvents", "characterName" }) {
			a_packet.erase(field);
		}
		const auto x = ReadDouble(a_packet, "x");
		const auto y = ReadDouble(a_packet, "y");
		const auto z = ReadDouble(a_packet, "z");
		const auto angleZ = ReadDouble(a_packet, "angleZ");
		const auto cellId = ReadString(a_packet, "cellId");
		const auto worldspaceId = ReadString(a_packet, "worldspaceId");
		const auto movementType = ReadString(a_packet, "movementType");
		if (!x || !y || !z || !angleZ || !cellId || !worldspaceId || !movementType) {
			return std::nullopt;
		}
		return StoredMovementState{ *x, *y, *z, *angleZ, *cellId, *worldspaceId, *movementType };
	}

	std::optional<Json> NormalizePlayerState(const Json& a_packet)
	{
		return F4T::PlayerStateValidation::NormalizePlayerState(a_packet);
	}

	std::optional<Json> NormalizeWorldState(const Json& a_packet)
	{
		return F4T::AuthoritativePacketValidation::NormalizeWorldState(a_packet);
	}

	std::optional<Json> NormalizeNpcState(const Json& a_packet)
	{
		return F4T::AuthoritativePacketValidation::NormalizeNpcState(a_packet);
	}

	std::optional<Json> NormalizeCombatHit(const Json& a_packet)
	{
		return F4T::AuthoritativePacketValidation::NormalizeCombatHit(a_packet);
	}

	std::optional<Json> NormalizeNpcHit(const Json& a_packet)
	{
		return F4T::AuthoritativePacketValidation::NormalizeNpcHit(a_packet);
	}

	std::optional<Json> NormalizeNpcDamage(const Json& a_packet)
	{
		return F4T::AuthoritativePacketValidation::NormalizeNpcDamage(a_packet);
	}

	std::optional<Json> NormalizeNpcLoot(const Json& a_packet)
	{
		return F4T::NpcCoopValidation::NormalizeNpcLoot(a_packet);
	}

	std::optional<Json> NormalizeNpcLootTake(const Json& a_packet)
	{
		return F4T::NpcCoopValidation::NormalizeNpcLootTake(a_packet);
	}

	std::optional<Json> NormalizeNpcLootResult(const Json& a_packet)
	{
		return F4T::NpcCoopValidation::NormalizeNpcLootResult(a_packet);
	}

	std::optional<Scope> ScopeFromMovement(const std::optional<StoredMovementState>& a_movement)
	{
		return a_movement ? std::optional<Scope>{ Scope{ a_movement->cellId, a_movement->worldspaceId } } : std::nullopt;
	}

	std::optional<F4T::InterestScope::StateScope> InterestScopeFromMovement(const std::optional<StoredMovementState>& a_movement)
	{
		if (!a_movement) {
			return std::nullopt;
		}
		return F4T::InterestScope::StateScope{ a_movement->cellId, a_movement->worldspaceId, a_movement->x, a_movement->y };
	}

	std::optional<F4T::InterestScope::StateScope> InterestScopeFromNpc(const Json& a_npc)
	{
		const auto cellId = ReadString(a_npc, "cellId");
		const auto worldspaceId = ReadString(a_npc, "worldspaceId").value_or("");
		const auto x = ReadDouble(a_npc, "x");
		const auto y = ReadDouble(a_npc, "y");
		if (!cellId || !x || !y) {
			return std::nullopt;
		}
		return F4T::InterestScope::StateScope{ *cellId, worldspaceId, *x, *y };
	}

	std::optional<Scope> ScopeFromPacket(const Json& a_packet)
	{
		const auto cellId = ReadString(a_packet, "authorityCellId");
		const auto worldspaceId = ReadString(a_packet, "authorityWorldspaceId").value_or("");
		if (!cellId ||
			!F4T::NetworkValidation::IsValidHexFormId(*cellId, false, false) ||
			!F4T::NetworkValidation::IsValidHexFormId(worldspaceId, true, true)) {
			return std::nullopt;
		}
		return Scope{ NormalizeFormId(*cellId), worldspaceId.empty() ? std::string{} : NormalizeFormId(worldspaceId) };
	}

	std::string MovementRejectReason(const F4T::NetworkValidation::MovementValidationResult& a_result)
	{
		using F4T::NetworkValidation::MovementVerdict;
		if (a_result.verdict == MovementVerdict::RejectedScopeChange) {
			return "scope changed without an explicit movement transition";
		}
		if (a_result.verdict == MovementVerdict::RejectedTransitionContext) {
			return "spawn and load transitions are only valid for a session's first transform";
		}
		if (a_result.verdict == MovementVerdict::RejectedTransitionRate) {
			return "movement transitions exceeded the server's transition allowance";
		}
		if (a_result.verdict == MovementVerdict::RejectedTime) {
			return "movement timestamp is invalid or moved backwards";
		}
		if (a_result.verdict == MovementVerdict::RejectedDistance) {
			char buffer[192]{};
			std::snprintf(
				buffer,
				sizeof(buffer),
				"normal movement exceeded server envelope: distance=%.1f, allowed=%.1f, elapsed=%.3fs",
				a_result.distance,
				a_result.allowedDistance,
				a_result.elapsedSeconds);
			return buffer;
		}
		return "movement rejected by server";
	}

	Json AuthorityPacket(const F4T::NpcAuthority::Assignment& a_assignment, double a_unixTimeSeconds)
	{
		return Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", a_assignment.playerId },
			{ "authorityEpoch", a_assignment.epoch },
			{ "authorityCellId", a_assignment.scope.cellId },
			{ "authorityWorldspaceId", a_assignment.scope.worldspaceId },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	Json AuthorityPacket(const F4T::NpcAuthority::Change& a_change, double a_unixTimeSeconds)
	{
		return Json{
			{ "type", "npcAuthority" },
			{ "authorityPlayerId", a_change.playerId },
			{ "authorityEpoch", a_change.epoch },
			{ "authorityCellId", a_change.scope.cellId },
			{ "authorityWorldspaceId", a_change.scope.worldspaceId },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	Json WorldStateHostPacket(std::uint32_t a_playerId, double a_unixTimeSeconds)
	{
		return Json{
			{ "type", "worldStateHost" },
			{ "worldStateHostPlayerId", a_playerId },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	std::vector<OutboundFrame> SendLifecycleState(
		std::uint32_t a_connectionId,
		Session& a_session,
		F4T::Session::LifecycleState a_next,
		double a_unixTimeSeconds)
	{
		if (a_session.lifecycle.State() == a_next) {
			return {};
		}
		const auto result = a_session.lifecycle.MoveTo(a_next);
		if (!result.accepted || !a_session.compatibilityCapabilities.Has("session-lifecycle-v1")) {
			return {};
		}
		return { ReliableFrame(a_connectionId, SessionStatePacket(a_session, result.state, a_unixTimeSeconds)) };
	}

	std::vector<OutboundFrame> LifecycleFramesForMovement(
		std::uint32_t a_connectionId,
		Session& a_session,
		double a_unixTimeSeconds)
	{
		const auto state = a_session.lifecycle.State();
		if (!F4T::Session::IsSpawnable(state)) {
			return {};
		}
		if (a_session.lastMovement && IsSpaceChangeMovementType(a_session.lastMovement->movementType)) {
			return SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::SpaceChange, a_unixTimeSeconds);
		}
		return SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::CharacterLoaded, a_unixTimeSeconds);
	}

	nlohmann::json RuntimeCountersJson(const ProtocolStats& a_stats)
	{
		return nlohmann::json{
			{ "packets_received", a_stats.packetsReceived },
			{ "packets_rejected", a_stats.packetsRejected },
			{ "downed_events_emitted", a_stats.downedEventsEmitted },
			{ "revive_requests_received", a_stats.reviveRequestsReceived },
			{ "revive_requests_rejected", a_stats.reviveRequestsRejected },
			{ "revive_cancels_received", a_stats.reviveCancelsReceived },
			{ "checkpoints_received", a_stats.checkpointsReceived },
			{ "checkpoints_rejected", a_stats.checkpointsRejected },
			{ "respawn_requests_received", a_stats.respawnRequestsReceived },
			{ "respawn_requests_rejected", a_stats.respawnRequestsRejected },
			{ "quest_scopes_received", a_stats.questScopesReceived },
			{ "quest_scopes_rejected", a_stats.questScopesRejected },
			{ "quest_scopes_applied", a_stats.questScopesApplied },
			{ "voice", VoiceStatsJson(a_stats) }
		};
	}

	%:include "ServerProtocolApi.inc"
}
