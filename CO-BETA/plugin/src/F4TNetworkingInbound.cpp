#include "pch.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TChat.h"
#include "F4TDownedGlue.h"
#include "F4TConsoleGuard.h"
#include "F4TFactions.h"
#include "F4TFriends.h"
#include "F4TGnsVoice.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TCharacterSlotsClient.h"
#include "F4TInventoryContract.h"
#include "F4TInventorySync.h"
#include "F4TLoadOrderSync.h"
#include "F4TObjectSync.h"
#include "F4TFriendlyFireState.h"
#include "F4TSteamTunnelService.h"
#include "F4TPvPCombatContract.h"
#include "F4TQuestInbound.h"
#include "F4TQuestSync.h"
#include "F4TDebugLogging.h"
#include "F4TNetworkValidation.h"
#include "F4TReceivePolicy.h"
#include "F4TPlayerAnimEventSync.h"
#include "F4TProjectileAim.h"
#include "F4TPlayerVitals.h"
#include "F4TProxyRemoteAnimationState.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TServerProfileStore.h"
#include "F4TSessionLaunch.h"
#include "F4TUIClient.h"
#include "F4TWorldStateSync.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::Networking::Detail
{
	void HandleWelcomePacket(const Json& a_packet)
	{
		const auto playerId = ReadPlayerId(a_packet);
		if (!playerId) {
			LogThrottledWarning("welcome_playerId", "Ignoring welcome packet without a valid playerId.");
			return;
		}

		F4T::ServerProfileStore::SetConnectedWorldId(ReadString(a_packet, "serverInstanceId").value_or(""));
		F4T::CharacterSlotsClient::NoteWelcome(a_packet);
		F4T::RemotePlayerState::SetAssignedPlayerId(*playerId);
		F4T::LoadOrderSync::NoteWelcome();
		F4T::Factions::NoteWelcome(a_packet);
		F4T::Chat::NoteWelcome(a_packet);
		F4T::InventorySync::NoteWelcome(a_packet);
		F4T::ObjectSync::NoteWelcome(a_packet);
		F4T::GnsVoice::NoteWelcome(a_packet);
		F4T::QuestSync::NoteWelcome(a_packet);
		F4T::ConsoleGuard::NoteWelcome(a_packet);
		F4T::Downed::NoteWelcome();

		const auto worldStateHostPlayerId = ReadPlayerId(a_packet, "worldStateHostPlayerId");
		if (worldStateHostPlayerId) {
			F4T::WorldStateSync::QueueWorldStateHostPlayerId(*worldStateHostPlayerId);
		}

		LogStatusWithLocalPlayerPrefix(std::format("Assigned server playerId: {}", *playerId));
	}

	constexpr std::uint64_t kMaximumAdvertisedPlayers = 250; // Steam's lobby slot cap

	std::optional<std::uint32_t> ReadMemberLimit(const Json& a_packet)
	{
		const auto found = a_packet.find("maxPlayers");
		if (found == a_packet.end()) {
			return std::nullopt;
		}
		std::uint64_t value = 0;
		if (found->is_number_unsigned()) {
			value = found->get<std::uint64_t>();
		} else if (found->is_number_integer()) {
			const auto signedValue = found->get<std::int64_t>();
			if (signedValue <= 0) {
				return std::nullopt;
			}
			value = static_cast<std::uint64_t>(signedValue);
		} else {
			return std::nullopt;
		}
		if (value == 0 || value > kMaximumAdvertisedPlayers) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(value);
	}

	void HandleCapabilitiesPacket(const Json& a_packet)
	{
		F4T::Factions::NoteWelcome(a_packet);
		F4T::Chat::NoteWelcome(a_packet);
		F4T::ObjectSync::NoteCapabilities(a_packet);
		F4T::FriendlyFireState::NoteWelcome(a_packet);
		if (const auto name = ReadString(a_packet, "serverName"); name && F4T::Steam::HostRunning()) {
			F4T::Steam::AnnounceHosting(*name);
		}
		if (const auto limit = ReadMemberLimit(a_packet); limit && F4T::Steam::HostRunning()) {
			F4T::Steam::SetHostMemberLimit(*limit);
		}
	}

	void HandleWorldStateHostPacket(const Json& a_packet)
	{
		const auto hostPlayerId = ReadPlayerId(a_packet, "worldStateHostPlayerId");
		if (!hostPlayerId) {
			LogThrottledWarning(
				"world_state_host_missing",
				"Ignoring worldStateHost packet without a valid worldStateHostPlayerId.");
			return;
		}

		F4T::WorldStateSync::QueueWorldStateHostPlayerId(*hostPlayerId);
	}

	void HandleServerWorldStatePacket(const Json& a_packet)
	{
		F4T::WorldStateSync::PendingServerWorldState pendingState{};

		const auto timeHHmm = ReadString(a_packet, "timeHHmm");
		if (timeHHmm && F4T::NetworkValidation::ParseServerTimeHHmm(*timeHHmm)) {
			pendingState.timeHHmm = *timeHHmm;
		}

		const auto weatherFormId = ReadString(a_packet, "weatherFormId");
		if (weatherFormId && F4T::NetworkValidation::IsValidHexFormId(*weatherFormId, false, false)) {
			pendingState.weatherFormId = *weatherFormId;
		}

		if (!pendingState.timeHHmm && !pendingState.weatherFormId) {
			LogThrottledWarning(
				"server_world_state_invalid",
				"Ignoring serverWorldState packet without validated time or weather values.");
			return;
		}

		F4T::WorldStateSync::SetPendingServerWorldState(std::move(pendingState));
	}

	void HandleWorldStatePacket(const Json& a_packet)
	{
		const auto gameHour = ReadDouble(a_packet, "gameHour");
		const auto gameDaysPassed = ReadDouble(a_packet, "gameDaysPassed");
		if (!gameHour || !gameDaysPassed) {
			LogThrottledWarning(
				"world_state_missing_fields",
				"Ignoring worldState packet missing required gameHour or gameDaysPassed.");
			return;
		}

		if (*gameHour < 0.0 || *gameHour > 24.0 || *gameDaysPassed < 0.0 || *gameDaysPassed > 1e9) {
			LogThrottledWarning("world_state_invalid_numbers", "Ignoring worldState packet with out-of-range time values.");
			return;
		}

		F4T::WorldStateSync::PendingWorldState pendingState{
			static_cast<float>(*gameHour),
			static_cast<float>(*gameDaysPassed),
			std::nullopt,
			false,
			ReadBool(a_packet, "timeSync").value_or(false)
		};

		const auto weatherFormIdText = ReadString(a_packet, "weatherFormId");
		if (weatherFormIdText && !weatherFormIdText->empty()) {
			if (const auto weatherFormId = ParseHexFormId(*weatherFormIdText)) {
				pendingState.weatherFormId = *weatherFormId;
				pendingState.hasWeatherFormId = true;
			} else if (ShouldLogWarning("world_state_weather_parse")) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Ignoring invalid weatherFormId '{}' in worldState packet.",
					F4T::DebugLogging::SanitizeLogText(*weatherFormIdText, 32)));
			}
		}

		if (ShouldLogRemoteUpdate(1)) {
			const auto timeSyncNote = pendingState.applyTime ? "timeSync=yes" : "timeSync=no";
			if (pendingState.hasWeatherFormId && pendingState.weatherFormId) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Queued host worldState ({}): gameHour={:.2f}, gameDaysPassed={:.3f}, weatherFormId=0x{:08X}.",
					timeSyncNote,
					pendingState.gameHour,
					pendingState.gameDaysPassed,
					*pendingState.weatherFormId));
			} else {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Queued host worldState ({}): gameHour={:.2f}, gameDaysPassed={:.3f}, weatherFormId=<none>.",
					timeSyncNote,
					pendingState.gameHour,
					pendingState.gameDaysPassed));
			}
		}

		F4T::WorldStateSync::SetPendingWorldState(std::move(pendingState));
	}

	void HandleNpcStatePacket(const Json& a_packet)
	{
		const auto hostPlayerId = ReadPlayerId(a_packet);
		const auto npcs = a_packet.find("npcs");
		if (!hostPlayerId || npcs == a_packet.end() || !npcs->is_array() || npcs->size() > 16) {
			LogThrottledWarning("npc_state_shape", "Ignoring malformed npcState packet.");
			return;
		}

		std::vector<F4T::RemoteNpcState::NpcState> states;
		states.reserve(npcs->size());
		for (const auto& npc : *npcs) {
			if (!npc.is_object()) {
				continue;
			}

			const auto npcId = ReadPlayerId(npc, "npcId");
			const auto baseFormIdText = ReadString(npc, "baseFormId");
			const auto sourceFormIdText = ReadString(npc, "sourceFormId");
			const auto x = ReadDouble(npc, "x");
			const auto y = ReadDouble(npc, "y");
			const auto z = ReadDouble(npc, "z");
			const auto angleZ = ReadDouble(npc, "angleZ");
			const auto cellId = ReadString(npc, "cellId");
			if (!npcId || !baseFormIdText || !sourceFormIdText || !x || !y || !z || !angleZ || !cellId ||
				!F4T::NetworkValidation::IsValidCoordinate(*x) ||
				!F4T::NetworkValidation::IsValidCoordinate(*y) ||
				!F4T::NetworkValidation::IsValidCoordinate(*z) ||
				!F4T::NetworkValidation::IsValidCoordinate(*angleZ) ||
				!F4T::NetworkValidation::IsValidHexFormId(*cellId, false, false)) {
				continue;
			}

			const auto baseFormId = ParseHexFormId(*baseFormIdText);
			const auto sourceFormId = ParseHexFormId(*sourceFormIdText);
			if (!baseFormId || *baseFormId == 0 || !sourceFormId || *sourceFormId == 0 ||
				F4T::RemoteNpcState::IsRuntimeFormId(*baseFormId) ||
				F4T::RemoteNpcState::IsRuntimeFormId(*sourceFormId) ||
				!F4T::NetworkValidation::IsValidHexFormId(*baseFormIdText, false, false) ||
				!F4T::NetworkValidation::IsValidHexFormId(*sourceFormIdText, false, false)) {
				continue;
			}

			const auto worldspaceId = ReadString(npc, "worldspaceId").value_or("");
			if (!F4T::NetworkValidation::IsValidHexFormId(worldspaceId, true, true)) {
				continue;
			}
			const auto movementSpeed = ReadDouble(npc, "movementSpeed").value_or(0.0);
			if (!F4T::NetworkValidation::IsValidMovementSpeed(movementSpeed)) {
				continue;
			}

			const auto health = ReadDouble(npc, "health");
			const auto maxHealth = ReadDouble(npc, "maxHealth");
			if (health.has_value() != maxHealth.has_value() ||
				(health && (!(*health >= 0.0) || !(*maxHealth >= *health) ||
							*maxHealth > F4T::NetworkValidation::MaximumActorHealth))) {
				continue;
			}

			states.push_back(F4T::RemoteNpcState::NpcState{
				*npcId,
				*baseFormId,
				*sourceFormId,
				static_cast<float>(*x),
				static_cast<float>(*y),
				static_cast<float>(*z),
				static_cast<float>(*angleZ),
				*cellId,
				worldspaceId,
				ReadBool(npc, "isDead").value_or(false),
				ReadBool(npc, "isMoving").value_or(false),
				ReadBool(npc, "isSprinting").value_or(false),
				ReadBool(npc, "isSneaking").value_or(false),
				static_cast<float>(movementSpeed),
				health.has_value(),
				health ? static_cast<float>(*health) : 0.0F,
				maxHealth ? static_cast<float>(*maxHealth) : 0.0F
			});
		}

		F4T::RemoteNpcState::ReplaceSnapshot(*hostPlayerId, std::move(states));
	}

	void HandleTransformPacket(const Json& a_packet)
	{
		if (const auto missingField = GetMissingRequiredTransformField(a_packet)) {
			LogThrottledWarning(
				"transform_missing_" + std::string(*missingField),
				std::format("Ignoring transform packet missing required field '{}'.", *missingField));
			return;
		}

		const auto playerId = ReadPlayerId(a_packet);
		const auto x = ReadDouble(a_packet, "x");
		const auto y = ReadDouble(a_packet, "y");
		const auto z = ReadDouble(a_packet, "z");
		const auto angleZ = ReadDouble(a_packet, "angleZ");
		const auto cellId = ReadString(a_packet, "cellId");

		if (!playerId) {
			LogThrottledWarning("transform_invalid_playerId", "Ignoring transform packet with invalid required field 'playerId'.");
			return;
		}
		if (!x) {
			LogThrottledWarning("transform_invalid_x", "Ignoring transform packet with invalid required field 'x'.");
			return;
		}
		if (!y) {
			LogThrottledWarning("transform_invalid_y", "Ignoring transform packet with invalid required field 'y'.");
			return;
		}
		if (!z) {
			LogThrottledWarning("transform_invalid_z", "Ignoring transform packet with invalid required field 'z'.");
			return;
		}
		if (!angleZ) {
			LogThrottledWarning("transform_invalid_angleZ", "Ignoring transform packet with invalid required field 'angleZ'.");
			return;
		}
		if (!cellId ||
			!F4T::NetworkValidation::IsValidHexFormId(*cellId, false, false) ||
			!F4T::NetworkValidation::IsValidCoordinate(*x) ||
			!F4T::NetworkValidation::IsValidCoordinate(*y) ||
			!F4T::NetworkValidation::IsValidCoordinate(*z) ||
			!F4T::NetworkValidation::IsValidCoordinate(*angleZ)) {
			LogThrottledWarning("transform_invalid_values", "Ignoring transform packet with out-of-range required values.");
			return;
		}

		if (F4T::RemotePlayerState::IsAssignedPlayerId(*playerId)) {
			return;
		}

	const auto movementType = ReadString(a_packet, "movementType").value_or("normal");
	const auto worldspaceId = ReadString(a_packet, "worldspaceId").value_or("");
	if (!F4T::NetworkValidation::IsValidHexFormId(worldspaceId, true, true)) {
		LogThrottledWarning("transform_invalid_worldspace", "Ignoring transform packet with invalid worldspaceId.");
		return;
	}
	auto movementSpeed = ReadDouble(a_packet, "movementSpeed").value_or(0.0);
	if (!F4T::NetworkValidation::IsValidMovementSpeed(movementSpeed)) {
		movementSpeed = 0.0;
	}

	auto animationGraphSpeed = -1.0;
	if (const auto graphSpeed = ReadDouble(a_packet, "animationGraphSpeed")) {
		if (F4T::NetworkValidation::IsValidMovementSpeed(*graphSpeed)) {
			animationGraphSpeed = *graphSpeed;
		}
	}

	bool hasEquipmentUpdate = false;
	auto equippedItems = ReadEquippedItems(a_packet, hasEquipmentUpdate);
	bool hasAppearanceUpdate = false;
	auto appearance = ReadAppearance(a_packet, hasAppearanceUpdate);
	bool hasPowerArmorUpdate = false;
	auto powerArmor = ReadPowerArmor(a_packet, hasPowerArmorUpdate);

	F4T::RemotePlayerState::RemotePlayerState remoteState{
		*playerId,
		static_cast<float>(*x),
		static_cast<float>(*y),
		static_cast<float>(*z),
		static_cast<float>(*angleZ),
		movementType,
		*cellId,
		worldspaceId,
		ReadDouble(a_packet, "clientTime").value_or(0.0),
		ReadDouble(a_packet, "serverTime").value_or(0.0),
		ReadBool(a_packet, "isMoving").value_or(false),
		ReadBool(a_packet, "isSprinting").value_or(false),
		ReadBool(a_packet, "isSneaking").value_or(false),
		ReadBool(a_packet, "isJumping").value_or(false),
		ReadBool(a_packet, "isCrouching").value_or(false),
		ReadBool(a_packet, "weaponDrawn").value_or(false),
		static_cast<float>(movementSpeed),
		static_cast<float>(animationGraphSpeed),
		hasEquipmentUpdate,
		std::move(equippedItems),
		hasAppearanceUpdate,
		std::move(appearance),
		std::chrono::steady_clock::now()
	};

	remoteState.actionEvents = ReadActionEvents(a_packet);
	if (const auto characterName = ReadString(a_packet, "characterName")) {
		remoteState.characterName = *characterName;
	}
	if (const auto displayName = ReadString(a_packet, "displayName")) {
		remoteState.displayName = *displayName;
	}
	remoteState.hasPowerArmorUpdate = hasPowerArmorUpdate;
	remoteState.powerArmorInFrame = powerArmor.inFrame;
	remoteState.powerArmorFrameBaseFormId = powerArmor.frameBaseFormId;
	remoteState.powerArmorPieces = std::move(powerArmor.pieces);

	const auto animationFields = F4T::ProxyRemoteAnimationState::MakeRemoteAnimationFields(
		ReadDouble(a_packet, "animationDirection"),
		ReadDouble(a_packet, "aimPitch"),
		ReadDouble(a_packet, "turnDelta"));
	remoteState.animationDirection = animationFields.animationDirection;
	remoteState.hasAnimationDirection = animationFields.hasAnimationDirection;
	remoteState.aimPitch = animationFields.aimPitch;
	remoteState.turnDelta = animationFields.turnDelta;
	remoteState.hasAimState = animationFields.hasAimState;

		F4T::RemotePlayerState::UpdateRemotePlayer(remoteState);

		if (a_packet.contains("vitals")) {
			F4T::PlayerVitals::ApplyRemoteVitalsFromPacket(*playerId, a_packet);
		}

		if (F4T::RemotePlayerState::HasRemotePlayer(*playerId) && ShouldLogRemoteUpdate(*playerId)) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Remote player {} updated: X={:.2f}, Y={:.2f}, Z={:.2f}, AngleZ={:.2f}, Cell={}, Worldspace={}",
				remoteState.playerId,
				remoteState.x,
				remoteState.y,
				remoteState.z,
				remoteState.angleZ,
				remoteState.cellId,
				remoteState.worldspaceId));
		}

	if (F4T::RemotePlayerState::HasRemotePlayer(*playerId) && ShouldLogRemoteMovementState(*playerId)) {
		LogInfoWithLocalPlayerPrefix(std::format(
			"Remote player {} movement state: moving={}, sprinting={}, sneaking={}, jumping={}, crouching={}, weaponDrawn={}, speed={:.1f}, animationDirection={}({:.1f}), aimPitch={:.1f}, turnDelta={:.1f}",
			remoteState.playerId,
			remoteState.isMoving,
			remoteState.isSprinting,
			remoteState.isSneaking,
			remoteState.isJumping,
			remoteState.isCrouching,
			remoteState.weaponDrawn,
			remoteState.movementSpeed,
			remoteState.hasAnimationDirection,
			remoteState.animationDirection,
			remoteState.aimPitch,
			remoteState.turnDelta));
	}
	}

	void HandleDisconnectPacket(const Json& a_packet)
	{
		const auto playerId = ReadPlayerId(a_packet);
		if (!playerId) {
			LogThrottledWarning("disconnect_playerId", "Ignoring disconnect packet without a valid playerId.");
			return;
		}

		ErasePacketDiagnostics(*playerId);
		F4T::LoadOrderSync::Forget(*playerId);

		if (F4T::RemotePlayerState::RemoveRemotePlayer(*playerId)) {
			LogStatusWithLocalPlayerPrefix(std::format("Remote player {} disconnected and was removed.", *playerId));
		} else {
			LogStatusWithLocalPlayerPrefix(std::format("Received disconnect for unknown remote player {}.", *playerId));
		}
	}

	void HandleSessionEndedPacket(const Json& a_packet)
	{
		const auto code = ReadString(a_packet, "code").value_or("banned");
		const auto reason = ReadString(a_packet, "reason").value_or("");

		LogWarningWithLocalPlayerPrefix(std::format(
			"Session ended by server (code={}, reason={}).",
			F4T::DebugLogging::SanitizeLogText(code, 64),
			reason.empty() ? std::string{ "<none>" } : F4T::DebugLogging::SanitizeLogText(reason)));

		F4T::Networking::DisconnectFromLocalServer();
		F4T::UIClient::ResetConnectionOwnership();
		F4T::UIClient::NotifyTransportDisconnected("Server ended the session.");

		const std::string codeCopy = code;
		const std::string reasonCopy = reason;
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarningWithLocalPlayerPrefix("Cannot show session-ended notification: task interface unavailable.");
			return;
		}

		taskInterface->AddTask([codeCopy, reasonCopy]() {
			F4T::UIClient::ShowSessionEndedNotification(codeCopy.c_str(), reasonCopy.c_str());
		});
	}

	void HandleSessionStatePacket(const Json& a_packet)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarningWithLocalPlayerPrefix("Cannot push session state: task interface unavailable.");
			return;
		}

		const auto text = a_packet.dump();
		taskInterface->AddTask([text]() {
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI6>();
			if (ui == nullptr || ui->PushSessionState == nullptr) {
				return;
			}
			ui->PushSessionState(text.c_str());
		});
	}

	void HandleUnknownPacket(const Json& a_packet)
	{
		const auto type = ReadString(a_packet, "type").value_or("<missing>");
		const auto safeType = F4T::DebugLogging::SanitizeLogText(type, 64);
		if (ShouldLogWarning("unknown_packet_type")) {
			LogWarningWithLocalPlayerPrefix(std::format("Ignoring unknown server packet type: {}", safeType));
		}
	}

	void HandleSaveAttestationResultPacket(const Json& a_packet, std::string_view a_type)
	{
		const auto saveFile = ReadString(a_packet, "saveFile");
		const auto safeSave = F4T::DebugLogging::SanitizeLogText(saveFile.value_or("<missing>"), 128);
		if (a_type == "saveAttestedRejected") {
			const auto code = ReadString(a_packet, "code");
			const auto reason = ReadString(a_packet, "reason");
			LogWarningWithLocalPlayerPrefix(std::format(
				"Server refused the save attestation for '{}' ({}): {}",
				safeSave,
				F4T::DebugLogging::SanitizeLogText(code.value_or("<missing>"), 64),
				F4T::DebugLogging::SanitizeLogText(reason.value_or("<none>"), 160)));
			return;
		}

		const auto recorded = a_packet.find("recorded");
		F4T::DebugLogging::Log(
			"SaveLineage",
			"server save attestation ack save='{}' recorded={}",
			safeSave,
			recorded != a_packet.end() && recorded->is_boolean() && recorded->get<bool>() ? "true" : "false");
	}

	void HandleCombatHitPacket(const Json& a_packet)
	{
		const auto senderId = ReadPlayerId(a_packet, "playerId");
		const auto targetPlayerId = ReadPlayerId(a_packet, "targetPlayerId");
		const auto sequence = ReadUint32(a_packet, "sequence");
		const auto damage = ReadFloat(a_packet, "damage");
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();

		if (!senderId || !targetPlayerId || !sequence || !damage || !localPlayerId) {
			LogThrottledWarning(
				"combat_hit_malformed",
				std::format(
					"Ignoring malformed combatHit packet: senderId={}, targetPlayerId={}, sequence={}, damage={}, localPlayerId={}.",
					senderId.has_value(),
					targetPlayerId.has_value(),
					sequence.has_value(),
					damage.has_value(),
					localPlayerId.has_value()));
			return;
		}

		if (!F4T::PvPCombatContract::ValidateHitRouting(
				*senderId,
				*targetPlayerId,
				*localPlayerId,
				*sequence,
				*damage)) {
			LogThrottledWarning(
				"combat_hit_invalid",
				std::format(
					"Ignoring invalid combatHit: senderId={}, targetPlayerId={}, localPlayerId={}, sequence={}, damage={}.",
					*senderId,
					*targetPlayerId,
					*localPlayerId,
					*sequence,
					*damage));
			return;
		}

		F4T::DebugLogging::Log(
			"Combat",
			"received hit senderId={} targetPlayerId={} sequence={} damage={:.1f}",
			*senderId,
			*targetPlayerId,
			*sequence,
			*damage);

		if (!F4T::RemotePlayerState::HasRemotePlayer(*senderId)) {
			LogThrottledWarning(
				"combat_hit_sender_missing",
				std::format(
					"Applying combatHit from sender {} who is not currently in the remote-player table.",
					*senderId));
		}

		if (F4T::RemotePlayerState::QueueIncomingCombatHit(*senderId, *sequence, *damage)) {
			F4T::DebugLogging::Log(
				"Combat",
				"queued hit senderId={} sequence={} damage={:.1f}",
				*senderId,
				*sequence,
				*damage);
		} else {
			F4T::DebugLogging::Log(
				"Combat",
				"dropped duplicate/out-of-order hit senderId={} sequence={} damage={:.1f}",
				*senderId,
				*sequence,
				*damage);
		}
	}

	void HandlePacketLine(const std::string& a_line)
	{
		if (a_line.empty()) {
			return;
		}

		Json packet;
		try {
			packet = Json::parse(a_line);
		} catch (const std::exception& a_error) {
			if (ShouldLogWarning("json_parse")) {
				LogWarningWithLocalPlayerPrefix(std::format("Ignoring invalid JSON packet from server: {}", a_error.what()));
			}
			return;
		}

		if (!packet.is_object()) {
			LogThrottledWarning("json_object", "Ignoring server packet that was not a JSON object.");
			return;
		}

		const auto packetType = ReadString(packet, "type");
		if (!packetType) {
			LogThrottledWarning("packet_type", "Ignoring server packet without a string type field.");
			return;
		}

		F4T::LoadOrderSync::TranslateInboundPacket(packet);

		if (F4T::Friends::TryHandlePacket(*packetType, packet)) return;
		if (F4T::Factions::TryHandlePacket(*packetType, packet)) {
			return;
		}
		if (F4T::SettlementAccessClient::TryHandlePacket(*packetType, packet)) {
			return;
		}
		if (F4T::SettlementCaptureClient::TryHandlePacket(*packetType, packet)) {
			return;
		}
		if (F4T::CharacterSlotsClient::TryHandlePacket(*packetType, packet)) {
			return;
		}
		if (F4T::Downed::TryHandlePacket(*packetType, packet)) {
			return;
		}
		if (F4T::Chat::TryHandleChatPacket(*packetType, packet)) {
			return;
		}
		if (F4T::GnsVoice::TryHandleControlPacket(*packetType, packet)) {
			return;
		}
		if (F4T::QuestInbound::HandlePacket(*packetType, packet)) {
			return;
		}

		if (*packetType == "welcome") {
			HandleWelcomePacket(packet);
		} else if (*packetType == "capabilities") {
			HandleCapabilitiesPacket(packet);
		} else if (*packetType == "sessionState") {
			HandleSessionStatePacket(packet);
		} else if (*packetType == "transform") {
			HandleTransformPacket(packet);
		} else if (*packetType == "worldState") {
			HandleWorldStatePacket(packet);
		} else if (*packetType == "npcState") {
			HandleNpcStatePacket(packet);
		} else if (*packetType == "serverWorldState") {
			HandleServerWorldStatePacket(packet);
		} else if (*packetType == "worldStateHost") {
			HandleWorldStateHostPacket(packet);
		} else if (*packetType == "disconnect") {
			HandleDisconnectPacket(packet);
		} else if (*packetType == "sessionEnded") {
			HandleSessionEndedPacket(packet);
		} else if (*packetType == "combatHit") {
			HandleCombatHitPacket(packet);
		} else if (*packetType == "saveAttestedAck" || *packetType == "saveAttestedRejected") {
			HandleSaveAttestationResultPacket(packet, *packetType);
		} else if (*packetType == "objectState" || *packetType == "objectStateSnapshot" || *packetType == "objectStateRejected") {
			HandleObjectPacket(packet);
		} else if (*packetType == "inventoryDropResult") {
			HandleInventoryResultPacket(packet, F4T::InventoryContract::OwnedOperation::Drop);
		} else if (*packetType == "inventoryPickupResult") {
			HandleInventoryResultPacket(packet, F4T::InventoryContract::OwnedOperation::Pickup);
		} else if (*packetType == "inventoryTransfer") {
			F4T::InventorySync::NoteTransferChunk(packet);
		} else if (*packetType == "npcHit") {
			HandleNpcHitPacket(packet);
		} else if (*packetType == "npcDamage") {
			HandleNpcDamagePacket(packet);
		} else if (*packetType == "npcLoot") {
			HandleNpcLootPacket(packet);
		} else if (*packetType == "npcLootTake") {
			HandleNpcLootTakePacket(packet);
		} else if (*packetType == "npcLootResult") {
			HandleNpcLootResultPacket(packet);
		} else if (*packetType == "playerAnimEvent") {
			F4T::PlayerAnimEventSync::HandleRelayedPacket(packet);
		} else if (*packetType == "projectileAim") {
			F4T::ProjectileAim::HandleRelayedAimPacket(packet);
		} else if (*packetType == "pluginList") {
			F4T::LoadOrderSync::TryHandlePacket(packet);
		} else {
			HandleUnknownPacket(packet);
		}
	}

}
