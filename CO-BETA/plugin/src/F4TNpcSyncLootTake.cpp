#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncLootInternal.h"

#include "F4TDebugLogging.h"
#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"

#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

namespace F4T::NpcSync::Loot::Detail
{
	namespace
	{
		struct PendingTake
		{
			std::uint32_t containerFormId{};
			std::uint32_t itemId{};
			std::uint32_t count{};
			std::uint64_t timestampMs{};
		};

		constexpr std::size_t kMaximumPendingTakes = 64;
		constexpr std::uint64_t kPendingTakeLifetimeMilliseconds = 4000;

		NpcLootSync::OperationLru g_results;
		std::unordered_map<std::string, PendingTake> g_pendingTakes;
		std::uint32_t g_takeOperations{};

		NpcLootSync::TakeResult MakeTakeResult(const std::string& a_operationId, std::uint32_t a_takerPlayerId, std::uint32_t a_itemId, std::uint32_t a_count, bool a_accepted, const char* a_reason, std::uint32_t a_revision)
		{
			return NpcLootSync::TakeResult{ a_operationId, a_accepted, a_reason, a_itemId, a_count, a_revision, a_takerPlayerId };
		}

		NpcLootSync::TakeResult GrantTake(
			RE::TESObjectREFR& a_container,
			const NpcLootSync::LootIdentity& a_identity,
			std::uint32_t a_takerPlayerId,
			const std::string& a_operationId,
			std::uint32_t a_itemId,
			std::uint32_t a_requested,
			std::uint32_t a_authorityPlayerId)
		{
			const auto granted = NpcLootSync::ClampToAvailable(a_requested, CountLootItem(a_container, a_itemId));
			if (granted == 0) {
				return MakeTakeResult(a_operationId, a_takerPlayerId, a_itemId, a_requested, false, "unavailable", 0);
			}
			const auto removed = RemoveLootFromContainer(a_container, a_itemId, granted);
			if (removed == 0) {
				return MakeTakeResult(a_operationId, a_takerPlayerId, a_itemId, a_requested, false, "unavailable", 0);
			}
			{
				const std::scoped_lock lock(LedgerMutex());
				Ledger().Expect(a_container.GetFormID(), a_itemId, removed, NowMilliseconds());
			}
			const auto revision = Revisions().Next(a_identity.npcId);
			const auto published = NpcLootSync::BuildPublishList(ReadContainerLoot(a_container));
			if (!published.truncated) {
				const auto* cell = a_container.GetParentCell();
				const auto worldspaceId = cell && cell->IsExterior() && cell->worldSpace ? cell->worldSpace->GetFormID() : 0U;
				EnqueueOutboundLoot(NpcLootSync::RemoteLoot{
					NpcLootSync::LootIdentity{
						a_identity.npcId,
						a_identity.sourceFormId,
						a_identity.cellId,
						worldspaceId,
						a_identity.authorityEpoch },
					revision,
					a_authorityPlayerId,
					published.items });
			}
			return MakeTakeResult(a_operationId, a_takerPlayerId, a_itemId, removed, true, "", revision);
		}

		std::uint32_t UnmatchedTakeCount(const PendingTake& a_operation, const NpcLootSync::TakeResult& a_result)
		{
			const auto& [operationId, accepted, reason, itemId, count, revision, takerPlayerId] = a_result;
			if (!accepted || itemId != a_operation.itemId) {
				return a_operation.count;
			}
			return a_operation.count - NpcLootSync::ClampToAvailable(a_operation.count, count);
		}

		void ResolvePendingTake(const NpcLootSync::TakeResult& a_result, const char* a_reason)
		{
			const auto pending = g_pendingTakes.find(a_result.operationId);
			if (pending == g_pendingTakes.end()) {
				return;
			}
			const auto operation = pending->second;
			g_pendingTakes.erase(pending);
			RollBackTake(operation.containerFormId, operation.itemId, UnmatchedTakeCount(operation, a_result), a_reason, false);
		}

		std::uint32_t SessionNonce()
		{
			static const std::uint32_t nonce = std::random_device{}();
			return nonce;
		}
	}

