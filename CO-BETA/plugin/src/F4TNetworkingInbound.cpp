#include "pch.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TAdminItems.h"
#include "F4TPlayerProgression.h"
#include "F4TNetworkingInboundWorldState.h"
#include "F4TChat.h"
#include "F4TChatFactionTag.h"
#include "F4TDownedGlue.h"
#include "F4TDownedState.h"
#include "F4TConsoleGuard.h"
#include "F4TFactions.h"
#include "F4TFriends.h"
#include "F4TMapPresence.h"
#include "F4TReport.h"
#include "F4TGnsVoice.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TSettlementRaidClient.h"
#include "F4TWorkshopReplicationClient.h"
#include "F4TStateBagSync.h"
#include "F4TCharacterSlotsClient.h"
#include "F4TTradeClient.h"
#include "F4TInventoryContract.h"
#include "F4TInventoryGrant.h"
#include "F4TInventorySync.h"
#include "F4TLoadOrderSync.h"
#include "F4TObjectSync.h"
#include "F4TParkedSuitSync.h"
#include "F4TPlayerKey.h"
#include "F4TFriendlyFireState.h"
#include "F4TSteamJoinHold.h"
#include "F4TSteamTunnelService.h"
#include "F4TSurvivalLogPolicy.h"
#include "F4TSurvivalNeedsClient.h"
#include "F4TSurvivalPolicyClient.h"
#include "F4TPvPCombatContract.h"
#include "F4TQuestInbound.h"
#include "F4TQuestSync.h"
#include "F4TConnectionPolicy.h"
#include "F4TDebugLogging.h"
#include "F4TNetworkValidation.h"
#include "F4TReceivePolicy.h"
#include "F4TFurnitureSeatSync.h"
#include "F4TSleepRemote.h"
#include "F4TPlayerAnimEventSync.h"
#include "F4TExplosionSync.h"
#include "F4TProjectileAim.h"
#include "F4TPlayerVitals.h"
#include "F4TProxyRemoteAnimationState.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TServerProfileStore.h"
#include "F4TClientIdentity.h"
#include "F4TSaveLineageSession.h"
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

