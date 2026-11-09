#include "pch.h"

#include "F4TTradeWindow.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDebugLogging.h"
#include "F4TDropMirrorRefs.h"
#include "F4TFactions.h"
#include "F4TInteractWheel.h"
#include "F4TInteractWheelGate.h"
#include "F4TInventorySync.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TTradeClient.h"
#include "F4TVendorTradeClient.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4T::TradeWindow
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::size_t kMaximumOfferLines = 16;
		constexpr std::uint64_t kMaximumOfferCount = 65535;

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI20* g_api{ nullptr };
		std::atomic_bool g_attached{ false };

		// Constraint: the item name is the engine full name; source:CO-BETA/plugin/lib/commonlibf4/include/RE/T/TESFullName.h
		std::string ItemName(const std::string& a_itemKey)
		{
			if (const auto formId = F4T::DropMirrorRefs::ResolveItemFormId(a_itemKey)) {
				if (const auto* form = RE::TESForm::GetFormByID<RE::TESBoundObject>(*formId)) {
					const auto fullName = RE::TESFullName::GetFullName(*form);
					if (!fullName.empty()) {
						return std::string{ fullName };
					}
				}
			}
			return a_itemKey;
		}

		Json LinesJson(const std::vector<F4T::TradeClient::Line>& a_lines)
		{
			Json lines = Json::array();
			for (const auto& line : a_lines) {
				lines.push_back(Json{ { "itemKey", line.itemKey }, { "name", ItemName(line.itemKey) }, { "count", line.count } });
			}
			return lines;
		}

		std::string PartnerName(std::uint32_t a_playerId)
		{
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerId != a_playerId) {
					continue;
				}
				if (auto shown = F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName); !shown.empty()) {
					return shown;
				}
			}
			return "Player " + std::to_string(a_playerId);
		}

		void PushState()
		{
			if (F4T::VendorTradeClient::Active()) {
				return;
			}
			if (!g_api || !g_api->NotifyTradeStateChanged) {
				return;
			}
			Json holdings = LinesJson(F4T::InventorySync::LocalHoldings());
			Json state{ { "visible", false }, { "holdings", holdings } };
			if (const auto trade = F4T::TradeClient::CurrentTrade()) {
				state["visible"] = true;
				state["tradeId"] = trade->tradeId;
				state["phase"] = trade->phase;
				state["role"] = trade->role;
				state["partnerPlayerId"] = trade->partnerPlayerId;
				state["partnerName"] = PartnerName(trade->partnerPlayerId);
				state["ownOffer"] = LinesJson(trade->ownOffer);
				state["partnerOffer"] = LinesJson(trade->partnerOffer);
				state["ownConfirmed"] = trade->ownConfirmed;
				state["partnerConfirmed"] = trade->partnerConfirmed;
				state["offerVersion"] = trade->offerVersion;
			}
			const auto json = state.dump();
			g_api->NotifyTradeStateChanged(json.c_str());
		}

		std::string TextField(const Json& a_json, const char* a_key)
		{
			const auto field = a_json.find(a_key);
			return field != a_json.end() && field->is_string() ? field->get<std::string>() : std::string{};
		}

		std::optional<std::vector<F4T::TradeClient::Line>> ReadOffer(const Json& a_action)
		{
			const auto lines = a_action.find("lines");
			if (lines == a_action.end() || !lines->is_array() || lines->size() > kMaximumOfferLines) {
				return std::nullopt;
			}
			std::vector<F4T::TradeClient::Line> offer;
			for (const auto& entry : *lines) {
				const auto count = entry.find("count");
				if (!entry.is_object() || count == entry.end() || !count->is_number_unsigned()) {
					return std::nullopt;
				}
				const auto value = count->get<std::uint64_t>();
				const auto itemKey = TextField(entry, "itemKey");
				if (itemKey.empty() || value == 0 || value > kMaximumOfferCount) {
					return std::nullopt;
				}
				offer.push_back(F4T::TradeClient::Line{ itemKey, static_cast<std::uint32_t>(value) });
			}
			return offer;
		}

		void OnTradeAction(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			const auto action = Json::parse(a_actionJson, nullptr, false);
			if (!action.is_object()) {
				return;
			}
			if (F4T::VendorTradeClient::HandleWindowAction(action)) {
				return;
			}
			const auto trade = F4T::TradeClient::CurrentTrade();
			const auto tradeId = TextField(action, "tradeId");
			if (!trade || trade->tradeId != tradeId) {
				return;
			}
			const auto op = TextField(action, "op");
			if (op == "offer") {
				if (const auto offer = ReadOffer(action)) {
					F4T::TradeClient::SetOffer(tradeId, *offer);
				}
			} else if (op == "confirm") {
				const auto offerVersion = action.find("offerVersion");
				if (offerVersion != action.end() && offerVersion->is_number_unsigned()) {
					F4T::TradeClient::ConfirmTrade(tradeId, offerVersion->get<std::uint32_t>());
				}
			} else if (op == "cancel") {
				F4T::TradeClient::CancelTrade(tradeId);
			} else if (op == "respond") {
				const auto accept = action.find("accept");
				if (accept != action.end() && accept->is_boolean()) {
					F4T::TradeClient::RespondTrade(tradeId, accept->get<bool>());
				}
			}
		}
	}

	void Attach()
	{
		g_api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI20>();
		if (!g_api) {
			F4T::DebugLogging::Log("Trade", "the V20 trade window API is unavailable");
			return;
		}
		if (g_api->SetTradeActionCallback) {
			g_api->SetTradeActionCallback(&OnTradeAction);
			g_attached = true;
		}
	}

	bool Attached()
	{
		return g_attached.load();
	}

	void PushRawState(const std::string& a_stateJson)
	{
		if (g_api && g_api->NotifyTradeStateChanged) {
			g_api->NotifyTradeStateChanged(a_stateJson.c_str());
		}
	}

	void NoteStateChanged()
	{
		F4T::Factions::QueueGameTask([]() {
			if (F4T::InteractWheelGate::ShouldCloseWheelForTrade(F4T::InteractWheel::IsOpen(), F4T::TradeClient::CurrentTrade().has_value())) {
				F4T::InteractWheel::Close("TradeState");
			}
			PushState();
		});
	}
}
