#include "pch.h"

#include "F4TInventorySync.h"
#include "F4TInventoryDropGate.h"
#include "F4TInventoryPickupGate.h"
#include "F4TInventorySnapshotRead.h"

#include "F4TInventorySnapshot.h"
#include "F4TSurvivalNeedsClient.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TDropMirrorRefs.h"
#include "F4TLocalPlayerState.h"
#include "F4TRemotePlayerState.h"
#include "F4TTradeClient.h"
#include "F4TDeathBagView.h"

#include <algorithm>
#include <chrono>
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
		struct TradeCommit
		{
			std::uint32_t revision{};
			std::vector<F4T::Trade::Line> give;
			std::vector<F4T::Trade::Line> receive;
		};
		std::vector<TradeCommit> g_inboundTrades;

		struct CompletedOp
		{
			std::string operationId;
			bool committed{};
			std::optional<EntityId> entityId;
		};
		std::uint64_t g_droppedResults{};
		std::uint32_t g_sessionCounter{};
		bool g_resyncNeeded{};
		bool g_sessionActive{};
		bool g_tradeSupported{};

		constexpr std::uint64_t kSnapshotIntervalMs = 2000;
		constexpr std::uint64_t kSnapshotReplyTimeoutMs = 10000;
		struct SnapshotInFlight
		{
			std::string operationId;
			nlohmann::json items;
			std::uint64_t sentMs{};
			std::uint64_t generation{};
		};
		std::optional<SnapshotInFlight> g_snapshotInFlight;
		std::optional<nlohmann::json> g_snapshotCommitted;
		std::optional<std::uint32_t> g_snapshotCommittedRevision;
		std::uint64_t g_snapshotNextMs{};

		std::string NextSessionNonce()
		{
			const auto playerId = F4T::RemotePlayerState::GetAssignedPlayerId();
			g_sessionCounter += 1;
			return std::format("inv{}-{}", playerId.value_or(0), g_sessionCounter);
		}

		bool WelcomeListsTrade(const nlohmann::json& a_welcome)
		{
			const auto capabilities = a_welcome.find("capabilities");
			return capabilities != a_welcome.end() && capabilities->is_array() && std::any_of(capabilities->begin(), capabilities->end(), [](const nlohmann::json& a_capability) { return a_capability.is_string() && a_capability.get<std::string>() == kTradeCapability; });
		}

		bool ApplyTradeLines(ServerInventoryView& a_view, const std::vector<F4T::Trade::Line>& a_give, const std::vector<F4T::Trade::Line>& a_receive)
		{
			for (const auto& line : a_give) {
				const auto found = a_view.items.find(line.itemKey);
				if (found == a_view.items.end() || found->second.count < line.count) {
					return false;
				}
			}
			for (const auto& line : a_receive) {
				if (!F4T::InventoryContract::ParseItemKey(line.itemKey)) {
					return false;
				}
			}
			for (const auto& line : a_give) {
				auto& item = a_view.items.at(line.itemKey);
				item.count -= line.count;
				if (item.count == 0) {
					a_view.items.erase(line.itemKey);
				}
			}
			for (const auto& line : a_receive) {
				const auto parts = F4T::InventoryContract::ParseItemKey(line.itemKey);
				auto& item = a_view.items[line.itemKey];
				item.plugin = parts->plugin;
				item.localFormId = std::format("{:08X}", parts->localId);
				item.count += line.count;
			}
			return true;
		}

		void HandleLateCommit(F4T::InventoryOps::PendingOwnedOps& a_pending, const F4T::InventoryOps::LateCommitAction& a_late, std::vector<EntityId>& a_replays)
		{
			if (a_late.kind == Operation::Pickup) {
				a_replays.push_back(a_late.entityId);
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

		void SendSnapshotIfDue(std::uint64_t a_nowMs)
		{
			std::uint64_t generation{};
			{
				const std::scoped_lock lock(g_mutex);
				if (g_snapshotInFlight && g_snapshotInFlight->sentMs + kSnapshotReplyTimeoutMs <= a_nowMs) {
					g_snapshotInFlight.reset();
				}
				if (!g_sessionActive || !g_serverInventory || !g_pending || g_pending->PendingCount() != 0 ||
					g_snapshotInFlight || a_nowMs < g_snapshotNextMs) {
					return;
				}
				g_snapshotNextMs = a_nowMs + kSnapshotIntervalMs;
				generation = F4T::Networking::GetConnectionGeneration();
			}

			F4T::SurvivalNeedsClient::FlushPendingSends();
			if (F4T::DeathBagView::SnapshotHold()) {
				return;
			}
			const auto items = F4T::InventorySnapshotRead::ReadLocalItems();
			if (!items) {
				return;
			}
			const auto normalized = F4T::InventorySnapshot::NormalizeSnapshotItems(*items);
			if (!normalized) {
				LogThrottledWarning("inventory_snapshot_limit", "Skipped the owned inventory snapshot: the local items exceed the server limits.");
				return;
			}

			std::vector<nlohmann::json> wire;
			for (const auto& [key, item] : normalized->items()) {
				wire.push_back(item);
			}
			const auto parts = std::max<std::size_t>(1, (wire.size() + F4T::InventorySnapshot::kMaximumPartItems - 1) / F4T::InventorySnapshot::kMaximumPartItems);
			std::string operationId;
			std::uint32_t baseRevision{};
			{
				const std::scoped_lock lock(g_mutex);
				if (!g_sessionActive || generation != F4T::Networking::GetConnectionGeneration() || !g_pending || g_pending->PendingCount() != 0 || g_snapshotInFlight ||
					(g_snapshotCommitted && *g_snapshotCommitted == *normalized &&
					g_snapshotCommittedRevision == g_pending->ExpectedRevision())) {
					return;
				}
				operationId = std::format("snap-{}", std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
				baseRevision = g_pending->ExpectedRevision();
				g_snapshotInFlight = SnapshotInFlight{ operationId, *normalized, a_nowMs, generation };
			}
			for (std::size_t part = 0; part < parts; ++part) {
				auto slice = nlohmann::json::array();
				const auto begin = part * F4T::InventorySnapshot::kMaximumPartItems;
				const auto end = (std::min)(wire.size(), begin + F4T::InventorySnapshot::kMaximumPartItems);
				for (auto index = begin; index < end; ++index) {
					slice.push_back(wire[index]);
				}
				const nlohmann::json request{ { "type", "inventorySnapshotRequest" }, { "operationId", operationId },
					{ "baseRevision", baseRevision }, { "part", part }, { "parts", parts }, { "items", slice } };
				if (!F4T::Networking::SendInventorySnapshotRequest(request, generation)) {
					const std::scoped_lock lock(g_mutex);
					if (g_snapshotInFlight && g_snapshotInFlight->generation == generation && g_snapshotInFlight->operationId == operationId) {
						g_snapshotInFlight.reset();
					}
					LogThrottledWarning("inventory_snapshot_send_failed", "Could not send the owned inventory snapshot.");
					return;
				}
			}
		}

		void ApplyTradeCommit(F4T::InventoryOps::PendingOwnedOps& a_pending, std::uint32_t a_revision, const std::vector<F4T::Trade::Line>& a_give, const std::vector<F4T::Trade::Line>& a_receive)
		{
			const auto expected = a_pending.ExpectedRevision();
			if (a_revision != expected + 1) {
				g_resyncNeeded = true;
				LogThrottledWarning("inventory_trade_out_of_order", "A committed trade arrived out of revision order; a resync is needed.");
				return;
			}
			a_pending.SetExpectedRevision(a_revision);
			if (g_serverInventory) {
				if (!ApplyTradeLines(*g_serverInventory, a_give, a_receive)) {
					g_resyncNeeded = true;
				}
				g_serverInventory->revision = a_revision;
			}
		}

		bool DrainInbound(F4T::InventoryOps::PendingOwnedOps& a_pending, std::uint64_t a_nowMs, std::vector<CompletedOp>& a_completed, std::vector<EntityId>& a_replays)
		{
			bool tradeProcessed = false;
			while (!g_inbound.empty() || !g_inboundTrades.empty()) {
				const auto nextOwned = std::find_if(g_inbound.begin(), g_inbound.end(), [](const OwnedResult& a_result) { return a_result.revision.has_value(); });
				if (!g_inboundTrades.empty() && (nextOwned == g_inbound.end() || g_inboundTrades.front().revision <= *nextOwned->revision)) {
					const auto trade = g_inboundTrades.front();
					g_inboundTrades.erase(g_inboundTrades.begin());
					ApplyTradeCommit(a_pending, trade.revision, trade.give, trade.receive);
					tradeProcessed = true;
					continue;
				}
				const auto result = g_inbound.front();
				g_inbound.erase(g_inbound.begin());

				F4T::InventoryOps::LateCommitAction late{};
				switch (a_pending.Resolve(result, a_nowMs, &late)) {
				case ResolveOutcome::LateCommit:
					HandleLateCommit(a_pending, late, a_replays);
					break;
				case ResolveOutcome::Requeued:
					break;
				case ResolveOutcome::Commit:
					a_completed.push_back(CompletedOp{ result.operationId, true, result.entityId });
					break;
				case ResolveOutcome::Reject:
					a_completed.push_back(CompletedOp{ result.operationId, false, std::nullopt });
					LogThrottledWarning("inventory_pickup_rejected", std::format("Owned inventory rejected: reason {}.", static_cast<int>(result.reason)));
					break;
				case ResolveOutcome::StaleResync:
				case ResolveOutcome::Unknown:
				case ResolveOutcome::Duplicate:
					break;
				}
			}
			return tradeProcessed;
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
			g_tradeSupported = WelcomeListsTrade(a_welcome);
			g_resyncNeeded = false;
			g_snapshotInFlight.reset();
			g_snapshotCommitted.reset();
			g_snapshotCommittedRevision.reset();
			g_snapshotNextMs = 0;
			g_serverInventory = view;
			g_transferReceiver.Reset();
			const bool transferStarted = transfer && g_transferReceiver.Begin(*transfer);
			g_inbound.clear();
			g_inboundTrades.clear();
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
			F4T::InventoryDropGate::Complete(operationId, false, std::nullopt);
		}
		F4T::Networking::SendDeathBagGrantReconcile();
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

	void NoteSnapshotResult(const nlohmann::json& a_result)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_snapshotInFlight || !a_result.is_object() ||
			a_result.value("operationId", std::string{}) != g_snapshotInFlight->operationId) {
			return;
		}
		const auto inFlight = std::move(*g_snapshotInFlight);
		g_snapshotInFlight.reset();
		if (!a_result.contains("revision") || !a_result.at("revision").is_number_unsigned()) {
			return;
		}
		const auto revision = a_result.at("revision").get<std::uint32_t>();
		if (g_pending && revision > g_pending->ExpectedRevision()) {
			g_pending->SetExpectedRevision(revision);
		}
		if (a_result.value("status", std::string{}) != "committed") {
			return;
		}
		ServerInventoryView view;
		view.revision = revision;
		for (const auto& [key, item] : inFlight.items.items()) {
			view.items[key] = F4T::InventoryContract::ServerItem{
				item.at("plugin").get<std::string>(),
				item.at("localFormId").get<std::string>(),
				item.at("count").get<std::uint32_t>() };
		}
		g_serverInventory = std::move(view);
		g_snapshotCommitted = inFlight.items;
		g_snapshotCommittedRevision = revision;
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

	// Constraint: hold list->rwLock across the whole stack walk; source:CO-BETA/plugin/include/F4TEquipmentInstance.h (FindStackIndexed).
	bool HoldsLines(const std::vector<F4T::Trade::Line>& a_lines)
	{
		auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
		if (!player || !player->inventoryList) {
			return false;
		}
		std::map<std::string, std::uint32_t> wanted;
		for (const auto& line : a_lines) {
			wanted[line.itemKey] += line.count;
		}
		auto* list = player->inventoryList;
		for (const auto& [itemKey, count] : wanted) {
			const auto formId = F4T::DropMirrorRefs::ResolveItemFormId(itemKey);
			if (!formId) {
				return false;
			}
			std::uint32_t held = 0;
			bool instanced = false;
			{
				const RE::BSAutoReadLock lock{ list->rwLock };
				for (auto& item : list->data) {
					if (!item.object || item.object->GetFormID() != *formId) {
						continue;
					}
					for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
						if (stack->IsEquipped()) {
							continue;
						}
						instanced = instanced || (stack->extra && stack->extra->GetByType<RE::BGSObjectInstanceExtra>());
						held += static_cast<std::uint32_t>(stack->GetCount());
					}
				}
			}
			if (instanced || held < count) {
				return false;
			}
		}
		return true;
	}

	std::vector<F4T::Trade::Line> LocalHoldings()
	{
		std::vector<F4T::Trade::Line> holdings;
		auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
		if (!player || !player->inventoryList) {
			return holdings;
		}
		std::map<std::string, std::uint32_t> counts;
		std::set<std::string> instanced;
		auto* list = player->inventoryList;
		{
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (auto& item : list->data) {
				const auto itemKey = F4T::InventoryDropGate::TranslateItemKey(item.object);
				if (!itemKey) {
					continue;
				}
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					if (stack->IsEquipped()) {
						continue;
					}
					if (stack->extra && stack->extra->GetByType<RE::BGSObjectInstanceExtra>()) {
						instanced.insert(*itemKey);
						continue;
					}
					counts[*itemKey] += static_cast<std::uint32_t>(stack->GetCount());
				}
			}
		}
		for (const auto& [itemKey, count] : counts) {
			if (!instanced.contains(itemKey)) {
				holdings.push_back(F4T::Trade::Line{ itemKey, count });
			}
		}
		return holdings;
	}

	bool NoteTradeCommitted(std::uint32_t a_revision, const std::vector<F4T::Trade::Line>& a_give, const std::vector<F4T::Trade::Line>& a_receive)
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_pending || a_revision <= g_pending->ExpectedRevision()) {
			return false;
		}
		if (std::any_of(g_inboundTrades.begin(), g_inboundTrades.end(), [a_revision](const TradeCommit& a_trade) { return a_trade.revision == a_revision; })) {
			return false;
		}
		g_inboundTrades.push_back(TradeCommit{ a_revision, a_give, a_receive });
		return true;
	}

	void NoteResyncNeeded()
	{
		const std::scoped_lock lock(g_mutex);
		g_resyncNeeded = true;
	}

	bool TakeResyncNeeded()
	{
		const std::scoped_lock lock(g_mutex);
		const auto needed = g_resyncNeeded;
		g_resyncNeeded = false;
		return needed;
	}

	bool TradeSupported()
	{
		const std::scoped_lock lock(g_mutex);
		return g_tradeSupported;
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
		std::vector<CompletedOp> completed;
		std::vector<EntityId> replays;
		bool tradeProcessed{};
		std::uint64_t generation{};
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_sessionActive || !g_pending) {
				return;
			}
			generation = F4T::Networking::GetConnectionGeneration();

			tradeProcessed = DrainInbound(*g_pending, a_nowMs, completed, replays);
			const auto outcome = g_pending->Poll(a_nowMs);
			if (outcome.action == PollAction::Fail) {
				completed.push_back(CompletedOp{ outcome.operationId, false, std::nullopt });
				LogThrottledWarning("inventory_pickup_timeout", "Owned inventory operation failed closed at timeout.");
			}
			if (outcome.action == PollAction::Send || outcome.action == PollAction::Resend) {
				outbound = outcome;
			}
		}

		for (const auto entityId : replays) {
			if (!F4T::InventoryPickupGate::ReplayRetired(entityId)) {
				const std::scoped_lock lock(g_mutex);
				g_resyncNeeded = true;
			}
		}

		for (const auto& completedOp : completed) {
			F4T::InventoryPickupGate::Complete(completedOp.operationId, completedOp.committed);
			F4T::InventoryDropGate::Complete(completedOp.operationId, completedOp.committed, completedOp.entityId);
		}

		if (!outbound) {
			if (!tradeProcessed && !F4T::TradeClient::HasPendingCommits()) SendSnapshotIfDue(a_nowMs);
			return;
		}

		const bool sent = outbound->kind == Operation::Drop ?
			F4T::Networking::SendInventoryDropRequest(outbound->request, generation) :
			F4T::Networking::SendInventoryPickupRequest(outbound->request, generation);
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
			g_inboundTrades.clear();
			g_droppedResults = 0;
			g_resyncNeeded = false;
			g_sessionActive = false;
			g_snapshotInFlight.reset();
			g_snapshotCommitted.reset();
			g_snapshotCommittedRevision.reset();
			g_snapshotNextMs = 0;
			g_tradeSupported = false;
		}
		for (const auto& operationId : failed) {
			F4T::InventoryPickupGate::Complete(operationId, false);
			F4T::InventoryDropGate::Complete(operationId, false, std::nullopt);
		}
		F4T::InventoryPickupGate::ResetRetired();
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