#include "F4TDeathBagGrant.h"
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
		F4T::SurvivalPolicy::g_clientState.Welcome(F4T::Networking::GetConnectionGeneration(), ReadBool(a_packet, "survivalMode"));
		if (const auto required = ReadUint32(a_packet, "requiredDifficulty")) {
			if (*required <= 6) F4T::SurvivalPolicy::g_clientState.SetRequiredDifficulty(F4T::Networking::GetConnectionGeneration(), static_cast<std::int32_t>(*required));
		}
		F4T::SurvivalPolicy::g_needsEnabled = ReadBool(a_packet, "survivalNeeds").value_or(false);
		F4T::SurvivalNeedsClient::PushHudState();
		REX::INFO("[Survival] welcome survivalMode={} survivalNeeds={} playerId={} serverInstanceId={}",
			F4T::SurvivalLogPolicy::FlagText(ReadBool(a_packet, "survivalMode")),
			F4T::SurvivalLogPolicy::FlagText(ReadBool(a_packet, "survivalNeeds")), *playerId,
			F4T::DebugLogging::SanitizeLogText(ReadString(a_packet, "serverInstanceId").value_or(""), 64));

		F4T::ServerProfileStore::SetConnectedWorldId(ReadString(a_packet, "serverInstanceId").value_or(""));
		if (const auto selected = ReadString(a_packet, "persistenceProfile")) {
			(void)F4T::SaveLineageSession::RequestFirstJoinRecovery(F4T::ClientIdentity::ConnectedServerKey(), ReadString(a_packet, "serverInstanceId").value_or(""), *selected, F4T::Networking::GetConnectionGeneration());
		}
		F4T::CharacterSlotsClient::NoteWelcome(a_packet);
		if (const auto recovery = a_packet.find("startingInventoryRecovery"); recovery != a_packet.end()) F4T::InventoryGrant::NoteRecoveryWelcome(*recovery);
		F4T::RemotePlayerState::SetAssignedPlayerResumed(ReadBool(a_packet, "resumed").value_or(false));
		F4T::RemotePlayerState::SetAssignedPlayerId(*playerId);
		F4T::LoadOrderSync::NoteWelcome();
		F4T::Factions::NoteWelcome(a_packet);
		F4T::SettlementRaidClient::NoteCapabilities(a_packet);
		F4T::SettlementRaidClient::NoteServerTime(a_packet);
		F4T::Chat::NoteWelcome(a_packet);
		F4T::InventorySync::NoteWelcome(a_packet);
		F4T::ObjectSync::NoteWelcome(a_packet);
		F4T::GnsVoice::NoteWelcome(a_packet);
		F4T::QuestSync::NoteWelcome(a_packet);
		F4T::ConsoleGuard::NoteWelcome(a_packet);
		F4T::Downed::NoteWelcome();
		const auto cost = a_packet.find("stimpaksPerRevive");
		F4T::Downed::SetReviveStimpakCost(cost != a_packet.end() && cost->is_number_integer() && cost->get<std::int64_t>() >= 0 ? cost->get<std::int64_t>() : 1);

		const auto worldStateHostPlayerId = ReadPlayerId(a_packet, "worldStateHostPlayerId");
		if (worldStateHostPlayerId) {
			F4T::WorldStateSync::QueueWorldStateHostPlayerId(*worldStateHostPlayerId);
		}

		LogStatusWithLocalPlayerPrefix(std::format("Assigned server playerId: {}", *playerId));
	}

	void HandleCapabilitiesPacket(const Json& a_packet)
	{
		static F4T::SurvivalLogPolicy::Throttle capabilitiesLog{ 10.0 };
		if (a_packet.contains("survivalMode")) {
			F4T::SurvivalPolicy::g_clientState.Update(F4T::Networking::GetConnectionGeneration(), ReadBool(a_packet, "survivalMode"));
		}
		if (const auto required = ReadUint32(a_packet, "requiredDifficulty")) {
			if (*required <= 6) F4T::SurvivalPolicy::g_clientState.SetRequiredDifficulty(F4T::Networking::GetConnectionGeneration(), static_cast<std::int32_t>(*required));
		}
		if (a_packet.contains("survivalNeeds")) {
			F4T::SurvivalPolicy::g_needsEnabled = ReadBool(a_packet, "survivalNeeds").value_or(false);
			F4T::SurvivalNeedsClient::PushHudState();
		}
		if (a_packet.contains("survivalMode") || a_packet.contains("survivalNeeds")) {
			if (capabilitiesLog.Due(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count())) REX::INFO("[Survival] capabilities survivalMode={} survivalNeeds={}",
				F4T::SurvivalLogPolicy::FlagText(ReadBool(a_packet, "survivalMode")),
				F4T::SurvivalLogPolicy::FlagText(ReadBool(a_packet, "survivalNeeds")));
		}
		F4T::Factions::NoteWelcome(a_packet);
		F4T::SettlementRaidClient::NoteCapabilities(a_packet);
		F4T::Chat::NoteWelcome(a_packet);
		F4T::ObjectSync::NoteCapabilities(a_packet);
		F4T::FriendlyFireState::NoteWelcome(a_packet);
		if (const auto name = ReadString(a_packet, "serverName"); name && F4T::Steam::HostRunning()) {
			F4T::Steam::AnnounceHostWhenOpen(*name, 0);
		}
		if (const auto limit = ReadMemberLimit(a_packet); limit && F4T::Steam::HostRunning()) {
			F4T::Steam::SetHostMemberLimit(*limit);
		}
	}

	void HandleNpcStatePacket(const Json& a_packet)
	{
		const auto hostPlayerId = ReadPlayerId(a_packet);
		const auto authorityCell = ReadString(a_packet, "authorityCellId");
		const auto authorityWorld = ReadString(a_packet, "authorityWorldspaceId");
		const auto epoch = ReadUint32(a_packet, "authorityEpoch");
		const auto npcs = a_packet.find("npcs");
		if (!hostPlayerId || !authorityCell || !authorityWorld || !epoch || *epoch == 0 ||
			!F4T::NetworkValidation::IsValidHexFormId(*authorityCell, false, false) ||
			!F4T::NetworkValidation::IsValidHexFormId(*authorityWorld, true, true) ||
			npcs == a_packet.end() || !npcs->is_array() || npcs->size() > 16) {
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
			const auto runtimeNpc = ReadBool(npc, "runtimeNpc").value_or(false);
			if (!baseFormId || *baseFormId == 0 || !sourceFormId ||
				F4T::RemoteNpcState::IsRuntimeFormId(*baseFormId) ||
				!F4T::NetworkValidation::IsValidHexFormId(*baseFormIdText, false, false) ||
				!F4T::NetworkValidation::IsValidNpcSourceFormId(*sourceFormIdText, runtimeNpc)) {
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

		F4T::RemoteNpcState::ReplaceSnapshot({*authorityCell, *authorityWorld}, *hostPlayerId, *epoch, std::move(states));
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

		if (F4T::RemotePlayerState::IsAssignedPlayerId(*playerId)) { return; }

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
	if (!equippedItems) {
		return;
	}
	bool hasAppearanceUpdate = false;
	auto appearance = ReadAppearance(a_packet, hasAppearanceUpdate);
	if (!appearance) {
		return;
	}
	bool hasPowerArmorUpdate = false;
	auto powerArmor = ReadPowerArmor(a_packet, hasPowerArmorUpdate);
	bool hasPipboyLightUpdate = false;
	const auto pipboyLight = ReadPipboyLight(a_packet, hasPipboyLightUpdate);

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
		std::move(*equippedItems),
		hasAppearanceUpdate,
		std::move(*appearance),
		std::chrono::steady_clock::now()
	};

	remoteState.actionEvents = ReadActionEvents(a_packet);
	if (const auto characterName = ReadString(a_packet, "characterName")) {
		remoteState.characterName = *characterName;
	}
	if (const auto displayName = ReadString(a_packet, "displayName")) {
		remoteState.displayName = *displayName;
		remoteState.hasFactionTagUpdate = true;
		remoteState.factionTag = F4T::ChatFactionTag::Sanitize(ReadString(a_packet, "factionTag").value_or(""));
	}
	if (const auto playerKey = ReadString(a_packet, "playerKey"); playerKey && F4T::PlayerKey::IsPlayerKey(*playerKey)) {
		remoteState.playerKey = *playerKey;
	}
	remoteState.hasPowerArmorUpdate = hasPowerArmorUpdate;
	remoteState.powerArmorInFrame = powerArmor.inFrame;
	remoteState.powerArmorFrameBaseFormId = powerArmor.frameBaseFormId;
	remoteState.powerArmorPieces = std::move(powerArmor.pieces);
	remoteState.hasPipboyLightUpdate = hasPipboyLightUpdate;
	remoteState.pipboyLightOn = pipboyLight.on;
	remoteState.pipboyLightColor = pipboyLight.color;

	const auto animationFields = F4T::ProxyRemoteAnimationState::MakeRemoteAnimationFields(
		ReadDouble(a_packet, "animationDirection"),
		ReadDouble(a_packet, "aimPitch"),
		ReadDouble(a_packet, "turnDelta"));
	remoteState.animationDirection = animationFields.animationDirection;
	remoteState.hasAnimationDirection = animationFields.hasAnimationDirection;
	remoteState.aimPitch = animationFields.aimPitch;
	remoteState.turnDelta = animationFields.turnDelta;
	remoteState.hasAimState = animationFields.hasAimState;
		if (const auto downed = a_packet.find("serverDowned"); downed != a_packet.end() && downed->is_object()) {
			const auto incarnation = a_packet.find("playerIncarnation");
			const auto revision = downed->find("revision");
			const auto isDowned = ReadBool(*downed, "downed");
			const auto isDead = ReadBool(*downed, "dead");
			if (incarnation != a_packet.end() && incarnation->is_number_unsigned() && revision != downed->end() &&
				revision->is_number_unsigned() && isDowned && isDead) {
				F4T::RemotePlayerState::SetRemotePlayerAuthoritativeDowned(*playerId, incarnation->get<std::uint64_t>(), revision->get<std::uint64_t>(), *isDowned, *isDead);
			}
		}

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

	void HandlePeerHiddenPacket(const Json& a_packet)
	{
		const auto playerId = ReadPlayerId(a_packet);
		if (!playerId) {
			LogThrottledWarning("peer_hidden_playerId", "Ignoring peerHidden packet without a valid playerId.");
			return;
		}

		if (F4T::RemotePlayerState::RemoveRemotePlayer(*playerId)) {
			LogStatusWithLocalPlayerPrefix(std::format("Remote player {} left shared space; proxy hidden until it is back in range.", *playerId));
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

		const auto disconnectReason = reason.empty() ? std::string{ "Server ended the session." } : reason;
		F4T::Networking::DisconnectFromLocalServer();
		F4T::UIClient::ResetConnectionOwnership();
		F4T::UIClient::NotifyTransportDisconnected(disconnectReason.c_str());

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
		if (!F4T::ConnectionPolicy::ServerMessageWithinTypeLimit(*packetType, a_line.size())) {
			LogThrottledWarning("packet_oversized", std::format("Dropping oversized server packet of type {} ({} bytes).", F4T::DebugLogging::SanitizeLogText(*packetType, 64), a_line.size()));
			return;
		}

		F4T::LoadOrderSync::TranslateInboundPacket(packet);

		if (F4T::Friends::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::Report::TryHandlePacket(*packetType, packet) || F4T::DeathBagGrant::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::Factions::TryHandlePacket(*packetType, packet) || F4T::Admin::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::SettlementAccessClient::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::SettlementCaptureClient::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::SettlementRaidClient::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::WorkshopReplicationClient::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::CharacterSlotsClient::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::Downed::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::Chat::TryHandleChatPacket(*packetType, packet)) { return; }
		if (F4T::GnsVoice::TryHandleControlPacket(*packetType, packet)) { return; }
		if (F4T::QuestInbound::HandlePacket(*packetType, packet)) { return; }
		if (F4T::StateBagSync::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::SurvivalNeedsClient::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::ParkedSuitSync::TryHandlePacket(*packetType, packet)) { return; }
		if (F4T::ExplosionSync::TryHandlePacket(*packetType, packet)) { return; }

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
		} else if (*packetType == "peerHidden") {
			HandlePeerHiddenPacket(packet);
		} else if (*packetType == "sessionEnded") {
			HandleSessionEndedPacket(packet);
		} else if (*packetType == "combatHit") {
			HandleCombatHitPacket(packet);
		} else if (*packetType == "saveAttestedAck" || *packetType == "saveAttestedRejected") {
			HandleSaveAttestationResultPacket(packet, *packetType);
		} else if (*packetType == "progressionRejected") {
			F4T::PlayerProgression::HandleRejectedPacket(packet);
		} else if (*packetType == "objectState" || *packetType == "objectStateSnapshot" || *packetType == "objectStateRejected") {
			HandleObjectPacket(packet);
		} else if (*packetType == "inventoryDropResult" || *packetType == "inventoryPickupResult") {
			HandleInventoryResultPacket(packet, *packetType == "inventoryDropResult" ? F4T::InventoryContract::OwnedOperation::Drop : F4T::InventoryContract::OwnedOperation::Pickup);
		} else if (*packetType == "inventoryTransfer") {
			F4T::InventorySync::NoteTransferChunk(packet);
		} else if (*packetType == "inventorySnapshotResult") {
			F4T::InventorySync::NoteSnapshotResult(packet);
		} else if (*packetType == "inventoryContainerResult") {
			F4T::InventorySync::NoteContainerResult(packet);
		} else if (*packetType == "startingInventoryGrant") {
			F4T::InventoryGrant::NoteStartingGrant(packet);
		} else if (*packetType == "startingInventoryRecovery") {
			F4T::InventoryGrant::NoteRecovery(packet);
		} else if (*packetType == "saveRecoveryOffer") {
			(void)F4T::SaveLineageSession::HandleFirstJoinRecoveryOffer(packet);
		} else if (*packetType == "saveRecoveryConsumed") {
			(void)F4T::SaveLineageSession::HandleFirstJoinRecoveryConsumed(packet);
		} else if (*packetType == "saveRecoveryRejected") {
			(void)F4T::SaveLineageSession::HandleFirstJoinRecoveryRejected();
		} else if (*packetType == "startingInventoryVerification") {
			F4T::InventoryGrant::NoteVerification(packet);
		} else if (*packetType == "tradeState" || *packetType == "tradeResult" || packetType->rfind("vendor", 0) == 0) {
			F4T::TradeClient::HandlePacket(packet);
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
		} else if (*packetType == "npcLootAck") {
			HandleNpcLootAckPacket(packet);
		} else if (*packetType == "playerAnimEvent") {
			F4T::PlayerAnimEventSync::HandleRelayedPacket(packet);
			} else if (*packetType == "furnitureUse") {
				F4T::FurnitureSeatSync::HandleRelayedPacket(packet);
		} else if (*packetType == "sleepUse") {
			F4T::SleepRemote::HandlePacket(packet);
		} else if (*packetType == "projectileAim") {
			F4T::ProjectileAim::HandleRelayedAimPacket(packet);
		} else if (*packetType == "pluginList") {
			F4T::LoadOrderSync::TryHandlePacket(packet);
		} else if (*packetType == "mapPresence") {
			F4T::MapPresence::HandlePacket(packet);
		} else if (*packetType == "adminItemOp") {
			HandleAdminItemOpPacket(packet);
		} else if (*packetType == "adminInventoryRequest") {
			HandleAdminInventoryRequestPacket(packet);
		} else {
			HandleUnknownPacket(packet);
		}
	}
}
