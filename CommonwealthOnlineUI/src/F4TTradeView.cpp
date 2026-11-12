#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TMenuInput.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::TradeView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;
		using F4T::PrismaUI::Internal::PassthroughControllerActions;
		using F4T::PrismaUI::Internal::SyncPassthroughControllerActions;

		constexpr auto kHtmlPath = "CommonwealthOnline/trade/index.html";
		constexpr std::size_t kActionMaxBytes = 4 * 1024;
		// Constraint: the trade window sits above the interact wheel (650) so an open trade is never covered.
		constexpr int kViewOrder = 680;

		F4T::PrismaViewState::Controller g_viewState{ "Trade" };
		PassthroughControllerActions g_passthrough;
		COMMONWEALTH_ONLINE_UI_API::TradeActionFn g_actionCallback{ nullptr };
		std::string g_stateJson{ R"({"visible":false})" };
		std::string g_tradeId;
		std::string g_hiddenTradeId;
		bool g_inputCaptured{ false };

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Trade] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Trade] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped trade task: game task interface unavailable.");
		}

		void PushState()
		{
			if (!g_api || !g_viewState.IsDesiredVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
				return;
			}
			const auto script = std::format("if(window.coTradeSetState){{window.coTradeSetState({});}}", g_stateJson);
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
		}

		void SyncEscapeOwnership()
		{
			const auto handle = g_viewState.GetHandle();
			if (handle == 0) {
				return;
			}
			auto* api = F4T::PrismaCapabilities::GetV12();
			if (!api || !api->IsValid(handle)) {
				return;
			}
			api->SetViewOwnsEscape(handle, g_viewState.IsDesiredVisible() && g_viewState.IsCurrentlyFocused());
			SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());
		}

		void SetInputCaptured(bool a_captured)
		{
			if (g_inputCaptured == a_captured) {
				return;
			}
			g_inputCaptured = a_captured;
			F4T::MenuInput::SetTradeActive(a_captured);
		}

		void CloseInternal(std::string_view a_reason)
		{
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			g_viewState.SetDesired(false, false);
			g_viewState.ApplyState(a_reason);
			SyncEscapeOwnership();
			SetInputCaptured(false);
		}

		void HandleAction(const std::string& a_json)
		{
			const auto action = nlohmann::json::parse(a_json, nullptr, false);
			const auto op = action.is_object() && action.contains("op") && action["op"].is_string() ? action["op"].get<std::string>() : std::string{};
			if (op == "hide") {
				g_hiddenTradeId = g_tradeId;
				CloseInternal("UserHide");
				return;
			}
			if (op.empty() || !g_actionCallback) {
				LogWarn("Dropped trade action: missing op or action callback.");
				return;
			}
			g_actionCallback(a_json.c_str());
		}

		void OnTradeAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped trade action larger than 4 KB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() { HandleAction(payload); });
		}

		void FinishDomReady(PrismaView a_view)
		{
			if (!g_viewState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			g_api->BindUIEvent(a_view, "coTradeAction", OnTradeAction);
			g_api->SetOrder(a_view, kViewOrder);
			g_viewState.ApplyState("DomReady");
			SyncEscapeOwnership();
			if (g_viewState.IsDesiredVisible()) {
				PushState();
			}
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() { FinishDomReady(a_view); });
		}

		void OpenInternal(const std::string& a_reason)
		{
			if (!g_api) {
				LogWarn("Open skipped: Prisma API unavailable.");
				return;
			}
			if (g_sessionLockoutActive) {
				return;
			}
			if (g_viewState.IsDesiredVisible() && g_viewState.IsHandleValid()) {
				PushState();
				return;
			}
			LogInfo(std::format("Open reason={}", a_reason));
			g_viewState.SetDesired(true, true);
			if (g_viewState.EnsureCreated(kHtmlPath, OnDomReady, true) == 0) {
				LogWarn("Open failed: CreateView returned no handle.");
				g_viewState.SetDesired(false, false);
				return;
			}
			g_viewState.ApplyState(a_reason);
			SyncEscapeOwnership();
			if (g_viewState.IsDomReady()) {
				PushState();
			}
		}

		void OpenForCurrentState(const std::string& a_reason)
		{
			const auto state = nlohmann::json::parse(g_stateJson, nullptr, false);
			const auto awaitingAccept = state.is_object() && state.value("role", std::string{}) == "target" && state.value("phase", std::string{}) == "requested";
			OpenInternal(a_reason);
			SetInputCaptured(g_viewState.IsDesiredVisible() && !awaitingAccept);
		}

		void ApplyStateJson(const std::string& a_json)
		{
			const auto state = nlohmann::json::parse(a_json, nullptr, false);
			if (!state.is_object()) {
				LogWarn("Dropped malformed trade state.");
				return;
			}
			g_stateJson = F4T::ViewJson::Dump(state);
			g_tradeId = state.contains("tradeId") && state["tradeId"].is_string() ? state["tradeId"].get<std::string>() : std::string{};
			const auto visible = state.contains("visible") && state["visible"].is_boolean() && state["visible"].get<bool>();
			if (!visible) {
				g_hiddenTradeId.clear();
				CloseInternal("NoTrade");
				return;
			}
			if (g_tradeId == g_hiddenTradeId || F4T::MenuInput::IsPauseMenuActive()) {
				return;
			}
			OpenForCurrentState("TradeState");
		}
	}

	void Initialize()
	{
		g_viewState.SetApi(g_api);
		g_viewState.SetFocusArgs(false, true);
		g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_viewState.SetDesired(false, false);
	}

	void SetActionCallback(COMMONWEALTH_ONLINE_UI_API::TradeActionFn a_callback)
	{
		RunOnGameThread([callback = a_callback]() { g_actionCallback = callback; });
	}

	void NotifyStateChanged(const char* a_stateJson)
	{
		if (!a_stateJson) {
			return;
		}
		RunOnGameThread([json = std::string(a_stateJson)]() { ApplyStateJson(json); });
	}

	void SyncControllerActions()
	{
		if (g_viewState.IsDesiredVisible()) SyncEscapeOwnership();
	}

	void Close(std::string_view a_reason)
	{
		if (a_reason == "PauseMenuHidden" && !g_viewState.IsDesiredVisible()) {
			const auto pending = nlohmann::json::parse(g_stateJson, nullptr, false);
			if (pending.is_object() && pending.value("visible", false) && !g_tradeId.empty() && g_tradeId != g_hiddenTradeId) {
				OpenForCurrentState("PauseMenuClosed");
			}
			return;
		}
		if (a_reason == "PauseMenuHidden" || a_reason == "SessionLockout") {
			const auto state = nlohmann::json::parse(g_stateJson, nullptr, false);
			const auto isVendor = state.is_object() && state.value("kind", std::string{}) == "vendor";
			if (isVendor && g_actionCallback && !g_tradeId.empty()) {
				const auto closeAction = F4T::ViewJson::Dump(nlohmann::json{ { "op", "vendorClose" }, { "tradeId", g_tradeId } });
				g_actionCallback(closeAction.c_str());
			} else {
				g_hiddenTradeId = g_tradeId;
			}
		}
		CloseInternal(a_reason);
	}

	void DispatchControllerInput(const char* a_button, const char* a_phase)
	{
		if (!g_api || !g_viewState.IsCurrentlyVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
			return;
		}

		const auto script = std::format(
			"if(window.handleControllerInput){{window.handleControllerInput('{}', '{}');}}",
			a_button ? a_button : "",
			a_phase ? a_phase : "");
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY)
	{
		if (!g_api || !g_viewState.IsCurrentlyVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
			return;
		}
		const auto script = std::format(
			"if(window.handleControllerAxes){{window.handleControllerAxes({:.4f},{:.4f},{:.4f},{:.4f});}}",
			a_leftX, a_leftY, a_rightX, a_rightY);
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	bool IsCurrentlyVisible()
	{
		return g_viewState.IsCurrentlyVisible() && g_viewState.IsHandleValid();
	}
}
