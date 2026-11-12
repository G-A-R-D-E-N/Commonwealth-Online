#pragma once

#include "F4TNpcSync.h"

#include "F4TNpcLootSync.h"

#include "RE/T/TESObjectREFR.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace F4T::NpcSync::Loot::Detail
{
	struct AuthorityScope
	{
		bool isHost;
		std::uint64_t epoch;
		std::string cellId;
		std::string worldspaceId;
	};

	struct QueuedTake
	{
		NpcLootSync::TakeRequest request;
		std::uint64_t receivedMs{};
	};

	struct DeferredRollback
	{
		std::uint32_t containerFormId{};
		std::uint32_t itemId{};
		std::uint32_t count{};
		bool restoreToContainer{};
		NpcLootSync::TakeContext context;
	};

	struct TimedOutTake
	{
		std::string operationId;
		std::uint32_t containerFormId{};
		std::uint32_t itemId{};
		std::uint32_t removed{};
		std::uint64_t timestampMs{};
		NpcLootSync::TakeContext context;
		std::optional<NpcLootSync::TakeResult> lateResult;
	};

	struct GrantAwaitingAck
	{
		NpcLootSync::LootIdentity identity;
		std::uint32_t itemId{};
		std::uint32_t count{};
		std::uint32_t authorityPlayerId{};
		std::uint64_t grantedMs{};
		NpcLootSync::TakeContext context;
	};

	bool EnqueueRemoteLoot(NpcLootSync::RemoteLoot a_loot);
	bool EnqueueTakeRequest(NpcLootSync::TakeRequest a_request);
	bool EnqueueTakeResult(NpcLootSync::TakeResult a_result);
	bool EnqueueTakeAck(NpcLootSync::TakeAck a_ack);
	bool EnqueueOutboundLoot(NpcLootSync::RemoteLoot a_loot);
	bool EnqueueOutboundTakeResult(NpcLootSync::TakeResult a_result);
	std::vector<NpcLootSync::RemoteLoot> DrainRemoteLoot();
	std::vector<QueuedTake> DrainTakeRequests();
	std::vector<NpcLootSync::TakeAck> DrainTakeAcks();
	std::vector<NpcLootSync::TakeResult> DrainTakeResults();
	std::vector<NpcLootSync::RemoteLoot> DrainOutboundLoot();
	std::vector<NpcLootSync::TakeResult> DrainOutboundTakeResults();
	void InitializeCapture();
	void CaptureTick(const AuthorityScope& a_scope);
	void MarkLootDirty(std::uint32_t a_containerFormId);
	void ForgetLootContainer(std::uint32_t a_containerFormId);
	void ResetCapture();
	void MirrorTick(const AuthorityScope& a_scope);
	void ResetMirror();
	void ResetQueues();
	void TakeTick(const AuthorityScope& a_scope, const std::function<void(const std::function<void()>&)>& a_runGameplay);
	void ResetTake();
	void SendTake(std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, const AuthorityScope& a_scope, const NpcLootSync::TakeContext& a_context);
	std::uint32_t RollBackTake(const NpcLootSync::TakeContext& a_context, std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, const char* a_reason, bool a_restoreToContainer);
	NpcLootSync::TakeContext CurrentTakeContext();
	void AddTimedOutTake(TimedOutTake a_entry);
	void DeferRollback(const NpcLootSync::TakeContext& a_context, std::uint32_t a_containerFormId, std::uint32_t a_itemId, std::uint32_t a_count, bool a_restoreToContainer);
	void HoldGrantUndo(const GrantAwaitingAck& a_grant);
	const std::vector<DeferredRollback>& DeferredRollbacks();
	const std::vector<TimedOutTake>& TimedOutTakes();
	const std::vector<GrantAwaitingAck>& HeldGrantUndos();
	void AddLootToContainer(RE::TESObjectREFR& a_container, std::uint32_t a_itemId, std::uint32_t a_count);
	std::uint64_t NowMilliseconds();
	std::optional<std::uint32_t> NpcIdForLootContainer(std::uint32_t a_containerFormId);
	RE::TESObjectREFR* FindLootContainer(std::uint32_t a_npcId, const AuthorityScope& a_scope);
	std::vector<NpcLootSync::LootItem> ReadContainerLoot(RE::TESObjectREFR& a_container);
	std::uint32_t CountLootItem(RE::TESObjectREFR& a_container, std::uint32_t a_itemId);
	std::uint32_t RemoveLootFromContainer(RE::TESObjectREFR& a_container, std::uint32_t a_formId, std::uint32_t a_count);
	bool IsLootableContainer(RE::TESObjectREFR& a_container);
	bool IsContainerInScope(RE::TESObjectREFR& a_container, const AuthorityScope& a_scope);
	NpcLootSync::EchoLedger& Ledger();
	std::mutex& LedgerMutex();
	NpcLootSync::RevisionCounter& Revisions();
}
