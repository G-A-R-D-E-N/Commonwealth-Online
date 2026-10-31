#include "F4TPrismaUIInternal.h"
#include "F4TVoiceBridge.h"

#include <nlohmann/json.hpp>

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

		constexpr auto kFactionHtmlPath = "CommonwealthOnline/faction/index.html";
		constexpr int  kFactionViewOrder = 600;
		// Constraint: 200 friends at the longest name plus the capture budgets need more than 64 KB.
		constexpr std::size_t kStateBufferSize = 256 * 1024;
		constexpr std::size_t kErrorBufferSize = 512;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kDisabledStateJson = R"({"enabled":false})";
		constexpr auto kRefreshActionJson = R"({"op":"refresh"})";

		F4T::PrismaViewState::Controller g_viewState{ "Faction" };
		COMMONWEALTH_ONLINE_UI_API::FactionCallbacks g_callbacks{};
		bool g_pauseFocusHandedOff = false;

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
				a_op == "capture" || a_op == "cancelCapture" || a_op == "refreshCapture" ||
				a_op == "listFactions" || a_op == "friendAccept" || a_op == "friendDecline" ||
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
				const auto literal = nlohmann::json(json).dump();
				const auto script = std::format(
					"if(window.coFactionSetState){{window.coFactionSetState({});}}",
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
			PushState();
		}

		void CloseInternal(std::string_view a_reason)
		{
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			g_viewState.SetDesired(false, false);
			g_viewState.ApplyState(a_reason);
			ResumePauseFocus();
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
			F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::Social, a_view);
			g_api->SetOrder(a_view, kFactionViewOrder);
			ApplyWindowState("DomReady");
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
			if (!g_api) {
				LogWarn("Open skipped — Prisma API unavailable.");
				return;
			}
			if (g_sessionLockoutActive) {
				LogInfo("Open skipped — session lockout active.");
				return;
			}
			if (!g_pauseMenuState.IsCurrentlyVisible()) {
				LogWarn(std::format("Open refused — pause menu is not open (reason={}).", a_reason));
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
			OpenInternal(reason);
		});
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

	bool IsCurrentlyVisible()
	{
		return g_viewState.IsCurrentlyVisible() && g_viewState.IsHandleValid();
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
