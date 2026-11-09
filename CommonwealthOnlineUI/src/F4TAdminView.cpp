#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace F4T::AdminView
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

		constexpr auto kAdminHtmlPath = "CommonwealthOnline/admin/index.html";
		constexpr int  kAdminViewOrder = 630;
		constexpr std::size_t kStateBufferSize = 64 * 1024;
		constexpr std::size_t kErrorBufferSize = 512;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kDisabledStateJson = R"({"connected":false,"isAdmin":false})";
		constexpr auto kRefreshActionJson = R"({"op":"refresh"})";
		constexpr auto kResetScript = "if(window.coAdminReset){window.coAdminReset();}";

		F4T::PrismaViewState::Controller g_viewState{ "Admin" };
		PassthroughControllerActions g_passthrough;
		COMMONWEALTH_ONLINE_UI_API::AdminCallbacks g_callbacks{};
		bool g_pauseFocusHandedOff = false;
		bool g_available = false;

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Admin] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Admin] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped admin task — game task interface unavailable.");
		}

		bool IsActionKnown(std::string_view a_op)
		{
			return a_op == "close" || a_op == "refresh" || a_op == "action";
		}

		std::string ErrorStateJson(std::string_view a_message)
		{
			return F4T::ViewJson::Dump(nlohmann::json{ { "connected", true }, { "isAdmin", true }, { "error", std::string(a_message) } });
		}

		void PushStateJson(const std::string& a_json)
		{
			const auto literal = F4T::ViewJson::Dump(nlohmann::json(a_json));
			const auto script = std::format(
				"if(window.coAdminSetState){{window.coAdminSetState({});}}",
				literal);
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
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
					LogWarn(std::format("State pull failed: {}", error.c_str()));
					json = ErrorStateJson("State unavailable.");
				} else if (length == buffer.size()) {
					LogWarn(std::format("State JSON filled the {} byte buffer.", kStateBufferSize));
					json = ErrorStateJson("State too large.");
				} else {
					json.assign(buffer.data(), length);
				}
			}

			try {
				PushStateJson(json);
			} catch (const std::exception& ex) {
				LogWarn(std::format("State could not be displayed: {}", ex.what()));
				PushStateJson(ErrorStateJson("State could not be displayed."));
			}
		}

		void SuspendPauseFocus()
		{
			if (g_pauseFocusHandedOff || !g_pauseMenuState.IsDesiredVisible() || !g_pauseMenuState.IsCurrentlyFocused()) {
				return;
			}
			g_pauseFocusHandedOff = true;
			g_pauseMenuState.SetDesiredFocused(false);
			g_pauseMenuState.ApplyState("AdminWindowOpened");
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

		// Constraint: the admin page closes on Escape, so the view owns Escape only while it is visible and focused.
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
		}

		void CloseInternal(std::string_view a_reason)
		{
			if (!g_viewState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			if (g_api && g_viewState.IsDomReady() && g_viewState.IsHandleValid()) {
				g_api->Invoke(g_viewState.GetHandle(), kResetScript);
			}
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
				LogWarn(std::format("Dropped malformed admin action: {}", ex.what()));
				return;
			}

			if (op == "close") {
				CloseInternal("AdminCloseRequested");
				return;
			}
			if (!IsActionKnown(op)) {
				LogWarn(std::format("Dropped admin action with unknown op '{}'", op));
				return;
			}
			if (op == "refresh") {
				PushState();
			}
			if (!g_callbacks.SendAction) {
				LogWarn(std::format("Dropped admin action '{}': no action callback", op));
				return;
			}
			g_callbacks.SendAction(a_json.c_str());
		}

		void OnAdminAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped admin action larger than 1 KB.");
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
						REX::CRITICAL("[COUI] [Admin][JS] {}", a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("[COUI] [Admin][JS] {}", a_message);
						break;
					default:
						REX::INFO("[COUI] [Admin][JS] {}", a_message);
						break;
					}
				});

			g_api->BindUIEvent(a_view, "coAdminAction", OnAdminAction);
			g_api->SetOrder(a_view, kAdminViewOrder);
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
			if (!g_api) {
				LogWarn("Open skipped — Prisma API unavailable.");
				return;
			}
			if (g_sessionLockoutActive) {
				LogInfo("Open skipped — session lockout active.");
				return;
			}
			if (!g_available) {
				LogInfo(std::format("Open skipped — admin menu not available (reason={}).", a_reason));
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
			if (g_viewState.EnsureCreated(kAdminHtmlPath, OnDomReady, true) == 0) {
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

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::AdminCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::AdminCallbacks{}]() {
			g_callbacks = callbacks;
		});
	}

	void SetAvailable(bool a_available)
	{
		RunOnGameThread([available = a_available]() {
			g_available = available;
			SyncPauseMenuAvailability();
		});
	}

	void SyncPauseMenuAvailability()
	{
		if (!g_api || !g_pauseMenuState.IsDomReady() || !g_pauseMenuState.IsHandleValid()) {
			return;
		}
		const auto script = std::format(
			"if(window.coSetAdminAvailable){{window.coSetAdminAvailable({});}}",
			g_available ? "true" : "false");
		g_api->Invoke(g_pauseMenuState.GetHandle(), script.c_str());
	}

	void Open(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)]() {
			OpenInternal(reason);
		});
	}

	void SyncControllerActions()
	{
		if (g_viewState.IsDesiredVisible()) SyncEscapeOwnership();
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
