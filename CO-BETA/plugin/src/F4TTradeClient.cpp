#include "pch.h"

#include "F4TTradeClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDropMirrorRefs.h"
#include "F4TEquipmentInstance.h"
#include "F4TFactions.h"
#include "F4TInventorySync.h"
#include "F4TLocalPlayerState.h"
#include "F4TNetworking.h"
#include "F4TVendorTradeClient.h"
#include "F4TNetworkingInternal.h"
#include "F4TNpcSyncLootInternal.h"
#include "F4TTradeWindow.h"

#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/E/ExtraInstanceData.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <mutex>
#include <string_view>
#include <utility>

namespace F4T::TradeClient
{
	namespace
	{
		using F4T::LocalPlayerState::LogInfoWithLocalPlayerPrefix;
		using F4T::Networking::Detail::LogThrottledWarning;
		using F4T::Networking::Detail::ReadBool;
		using F4T::Networking::Detail::ReadString;
		using F4T::Networking::Detail::ReadUint32;
		using F4T::Networking::Detail::SendPacketBytes;
		using F4T::NpcSync::Loot::Detail::AddLootToContainer;
		using F4T::NpcSync::Loot::Detail::CountLootItem;
		using F4T::NpcSync::Loot::Detail::RemoveLootFromContainer;

		constexpr std::size_t kMaximumOfferLines = 16;
		constexpr std::size_t kMaximumQueuedCommits = 64;
		constexpr std::uint32_t kMaximumOfferCount = 65535;
		constexpr std::string_view kTradeAlertPrefix = "trade-request:";

		std::mutex g_mutex;
		std::optional<Trade> g_trade;
		struct Commit
		{
			std::uint64_t id{};
			std::vector<Line> give;
			std::vector<Line> receive;
		};
		std::vector<Commit> g_commits;
		std::uint64_t g_commitCounter{};
		std::string g_settledTradeId;

		struct TradeApplyScope
		{
			TradeApplyScope() { F4T::InventorySync::g_tradeApplying = true; }
			~TradeApplyScope() { F4T::InventorySync::g_tradeApplying = false; }
		};

		bool SendPacket(const Json& a_packet)
		{
			return F4T::Networking::IsConnectedToServer() && SendPacketBytes(a_packet.dump());
		}

		std::optional<std::vector<Line>> ReadLines(const Json& a_packet, const char* a_fieldName)
		{
			const auto field = a_packet.find(a_fieldName);
			if (field == a_packet.end() || !field->is_array() || field->size() > kMaximumOfferLines) {
				return std::nullopt;
			}

			std::vector<Line> lines;
			for (const auto& entry : *field) {
				if (!entry.is_object()) {
					return std::nullopt;
				}
				const auto itemKey = ReadString(entry, "itemKey");
				const auto count = ReadUint32(entry, "count");
				if (!itemKey || !count) {
					return std::nullopt;
				}
				lines.push_back(Line{ *itemKey, *count });
			}
			return lines;
		}

		std::optional<Trade> ParseTrade(const Json& a_packet, const std::string& a_tradeId, const std::string& a_phase)
		{
			const auto role = ReadString(a_packet, "role");
			const auto partnerPlayerId = ReadUint32(a_packet, "partnerPlayerId");
			const auto offerVersion = ReadUint32(a_packet, "offerVersion");
			const auto ownConfirmed = ReadBool(a_packet, "ownConfirmed");
			const auto partnerConfirmed = ReadBool(a_packet, "partnerConfirmed");
			auto ownOffer = ReadLines(a_packet, "ownOffer");
			auto partnerOffer = ReadLines(a_packet, "partnerOffer");
			if (!role || !partnerPlayerId || !offerVersion || !ownConfirmed || !partnerConfirmed || !ownOffer || !partnerOffer) {
				return std::nullopt;
			}
			return Trade{ a_tradeId, a_phase, *role, *partnerPlayerId, std::move(*ownOffer), *ownConfirmed, std::move(*partnerOffer), *partnerConfirmed, *offerVersion };
		}

		void ClearTrade(const std::string& a_tradeId)
		{
			const std::scoped_lock lock(g_mutex);
			if (g_trade && g_trade->tradeId == a_tradeId) {
				g_trade.reset();
			}
		}