	void RollBackTake(std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, const char* a_reason, bool a_restoreToContainer)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (a_count == 0 || !player) {
			return;
		}
		if (!player->GetParentCell()) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot take rollback skipped reason={} item={} count={} player has no cell", a_reason, a_itemId, a_count);
			return;
		}
		const auto removed = RemoveLootFromContainer(*player, a_itemId, a_count);
		auto* form = RE::TESForm::GetFormByID(a_containerFormId);
		if (auto* container = form ? form->As<RE::TESObjectREFR>() : nullptr; container && removed != 0 && a_restoreToContainer) {
			AddLootToContainer(*container, a_itemId, removed);
		}
		F4T::DebugLogging::Log("NpcCoop", "npc loot take rolled back reason={} item={} count={} removed={}", a_reason, a_itemId, a_count, removed);
	}

	void SendTake(std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, const AuthorityScope& a_scope)
	{
		const auto puppet = TryGetPuppetByActorFormId(a_containerFormId);
		auto* form = RE::TESForm::GetFormByID(a_containerFormId);
		auto* container = form ? form->As<RE::TESObjectREFR>() : nullptr;
		if (!puppet || !container || !IsLootableContainer(*container) || !IsContainerInScope(*container, a_scope) ||
			a_itemId == 0 || a_count == 0) {
			return;
		}
		if (g_pendingTakes.size() >= kMaximumPendingTakes || a_scope.epoch == 0 ||
			a_scope.epoch > (std::numeric_limits<std::uint32_t>::max)()) {
			RollBackTake(a_containerFormId, a_itemId, a_count, "refused", true);
			return;
		}
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = F4T::Networking::GetNpcAuthorityForScope(a_scope.cellId, a_scope.worldspaceId);
		if (!localPlayerId || !authority || authority->playerId == *localPlayerId || authority->epoch != a_scope.epoch) {
			RollBackTake(a_containerFormId, a_itemId, a_count, "refused", true);
			return;
		}
		const auto* cell = container->GetParentCell();
		const auto worldspaceId = cell->IsExterior() && cell->worldSpace ? cell->worldSpace->GetFormID() : 0U;
		const auto operationId = std::format("{:08X}-{}", SessionNonce(), NpcLootSync::MakeOperationId(*localPlayerId, ++g_takeOperations));
		const NpcLootSync::TakeRequest request{
			NpcLootSync::LootIdentity{ puppet->npcId, puppet->sourceFormId, cell->GetFormID(), worldspaceId, static_cast<std::uint32_t>(a_scope.epoch) },
			*localPlayerId,
			operationId,
			a_itemId,
			a_count };
		if (!F4T::Networking::SendNpcLootTakePacket(request)) {
			RollBackTake(a_containerFormId, a_itemId, a_count, "send failed", true);
			return;
		}
		g_pendingTakes[operationId] = PendingTake{ a_containerFormId, a_itemId, a_count, NowMilliseconds() };
	}

	void TakeTick(const AuthorityScope& a_scope)
	{
		if (!a_scope.isHost) {
			for (const auto& result : DrainTakeResults()) {
				F4T::DebugLogging::Log(
					"NpcCoop",
					"npc loot take {} operation={} revision={}",
					result.accepted ? "accepted" : "rejected",
					result.operationId,
					result.revision);
				ResolvePendingTake(result, "unfilled");
			}
			const auto now = NowMilliseconds();
			for (auto entry = g_pendingTakes.begin(); entry != g_pendingTakes.end();) {
				if (now - entry->second.timestampMs > kPendingTakeLifetimeMilliseconds) {
					RollBackTake(entry->second.containerFormId, entry->second.itemId, entry->second.count, "expired", true);
					entry = g_pendingTakes.erase(entry);
				} else {
					++entry;
				}
			}
			return;
		}
		const auto requests = DrainTakeRequests();
		if (requests.empty() || a_scope.epoch == 0 ||
			a_scope.epoch > (std::numeric_limits<std::uint32_t>::max)()) {
			return;
		}
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = F4T::Networking::GetNpcAuthorityForScope(a_scope.cellId, a_scope.worldspaceId);
		if (!localPlayerId || !authority || authority->playerId != *localPlayerId || authority->epoch != a_scope.epoch) {
			return;
		}
		for (const auto& request : requests) {
			const auto& [identity, takerPlayerId, operationId, itemId, requested] = request;
			if (const auto cached = g_results.Find(takerPlayerId, operationId)) {
				EnqueueOutboundTakeResult(*cached);
				continue;
			}
			auto result = MakeTakeResult(operationId, takerPlayerId, itemId, requested, false, "unavailable", 0);
			if (itemId != 0 && requested != 0 && identity.npcId != 0 && identity.sourceFormId != 0 &&
				identity.authorityEpoch == static_cast<std::uint32_t>(a_scope.epoch)) {
				if (auto* container = FindLootContainer(identity.npcId);
					container && container->GetFormID() == identity.sourceFormId && IsLootableContainer(*container) &&
					IsContainerInScope(*container, a_scope)) {
					result = GrantTake(*container, identity, takerPlayerId, operationId, itemId, requested, *localPlayerId);
				}
			}
			g_results.Remember(takerPlayerId, operationId, result);
			EnqueueOutboundTakeResult(std::move(result));
		}
	}

	void ResetTake()
	{
		for (const auto& result : DrainTakeResults()) {
			ResolvePendingTake(result, "unfilled");
		}
		for (const auto& entry : g_pendingTakes) {
			RollBackTake(entry.second.containerFormId, entry.second.itemId, entry.second.count, "reset", true);
		}
		g_pendingTakes.clear();
		g_results = NpcLootSync::OperationLru{};
	}
}
