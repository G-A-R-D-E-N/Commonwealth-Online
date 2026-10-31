#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcLootSync.h"

#include "F4TNetworkValidation.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TNpcSyncLootInternal.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace F4T::Networking
{
	using namespace Detail;

	namespace
	{
		constexpr std::size_t kMaximumTakeOperationIdLength = 64;
		constexpr std::size_t kMaximumTakeReasonLength = 64;

		bool IsValidLootEpoch(std::uint64_t a_epoch)
		{
			return a_epoch != 0 && a_epoch <= (std::numeric_limits<std::uint32_t>::max)();
		}

		std::string FormatScopeFormId(std::uint32_t a_formId)
		{
			return std::format("{:08X}", a_formId);
		}

		std::string FormatScopeWorldspaceId(std::uint32_t a_worldspaceFormId)
		{
			return a_worldspaceFormId == 0 ? std::string{} : FormatScopeFormId(a_worldspaceFormId);
		}

		bool IsValidLootScope(const std::string& a_cellId, const std::string& a_worldspaceId)
		{
			return F4T::NetworkValidation::IsValidHexFormId(a_cellId, false, false) &&
			       F4T::NetworkValidation::IsValidHexFormId(a_worldspaceId, true, true);
		}
	}

	bool SendNpcLootPacket(const F4T::NpcLootSync::RemoteLoot& a_loot, bool* a_retryable)
	{
		if (!IsConnectedToServer()) {
			if (a_retryable) {
				*a_retryable = true;
			}
			return false;
		}
		const auto& identity = a_loot.identity;
		const auto cellId = FormatScopeFormId(identity.cellId);
		const auto worldspaceId = FormatScopeWorldspaceId(identity.worldspaceId);
		if (identity.npcId == 0 || identity.sourceFormId == 0 ||
			F4T::RemoteNpcState::IsRuntimeFormId(identity.sourceFormId) ||
			a_loot.revision == 0 || a_loot.authorityPlayerId == 0 ||
			!IsValidLootEpoch(identity.authorityEpoch) || !IsValidLootScope(cellId, worldspaceId)) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not send invalid npcLoot: npcId={}, revision={}, items={}.",
				identity.npcId,
				a_loot.revision,
				a_loot.items.size()));
			return false;
		}

		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = GetNpcAuthorityForScope(cellId, worldspaceId);
		if (!localPlayerId || !authority || authority->playerId != *localPlayerId ||
			authority->epoch != identity.authorityEpoch) {
			return false;
		}

		if (!SendPacketBytes(F4T::NpcLootSync::RemoteLootToJson(a_loot).dump())) {
			LogThrottledWarning("npc_loot_send_failed", "Could not send Commonwealth Online npcLoot packet.");
			if (a_retryable) {
				*a_retryable = true;
			}
			return false;
		}
		return true;
	}

	bool SendNpcLootTakePacket(const F4T::NpcLootSync::TakeRequest& a_take)
	{
		if (!IsConnectedToServer()) {
			return false;
		}
		const auto& [identity, takerPlayerId, operationId, itemId, count] = a_take;
		const auto cellId = FormatScopeFormId(identity.cellId);
		const auto worldspaceId = FormatScopeWorldspaceId(identity.worldspaceId);
		if (identity.npcId == 0 || identity.sourceFormId == 0 ||
			F4T::RemoteNpcState::IsRuntimeFormId(identity.sourceFormId) ||
			takerPlayerId == 0 || operationId.empty() || operationId.size() > kMaximumTakeOperationIdLength ||
			itemId == 0 || itemId >= F4T::NpcLootSync::RuntimeFormIdBase ||
			count == 0 || count > F4T::NpcLootSync::MaximumLootCount ||
			!IsValidLootEpoch(identity.authorityEpoch) || !IsValidLootScope(cellId, worldspaceId)) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not send invalid npcLootTake: npcId={}, operation={}, count={}.",
				identity.npcId,
				operationId,
				count));
			return false;
		}

		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = GetNpcAuthorityForScope(cellId, worldspaceId);
		if (!localPlayerId || !authority || authority->playerId == *localPlayerId ||
			authority->epoch != identity.authorityEpoch) {
			return false;
		}

		if (!SendPacketBytes(F4T::NpcLootSync::TakeRequestToJson(a_take).dump())) {
			LogThrottledWarning("npc_loot_take_send_failed", "Could not send Commonwealth Online npcLootTake packet.");
			return false;
		}
		return true;
	}

	bool SendNpcLootResultPacket(const F4T::NpcLootSync::TakeResult& a_result)
	{
		if (!IsConnectedToServer()) {
			return false;
		}
		const auto& [operationId, accepted, reason, itemId, count, revision, takerPlayerId] = a_result;
		if (operationId.empty() || operationId.size() > kMaximumTakeOperationIdLength ||
			takerPlayerId == 0 || reason.size() > kMaximumTakeReasonLength ||
			itemId == 0 || itemId >= F4T::NpcLootSync::RuntimeFormIdBase ||
			count == 0 || count > F4T::NpcLootSync::MaximumLootCount ||
			(accepted && revision == 0)) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not send invalid npcLootResult: operation={}, count={}.",
				operationId,
				count));
			return false;
		}

		if (!SendPacketBytes(F4T::NpcLootSync::TakeResultToJson(a_result).dump())) {
			LogThrottledWarning("npc_loot_result_send_failed", "Could not send Commonwealth Online npcLootResult packet.");
			return false;
		}
		return true;
	}

	void FlushNpcLootOutbound()
	{
		for (const auto& loot : F4T::NpcSync::Loot::Detail::DrainOutboundLoot()) {
			bool retryable = false;
			if (!SendNpcLootPacket(loot, &retryable) && retryable) {
				F4T::NpcSync::Loot::Detail::MarkLootDirty(loot.identity.sourceFormId);
			}
		}
		for (const auto& result : F4T::NpcSync::Loot::Detail::DrainOutboundTakeResults()) {
			SendNpcLootResultPacket(result);
		}
	}
}

