#include "pch.h"

#include "F4TVendorTradeClient.h"

#include "F4TLocalPlayerState.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TNpcSyncLootInternal.h"
#include "F4TReplicationRuntime.h"
#include "F4TTradeWindow.h"
#include "F4TVendorTrade.h"

#include "RE/B/BarterMenu.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESFullName.h"
#include "RE/T/TESValueForm.h"
#include "RE/U/UI.h"
#include "RE/U/UIMessageQueue.h"
#include "RE/U/UI_MESSAGE_TYPE.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::VendorTradeClient
{
	namespace
	{
		using Json = nlohmann::json;
		namespace VT = F4T::VendorTrade;
		using F4T::Networking::Detail::LogThrottledWarning;
		using F4T::Networking::Detail::SendPacketBytes;
		using F4T::NpcSync::Loot::Detail::AddLootToContainer;
		using F4T::NpcSync::Loot::Detail::CountLootItem;
		using F4T::NpcSync::Loot::Detail::ReadContainerLoot;
		using F4T::NpcSync::Loot::Detail::RemoveLootFromContainer;

		constexpr std::uint32_t kCapsItemId = 0x0000000F;
		constexpr std::size_t kMaximumInbox = 64;
		constexpr std::size_t kMaximumCartLines = 64;
		constexpr std::string_view kBarterMenuName = "BarterMenu";
		constexpr auto kResendDelay = std::chrono::seconds{ 5 };
		constexpr std::uint32_t kMaximumSnapshotQueries = 6;
		constexpr std::string_view kTraderUnavailable = "The trader is unavailable.";

		struct Escrow
		{
			std::string operationId;
			std::vector<VT::StockLine> taken;
			std::uint32_t capsTaken{};
			std::vector<VT::StockLine> grant;
			std::uint32_t capsGrant{};
			Json packet;
			std::optional<std::chrono::steady_clock::time_point> nextResend;
		};

		struct Payout
		{
			Escrow escrow;
			bool grant{};
		};

		struct Session
		{
			bool active{};
			std::string tradeId;
			std::string name;
			VT::Identity identity;
			std::uint32_t chestId{};
			std::optional<VT::StockSnapshot> snapshot;
			std::string status;
			bool clearCart{};
			bool completed{};
			std::string pendingOperation;
			std::uint32_t snapshotQueries{};
			std::chrono::steady_clock::time_point nextSnapshotQuery{};
		};

		struct OpenRequest
		{
			std::uint32_t chestId{};
			std::uint32_t actorId{};
		};

		std::mutex g_mutex;
		Session g_session;
		std::optional<OpenRequest> g_open;
		std::deque<Json> g_inbox;
		std::map<std::string, Escrow> g_unresolved;
		std::vector<Payout> g_payouts;
		std::uint64_t g_operationCounter{};
		std::uint64_t g_sessionCounter{};
		F4T::ReplicationRuntime::Generation g_generation{};
		std::atomic_bool g_sinkRegistered{ false };

		bool SendPacket(const Json& a_packet)
		{
			return F4T::Networking::IsConnectedToServer() && SendPacketBytes(a_packet.dump());
		}

		std::uint32_t BaseValue(std::uint32_t a_formId)
		{
			const auto* form = RE::TESForm::GetFormByID(a_formId);
			const auto* valueForm = form ? form->As<RE::TESValueForm>() : nullptr;
			return valueForm ? static_cast<std::uint32_t>(std::max(0, valueForm->GetFormValue())) : 0U;
		}

		std::uint32_t BuyPrice(std::uint32_t a_formId)
		{
			return std::max<std::uint32_t>(1, BaseValue(a_formId));
		}

		std::uint32_t SellPrice(std::uint32_t a_formId)
		{
			return std::max<std::uint32_t>(1, BaseValue(a_formId) / 2);
		}

		std::string ItemName(std::uint32_t a_formId)
		{
			if (const auto* form = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_formId)) {
				const auto fullName = RE::TESFullName::GetFullName(*form);
				if (!fullName.empty()) {
					return std::string{ fullName };
				}
			}
			return std::format("Item {:08X}", a_formId);
		}

		RE::TESObjectREFR* ResolveRef(std::uint32_t a_formId)
		{
			auto* form = a_formId != 0 ? RE::TESForm::GetFormByID(a_formId) : nullptr;
			return form ? form->As<RE::TESObjectREFR>() : nullptr;
		}

		Json IdentityJson(const VT::Identity& a_identity, const char* a_type)
		{
			Json packet = VT::StockPublishToJson(VT::StockSnapshot{ .identity = a_identity });
			packet.erase("items");
			packet.erase("caps");
			packet["type"] = a_type;
			return packet;
		}

		bool SameVendor(const VT::Identity& a_left, const VT::Identity& a_right)
		{
			return a_left.npcId == a_right.npcId && a_left.cellId == a_right.cellId && a_left.worldspaceId == a_right.worldspaceId;
		}

		std::vector<VT::StockLine> ReadStock(RE::TESObjectREFR& a_container)
		{
			std::vector<VT::StockLine> lines;
			for (const auto& item : ReadContainerLoot(a_container)) {
				if (item.formId != kCapsItemId && item.formId != 0 && item.count != 0) {
					lines.push_back(VT::StockLine{ item.formId, std::min(item.count, VT::MaximumCount) });
				}
			}
			return lines;
		}

		bool StockTooLarge(RE::TESObjectREFR& a_container)
		{
			if (ReadStock(a_container).size() <= VT::MaximumStockLines) {
				return false;
			}
			LogThrottledWarning("vendor_stock_too_large", "A trader's chest holds more stock lines than a trade can carry; its stock was left unsynced.");
			return true;
		}

		void SyncContainer(RE::TESObjectREFR& a_container, const VT::StockSnapshot& a_target)
		{
			if (StockTooLarge(a_container)) {
				return;
			}
			std::map<std::uint32_t, std::uint32_t> want;
			for (const auto& line : a_target.lines) {
				want[line.formId] = line.count;
			}
			want[kCapsItemId] = a_target.caps;
			std::map<std::uint32_t, std::uint32_t> have;
			for (const auto& item : ReadContainerLoot(a_container)) {
				have[item.formId] += item.count;
			}
			for (const auto& [formId, count] : have) {
				const auto target = want.find(formId);
				const auto wanted = target == want.end() ? 0U : target->second;
				if (count > wanted) {
					(void)RemoveLootFromContainer(a_container, formId, count - wanted);
				}
			}
			for (const auto& [formId, count] : want) {
				const auto existing = have.find(formId);
				const auto current = existing == have.end() ? 0U : existing->second;
				if (count > current) {
					AddLootToContainer(a_container, formId, count - current);
				}
			}
		}

		void PushState()
		{
			Json state{ { "visible", false } };
			{
				const std::scoped_lock lock(g_mutex);
				if (g_session.active) {
					state["visible"] = true;
					state["kind"] = "vendor";
					state["tradeId"] = g_session.tradeId;
					state["vendorName"] = g_session.name;
					state["status"] = g_session.status;
					state["busy"] = !g_session.pendingOperation.empty() || !g_session.snapshot.has_value();
					state["canClose"] = g_session.pendingOperation.empty();
					state["clearCart"] = g_session.clearCart;
					g_session.clearCart = false;
					state["vendorCaps"] = g_session.snapshot ? g_session.snapshot->caps : 0U;
					Json stock = Json::array();
					if (g_session.snapshot) {
						for (const auto& line : g_session.snapshot->lines) {
							stock.push_back(Json{ { "formId", line.formId }, { "name", ItemName(line.formId) }, { "count", line.count }, { "price", BuyPrice(line.formId) } });
						}
					}
					state["stock"] = std::move(stock);
				}
			}
			if (state["visible"].get<bool>()) {
				Json inventory = Json::array();
				std::uint32_t playerCaps = 0;
				// Constraint: engine local player; source:CO-BETA/plugin/include/F4TLocalPlayerState.h
				if (auto* player = F4T::LocalPlayerState::TryGetLocalPlayer()) {
					playerCaps = CountLootItem(*player, kCapsItemId);
					for (const auto& line : ReadStock(*player)) {
						inventory.push_back(Json{ { "formId", line.formId }, { "name", ItemName(line.formId) }, { "count", line.count }, { "price", SellPrice(line.formId) } });
					}
				}
				state["playerCaps"] = playerCaps;
				state["inventory"] = std::move(inventory);
			}
			F4T::TradeWindow::PushRawState(state.dump());
		}

		void SetStatus(const std::string& a_status)
		{
			const std::scoped_lock lock(g_mutex);
			g_session.status = a_status;
		}

		void ApplyEscrow(RE::TESObjectREFR& a_player, const Escrow& a_escrow, bool a_grant)
		{
			for (const auto& line : a_grant ? a_escrow.grant : a_escrow.taken) {
				AddLootToContainer(a_player, line.formId, line.count);
			}
			const auto caps = a_grant ? a_escrow.capsGrant : a_escrow.capsTaken;
			if (caps != 0) {
				AddLootToContainer(a_player, kCapsItemId, caps);
			}
		}

		void FlushPayouts()
		{
			auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				return;
			}
			std::vector<Payout> payouts;
			{
				const std::scoped_lock lock(g_mutex);
				payouts.swap(g_payouts);
			}
			for (const auto& payout : payouts) {
				ApplyEscrow(*player, payout.escrow, payout.grant);
			}
		}

		void RequestSnapshot()
		{
			VT::Identity identity;
			{
				const std::scoped_lock lock(g_mutex);
				identity = g_session.identity;
				++g_session.snapshotQueries;
				g_session.nextSnapshotQuery = std::chrono::steady_clock::now() + kResendDelay;
			}
			(void)SendPacket(IdentityJson(identity, "vendorStockQuery"));
		}

		void RetrySnapshotQuery()
		{
			bool due{};
			bool exhausted{};
			{
				const std::scoped_lock lock(g_mutex);
				const bool waiting = g_session.active && !g_session.snapshot.has_value() && std::chrono::steady_clock::now() >= g_session.nextSnapshotQuery;
				due = waiting && g_session.snapshotQueries < kMaximumSnapshotQueries;
				exhausted = waiting && !due && g_session.status != kTraderUnavailable;
				if (exhausted) g_session.status = kTraderUnavailable;
			}
			if (due) {
				RequestSnapshot();
			}
			if (exhausted) PushState();
		}

		void PublishLocalStock()
		{
			VT::StockSnapshot local;
			std::uint32_t chestId{};
			{
				const std::scoped_lock lock(g_mutex);
				local.identity = g_session.identity;
				chestId = g_session.chestId;
			}
			auto* chest = ResolveRef(chestId);
			if (!chest) {
				SetStatus("This trader's stock could not be read.");
				return;
			}
			if (StockTooLarge(*chest)) {
				SetStatus("This trader's stock is too large to trade.");
				return;
			}
			local.lines = ReadStock(*chest);
			local.caps = std::min(CountLootItem(*chest, kCapsItemId), VT::MaximumCaps);
			Json packet = VT::StockPublishToJson(local);
			packet["type"] = "vendorStock";
			(void)SendPacket(packet);
		}

		void BeginSession(const OpenRequest& a_request)
		{
			auto* chest = ResolveRef(a_request.chestId);
			auto* actor = ResolveRef(a_request.actorId);
			auto* keyRef = actor ? actor : chest;
			if (!chest || !keyRef) {
				return;
			}
			const auto* cell = keyRef->GetParentCell();
			Session session;
			session.active = true;
			session.chestId = chest->GetFormID();
			session.identity.npcId = keyRef->GetFormID();
			session.identity.sourceFormId = keyRef->GetObjectReference() ? keyRef->GetObjectReference()->GetFormID() : 0U;
			session.identity.cellId = cell ? cell->GetFormID() : 0U;
			session.identity.worldspaceId = cell && cell->IsExterior() && cell->worldSpace ? cell->worldSpace->GetFormID() : 0U;
			session.tradeId = std::format("vendor-{:08X}-{}", session.identity.npcId, ++g_sessionCounter);
			session.name = ItemName(session.identity.sourceFormId);
			session.status = "Contacting the server...";
			{
				const std::scoped_lock lock(g_mutex);
				g_session = std::move(session);
			}
			RequestSnapshot();
			PushState();
		}

		void HandleStockState(const Json& a_packet)
		{
			const auto stock = VT::ParseStockSnapshot(a_packet);
			if (!stock) {
				return;
			}
			std::uint32_t chestId{};
			{
				const std::scoped_lock lock(g_mutex);
				if (!g_session.active || !SameVendor(g_session.identity, stock->identity)) {
					return;
				}
				if (g_session.snapshot && stock->revision < g_session.snapshot->revision) {
					return;
				}
				g_session.snapshot = *stock;
				chestId = g_session.chestId;
				if (g_session.pendingOperation.empty() && !g_session.completed) {
					g_session.status = "Click an item to put it in the cart.";
				}
			}
			if (auto* chest = ResolveRef(chestId)) {
				SyncContainer(*chest, *stock);
			}
		}

		void HandleUnseeded(const Json& a_packet)
		{
			const auto identity = VT::ParseStockPublish([&] {
				Json copy = a_packet;
				copy["items"] = Json::array();
				copy["caps"] = 0;
				return copy;
			}());
			if (!identity) {
				return;
			}
			{
				const std::scoped_lock lock(g_mutex);
				if (!g_session.active || !SameVendor(g_session.identity, identity->identity) || g_session.snapshot) {
					return;
				}
			}
			PublishLocalStock();
		}

		void HandleRejectedPublish(const Json& a_packet)
		{
			const auto identity = VT::ParseStockPublish([&] {
				Json copy = a_packet;
				copy["items"] = Json::array();
				copy["caps"] = 0;
				return copy;
			}());
			if (!identity) {
				return;
			}
			{
				const std::scoped_lock lock(g_mutex);
				if (!g_session.active || !SameVendor(g_session.identity, identity->identity)) {
					return;
				}
			}
			RequestSnapshot();
		}

		std::string ReasonText(const std::string& a_reason)
		{
			if (a_reason == "staleRevision") return "The trader's stock just changed. Review the cart and try again.";
			if (a_reason == "insufficientStock") return "The trader no longer has that much stock.";
			if (a_reason == "insufficientCaps") return "The trader cannot afford that.";
			if (a_reason == "stockLimit" || a_reason == "capsLimit") return "The trader cannot hold that much.";
			if (a_reason == "unseededVendor") return "The trader's stock is not known yet.";
			return "The trade was refused.";
		}

		void HandleTradeResult(const Json& a_packet)
		{
			const auto result = VT::ParseTradeResult(a_packet);
			if (!result) {
				return;
			}
			const bool accepted = result->outcome.accepted;
			bool current{};
			{
				const std::scoped_lock lock(g_mutex);
				const auto found = g_unresolved.find(result->operationId);
				if (found == g_unresolved.end()) {
					return;
				}
				g_payouts.push_back(Payout{ std::move(found->second), accepted });
				g_unresolved.erase(found);
				current = g_session.active && g_session.pendingOperation == result->operationId;
				if (current) {
					g_session.pendingOperation.clear();
					g_session.completed = accepted;
					g_session.clearCart = accepted;
					g_session.status = accepted ? "Trade complete." : ReasonText(result->outcome.reason);
				}
			}
			if (current && !accepted && (result->outcome.reason == "staleRevision" || result->outcome.reason == "unseededVendor")) {
				RequestSnapshot();
			}
		}

		void ServiceUnresolved(bool a_reconnected)
		{
			const auto now = std::chrono::steady_clock::now();
			std::vector<std::pair<std::string, Json>> resends;
			{
				const std::scoped_lock lock(g_mutex);
				for (auto it = g_unresolved.begin(); it != g_unresolved.end();) {
					auto& escrow = it->second;
					if (a_reconnected) {
						escrow.nextResend = now;
					}
					if (escrow.nextResend && now >= *escrow.nextResend) {
						resends.emplace_back(it->first, escrow.packet);
					}
					++it;
				}
			}
			for (const auto& [operationId, packet] : resends) {
				if (SendPacket(packet)) {
					const std::scoped_lock lock(g_mutex);
					if (const auto found = g_unresolved.find(operationId); found != g_unresolved.end()) {
						found->second.nextResend = std::chrono::steady_clock::now() + kResendDelay;
					}
				}
			}
		}

		void Process(const Json& a_packet)
		{
			const auto type = a_packet.value("type", std::string{});
			if (type == "vendorStockState") {
				HandleStockState(a_packet);
			} else if (type == "vendorStockUnseeded") {
				HandleUnseeded(a_packet);
			} else if (type == "vendorStockRejected") {
				HandleRejectedPublish(a_packet);
			} else if (type == "vendorTradeResult") {
				HandleTradeResult(a_packet);
			}
		}

		void HideBarterMenu()
		{
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::BSFixedString{ kBarterMenuName }, RE::UI_MESSAGE_TYPE::kHide);
			}
		}

		class BarterSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto* menuName = a_event.menuName.c_str();
				if (!a_event.opening || !menuName || std::string_view{ menuName } != kBarterMenuName) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (!F4T::Networking::IsConnectedToServer() || !F4T::TradeWindow::Attached()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* ui = RE::UI::GetSingleton();
				const auto menu = ui ? ui->GetMenu<RE::BarterMenu>() : nullptr;
				if (!menu) {
					return RE::BSEventNotifyControl::kContinue;
				}
				OpenRequest request;
				if (const auto chest = menu->vendorChestRef.get()) {
					request.chestId = chest->GetFormID();
				}
				if (const auto actor = menu->vendorActor.get()) {
					request.actorId = actor->GetFormID();
				}
				if (request.chestId == 0) {
					return RE::BSEventNotifyControl::kContinue;
				}
				{
					const std::scoped_lock lock(g_mutex);
					g_open = request;
				}
				HideBarterMenu();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		BarterSink g_sink;

		void Confirm(const Json& a_action)
		{
			const auto lines = a_action.find("lines");
			if (lines == a_action.end() || !lines->is_array() || lines->empty() || lines->size() > kMaximumCartLines) {
				return;
			}
			VT::TradeRequest request;
			Escrow escrow;
			std::int64_t playerPays = 0;
			std::vector<VT::StockLine> sells;
			std::string tradeId;
			{
				const std::scoped_lock lock(g_mutex);
				if (!g_session.active || !g_session.pendingOperation.empty() || !g_session.snapshot) {
					return;
				}
				request.identity = g_session.identity;
				request.baseRevision = g_session.snapshot->revision;
				tradeId = g_session.tradeId;
			}
			escrow.operationId = std::format("{}-{}", ++g_operationCounter,
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
			request.operationId = escrow.operationId;
			Json cart = VT::TradeRequestToJson(request);
			cart["deltas"] = *lines;
			const auto checked = VT::ParseTradeRequest(cart);
			if (!checked) {
				return;
			}
			for (const auto& [itemId, change] : checked->lineDeltas) {
				request.lineDeltas.push_back(VT::LineDelta{ itemId, -change });
				if (change > 0) {
					playerPays += static_cast<std::int64_t>(change) * BuyPrice(itemId);
					escrow.grant.push_back(VT::StockLine{ itemId, static_cast<std::uint32_t>(change) });
				} else {
					playerPays -= static_cast<std::int64_t>(-change) * SellPrice(itemId);
					sells.push_back(VT::StockLine{ itemId, static_cast<std::uint32_t>(-change) });
				}
			}
			if (std::abs(playerPays) > static_cast<std::int64_t>(VT::MaximumCaps)) {
				SetStatus("That cart is too large.");
				return;
			}
			request.capsDelta = static_cast<std::int32_t>(playerPays);
			auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				return;
			}
			if (playerPays > 0 && CountLootItem(*player, kCapsItemId) < static_cast<std::uint32_t>(playerPays)) {
				SetStatus("You do not have enough caps.");
				return;
			}
			bool complete = true;
			for (const auto& sell : sells) {
				const auto removed = RemoveLootFromContainer(*player, sell.formId, sell.count);
				if (removed != 0) {
					escrow.taken.push_back(VT::StockLine{ sell.formId, removed });
				}
				if (removed != sell.count) {
					complete = false;
				}
			}
			if (complete && playerPays > 0) {
				const auto removed = RemoveLootFromContainer(*player, kCapsItemId, static_cast<std::uint32_t>(playerPays));
				escrow.capsTaken = removed;
				complete = removed == static_cast<std::uint32_t>(playerPays);
			}
			if (!complete) {
				ApplyEscrow(*player, escrow, false);
				SetStatus("Some of those items are equipped or missing. Unequip them and try again.");
				return;
			}
			if (playerPays < 0) {
				escrow.capsGrant = static_cast<std::uint32_t>(-playerPays);
			}
			escrow.packet = VT::TradeRequestToJson(request);
			escrow.packet["type"] = "vendorTrade";
			if (!SendPacket(escrow.packet)) {
				ApplyEscrow(*player, escrow, false);
				SetStatus("The server could not be reached.");
				return;
			}
			const auto operationId = escrow.operationId;
			const std::scoped_lock lock(g_mutex);
			if (g_session.active && g_session.tradeId == tradeId) {
				g_session.pendingOperation = operationId;
				g_session.completed = false;
				g_session.status = "Waiting for the server...";
			}
			escrow.nextResend = std::chrono::steady_clock::now() + kResendDelay;
			g_unresolved.emplace(operationId, std::move(escrow));
		}
	}

	void Attach()
	{
		if (g_sinkRegistered.load(std::memory_order_acquire)) {
			return;
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink(&g_sink);
			g_sinkRegistered.store(true, std::memory_order_release);
		}
	}

	void HandlePacket(const Json& a_packet)
	{
		const std::scoped_lock lock(g_mutex);
		if (g_inbox.size() < kMaximumInbox) {
			g_inbox.push_back(a_packet);
		}
	}

	bool HandleWindowAction(const Json& a_action)
	{
		const auto op = a_action.value("op", std::string{});
		if (op != "vendorConfirm" && op != "vendorClose") {
			return false;
		}
		bool busy{};
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_session.active || a_action.value("tradeId", std::string{}) != g_session.tradeId) {
				return true;
			}
			busy = !g_session.pendingOperation.empty();
		}
		if (op == "vendorClose") {
			if (busy) {
				SetStatus("Waiting for the trader...");
			} else {
				Reset();
			}
			PushState();
			return true;
		}
		Confirm(a_action);
		PushState();
		return true;
	}

	bool Active()
	{
		const std::scoped_lock lock(g_mutex);
		return g_session.active;
	}

	void Tick()
	{
		Attach();
		std::optional<OpenRequest> open;
		std::deque<Json> inbox;
		{
			const std::scoped_lock lock(g_mutex);
			open = std::exchange(g_open, std::nullopt);
			inbox.swap(g_inbox);
		}
		if (open) {
			BeginSession(*open);
		}
		bool changed = open.has_value();
		for (const auto& packet : inbox) {
			Process(packet);
			changed = true;
		}
		const auto generation = F4T::ReplicationRuntime::CurrentGeneration();
		const bool reconnected = generation != F4T::ReplicationRuntime::kNoSession && generation != g_generation;
		g_generation = generation;
		ServiceUnresolved(reconnected);
		if (!F4T::Networking::IsConnectedToServer() && Active()) {
			Reset();
			changed = true;
		}
		RetrySnapshotQuery();
		FlushPayouts();
		if (changed) {
			PushState();
		}
	}

	void Reset()
	{
		{
			const std::scoped_lock lock(g_mutex);
			g_session = Session{};
			g_open.reset();
			g_inbox.clear();
		}
		F4T::TradeWindow::NoteStateChanged();
	}
}