		void OnTradeRequestAlertResponse(const char* a_alertId, const char* a_actionId, void*)
		{
			if (!a_alertId || !a_actionId) {
				return;
			}
			const std::string_view alertId{ a_alertId };
			if (!alertId.starts_with(kTradeAlertPrefix)) {
				return;
			}
			const auto tradeId = std::string{ alertId.substr(kTradeAlertPrefix.size()) };
			const auto accept = std::string_view{ a_actionId } == "accept";
			F4T::Factions::QueueGameTask([tradeId, accept]() { RespondTrade(tradeId, accept); });
		}

		void ShowTradeRequestAlert(const std::string& a_tradeId, std::uint32_t a_partnerPlayerId)
		{
			F4T::Factions::QueueGameTask([alertId = std::string{ kTradeAlertPrefix } + a_tradeId, message = std::format("Player {} wants to trade with you.", a_partnerPlayerId)]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
				if (!ui || !ui->ShowAlert) {
					return;
				}
				std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 2> actions{ {
					{ "accept", "Accept", "Y" },
					{ "decline", "Decline", "N" }
				} };
				COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
				request.id = alertId.c_str();
				request.category = "Trade";
				request.title = "Trade request";
				request.message = message.c_str();
				request.tone = "invite";
				request.durationMs = static_cast<std::uint32_t>(F4T::Trade::kRequestExpirySeconds * 1000.0);
				request.actions = actions.data();
				request.actionCount = actions.size();
				(void)ui->ShowAlert(&request, &OnTradeRequestAlertResponse, nullptr, nullptr, 0);
			});
		}

