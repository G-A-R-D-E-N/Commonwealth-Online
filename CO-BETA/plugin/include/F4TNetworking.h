#pragma once

#include "F4TConnectFailure.h"
#include "F4TNpcCoopHit.h"
#include "F4TNpcLootSync.h"
#include "F4TNpcSync.h"
#include "F4TPowerArmorSync.h"
#include "F4TRemotePlayerState.h"
#include "F4TRemoteNpcState.h"
#include "F4TReplicationRuntime.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace F4T::Networking
{
	struct NpcAuthorityAssignment
	{
		std::uint32_t playerId{};
		std::uint64_t epoch{};
	};

	std::string GetLocalPlayerLogPrefix();
	bool ConnectToServer(const char* a_host, std::uint16_t a_port);
	bool ConnectToLocalServer();
	bool ConnectToLocalServer(std::string& a_error);
	bool ConnectAndValidateServer(const char* a_host, std::uint16_t a_port, std::string& a_error);
	bool ConnectAndValidateServer(
		const char* a_host,
		std::uint16_t a_port,
		std::string& a_error,
		std::optional<ConnectFailure>* a_failure,
		const std::optional<std::string>& a_serverPassword,
		const std::string& a_joinId = {});
	void DisconnectFromLocalServer();
	bool IsConnectedToServer();
	std::uint64_t GetConnectionGeneration();
	void UpdateConnectionMaintenance();
	std::optional<NpcAuthorityAssignment> GetNpcAuthorityForScope(
		std::string_view a_cellId,
		std::string_view a_worldspaceId);
	bool SendTransformPacket(
		const F4T::ReplicationRuntime::PlayerTransform& a_transform,
		const F4T::ReplicationRuntime::PlayerLocation& a_location,
		const F4T::ReplicationRuntime::PlayerMovementState& a_movement,
		const char* a_movementType,
		float a_animationGraphSpeed,
		std::span<const F4T::RemotePlayerState::RemoteEquippedItem> a_equippedItems,
		const F4T::RemotePlayerState::RemoteAppearance* a_appearance,
		std::span<const F4T::RemotePlayerState::RemoteActionEvent> a_actionEvents,
		const char* a_characterName,
		const F4T::PowerArmorSync::PowerArmorState* a_powerArmor);
	bool SendWorldStatePacket(
		float a_gameHour,
		float a_gameDaysPassed,
		bool a_hasWeatherFormId,
		std::uint32_t a_weatherFormId,
		bool a_timeSync = false);
	bool SendNpcStatePacket(
		std::span<const F4T::RemoteNpcState::NpcState> a_npcs,
		std::uint64_t a_authorityEpoch,
		std::string_view a_authorityCellId,
		std::string_view a_authorityWorldspaceId);
	bool SendCombatHitPacket(
		std::uint32_t a_sequence,
		std::uint32_t a_targetPlayerId,
		float a_damage,
		std::uint32_t a_attackSequence,
		std::uint32_t a_weaponFormId = 0);
	bool SendPlayerAnimEventPacket(std::string_view a_eventName);
	bool SendProjectileAimPacket(double a_x, double a_y, double a_z);
	bool SendObjectActivateRequest(const nlohmann::json& a_request);
	bool SendObjectLockRequest(const nlohmann::json& a_request);
	bool SendInventoryDropRequest(const nlohmann::json& a_request);
	bool SendInventoryPickupRequest(const nlohmann::json& a_request);
	bool SendNpcHitPacket(
		const F4T::NpcCoop::NpcHitIdentity& a_identity,
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		float a_damage,
		std::uint32_t a_weaponFormId = 0);
	bool SendNpcDamagePacket(
		const F4T::NpcCoop::NpcHitIdentity& a_identity,
		std::uint32_t a_targetPlayerId,
		std::uint32_t a_sequence,
		float a_damage,
		std::uint32_t a_weaponFormId = 0);
	bool SendNpcLootPacket(const F4T::NpcLootSync::RemoteLoot& a_loot, bool* a_retryable = nullptr);
	bool SendNpcLootTakePacket(const F4T::NpcLootSync::TakeRequest& a_take);
	bool SendNpcLootResultPacket(const F4T::NpcLootSync::TakeResult& a_result);
	void FlushNpcLootOutbound();
}
