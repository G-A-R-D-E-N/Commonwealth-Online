#include "F4TInventoryOperations.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace F4T::InventoryOps
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<Json> ItemObject(const std::string& a_key, std::uint32_t a_count)
		{
			const auto parts = F4T::InventoryContract::ParseItemKey(a_key);
			if (!parts) return std::nullopt;
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08X", parts->localId);
			return Json{ { "plugin", parts->plugin }, { "localFormId", form }, { "count", (std::min)(a_count, F4T::Drops::kMaximumDropCount) } };
		}

		std::uint32_t ClampCount(std::uint32_t a_count)
		{
			return (std::min)(a_count, F4T::Drops::kMaximumDropCount);
		}

		std::uint32_t AccumulateCount(std::uint32_t a_current, std::uint32_t a_add)
		{
			const auto total = static_cast<std::uint64_t>(a_current) + static_cast<std::uint64_t>(a_add);
			return static_cast<std::uint32_t>((std::min)(total, static_cast<std::uint64_t>(F4T::Drops::kMaximumDropCount)));
		}
	}

	PendingOwnedOps::PendingOwnedOps(std::string a_sessionNonce) :
		nonce(std::move(a_sessionNonce))
	{
	}

	std::optional<std::string> PendingOwnedOps::Begin(const OwnedRequest& a_request)
	{
		if (pending.size() >= kMaximumPendingOperations) return std::nullopt;
		if (a_request.kind == Operation::Drop) {
			if (!F4T::InventoryContract::ParseItemKey(a_request.itemKey) || a_request.count == 0 || a_request.count > F4T::Drops::kMaximumDropCount) return std::nullopt;
		} else if (a_request.kind == Operation::Pickup) {
			if (a_request.entityId < F4T::Drops::kFirstDroppedEntityId) return std::nullopt;
		} else {
			return std::nullopt;
		}
		const auto operation = F4T::InventoryContract::MakeOperationId(nonce, counter + 1);
		if (!operation) return std::nullopt;
		counter = counter + 1;
		Pending entry;
		entry.operationId = *operation;
		entry.request = a_request;
		pending.push_back(std::move(entry));
		return operation;
	}

	PollOutcome PendingOwnedOps::Poll(std::uint64_t a_nowMs)
	{
		PollOutcome outcome;
		if (pending.empty()) return outcome;
		auto& head = pending.front();
		outcome.kind = head.request.kind;
		outcome.operationId = head.operationId;
		if (head.everSent && a_nowMs >= head.firstSentMs + kOperationTimeoutMs) {
			Remember(head.operationId, FailedState::FailedSent, head.request);
			pending.erase(pending.begin());
			outcome.action = PollAction::Fail;
			return outcome;
		}
		if (!head.hasSent) {
			if (a_nowMs < head.notBeforeMs) return outcome;
			const auto request = BuildRequest(head);
			if (!request) {
				Remember(head.operationId, FailedState::FailedUnsent, head.request);
				pending.erase(pending.begin());
				outcome.action = PollAction::Fail;
				return outcome;
			}
			head.sent = *request;
			head.hasSent = true;
			head.sentAtMs = a_nowMs;
			head.sentRevision = expectedRevision;
			if (!head.everSent) {
				head.everSent = true;
				head.firstSentMs = a_nowMs;
			}
			head.firstResendDone = false;
			head.secondResendDone = false;
			head.notBeforeMs = 0;
			outcome.action = PollAction::Send;
			outcome.request = head.sent;
			return outcome;
		}
		if (!head.secondResendDone && a_nowMs >= head.sentAtMs + kSecondResendMs) {
			head.firstResendDone = true;
			head.secondResendDone = true;
			outcome.action = PollAction::Resend;
			outcome.request = head.sent;
			return outcome;
		}
		if (!head.firstResendDone && a_nowMs >= head.sentAtMs + kFirstResendMs) {
			head.firstResendDone = true;
			outcome.action = PollAction::Resend;
			outcome.request = head.sent;
			return outcome;
		}
		return outcome;
	}

	ResolveOutcome PendingOwnedOps::Resolve(const OwnedResult& a_result, std::uint64_t a_nowMs, LateCommitAction* a_late)
	{
		if (a_result.operationId.empty()) return ResolveOutcome::Unknown;
		if (pending.empty() || !pending.front().hasSent || pending.front().operationId != a_result.operationId) {
			auto* entry = FindHistory(a_result.operationId);
			if (!entry) return ResolveOutcome::Unknown;
			if (entry->state != FailedState::FailedSent || !a_result.committed || !a_result.revision) return ResolveOutcome::Duplicate;
			entry->state = FailedState::Committed;
			AdvanceRevision(*a_result.revision);
			if (a_late) {
				a_late->kind = entry->request.kind;
				a_late->entityId = a_result.entityId.value_or(entry->request.entityId);
				a_late->itemKey = entry->request.itemKey;
				a_late->count = entry->request.count;
			}
			return ResolveOutcome::LateCommit;
		}
		auto& head = pending.front();
		if (a_result.committed) {
			if (!a_result.revision) return ResolveOutcome::Unknown;
			AdvanceRevision(*a_result.revision);
			Remember(head.operationId, FailedState::Committed, head.request);
			pending.erase(pending.begin());
			return ResolveOutcome::Commit;
		}
		if (a_result.reason == F4T::InventoryContract::OwnedReason::StaleRevision) {
			if (!a_result.revision) {
				Remember(head.operationId, FailedState::FailedSent, head.request);
				pending.erase(pending.begin());
				return ResolveOutcome::Reject;
			}
			if (*a_result.revision <= head.sentRevision) return ResolveOutcome::Unknown;
			AdvanceRevision(*a_result.revision);
			if (head.staleResyncs >= kMaximumStaleResyncs) {
				Remember(head.operationId, FailedState::FailedSent, head.request);
				pending.erase(pending.begin());
				return ResolveOutcome::Reject;
			}
			head.staleResyncs += 1;
			Rearm(head, 0);
			return ResolveOutcome::StaleResync;
		}
		if (a_result.reason == F4T::InventoryContract::OwnedReason::RateLimited) {
			Rearm(head, a_nowMs + kRateLimitedRequeueMs);
			return ResolveOutcome::Requeued;
		}
		Remember(head.operationId, FailedState::FailedSent, head.request);
		pending.erase(pending.begin());
		return ResolveOutcome::Reject;
	}

	std::vector<std::string> PendingOwnedOps::Reset()
	{
		std::vector<std::string> failed;
		failed.reserve(pending.size());
		for (const auto& entry : pending) {
			failed.push_back(entry.operationId);
			Remember(entry.operationId, entry.everSent ? FailedState::FailedSent : FailedState::FailedUnsent, entry.request);
		}
		pending.clear();
		return failed;
	}

	void PendingOwnedOps::SetExpectedRevision(std::uint32_t a_revision) noexcept
	{
		expectedRevision = a_revision;
	}

	std::uint32_t PendingOwnedOps::ExpectedRevision() const noexcept
	{
		return expectedRevision;
	}

	std::size_t PendingOwnedOps::PendingCount() const noexcept
	{
		return pending.size();
	}

	bool PendingOwnedOps::InFlight() const noexcept
	{
		return !pending.empty() && pending.front().hasSent;
	}

	std::optional<nlohmann::json> PendingOwnedOps::BuildRequest(const Pending& a_pending) const
	{
		if (a_pending.request.kind == Operation::Drop) {
			return F4T::InventoryContract::BuildDropRequest(a_pending.operationId, expectedRevision, a_pending.request.itemKey, a_pending.request.count);
		}
		return F4T::InventoryContract::BuildPickupRequest(a_pending.operationId, expectedRevision, a_pending.request.entityId);
	}

	void PendingOwnedOps::Rearm(Pending& a_pending, std::uint64_t a_notBeforeMs) noexcept
	{
		a_pending.sent = Json{};
		a_pending.hasSent = false;
		a_pending.sentAtMs = 0;
		a_pending.firstResendDone = false;
		a_pending.secondResendDone = false;
		a_pending.notBeforeMs = a_notBeforeMs;
	}

	PendingOwnedOps::History* PendingOwnedOps::FindHistory(const std::string& a_operationId)
	{
		const auto found = std::find_if(history.begin(), history.end(), [&a_operationId](const History& a_entry) {
			return a_entry.operationId == a_operationId;
		});
		return found == history.end() ? nullptr : &*found;
	}

	void PendingOwnedOps::Remember(const std::string& a_operationId, FailedState a_state, const OwnedRequest& a_request)
	{
		history.push_back(History{ a_operationId, a_state, a_request });
		if (history.size() > kMaximumResolvedOperations) history.erase(history.begin());
	}

	void PendingOwnedOps::AdvanceRevision(std::uint32_t a_revision) noexcept
	{
		if (a_revision > expectedRevision) expectedRevision = a_revision;
	}

	bool DropEntityBindings::ReserveForLocalOp(EntityId a_entityId)
	{
		if (entries.contains(a_entityId)) return true;
		if (entries.size() >= kMaximumBoundEntities) return false;
		entries.emplace(a_entityId, Entry{});
		return true;
	}

	bool DropEntityBindings::Bind(EntityId a_entityId, const std::string& a_refKey)
	{
		if (a_refKey.empty()) return false;
		auto found = entries.find(a_entityId);
		if (found != entries.end() && found->second.bound) return found->second.refKey == a_refKey;
		const auto owner = byRef.find(a_refKey);
		if (owner != byRef.end() && owner->second != a_entityId) return false;
		if (found == entries.end()) {
			if (entries.size() >= kMaximumBoundEntities) return false;
			found = entries.emplace(a_entityId, Entry{}).first;
		}
		found->second.refKey = a_refKey;
		found->second.bound = true;
		byRef[a_refKey] = a_entityId;
		return true;
	}

	bool DropEntityBindings::Unbind(EntityId a_entityId)
	{
		const auto found = entries.find(a_entityId);
		if (found == entries.end()) return false;
		if (found->second.bound) byRef.erase(found->second.refKey);
		entries.erase(found);
		return true;
	}

	std::optional<EntityId> DropEntityBindings::FindEntity(const std::string& a_refKey) const
	{
		if (a_refKey.empty()) return std::nullopt;
		const auto found = byRef.find(a_refKey);
		if (found == byRef.end()) return std::nullopt;
		return found->second;
	}

	bool DropEntityBindings::ShouldMaterialize(EntityId a_entityId) const
	{
		return !entries.contains(a_entityId);
	}

	bool DropEntityBindings::IsReserved(EntityId a_entityId) const
	{
		const auto found = entries.find(a_entityId);
		return found != entries.end() && !found->second.bound;
	}

	bool DropEntityBindings::IsBound(EntityId a_entityId) const
	{
		const auto found = entries.find(a_entityId);
		return found != entries.end() && found->second.bound;
	}

	std::size_t DropEntityBindings::TrackedCount() const noexcept
	{
		return entries.size();
	}

	DropDecision ClassifyDrop(const DropContext& a_context, const DropPolicy& a_policy)
	{
		if (!a_context.itemKey || a_context.itemKey->empty()) return { DropVerdict::PassThrough, "runtimeForm" };
		if (!F4T::InventoryContract::ParseItemKey(*a_context.itemKey)) return { DropVerdict::Block, "invalidItemKey" };
		if (a_policy.excludeBooksAndNotes) {
			if (a_context.formKind == FormKind::Book) return { DropVerdict::PassThrough, "book" };
			if (a_context.formKind == FormKind::Note) return { DropVerdict::PassThrough, "note" };
		}
		if (a_context.hasInstanceData && a_policy.blockInstanceData) return { DropVerdict::Block, "instanceData" };
		if (a_context.origin == DropOrigin::CompanionApp && !a_policy.gateCompanionApp) return { DropVerdict::PassThrough, "companionAppDisabled" };
		if (a_context.count == 0 || a_context.count > F4T::Drops::kMaximumDropCount) return { DropVerdict::Block, "countOutOfRange" };
		if (a_context.viewCount && a_context.count > *a_context.viewCount) return { DropVerdict::Block, "countExceedsView" };
		return { DropVerdict::Gate, "gate" };
	}

	PickupDecision ClassifyPickup(const std::optional<std::string>& a_refKey, const DropEntityBindings& a_bindings)
	{
		if (a_refKey && a_bindings.FindEntity(*a_refKey)) return { PickupVerdict::Gate, "boundMirror" };
		return { PickupVerdict::PassThrough, "notBound" };
	}

	std::vector<nlohmann::json> BuildInventorySnapshot(const std::vector<InventoryCount>& a_engineCounts)
	{
		std::map<std::string, std::uint32_t> counts;
		for (const auto& entry : a_engineCounts) {
			if (entry.itemKey.empty()) continue;
			counts[entry.itemKey] = AccumulateCount(counts[entry.itemKey], entry.count);
		}
		std::vector<std::pair<std::string, Json>> items;
		for (const auto& [key, count] : counts) {
			if (count == 0) continue;
			const auto item = ItemObject(key, count);
			if (!item) continue;
			items.emplace_back(key, *item);
		}
		const auto chunkCount = (std::max)(std::size_t{ 1 }, (items.size() + kMaximumSnapshotEntriesPerChunk - 1) / kMaximumSnapshotEntriesPerChunk);
		std::vector<Json> packets;
		packets.reserve(chunkCount);
		for (std::size_t index = 0; index < chunkCount; ++index) {
			const auto begin = index * kMaximumSnapshotEntriesPerChunk;
			const auto end = (std::min)(begin + kMaximumSnapshotEntriesPerChunk, items.size());
			Json object = Json::object();
			for (auto position = begin; position < end; ++position) {
				object[items[position].first] = items[position].second;
			}
			packets.push_back(Json{ { "type", "inventorySnapshot" }, { "chunkIndex", static_cast<std::uint32_t>(index) },
				{ "chunkCount", static_cast<std::uint32_t>(chunkCount) }, { "lastChunk", index + 1 == chunkCount }, { "items", std::move(object) } });
		}
		return packets;
	}

	std::vector<nlohmann::json> BuildInventoryDelta(std::uint32_t a_sequence, const std::vector<InventoryChange>& a_changes)
	{
		std::vector<Json> changes;
		changes.reserve(a_changes.size());
		for (const auto& change : a_changes) {
			if (!F4T::InventoryContract::ParseItemKey(change.itemKey)) continue;
			changes.push_back(Json{ { "itemKey", change.itemKey }, { "count", ClampCount(change.count) } });
		}
		if (changes.empty()) return {};
		const auto chunkCount = (changes.size() + kMaximumDeltaChangesPerChunk - 1) / kMaximumDeltaChangesPerChunk;
		std::vector<Json> packets;
		packets.reserve(chunkCount);
		for (std::size_t index = 0; index < chunkCount; ++index) {
			const auto begin = index * kMaximumDeltaChangesPerChunk;
			const auto end = (std::min)(begin + kMaximumDeltaChangesPerChunk, changes.size());
			Json chunk = Json::array();
			for (auto position = begin; position < end; ++position) {
				chunk.push_back(changes[position]);
			}
			packets.push_back(Json{ { "type", "inventoryDelta" }, { "seq", a_sequence + static_cast<std::uint32_t>(index) },
				{ "chunkIndex", static_cast<std::uint32_t>(index) }, { "chunkCount", static_cast<std::uint32_t>(chunkCount) },
				{ "lastChunk", index + 1 == chunkCount }, { "changes", std::move(chunk) } });
		}
		return packets;
	}

	std::vector<InventoryChange> DiffSnapshot(const CountMap& a_mirror, const std::vector<InventoryCount>& a_engineCounts)
	{
		CountMap engine;
		for (const auto& entry : a_engineCounts) {
			if (!F4T::InventoryContract::ParseItemKey(entry.itemKey)) continue;
			engine[entry.itemKey] = AccumulateCount(engine[entry.itemKey], entry.count);
		}
		std::map<std::string, bool> keys;
		for (const auto& entry : a_mirror) {
			if (!F4T::InventoryContract::ParseItemKey(entry.first)) continue;
			keys[entry.first] = true;
		}
		for (const auto& entry : engine) {
			keys[entry.first] = true;
		}
		std::vector<InventoryChange> changes;
		for (const auto& key : keys) {
			const auto mirror = a_mirror.find(key.first);
			const auto mirrorCount = mirror == a_mirror.end() ? 0U : ClampCount(mirror->second);
			const auto found = engine.find(key.first);
			const auto engineCount = found == engine.end() ? 0U : found->second;
			if (mirrorCount != engineCount) changes.push_back(InventoryChange{ key.first, engineCount });
		}
		return changes;
	}
}
