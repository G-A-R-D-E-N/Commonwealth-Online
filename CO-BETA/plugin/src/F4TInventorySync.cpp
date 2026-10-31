#include "pch.h"

#include "F4TInventorySync.h"
#include "F4TInventoryPickupGate.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::InventorySync
{
	using namespace F4T::Networking::Detail;
	using Operation = F4T::InventoryContract::OwnedOperation;
	using PollAction = F4T::InventoryOps::PollAction;
	using ResolveOutcome = F4T::InventoryOps::ResolveOutcome;

	namespace
	{
		std::mutex g_mutex;
		std::optional<ServerInventoryView> g_serverInventory;
		F4T::InventoryContract::InventoryTransferReceiver g_transferReceiver;
		std::optional<F4T::InventoryOps::PendingOwnedOps> g_pending;
		F4T::InventoryOps::DropEntityBindings g_bindings;
		F4T::Drops::DropMirrorTable g_dropMirror;
		std::map<EntityId, std::uint32_t> g_mirrorOwned;
		std::set<EntityId> g_resetRemoved;
		std::uint64_t g_mirrorGeneration{};
		std::vector<OwnedResult> g_inbound;
		std::uint64_t g_droppedResults{};
		std::uint32_t g_sessionCounter{};
		bool g_resyncNeeded{};
		bool g_sessionActive{};

		std::string NextSessionNonce()
		{
			const auto playerId = F4T::RemotePlayerState::GetAssignedPlayerId();
			g_sessionCounter += 1;
			return std::format("inv{}-{}", playerId.value_or(0), g_sessionCounter);
		}

		void HandleLateCommit(F4T::InventoryOps::PendingOwnedOps& a_pending, const F4T::InventoryOps::LateCommitAction& a_late)
		{
			if (a_late.kind == Operation::Pickup) {
				g_resyncNeeded = true;
				return;
			}

			F4T::InventoryOps::OwnedRequest request;
			request.kind = Operation::Pickup;
			request.entityId = a_late.entityId;
			request.count = a_late.count;
			if (a_pending.Begin(request)) {
				return;
			}

			g_resyncNeeded = true;
			LogThrottledWarning(
				"inventory_late_commit_uncompensated",
				"Could not queue a compensating pickup for a late-committed inventory drop.");
		}

		void DrainInbound(F4T::InventoryOps::PendingOwnedOps& a_pending, std::uint64_t a_nowMs, std::vector<std::pair<std::string, bool>>& a_completed)
		{
			while (!g_inbound.empty()) {
				const auto result = g_inbound.front();
				g_inbound.erase(g_inbound.begin());

				F4T::InventoryOps::LateCommitAction late{};
				switch (a_pending.Resolve(result, a_nowMs, &late)) {
				case ResolveOutcome::LateCommit:
					HandleLateCommit(a_pending, late);
					break;
				case ResolveOutcome::Requeued:
					break;
				case ResolveOutcome::Commit:
					a_completed.emplace_back(result.operationId, true);
					break;
				case ResolveOutcome::Reject:
					a_completed.emplace_back(result.operationId, false);
					LogThrottledWarning("inventory_pickup_rejected", std::format("Owned inventory rejected: reason {}.", static_cast<int>(result.reason)));
					break;
				case ResolveOutcome::StaleResync:
				case ResolveOutcome::Unknown:
				case ResolveOutcome::Duplicate:
					break;
				}
			}
		}
	}

	void NoteWelcome(const nlohmann::json& a_welcome)
	{
		std::optional<ServerInventoryView> view;
		std::optional<F4T::InventoryContract::InventoryTransferHeader> transfer;
		if (const auto inventory = a_welcome.find("serverInventory"); inventory != a_welcome.end()) {
			view = F4T::InventoryContract::ParseServerInventory(*inventory);
			if (!view) {
				transfer = F4T::InventoryContract::ParseInventoryTransferHeader(*inventory);
			}
		}

		std::vector<std::string> failed;
		{
			const std::scoped_lock lock(g_mutex);
			if (g_pending) {
				failed = g_pending->Reset();
			}
			g_sessionActive = true;
			g_resyncNeeded = false;
			g_serverInventory = view;
			g_transferReceiver.Reset();
			const bool transferStarted = transfer && g_transferReceiver.Begin(*transfer);
			g_inbound.clear();
			g_droppedResults = 0;
			g_pending.emplace(NextSessionNonce());
			if (view) {
				g_pending->SetExpectedRevision(view->revision);
			} else if (transferStarted) {
				g_pending->SetExpectedRevision(transfer->revision);
			}
		}
		for (const auto& operationId : failed) {
			F4T::InventoryPickupGate::Complete(operationId, false);
		}
	}

	void NoteTransferChunk(const nlohmann::json& a_chunk)
	{
		std::optional<std::string> applied;
		std::optional<std::string> failure;
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_sessionActive || !g_transferReceiver.Active()) {
				failure = "Ignored an inventory transfer chunk without a pending transfer.";
			} else {
				nlohmann::json inventory;
				const auto result = g_transferReceiver.Accept(a_chunk, inventory);
				if (result == F4T::InventoryContract::InventoryTransferReceiver::Result::Rejected) {
					failure = "Rejected the server inventory transfer; the session continues without a server inventory view.";
				} else if (result == F4T::InventoryContract::InventoryTransferReceiver::Result::Complete) {
					if (const auto completed = F4T::InventoryContract::ParseServerInventory(inventory)) {
						g_serverInventory = completed;
						if (g_pending) {
							g_pending->SetExpectedRevision(completed->revision);
						}
						applied = std::format(
							"Applied the server inventory: {} item(s) at revision {}.",
							completed->items.size(),
							completed->revision);
					} else {
						failure = "The reassembled server inventory was invalid; the session continues without a server inventory view.";
					}
				}
			}
		}
		if (applied) {
			LogInfoWithLocalPlayerPrefix(*applied);
		} else if (failure) {
			LogThrottledWarning("inventory_transfer", *failure);
		}
	}

	void NoteOwnedResult(const OwnedResult& a_result)
	{
		const std::scoped_lock lock(g_mutex);
		if (g_inbound.size() >= kMaximumInboundResults) {
			g_droppedResults += 1;
			return;
		}

		g_inbound.push_back(a_result);
	}

	std::optional<std::string> BeginOwnedDrop(const std::string& a_itemKey, std::uint32_t a_count)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_pending) {
			return std::nullopt;
		}

		F4T::InventoryOps::OwnedRequest request;
		request.kind = Operation::Drop;
		request.itemKey = a_itemKey;
		request.count = a_count;
		return g_pending->Begin(request);
	}

	std::optional<std::string> BeginOwnedPickup(EntityId a_entityId)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_pending) {
			return std::nullopt;
		}

		F4T::InventoryOps::OwnedRequest request;
		request.kind = Operation::Pickup;
		request.entityId = a_entityId;
		return g_pending->Begin(request);
	}

	bool ReserveOwnedEntity(EntityId a_entityId)
	{
		const std::scoped_lock lock(g_mutex);
		return g_bindings.ReserveForLocalOp(a_entityId);
	}

	bool BindOwnedEntity(EntityId a_entityId, const std::string& a_refKey)
	{
		const std::scoped_lock lock(g_mutex);
		return g_bindings.Bind(a_entityId, a_refKey);
	}

	bool UnbindOwnedEntity(EntityId a_entityId)
	{
		const std::scoped_lock lock(g_mutex);
		return g_bindings.Unbind(a_entityId);
	}

	std::optional<EntityId> FindOwnedEntity(const std::string& a_refKey)
	{
		const std::scoped_lock lock(g_mutex);
		return g_bindings.FindEntity(a_refKey);
	}

	void BindMirrorEntity(EntityId a_entityId, std::uint32_t a_refFormId)
	{
		const std::scoped_lock lock(g_mutex);
		g_mirrorOwned[a_entityId] = a_refFormId;
	}

	void UnbindMirrorEntity(EntityId a_entityId)
	{
		const std::scoped_lock lock(g_mutex);
		g_mirrorOwned.erase(a_entityId);
	}

	bool ApplyDropFrame(const F4T::Replication::DecodedFrame& a_frame)
	{
		const std::scoped_lock lock(g_mutex);
		return g_dropMirror.ApplyFrame(a_frame) != F4T::Drops::MirrorApply::Rejected;
	}

	F4T::Drops::MirrorDiff TakeDropMirrorDiff(std::uint64_t& a_generation)
	{
		const std::scoped_lock lock(g_mutex);
		a_generation = g_mirrorGeneration;
		auto diff = g_dropMirror.TakeDiff();
		diff.removed.insert(diff.removed.end(), g_resetRemoved.begin(), g_resetRemoved.end());
		g_resetRemoved.clear();
		for (auto* entries : { &diff.added, &diff.changed }) {
			for (auto& entry : *entries) {
				entry.materialize = g_bindings.ShouldMaterialize(entry.entityId);
			}
		}
		return diff;
	}

	void Tick(std::uint64_t a_nowMs)
	{
		std::optional<F4T::InventoryOps::PollOutcome> outbound;
		std::vector<std::pair<std::string, bool>> completed;
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_sessionActive || !g_pending) {
				return;
			}

			DrainInbound(*g_pending, a_nowMs, completed);
			const auto outcome = g_pending->Poll(a_nowMs);
			if (outcome.action == PollAction::Fail) {
				completed.emplace_back(outcome.operationId, false);
				LogThrottledWarning("inventory_pickup_timeout", "Owned inventory operation failed closed at timeout.");
			}
			if (outcome.action == PollAction::Send || outcome.action == PollAction::Resend) {
				outbound = outcome;
			}
		}

		for (const auto& [operationId, committed] : completed) {
			F4T::InventoryPickupGate::Complete(operationId, committed);
		}

		if (!outbound) {
			return;
		}

		const bool sent = outbound->kind == Operation::Drop ?
			F4T::Networking::SendInventoryDropRequest(outbound->request) :
			F4T::Networking::SendInventoryPickupRequest(outbound->request);
		if (!sent) {
			LogThrottledWarning("inventory_send_failed", "Could not send a Commonwealth Online owned inventory request.");
		}
	}

	void Reset()
	{
		std::vector<std::string> failed;
		{
			const std::scoped_lock lock(g_mutex);
			if (g_pending) {
				failed = g_pending->Reset();
				g_pending.reset();
			}
			g_serverInventory.reset();
			g_transferReceiver.Reset();
			g_bindings = F4T::InventoryOps::DropEntityBindings{};
			for (const auto& [entityId, refFormId] : g_mirrorOwned) {
				g_resetRemoved.insert(entityId);
			}
			g_mirrorOwned.clear();
			++g_mirrorGeneration;
			g_dropMirror.Clear();
			g_inbound.clear();
			g_droppedResults = 0;
			g_resyncNeeded = false;
			g_sessionActive = false;
		}
		for (const auto& operationId : failed) {
			F4T::InventoryPickupGate::Complete(operationId, false);
		}
		if (!failed.empty()) {
			LogThrottledWarning(
				"inventory_reset_failed_ops",
				std::format("Failed {} pending owned inventory operation(s) on disconnect.", failed.size()));
		}
	}

	Status GetStatus()
	{
		const std::scoped_lock lock(g_mutex);
		Status status;
		status.sessionActive = g_sessionActive;
		status.hasServerInventory = g_serverInventory.has_value();
		status.serverRevision = g_serverInventory ? g_serverInventory->revision : 0;
		status.pendingOperations = g_pending ? g_pending->PendingCount() : 0;
		status.queuedResults = g_inbound.size();
		status.droppedResults = g_droppedResults;
		status.resyncNeeded = g_resyncNeeded;
		status.trackedEntities = g_bindings.TrackedCount();
		status.mirroredEntities = g_dropMirror.Size();
		return status;
	}

	std::optional<ServerInventoryView> ServerInventory()
	{
		const std::scoped_lock lock(g_mutex);
		return g_serverInventory;
	}
}
