#include "F4TPrismaUIInternal.h"

#include "F4TVoiceBridge.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::InfoViews
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

		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr std::size_t kReportStateBytes = 32 * 1024;
		constexpr std::size_t kReportErrorBytes = 256;
		constexpr int         kViewOrder = 620;

		struct Window
		{
			Window(const char* a_name, const char* a_htmlPath) :
				state(a_name),
				htmlPath(a_htmlPath)
			{}

			F4T::PrismaViewState::Controller state;
			const char*                      htmlPath;
			bool                             pauseFocusHandedOff{ false };
			PassthroughControllerActions     passthrough;
		};

		std::array<Window, 3> g_windows{
			Window{ "Server", "CommonwealthOnline/server/index.html" },
			Window{ "Help", "CommonwealthOnline/help/index.html" },
			Window{ "Report", "CommonwealthOnline/report/index.html" }
		};
		std::string g_serverInfoJson;
		std::string g_worldModeJson;
		COMMONWEALTH_ONLINE_UI_API::ReportCallbacks g_reportCallbacks{};

		Window& Get(Kind a_kind)
		{
			return g_windows[static_cast<std::size_t>(a_kind)];
		}

		void LogInfo(const Window& a_window, std::string_view a_message)
		{
			REX::INFO("[COUI] [{}] {}", a_window.state.GetName(), a_message);
		}

		void LogWarn(const Window& a_window, std::string_view a_message)
		{
			REX::WARN("[COUI] [{}] {}", a_window.state.GetName(), a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			REX::WARN("[COUI] [InfoViews] Dropped task - game task interface unavailable.");
		}

		void PushReportState(Window& a_window)
		{
			if (!g_reportCallbacks.GetStateJson) {
				return;
			}
			std::string buffer(kReportStateBytes, '\0');
			std::string error(kReportErrorBytes, '\0');
			if (!g_reportCallbacks.GetStateJson(buffer.data(), buffer.size(), error.data(), error.size())) {
				LogWarn(a_window, std::format("State pull failed: {}", error.c_str()));
				return;
			}
			g_api->Invoke(
				a_window.state.GetHandle(),
				std::format("if(window.coReportSetState){{window.coReportSetState({});}}", buffer.c_str()).c_str());
		}

		void PushState(Window& a_window)
		{
			if (!g_api || !a_window.state.IsDesiredVisible() || !a_window.state.IsDomReady() || !a_window.state.IsHandleValid()) {
				return;
			}
			if (&a_window == &Get(Kind::Report)) {
				PushReportState(a_window);
				return;
			}
			if (&a_window != &Get(Kind::Server)) {
				return;
			}
			if (!g_serverInfoJson.empty()) {
				g_api->Invoke(
					a_window.state.GetHandle(),
					std::format("if(window.coServerSetInfo){{window.coServerSetInfo({});}}", g_serverInfoJson).c_str());
			}
			if (!g_worldModeJson.empty()) {
				g_api->Invoke(
					a_window.state.GetHandle(),
					std::format("if(window.coServerSetMode){{window.coServerSetMode({});}}", g_worldModeJson).c_str());
			}
		}

		void SuspendPauseFocus(Window& a_window)
		{
			if (a_window.pauseFocusHandedOff || !g_pauseMenuState.IsDesiredVisible() || !g_pauseMenuState.IsCurrentlyFocused()) {
				return;
			}
			a_window.pauseFocusHandedOff = true;
			g_pauseMenuState.SetDesiredFocused(false);
			g_pauseMenuState.ApplyState("InfoWindowOpened");
			SyncPauseMenuEscapeOwnership();
		}

		void ResumePauseFocus(Window& a_window)
		{
			if (!a_window.pauseFocusHandedOff) {
				return;
			}
			a_window.pauseFocusHandedOff = false;
			if (!g_pauseMenuState.IsDesiredVisible()) {
				return;
			}
			ApplyMenuFocusForPointerMode(g_menuMouseCursorVisible);
			SyncPauseMenuEscapeOwnership();
		}

		void SyncEscapeOwnership(Window& a_window)
		{
			const auto handle = a_window.state.GetHandle();
			if (handle == 0) {
				return;
			}
			auto* api = F4T::PrismaCapabilities::GetV12();
			if (!api || !api->IsValid(handle)) {
				return;
			}
			api->SetViewOwnsEscape(handle, a_window.state.IsDesiredVisible() && a_window.state.IsCurrentlyFocused());
			SyncPassthroughControllerActions(a_window.state, a_window.passthrough, a_window.state.IsCurrentlyVisible());
		}

		void ApplyWindowState(Window& a_window, std::string_view a_reason)
		{
			if (a_window.state.IsDesiredVisible() && a_window.state.IsHandleValid() && a_window.state.IsDomReady()) {
				SuspendPauseFocus(a_window);
			}
			a_window.state.ApplyState(a_reason);
			if (a_window.state.IsDesiredVisible() && a_window.state.IsDomReady() && !a_window.state.IsCurrentlyFocused()) {
				ResumePauseFocus(a_window);
			}
			SyncEscapeOwnership(a_window);
			PushState(a_window);
		}

		void CloseWindow(Window& a_window, std::string_view a_reason)
		{
			if (!a_window.state.IsDesiredVisible()) {
				return;
			}
			LogInfo(a_window, std::format("Close reason={}", a_reason));
			a_window.state.SetDesired(false, false);
			a_window.state.ApplyState(a_reason);
			SyncEscapeOwnership(a_window);
			ResumePauseFocus(a_window);
		}

		void OnAction(Window& a_window, const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn(a_window, "Dropped action larger than 1 KB.");
				return;
			}
			RunOnGameThread([&a_window, payload = std::string(a_json)]() {
				try {
					const auto action = nlohmann::json::parse(payload).value("action", "");
					if (action == "close") {
						CloseWindow(a_window, "InfoCloseRequested");
					} else if (action == "submit" && &a_window == &Get(Kind::Report) && g_reportCallbacks.SendAction) {
						g_reportCallbacks.SendAction(payload.c_str());
					}
				} catch (const std::exception&) {
					LogWarn(a_window, "Dropped malformed action.");
				}
			});
		}

		void FinishDomReady(Window& a_window, Kind a_kind, PrismaView a_view)
		{
			if (!a_window.state.MarkDomReady(a_view) || !g_api) {
				return;
			}

			g_api->RegisterConsoleCallback(
				a_view,
				[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
					if (!a_message) {
						return;
					}
					if (a_level == PRISMA_UI_API::ConsoleMessageLevel::Error) {
						REX::CRITICAL("[COUI] [InfoViews][JS] {}", a_message);
					} else if (a_level == PRISMA_UI_API::ConsoleMessageLevel::Warning) {
						REX::WARN("[COUI] [InfoViews][JS] {}", a_message);
					} else {
						REX::INFO("[COUI] [InfoViews][JS] {}", a_message);
					}
				});

			if (a_kind == Kind::Server) {
				g_api->BindUIEvent(a_view, "coInfoAction", [](const char* a_json) { OnAction(Get(Kind::Server), a_json); });
			} else if (a_kind == Kind::Help) {
				g_api->BindUIEvent(a_view, "coInfoAction", [](const char* a_json) { OnAction(Get(Kind::Help), a_json); });
			} else {
				g_api->BindUIEvent(a_view, "coInfoAction", [](const char* a_json) { OnAction(Get(Kind::Report), a_json); });
			}
			g_api->SetOrder(a_view, kViewOrder);
			if (a_kind == Kind::Help) {
				F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::Help, a_view);
			}
			ApplyWindowState(a_window, "DomReady");
		}

		void OpenWindow(Window& a_window, std::string_view a_reason, PRISMA_UI_API::OnDomReadyCallback a_onDomReady)
		{
			LogInfo(a_window, std::format("Open reason={}", a_reason));
			if (!g_api) {
				LogWarn(a_window, "Open skipped - Prisma API unavailable.");
				return;
			}
			if (g_sessionLockoutActive) {
				LogInfo(a_window, "Open skipped - session lockout active.");
				return;
			}
			if (!g_pauseMenuState.IsCurrentlyVisible()) {
				LogWarn(a_window, std::format("Open refused - pause menu is not open (reason={}).", a_reason));
				return;
			}
			if (a_window.state.IsDesiredVisible() && a_window.state.IsHandleValid()) {
				PushState(a_window);
				return;
			}

			for (auto& other : g_windows) {
				if (&other != &a_window) {
					CloseWindow(other, "OtherInfoWindowOpened");
				}
			}
			a_window.state.SetDesired(true, true);
			if (a_window.state.EnsureCreated(a_window.htmlPath, a_onDomReady, true) == 0) {
				LogWarn(a_window, "Open failed - CreateView returned no handle.");
				a_window.state.SetDesired(false, false);
				ResumePauseFocus(a_window);
				return;
			}
			ApplyWindowState(a_window, a_reason);
		}

		void OnServerDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() { FinishDomReady(Get(Kind::Server), Kind::Server, a_view); });
		}

		void OnHelpDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() { FinishDomReady(Get(Kind::Help), Kind::Help, a_view); });
		}

		void OnReportDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() { FinishDomReady(Get(Kind::Report), Kind::Report, a_view); });
		}

		PRISMA_UI_API::OnDomReadyCallback DomReadyFor(Kind a_kind)
		{
			switch (a_kind) {
			case Kind::Help:
				return OnHelpDomReady;
			case Kind::Report:
				return OnReportDomReady;
			default:
				return OnServerDomReady;
			}
		}
	}

	void Initialize()
	{
		for (auto& window : g_windows) {
			window.state.SetApi(g_api);
			window.state.SetFocusArgs(false, true);
			window.state.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
			window.state.SetDesired(false, false);
		}
	}

	void Open(Kind a_kind, std::string_view a_reason)
	{
		RunOnGameThread([a_kind, reason = std::string(a_reason)]() {
			OpenWindow(Get(a_kind), reason, DomReadyFor(a_kind));
		});
	}

	void SyncControllerActions()
	{
		for (auto& window : g_windows) {
			if (window.state.IsDesiredVisible()) SyncEscapeOwnership(window);
		}
	}

	void Close(std::string_view a_reason)
	{
		for (auto& window : g_windows) {
			CloseWindow(window, a_reason);
		}
	}

	void QueueClose(std::string_view a_reason)
	{
		RunOnGameThread([reason = std::string(a_reason)]() {
			Close(reason);
		});
	}

	void NotifyServerInfo(std::string_view a_json)
	{
		RunOnGameThread([json = std::string(a_json)]() {
			g_serverInfoJson = json;
			PushState(Get(Kind::Server));
		});
	}

	void NotifyWorldMode(std::string_view a_json)
	{
		RunOnGameThread([json = std::string(a_json)]() {
			g_worldModeJson = json;
			PushState(Get(Kind::Server));
		});
	}

	void SetReportCallbacks(const COMMONWEALTH_ONLINE_UI_API::ReportCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::ReportCallbacks{}]() {
			g_reportCallbacks = callbacks;
		});
	}

	void PushReportEvent(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		RunOnGameThread([json = std::string(a_json)]() {
			auto& window = Get(Kind::Report);
			if (!g_api || !window.state.IsCurrentlyVisible() || !window.state.IsDomReady() || !window.state.IsHandleValid()) {
				return;
			}
			g_api->Invoke(
				window.state.GetHandle(),
				std::format("if(window.coReportEvent){{window.coReportEvent({});}}", json).c_str());
		});
	}

	bool IsVisible()
	{
		for (const auto& window : g_windows) {
			if (window.state.IsDesiredVisible() || window.state.IsCurrentlyVisible()) {
				return true;
			}
		}
		return false;
	}

	void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY)
	{
		for (auto& window : g_windows) {
			if (!g_api || !window.state.IsCurrentlyVisible() || !window.state.IsDomReady() || !window.state.IsHandleValid()) {
				continue;
			}
			const auto script = std::format(
				"if(window.handleControllerAxes){{window.handleControllerAxes({:.4f},{:.4f},{:.4f},{:.4f});}}",
				a_leftX, a_leftY, a_rightX, a_rightY);
			g_api->Invoke(window.state.GetHandle(), script.c_str());
		}
	}

	bool IsCurrentlyVisible()
	{
		for (const auto& window : g_windows) {
			if (window.state.IsCurrentlyVisible() && window.state.IsHandleValid()) {
				return true;
			}
		}
		return false;
	}

	void DispatchControllerInput(const char* a_button, const char* a_phase)
	{
		if (!g_api) {
			return;
		}
		const auto script = std::format(
			"if(window.handleControllerInput){{window.handleControllerInput('{}', '{}');}}",
			a_button ? a_button : "",
			a_phase ? a_phase : "");
		for (auto& window : g_windows) {
			if (window.state.IsCurrentlyVisible() && window.state.IsDomReady() && window.state.IsHandleValid()) {
				g_api->Invoke(window.state.GetHandle(), script.c_str());
			}
		}
	}
}
