#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TVoiceBridge.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace F4T::FactionView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_pauseMenuState;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;
		using F4T::PrismaUI::Internal::g_menuMouseCursorVisible;
		using F4T::PrismaUI::Internal::ApplyMenuFocusForPointerMode;
		using F4T::PrismaUI::Internal::SyncPauseMenuEscapeOwnership;
		using F4T::PrismaUI::Internal::PassthroughControllerActions;
		using F4T::PrismaUI::Internal::SyncPassthroughControllerActions;

		constexpr auto kFactionHtmlPath = "CommonwealthOnline/faction/index.html";
		constexpr int  kFactionViewOrder = 600;
		// Constraint: 200 friends at the longest name plus the capture budgets need more than 64 KB.
		constexpr std::size_t kStateBufferSize = 256 * 1024;
		constexpr std::size_t kErrorBufferSize = 512;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kDisabledStateJson = R"({"enabled":false})";
		constexpr auto kRefreshActionJson = R"({"op":"refresh"})";

		F4T::PrismaViewState::Controller g_viewState{ "Faction" };
		PassthroughControllerActions g_passthrough;
		COMMONWEALTH_ONLINE_UI_API::FactionCallbacks g_callbacks{};
		bool g_pauseFocusHandedOff = false;
		std::atomic<bool> g_worldOpenQueued{ false };
		int g_promptPush{ -1 };

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Factions] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Factions] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped faction task — game task interface unavailable.");
		}

		bool IsActionKnown(std::string_view a_op)
		{
			return a_op == "create" || a_op == "invite" || a_op == "inviteResponse" ||
				a_op == "leave" || a_op == "kick" || a_op == "promote" || a_op == "setRank" ||
				a_op == "setProfile" || a_op == "disband" || a_op == "refresh" ||
				a_op == "buildAccessGet" || a_op == "buildAccessGrant" || a_op == "buildAccessRevoke" ||
				a_op == "raidDeclare" || a_op == "settlementWindowSet" || a_op == "settlerRecruit" ||
				a_op == "settlerAssign" || a_op == "settlerRecover" ||
				a_op == "capture" || a_op == "cancelCapture" || a_op == "refreshCapture" ||
				a_op == "structureRepair" || a_op == "structureRebuild" || a_op == "workbenchRepair" || a_op == "upkeepPay" ||
				a_op == "listFactions" || a_op == "friendAdd" || a_op == "friendAccept" || a_op == "friendDecline" ||
				a_op == "friendRemove" || a_op == "friendList";
		}

		void PushState()
		{
			if (!g_api || !g_viewState.IsDesiredVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
				return;
			}

			std::string json(kDisabledStateJson);
			if (g_callbacks.GetStateJson) {
				std::string buffer(kStateBufferSize, '\0');
				std::string error(kErrorBufferSize, '\0');
				const auto ok = g_callbacks.GetStateJson(buffer.data(), buffer.size(), error.data(), error.size());
				const auto length = strnlen(buffer.data(), buffer.size());
				if (!ok) {
					LogWarn(std::format("State pull failed, showing not-available: {}", error.c_str()));
				} else if (length == buffer.size()) {
					LogWarn(std::format("State JSON filled the {} byte buffer, showing not-available.", kStateBufferSize));
				} else {
					json.assign(buffer.data(), length);
				}
			}

			try {
				const auto literal = F4T::ViewJson::Dump(nlohmann::json(json));
				const auto prompt = g_promptPush < 0 ? std::string() : std::format("if(window.coFactionSetPrompt){{window.coFactionSetPrompt({});}}", g_promptPush ? "true" : "false");
				g_promptPush = -1;
				const auto script = std::format(
					"{}if(window.coFactionSetState){{window.coFactionSetState({});}}",
					prompt,
					literal);
				g_api->Invoke(g_viewState.GetHandle(), script.c_str());
			} catch (const std::exception& ex) {
				LogWarn(std::format("State push skipped: {}", ex.what()));
			}
		}

		void SuspendPauseFocus()
		{
			if (g_pauseFocusHandedOff || !g_pauseMenuState.IsDesiredVisible() || !g_pauseMenuState.IsCurrentlyFocused()) {
				return;
			}
			g_pauseFocusHandedOff = true;
			g_pauseMenuState.SetDesiredFocused(false);
			g_pauseMenuState.ApplyState("FactionWindowOpened");
			SyncPauseMenuEscapeOwnership();
		}

		void ResumePauseFocus()
		{
			if (!g_pauseFocusHandedOff) {
				return;
			}
			g_pauseFocusHandedOff = false;
			if (!g_pauseMenuState.IsDesiredVisible()) {
				return;
			}
			ApplyMenuFocusForPointerMode(g_menuMouseCursorVisible);
			SyncPauseMenuEscapeOwnership();
		}

		// Constraint: the page closes on Escape, so the view owns Escape only while it is visible and focused.
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

		void ApplyWindowState(std::string_view a_reason)
		{
			if (g_viewState.IsDesiredVisible() && g_viewState.IsHandleValid() && g_viewState.IsDomReady()) {
				SuspendPauseFocus();
			}
			g_viewState.ApplyState(a_reason);
			if (g_viewState.IsDesiredVisible() && g_viewState.IsDomReady() && !g_viewState.IsCurrentlyFocused()) {
				ResumePauseFocus();
			}
			SyncEscapeOwnership();
			PushState();
			if (g_viewState.IsDesiredVisible() && g_viewState.IsCurrentlyVisible() && !g_pauseMenuState.IsCurrentlyVisible()) {
				F4T::MenuInput::SetWorldPanelActive(true);
			}
		}

		void CloseInternal(std::string_view a_reason)
		{
			F4T::MenuInput::SetWorldPanelActive(false);
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			g_viewState.SetDesired(false, false);
			g_viewState.ApplyState(a_reason);
			ResumePauseFocus();
			SyncEscapeOwnership();
		}

		void HandleAction(const std::string& a_json)
		{
			std::string op;
			try {
				op = nlohmann::json::parse(a_json).value("op", "");
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed faction action: {}", ex.what()));
				return;
			}

			if (op == "close") {
				CloseInternal("FactionCloseRequested");
				return;
			}
			if (!IsActionKnown(op)) {
				LogWarn(std::format("Dropped faction action with unknown op '{}'", op));
				return;
			}
			if (op == "refresh") {
				PushState();
			}
			if (!g_callbacks.SendAction) {
				LogWarn(std::format("Dropped faction action '{}': no action callback", op));
				return;
			}
			g_callbacks.SendAction(a_json.c_str());
		}

		void OnFactionAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped faction action larger than 1 KB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() {
				HandleAction(payload);
			});
		}

		void OnAlertHotkey(const char* a_key)
		{
			if (!a_key || (std::strcmp(a_key, "Y") != 0 && std::strcmp(a_key, "N") != 0)) return;
			RunOnGameThread([key = std::string(a_key)]() { if (g_viewState.IsCurrentlyVisible()) F4T::Alerts::ResolveMenuHotkey(key.c_str()); });
		}

		void RequestRefresh()
		{
			if (g_callbacks.SendAction) {
				g_callbacks.SendAction(kRefreshActionJson);
			}
		}

		void FinishDomReady(PrismaView a_view)
		{
			if (!g_viewState.MarkDomReady(a_view) || !g_api) {
				return;
			}

			g_api->RegisterConsoleCallback(
				a_view,
				[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
					if (!a_message) {
						return;
					}
					switch (a_level) {
					case PRISMA_UI_API::ConsoleMessageLevel::Error:
						REX::CRITICAL("[COUI] [Factions][JS] {}", a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("[COUI] [Factions][JS] {}", a_message);
						break;
					default:
						REX::INFO("[COUI] [Factions][JS] {}", a_message);
						break;
					}
				});

			g_api->BindUIEvent(a_view, "coFactionAction", OnFactionAction);
			g_api->BindUIEvent(a_view, "coFactionAlertHotkey", OnAlertHotkey);
			F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::Social, a_view);
			g_api->SetOrder(a_view, kFactionViewOrder);
			ApplyWindowState("DomReady");
			SyncEscapeOwnership();
			if (g_viewState.IsDesiredVisible()) {
				RequestRefresh();
			}
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishDomReady(a_view);
			});
		}

		void OpenInternal(const std::string& a_reason)
		{
			LogInfo(std::format("Open reason={}", a_reason));
			g_promptPush = a_reason == "WorldOpen" ? 1 : 0;
			if (!g_api) {
				LogWarn("Open skipped — Prisma API unavailable.");
				return;
			}
			if (g_sessionLockoutActive) {
				LogInfo("Open skipped — session lockout active.");
				return;
			}
			if (g_viewState.IsDesiredVisible() && g_viewState.IsHandleValid()) {
				PushState();
				return;
			}

			g_viewState.SetDesired(true, true);
			if (g_viewState.EnsureCreated(kFactionHtmlPath, OnDomReady, true) == 0) {
				LogWarn("Open failed — CreateView returned no handle.");
				g_viewState.SetDesired(false, false);
				ResumePauseFocus();
				return;
			}
			ApplyWindowState(a_reason);
			if (g_viewState.IsDomReady()) {
				RequestRefresh();
			}
		}
	}

	void Initialize()
	{
		g_viewState.SetApi(g_api);
		g_viewState.SetFocusArgs(false, true);
		g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_viewState.SetDesired(false, false);
		F4T::MenuInput::SetWorldPanelActive(false);
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::FactionCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::FactionCallbacks{}]() {
			g_callbacks = callbacks;
		});
	}

	void Open(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)]() {
			if (!g_pauseMenuState.IsCurrentlyVisible()) {
				LogWarn(std::format("Open refused — pause menu is not open (reason={}).", reason));
				return;
			}
			OpenInternal(reason);
		});
	}

	void SyncControllerActions()
	{
		if (g_viewState.IsDesiredVisible()) SyncEscapeOwnership();
	}

	bool OpenWorld(std::string_view a_reason)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks || !g_api || g_sessionLockoutActive) {
			return false;
		}
		if (!g_viewState.IsHandleValid() && g_viewState.DidCreateFail() && !g_viewState.CanRetryCreate()) {
			return false;
		}
		if (!g_worldOpenQueued.exchange(true)) {
			tasks->AddTask([reason = std::string(a_reason)]() {
				g_worldOpenQueued.store(false);
				OpenInternal(reason);
			});
		}
		return true;
	}

	void Close(std::string_view a_reason)
	{
		CloseInternal(a_reason);
	}

	void QueueClose(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)]() {
			CloseInternal(reason);
		});
	}

	void NotifyStateChanged()
	{
		RunOnGameThread([]() {
			PushState();
		});
	}

	bool IsVisible()
	{
		return g_viewState.IsDesiredVisible() || g_viewState.IsCurrentlyVisible();
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

	void SetAlertPointerFocus(bool a_alertOwnsFocus)
	{
		if (!g_viewState.IsDesiredVisible() || !g_viewState.IsDomReady()) return;
		const bool focus = !a_alertOwnsFocus;
		if (g_viewState.IsDesiredFocused() == focus && g_viewState.IsCurrentlyFocused() == focus) return;
		g_viewState.SetDesiredFocused(focus);
		g_viewState.ApplyState("FactionAlertPointer");
		SyncEscapeOwnership();
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

	void InvokeScript(const std::string& a_script)
	{
		if (!g_api || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
			return;
		}
		g_api->Invoke(g_viewState.GetHandle(), a_script.c_str());
	}
}
