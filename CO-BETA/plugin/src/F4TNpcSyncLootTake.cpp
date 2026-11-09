#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncLootInternal.h"

#include "F4TDebugLogging.h"
#include "F4TInventoryGrant.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"
#include "F4TSaveProfileLogic.h"
#include "F4TServerProfileStore.h"
#include "F4TSessionLaunch.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
			NpcLootSync::TakeContext context;
		};

	constexpr std::size_t kMaximumPendingTakes = 64;
	constexpr std::uint64_t kGuestUnresolvedMilliseconds = 15000;
	constexpr std::size_t kMaximumDeferredRollbacks = 64;
	constexpr std::size_t kMaximumGrantsAwaitingAck = 256;
	constexpr std::uint64_t kGrantAckTimeoutMilliseconds = 20000;
	constexpr std::size_t kMaximumTimedOutTakes = 32;
	constexpr std::size_t kMaximumHeldGrantUndos = 64;

	NpcLootSync::OperationLru g_results;
	std::unordered_map<std::string, PendingTake> g_pendingTakes;
	std::vector<DeferredRollback> g_deferredRollbacks;
	std::vector<TimedOutTake> g_timedOutTakes;
	std::map<std::pair<std::uint32_t, std::string>, GrantAwaitingAck> g_grantsAwaitingAck;
	std::vector<GrantAwaitingAck> g_heldGrantUndos;
	std::uint32_t g_takeOperations{};

		std::uint32_t PublishContainerLoot(RE::TESObjectREFR& a_container, const NpcLootSync::LootIdentity& a_identity, std::uint32_t a_authorityPlayerId)
		{
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
			return revision;
		}

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
			const auto revision = PublishContainerLoot(a_container, a_identity, a_authorityPlayerId);
			return MakeTakeResult(a_operationId, a_takerPlayerId, a_itemId, removed, true, "", revision);
		}

		bool ApplyLateResult(const TimedOutTake& a_entry, const NpcLootSync::TakeResult& a_result)
		{
			const auto grant = NpcLootSync::LateGrantCount(a_entry.itemId, a_entry.removed, a_result);
			if (grant == 0) {
				return true;
			}
			const auto deferred = std::find_if(g_deferredRollbacks.begin(), g_deferredRollbacks.end(), [&](const DeferredRollback& a_rollback) {
				return a_rollback.context == a_entry.context && a_rollback.containerFormId == a_entry.containerFormId && a_rollback.itemId == a_entry.itemId &&
					a_rollback.count == a_entry.removed && !a_rollback.restoreToContainer;
			});
			if (deferred != g_deferredRollbacks.end()) {
				deferred->count -= grant;
				if (deferred->count == 0) {
					g_deferredRollbacks.erase(deferred);
				}
				F4T::DebugLogging::Log("NpcCoop", "npc loot take late accept kept item={} count={}", a_entry.itemId, grant);
				return true;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) {
				F4T::DebugLogging::Log("NpcCoop", "npc loot take late accept held item={} count={}, no player cell", a_entry.itemId, grant);
				return false;
			}
			AddLootToContainer(*player, a_entry.itemId, grant);
			F4T::DebugLogging::Log("NpcCoop", "npc loot take late accept regranted item={} count={}", a_entry.itemId, grant);
			return true;
		}

		void ReplayHeldLateResults()
		{
			if (g_timedOutTakes.empty()) {
				return;
			}
			const auto current = CurrentTakeContext();
			for (auto entry = g_timedOutTakes.begin(); entry != g_timedOutTakes.end();) {
				if (!entry->lateResult || !NpcLootSync::IsLiveTakeContext(current, entry->context) || !ApplyLateResult(*entry, *entry->lateResult)) {
					++entry;
					continue;
				}
				entry = g_timedOutTakes.erase(entry);
			}
		}

		void ResolveTimedOutTake(const NpcLootSync::TakeResult& a_result)
		{
			for (auto& timedOut : g_timedOutTakes) {
				if (timedOut.operationId == a_result.operationId) {
					timedOut.lateResult = a_result;
				}
			}
			ReplayHeldLateResults();
		}

		void ResolvePendingTake(const NpcLootSync::TakeResult& a_result, const char* a_reason)
		{
			const auto pending = g_pendingTakes.find(a_result.operationId);
			if (pending == g_pendingTakes.end()) {
				ResolveTimedOutTake(a_result);
				return;
			}
			const auto operation = pending->second;
			g_pendingTakes.erase(pending);
			const auto notGranted = !a_result.accepted && (a_result.reason.empty() || a_result.reason == "expired");
			RollBackTake(
				operation.context,
				operation.containerFormId,
				operation.itemId,
				NpcLootSync::UnmatchedTakeCount(operation.itemId, operation.count, a_result),
				a_reason,
				notGranted);
		}

		bool UndoGrant(const GrantAwaitingAck& a_grant, const AuthorityScope& a_scope)
		{
			auto* container = FindLootContainer(a_grant.identity.npcId, a_scope);
			if (!container || !F4T::NpcSync::SourceMatchesActor(a_grant.identity.sourceFormId, container->GetFormID())) {
				F4T::DebugLogging::Log("NpcCoop", "npc loot take undo skipped, container not loaded item={} count={}", a_grant.itemId, a_grant.count);
				return false;
			}
			AddLootToContainer(*container, a_grant.itemId, a_grant.count);
			PublishContainerLoot(*container, a_grant.identity, a_grant.authorityPlayerId);
			F4T::DebugLogging::Log("NpcCoop", "npc loot take undone item={} count={}", a_grant.itemId, a_grant.count);
			return true;
		}

		bool UndoPermitted(const GrantAwaitingAck& a_grant, const AuthorityScope& a_scope)
		{
			return NpcLootSync::IsLiveTakeContext(CurrentTakeContext(), a_grant.context) && a_grant.identity.authorityEpoch == a_scope.epoch;
		}

		void ReplayHeldGrantUndos(const AuthorityScope& a_scope, const std::function<void(const std::function<void()>&)>& a_runGameplay)
		{
			if (g_heldGrantUndos.empty()) {
				return;
			}
			for (auto entry = g_heldGrantUndos.begin(); entry != g_heldGrantUndos.end();) {
				if (!UndoPermitted(*entry, a_scope)) {
					++entry;
					continue;
				}
				const auto held = *entry;
				bool undone = false;
				a_runGameplay([&] { undone = UndoGrant(held, a_scope); });
				if (undone) {
					entry = g_heldGrantUndos.erase(entry);
				} else {
					++entry;
				}
			}
		}

		void ResolveGrantAck(const NpcLootSync::TakeAck& a_ack, const AuthorityScope& a_scope, const std::function<void(const std::function<void()>&)>& a_runGameplay)
		{
			const auto grant = g_grantsAwaitingAck.find(std::make_pair(a_ack.takerPlayerId, a_ack.operationId));
			if (grant == g_grantsAwaitingAck.end()) {
				return;
			}
			const auto awaiting = grant->second;
			g_grantsAwaitingAck.erase(grant);
			if (a_ack.state == NpcLootSync::TakeAckState::Expired) {
				bool undone = false;
				if (UndoPermitted(awaiting, a_scope)) {
					a_runGameplay([&] { undone = UndoGrant(awaiting, a_scope); });
				}
				if (!undone) {
					HoldGrantUndo(awaiting);
				}
			} else if (a_ack.state == NpcLootSync::TakeAckState::TakerGone) {
				F4T::DebugLogging::Log("NpcCoop", "npc loot take taker gone, kept on host item={} count={}", awaiting.itemId, awaiting.count);
			}
		}

		std::uint32_t SessionNonce()
		{
			static const std::uint32_t nonce = std::random_device{}();
			return nonce;
		}
	}

	void AddTimedOutTake(TimedOutTake a_entry)
	{
		if (g_timedOutTakes.size() >= kMaximumTimedOutTakes) {
			const auto unheld = std::find_if(g_timedOutTakes.begin(), g_timedOutTakes.end(), [](const TimedOutTake& a_timedOut) {
				return !a_timedOut.lateResult;
			});
			const auto victim = unheld != g_timedOutTakes.end() ? unheld : g_timedOutTakes.begin();
			F4T::DebugLogging::Log("NpcCoop", "npc loot timed-out take evicted at cap operation={} held={}", victim->operationId, victim->lateResult.has_value());
			g_timedOutTakes.erase(victim);
		}
		g_timedOutTakes.push_back(std::move(a_entry));
	}

	void HoldGrantUndo(const GrantAwaitingAck& a_grant)
	{
		if (g_heldGrantUndos.size() >= kMaximumHeldGrantUndos) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot held grant undo evicted at cap item={} count={}", g_heldGrantUndos.front().itemId, g_heldGrantUndos.front().count);
			g_heldGrantUndos.erase(g_heldGrantUndos.begin());
		}
		F4T::DebugLogging::Log("NpcCoop", "npc loot take undo held until its session returns item={} count={}", a_grant.itemId, a_grant.count);
		g_heldGrantUndos.push_back(a_grant);
	}

	const std::vector<DeferredRollback>& DeferredRollbacks()
	{
		return g_deferredRollbacks;
	}

	const std::vector<TimedOutTake>& TimedOutTakes()
	{
		return g_timedOutTakes;
	}

	const std::vector<GrantAwaitingAck>& HeldGrantUndos()
	{
		return g_heldGrantUndos;
	}

	NpcLootSync::TakeContext CurrentTakeContext()
	{
		if (!F4T::Networking::IsConnectedToServer() || F4T::InventoryGrant::UnattestedLoad()) {
			return {};
		}
		auto characterId = F4T::SessionLaunch::ActiveCharacterId();
		if (const auto loaded = F4T::InventoryGrant::LoadedCharacterId(); loaded && !F4T::SaveProfileLogic::CharacterIdsEqual(*loaded, characterId)) {
			return {};
		}
		return NpcLootSync::TakeContext{ F4T::SessionLaunch::ActiveServerKey(), F4T::ServerProfileStore::ConnectedWorldId(), std::move(characterId), F4T::InventoryGrant::LoadEpoch() };
	}

	void DeferRollback(const NpcLootSync::TakeContext& a_context, std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, bool a_restoreToContainer)
	{
		if (g_deferredRollbacks.size() >= kMaximumDeferredRollbacks) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot deferred rollback evicted at cap item={} count={}", g_deferredRollbacks.front().itemId, g_deferredRollbacks.front().count);
			g_deferredRollbacks.erase(g_deferredRollbacks.begin());
		}
		g_deferredRollbacks.push_back(DeferredRollback{ a_containerFormId, a_itemId, a_count, a_restoreToContainer, a_context });
	}

	std::uint32_t RollBackTake(const NpcLootSync::TakeContext& a_context, std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, const char* a_reason, bool a_restoreToContainer)
	{
		if (a_count == 0) {
			return 0;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!NpcLootSync::IsLiveTakeContext(CurrentTakeContext(), a_context) || !player || !player->GetParentCell()) {
			DeferRollback(a_context, a_containerFormId, a_itemId, a_count, a_restoreToContainer);
			F4T::DebugLogging::Log("NpcCoop", "npc loot take rollback deferred reason={} item={} count={}, session not live or no player cell", a_reason, a_itemId, a_count);
			return a_count;
		}
		const auto removed = RemoveLootFromContainer(*player, a_itemId, a_count);
		auto* form = RE::TESForm::GetFormByID(a_containerFormId);
		if (auto* container = form ? form->As<RE::TESObjectREFR>() : nullptr; container && removed != 0 && a_restoreToContainer) {
			AddLootToContainer(*container, a_itemId, removed);
		}
		F4T::DebugLogging::Log("NpcCoop", "npc loot take rolled back reason={} item={} count={} removed={}", a_reason, a_itemId, a_count, removed);
		return removed;
	}

	void AttemptDeferredRollbacks()
	{
		if (g_deferredRollbacks.empty()) {
			return;
		}
		const auto current = CurrentTakeContext();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto hasCell = player && player->GetParentCell();
		auto retry = std::exchange(g_deferredRollbacks, {});
		for (const auto& deferred : retry) {
			if (NpcLootSync::IsLiveTakeContext(current, deferred.context) && hasCell) {
				RollBackTake(deferred.context, deferred.containerFormId, deferred.itemId, deferred.count, "deferred", deferred.restoreToContainer);
			} else {
				g_deferredRollbacks.push_back(deferred);
			}
		}
	}

	void SendTake(std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, const AuthorityScope& a_scope, const NpcLootSync::TakeContext& a_context)
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
			RollBackTake(a_context, a_containerFormId, a_itemId, a_count, "refused", true);
			return;
		}
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = F4T::Networking::GetNpcAuthorityForScope(a_scope.cellId, a_scope.worldspaceId);
		if (!localPlayerId || !authority || authority->playerId == *localPlayerId || authority->epoch != a_scope.epoch ||
			authority->playerId != puppet->authorityPlayerId || authority->epoch != puppet->authorityEpoch) {
			RollBackTake(a_context, a_containerFormId, a_itemId, a_count, "refused", true);
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
			RollBackTake(a_context, a_containerFormId, a_itemId, a_count, "send failed", true);
			return;
		}
		g_pendingTakes[operationId] = PendingTake{ a_containerFormId, a_itemId, a_count, NowMilliseconds(), a_context };
	}

	void TakeTick(const AuthorityScope& a_scope, const std::function<void(const std::function<void()>&)>& a_runGameplay)
	{
		ReplayHeldGrantUndos(a_scope, a_runGameplay);
		AttemptDeferredRollbacks();
		ReplayHeldLateResults();
		for (const auto& ack : DrainTakeAcks()) {
			ResolveGrantAck(ack, a_scope, a_runGameplay);
		}
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
			if (now - entry->second.timestampMs > kGuestUnresolvedMilliseconds) {
				const auto removed = RollBackTake(entry->second.context, entry->second.containerFormId, entry->second.itemId, entry->second.count, "unresolved", false);
				if (removed != 0) {
					AddTimedOutTake(TimedOutTake{ entry->first, entry->second.containerFormId, entry->second.itemId, removed, now, entry->second.context });
				}
				entry = g_pendingTakes.erase(entry);
			} else {
				++entry;
			}
		}
		std::erase_if(g_timedOutTakes, [&](const TimedOutTake& a_entry) {
			return NpcLootSync::TimedOutTakeExpired(a_entry.lateResult.has_value(), a_entry.timestampMs, now);
		});
		if (!a_scope.isHost) {
			return;
		}
		const auto requests = DrainTakeRequests();
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = F4T::Networking::GetNpcAuthorityForScope(a_scope.cellId, a_scope.worldspaceId);
		if (localPlayerId && authority && authority->playerId == *localPlayerId && authority->epoch == a_scope.epoch &&
			a_scope.epoch != 0 && a_scope.epoch <= (std::numeric_limits<std::uint32_t>::max)()) {
			for (const auto& queued : requests) {
				const auto& [identity, takerPlayerId, operationId, itemId, requested, expiresInMs] = queued.request;
				if (const auto cached = g_results.Find(takerPlayerId, operationId)) {
					EnqueueOutboundTakeResult(*cached);
					continue;
				}
				const auto expired = NpcLootSync::TakeRequestExpired(NowMilliseconds(), queued.receivedMs, expiresInMs);
				auto result = MakeTakeResult(operationId, takerPlayerId, itemId, requested, false, expired ? "expired" : "unavailable", 0);
				if (!expired && itemId != 0 && requested != 0 && identity.npcId != 0 &&
					identity.authorityEpoch == static_cast<std::uint32_t>(a_scope.epoch) &&
					g_grantsAwaitingAck.size() < kMaximumGrantsAwaitingAck) {
					if (auto* container = FindLootContainer(identity.npcId, a_scope);
						container && F4T::NpcSync::SourceMatchesActor(identity.sourceFormId, container->GetFormID()) && IsLootableContainer(*container) &&
						IsContainerInScope(*container, a_scope)) {
						a_runGameplay([&] {
							result = GrantTake(*container, identity, takerPlayerId, operationId, itemId, requested, *localPlayerId);
						});
						if (result.accepted) {
							g_grantsAwaitingAck[std::make_pair(takerPlayerId, operationId)] = GrantAwaitingAck{ identity, itemId, result.count, *localPlayerId, NowMilliseconds(), CurrentTakeContext() };
						}
					}
				}
				g_results.Remember(takerPlayerId, operationId, result);
				EnqueueOutboundTakeResult(std::move(result));
			}
		}
		for (auto entry = g_grantsAwaitingAck.begin(); entry != g_grantsAwaitingAck.end();) {
			if (now - entry->second.grantedMs > kGrantAckTimeoutMilliseconds) {
				F4T::DebugLogging::Log("NpcCoop", "npc loot take unresolved, no ack operation={} item={} count={}", entry->first.second, entry->second.itemId, entry->second.count);
				entry = g_grantsAwaitingAck.erase(entry);
			} else {
				++entry;
			}
		}
	}

	void ResetTake()
	{
		for (const auto& result : DrainTakeResults()) {
			ResolvePendingTake(result, "unfilled");
		}
		AttemptDeferredRollbacks();
		g_results = NpcLootSync::OperationLru{};
	}
}
