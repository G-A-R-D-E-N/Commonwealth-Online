#pragma once

#include "F4TConnectFailure.h"
#include "F4TNpcCoopHit.h"
#include "F4TNpcLootSync.h"
#include "F4TNpcSync.h"
#include "F4TPowerArmorSync.h"
#include "F4TRemotePlayerState.h"
#include "F4TRemoteNpcState.h"
#include "F4TReplicationRuntime.h"

#include <atomic>
#include <cstdint>
#include <functional>
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

	struct AdminInventoryEntry
	{
		std::uint32_t itemId{};
		std::string name;
		std::uint32_t count{};
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
		const std::string& a_joinId = {},
		bool a_backgroundValidation = false,
		std::uint64_t* a_generationOut = nullptr);
	bool ConnectAndValidateServerOnce(
		const char* a_host,
		std::uint16_t a_port,
		std::string& a_error,
		std::optional<ConnectFailure>* a_failure,
		const std::optional<std::string>& a_serverPassword,
		const std::optional<std::string>& a_modpackFingerprint,
		const std::string& a_joinId,
		bool a_joinFlow = true,
		const std::function<bool()>& a_cancelRequested = {},
		bool a_backgroundValidation = false,
		std::uint64_t* a_generationOut = nullptr);
	void DisconnectFromLocalServer();
	bool DisconnectFromLocalServerIfGeneration(std::uint64_t a_generation, const std::function<bool()>& a_permitted);
	bool IsConnectedToServer();
	std::uint64_t GetConnectionGeneration();
	bool IsLossSaveHeld();
	void SetResumeAttempt(bool a_resume);
	bool IsResumeAttempt();
	void CloseAfterLossSave(const std::atomic_bool& a_receiveRunning, void (*a_close)());
	bool TryRunForConnectionGeneration(std::uint64_t a_generation, const std::function<void()>& a_work);
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
		const F4T::PowerArmorSync::PowerArmorState* a_powerArmor,
		std::uint64_t a_generation);
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
		std::uint32_t a_weaponFormId,
		std::uint64_t a_generation);
	bool SendAdminItemResult(
		std::uint32_t a_opId,
		bool a_ok,
		std::uint32_t a_applied,
		std::string_view a_error,
		std::uint64_t a_connectionGeneration);
	bool SendInventoryReport(
		std::uint32_t a_opId,
		bool a_truncated,
		std::span<const AdminInventoryEntry> a_items,
		std::uint64_t a_connectionGeneration);
	bool SendPlayerAnimEventPacket(std::string_view a_eventName);
	bool SendFurnitureUsePacket(std::string_view a_action);
	bool SendSleepUsePacket(const nlohmann::json& a_packet);
	bool SendParkedSuitPacket(const nlohmann::json& a_packet);
	bool SendProjectileAimPacket(double a_x, double a_y, double a_z);
	bool SendExplosionSpawnPacket(std::string_view a_formKey, double a_x, double a_y, double a_z);
	bool SendObjectActivateRequest(const nlohmann::json& a_request);
	bool SendObjectLockRequest(const nlohmann::json& a_request);
	bool SendDeathBagGrantReconcile(std::optional<std::string_view> a_cursor = std::nullopt);
	bool SendDeathBagDiscover(const nlohmann::json& a_request, std::uint64_t a_generation);
	bool SendDeathBagWithdraw(const nlohmann::json& a_request, std::uint64_t a_generation);
	bool SendInventoryDropRequest(const nlohmann::json& a_request, std::uint64_t a_generation);
	bool SendInventoryPickupRequest(const nlohmann::json& a_request, std::uint64_t a_generation);
	bool SendInventorySnapshotRequest(const nlohmann::json& a_request, std::uint64_t a_generation);
	bool SendNpcHitPacket(
		const F4T::NpcCoop::NpcHitIdentity& a_identity,
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		float a_damage,
		std::uint32_t a_weaponFormId,
		std::uint64_t a_generation);
	bool SendNpcDamagePacket(
		const F4T::NpcCoop::NpcHitIdentity& a_identity,
		std::uint32_t a_targetPlayerId,
		std::uint32_t a_sequence,
		float a_damage,
		std::uint32_t a_weaponFormId,
		std::uint64_t a_connectionGeneration);
	bool SendNpcLootPacket(const F4T::NpcLootSync::RemoteLoot& a_loot, bool* a_retryable = nullptr);
	bool SendNpcLootTakePacket(const F4T::NpcLootSync::TakeRequest& a_take);
	bool SendNpcLootResultPacket(const F4T::NpcLootSync::TakeResult& a_result);
	void FlushNpcLootOutbound();
}