namespace F4T::Networking::Detail
{
	void HandleNpcLootPacket(const Json& a_packet)
	{
		auto loot = F4T::NpcLootSync::ParseRemoteLoot(a_packet);
		if (!loot) {
			LogThrottledWarning("npc_loot_malformed", "Ignoring malformed Commonwealth Online npcLoot packet.");
			return;
		}
		if (!F4T::NpcSync::Loot::Detail::EnqueueRemoteLoot(std::move(*loot))) {
			LogThrottledWarning("npc_loot_queue_full", "Dropping Commonwealth Online npcLoot packet: the inbound queue is full.");
		}
	}

	void HandleNpcLootTakePacket(const Json& a_packet)
	{
		auto take = F4T::NpcLootSync::ParseTakeRequest(a_packet);
		if (!take) {
			LogThrottledWarning("npc_loot_take_malformed", "Ignoring malformed Commonwealth Online npcLootTake packet.");
			return;
		}
		if (!F4T::NpcSync::Loot::Detail::EnqueueTakeRequest(std::move(*take))) {
			LogThrottledWarning("npc_loot_take_queue_full", "Dropping Commonwealth Online npcLootTake packet: the inbound queue is full.");
		}
	}

	void HandleNpcLootResultPacket(const Json& a_packet)
	{
		auto result = F4T::NpcLootSync::ParseTakeResult(a_packet);
		if (!result) {
			LogThrottledWarning("npc_loot_result_malformed", "Ignoring malformed Commonwealth Online npcLootResult packet.");
			return;
		}
		if (!F4T::NpcSync::Loot::Detail::EnqueueTakeResult(std::move(*result))) {
			LogThrottledWarning("npc_loot_result_queue_full", "Dropping Commonwealth Online npcLootResult packet: the inbound queue is full.");
		}
	}
}