		void DismissTradeRequestAlert(const std::string& a_tradeId)
		{
			F4T::Factions::QueueGameTask([alertId = std::string{ kTradeAlertPrefix } + a_tradeId]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
				if (ui && ui->DismissAlert) {
					ui->DismissAlert(alertId.c_str());
				}
			});
		}

		void ShowResyncAlert()
		{
			F4T::Factions::QueueGameTask([]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
				if (!ui || !ui->ShowAlert) {
					return;
				}
				std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 1> actions{ {
					{ "ok", "OK", "Y" }
				} };
				COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
				request.id = "trade-resync";
				request.category = "Inventory";
				request.title = "Inventory out of sync";
				request.message = "Your inventory may differ from the server. Some changes could not be applied.";
				request.tone = "warning";
				request.durationMs = 15000;
				request.actions = actions.data();
				request.actionCount = actions.size();
				(void)ui->ShowAlert(&request, &OnTradeRequestAlertResponse, nullptr, nullptr, 0);
			});
		}

		bool IsPlainEquippedStack(const RE::BGSInventoryItem::Stack& a_stack)
		{
			return a_stack.IsEquipped() && !(a_stack.extra && a_stack.extra->GetByType<RE::BGSObjectInstanceExtra>());
		}

		void UnequipFoundStack(RE::Actor& a_actor, std::uint32_t a_formId, const F4T::EquipmentInstance::FoundStack& a_found, std::uint32_t a_count)
		{
			auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_formId);
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!object || !equipManager) {
				return;
			}
			auto* extra = a_found.stack->extra ? a_found.stack->extra->GetByType<RE::ExtraInstanceData>() : nullptr;
			const RE::BGSObjectInstance equippedObject{ object, extra ? extra->data.get() : nullptr };
			// Constraint: unequip a counted part of one equipped stack by its stack id; source:CO-BETA/plugin/src/F4TProxyActorControllerEquipment.cpp
			equipManager->UnequipObject(&a_actor, &equippedObject, a_count, nullptr, a_found.stackId, false, false, true, true, nullptr);
		}

		std::uint32_t RemoveOfferedLoot(RE::Actor& a_actor, std::uint32_t a_formId, std::uint32_t a_count)
		{
			const auto held = CountLootItem(a_actor, a_formId);
			if (held < a_count) {
				const auto equipped = F4T::EquipmentInstance::FindStack(a_actor, a_formId, IsPlainEquippedStack);
				if (equipped.stack) {
					UnequipFoundStack(a_actor, a_formId, equipped, static_cast<std::uint32_t>(equipped.stack->GetCount()));
				}
			}
			return RemoveLootFromContainer(a_actor, a_formId, a_count);
		}

		bool ApplyCommittedTrade(const std::vector<Line>& a_give, const std::vector<Line>& a_receive)
		{
			// Constraint: engine local player; source:CO-BETA/plugin/include/F4TLocalPlayerState.h
			auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				LogThrottledWarning("trade_apply_pending", "A committed trade waits for the local player.");
				return false;
			}
			const TradeApplyScope scope;
			bool complete = true;
			// Constraint: engine container remove after RemoveOfferedLoot unequips, since the remove skips equipped stacks; source:CO-BETA/plugin/src/F4TNpcSyncLoot.cpp
			for (const auto& line : a_give) {
				const auto formId = F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey);
				const auto removed = formId ? RemoveOfferedLoot(*player, *formId, line.count) : 0U;
				if (removed != line.count) {
					complete = false;
				}
			}
			// Constraint: engine container add; source:CO-BETA/plugin/include/F4TNpcSyncLootInternal.h
			for (const auto& line : a_receive) {
				const auto formId = F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey);
				if (!formId) {
					complete = false;
					continue;
				}
				AddLootToContainer(*player, *formId, line.count);
			}
			if (!complete) {
				LogThrottledWarning("trade_apply_short", "A committed trade did not fully apply to the local inventory.");
				F4T::InventorySync::NoteResyncNeeded();
			}
			return true;
		}

		void ApplyPendingCommits()
		{
			while (true) {
				std::optional<Commit> next;
				{
					const std::scoped_lock lock(g_mutex);
					if (!g_commits.empty()) {
						next = g_commits.front();
					}
				}
				if (!next || !ApplyCommittedTrade(next->give, next->receive)) {
					return;
				}
				const std::scoped_lock lock(g_mutex);
				if (!g_commits.empty() && g_commits.front().id == next->id) {
					g_commits.erase(g_commits.begin());
				}
			}
		}

		void CancelLostOffer()
		{
			const auto trade = CurrentTrade();
			if (!trade || !(trade->ownConfirmed || trade->confirmSent) || !F4T::LocalPlayerState::TryGetLocalPlayer() || F4T::InventorySync::HoldsLines(trade->ownOffer)) {
				return;
			}
			{
				const std::scoped_lock lock(g_mutex);
				if (g_settledTradeId == trade->tradeId) {
					return;
				}
				g_settledTradeId = trade->tradeId;
			}
			LogInfoWithLocalPlayerPrefix("Trade cancelled: an offered item is no longer available to trade.");
			CancelTrade(trade->tradeId);
		}

		void HandleState(const Json& a_packet)
		{
			const auto tradeId = ReadString(a_packet, "tradeId");
			const auto phase = ReadString(a_packet, "phase");
			if (!tradeId || !phase) {
				LogThrottledWarning("trade_state_malformed", "Ignoring a malformed trade state packet.");
				return;
			}

			if (*phase == "ended") {
				ClearTrade(*tradeId);
				DismissTradeRequestAlert(*tradeId);
				LogInfoWithLocalPlayerPrefix(std::format("Trade ended: {}.", ReadString(a_packet, "reason").value_or("unknown")));
				F4T::TradeWindow::NoteStateChanged();
				return;
			}

			auto trade = ParseTrade(a_packet, *tradeId, *phase);
			if (!trade || (*phase != "requested" && *phase != "negotiating")) {
				LogThrottledWarning("trade_state_malformed", "Ignoring a malformed trade state packet.");
				return;
			}

			if (*phase == "requested") {
				if (trade->role == "target") {
					ShowTradeRequestAlert(*tradeId, trade->partnerPlayerId);
				}
			} else {
				DismissTradeRequestAlert(*tradeId);
			}

			{
				const std::scoped_lock lock(g_mutex);
				trade->confirmSent = g_trade && g_trade->confirmSent && !g_trade->ownConfirmed && g_trade->tradeId == trade->tradeId && g_trade->offerVersion == trade->offerVersion;
				g_trade = std::move(trade);
			}
			F4T::TradeWindow::NoteStateChanged();
		}

		void HandleResult(const Json& a_packet)
		{
			const auto status = ReadString(a_packet, "status");
			if (!status || (*status != "committed" && *status != "rejected")) {
				LogThrottledWarning("trade_result_malformed", "Ignoring a malformed trade result packet.");
				return;
			}
			if (*status == "rejected") {
				LogInfoWithLocalPlayerPrefix(std::format("Trade refused: {}.", ReadString(a_packet, "reason").value_or("unknown")));
				return;
			}
			const auto tradeId = ReadString(a_packet, "tradeId");
			const auto revision = ReadUint32(a_packet, "revision");
			const auto give = ReadLines(a_packet, "give");
			const auto receive = ReadLines(a_packet, "receive");
			if (!tradeId || !revision || !give || !receive) {
				LogThrottledWarning("trade_result_malformed", "Ignoring a malformed trade result packet.");
				return;
			}
			bool overflowed = false;
			{
				const std::scoped_lock lock(g_mutex);
				g_settledTradeId = *tradeId;
				if (!F4T::InventorySync::NoteTradeCommitted(*revision, *give, *receive)) {
					return;
				}
				if (g_commits.size() >= kMaximumQueuedCommits) {
					g_commits.erase(g_commits.begin());
					overflowed = true;
					LogThrottledWarning("trade_commit_overflow", "A committed trade was dropped before it reached the local inventory.");
				}
				g_commits.push_back(Commit{ ++g_commitCounter, *give, *receive });
			}
			if (overflowed) {
				F4T::InventorySync::NoteResyncNeeded();
			}
			F4T::TradeWindow::NoteStateChanged();
			LogInfoWithLocalPlayerPrefix("Trade committed.");
		}
	}

	bool RequestTrade(std::uint32_t a_targetPlayerId)
	{
		return a_targetPlayerId != 0 && SendPacket(Json{ { "type", "tradeRequest" }, { "targetPlayerId", a_targetPlayerId } });
	}

	bool RespondTrade(const std::string& a_tradeId, bool a_accept)
	{
		return SendPacket(Json{ { "type", "tradeRespond" }, { "tradeId", a_tradeId }, { "accept", a_accept } });
	}

	bool SetOffer(const std::string& a_tradeId, const std::vector<Line>& a_lines)
	{
		if (a_lines.size() > kMaximumOfferLines) {
			return false;
		}
		Json lines = Json::array();
		for (const auto& line : a_lines) {
			if (line.count == 0 || line.count > kMaximumOfferCount) {
				return false;
			}
			lines.push_back(Json{ { "itemKey", line.itemKey }, { "count", line.count } });
		}
		return SendPacket(Json{ { "type", "tradeOffer" }, { "tradeId", a_tradeId }, { "lines", lines } });
	}

	bool ConfirmTrade(const std::string& a_tradeId, std::uint32_t a_offerVersion)
	{
		const auto trade = CurrentTrade();
		if (!trade || trade->tradeId != a_tradeId || !F4T::InventorySync::HoldsLines(trade->ownOffer)) {
			LogInfoWithLocalPlayerPrefix("Trade not confirmed: the trade changed or an offered item is no longer held as a plain stack.");
			return false;
		}
		if (!SendPacket(Json{ { "type", "tradeConfirm" }, { "tradeId", a_tradeId }, { "offerVersion", a_offerVersion } })) {
			return false;
		}
		const std::scoped_lock lock(g_mutex);
		if (g_trade && g_trade->tradeId == a_tradeId && g_trade->offerVersion == a_offerVersion) {
			g_trade->confirmSent = true;
		}
		return true;
	}

	bool CancelTrade(const std::string& a_tradeId)
	{
		return SendPacket(Json{ { "type", "tradeCancel" }, { "tradeId", a_tradeId } });
	}

	void HandlePacket(const Json& a_packet)
	{
		const auto type = ReadString(a_packet, "type");
		if (!type) {
			return;
		}
		if (*type == "tradeState") {
			HandleState(a_packet);
		} else if (*type == "tradeResult") {
			HandleResult(a_packet);
		} else {
			F4T::VendorTradeClient::HandlePacket(a_packet);
		}
	}

	std::optional<Trade> CurrentTrade()
	{
		const std::scoped_lock lock(g_mutex);
		return g_trade;
	}

	bool HasPendingCommits()
	{
		const std::scoped_lock lock(g_mutex);
		return !g_commits.empty();
	}

	void Tick()
	{
		CancelLostOffer();
		ApplyPendingCommits();
		if (F4T::InventorySync::TakeResyncNeeded()) {
			ShowResyncAlert();
		}
	}

	void Reset()
	{
		std::optional<Trade> previous;
		std::size_t droppedCommits{};
		{
			const std::scoped_lock lock(g_mutex);
			previous = std::exchange(g_trade, std::nullopt);
			droppedCommits = g_commits.size();
			g_commits.clear();
			g_settledTradeId.clear();
		}
		if (previous) {
			DismissTradeRequestAlert(previous->tradeId);
		}
		if (droppedCommits != 0) {
			LogThrottledWarning("trade_commit_dropped", "Committed trades waiting for the local player were dropped at session reset.");
		}
		F4T::TradeWindow::NoteStateChanged();
	}
}
