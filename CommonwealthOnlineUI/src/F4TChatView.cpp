#include "F4TPrismaUIInternal.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::ChatView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kFeedHtmlPath = "CommonwealthOnline/chat-feed/index.html";
		constexpr auto kWindowHtmlPath = "CommonwealthOnline/chat/index.html";
		constexpr int  kFeedViewOrder = 480;
		constexpr int  kWindowViewOrder = 610;
		constexpr std::size_t kEventMaxBytes = 32 * 1024;
		constexpr std::size_t kActionMaxBytes = 4 * 1024;
		constexpr std::size_t kHistoryCapacity = 100;
		constexpr auto kReleaseFocusJson = R"({"op":"focus","focused":false})";

		F4T::PrismaViewState::Controller g_feedState{ "ChatFeed" };
		F4T::PrismaViewState::Controller g_windowState{ "ChatWindow" };
		COMMONWEALTH_ONLINE_UI_API::ChatCallbacks g_callbacks{};
		nlohmann::json g_state{ { "kind", "state" }, { "enabled", false }, { "localPlayerId", "" } };
		std::deque<nlohmann::json> g_messages;
		bool g_enabled = false;
		bool g_feedDesired = false;
		bool g_windowRequested = false;
		bool g_windowDesired = false;
		bool g_ownsEscapeAssigned = false;
		bool g_cursorWritten = false;
		bool g_cursorVisible = false;
		bool g_cursorFocused = false;

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Chat] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Chat] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped chat task — game task interface unavailable.");
		}

		std::string EventScript(const nlohmann::json& a_event)
		{
			return std::format("if(window.coChatEvent){{window.coChatEvent({});}}", a_event.dump(-1, ' ', true));
		}

		const char* kOpenScript = "if(window.coChatOpen){window.coChatOpen();}";
		const char* kCloseScript = "if(window.coChatClose){window.coChatClose();}";

		void InvokeFeed(const std::string& a_script)
		{
			if (!g_api || !g_feedState.IsDomReady() || !g_feedState.IsHandleValid()) {
				return;
			}
			g_api->Invoke(g_feedState.GetHandle(), a_script.c_str());
		}

		void InvokeWindow(const std::string& a_script)
		{
			if (!g_api || !g_windowState.IsDomReady() || !g_windowState.IsHandleValid()) {
				return;
			}
			g_api->Invoke(g_windowState.GetHandle(), a_script.c_str());
		}

		void WriteWindowCursor(const char* a_reason)
		{
			if (!g_api || !g_windowState.IsDomReady() || !g_windowState.IsHandleValid()) {
				return;
			}

			const bool visible = g_windowState.IsCurrentlyVisible();
			if (g_cursorWritten && g_cursorVisible == visible) {
				return;
			}

			F4T::PrismaViewState::SetViewCursorPolicyCached(
				g_windowState.GetHandle(),
				visible ? PRISMA_UI_API::CursorPolicy::Default : PRISMA_UI_API::CursorPolicy::Hidden);
			g_cursorWritten = true;
			g_cursorVisible = visible;
			LogInfo(std::format("Pointer {} reason={}", visible ? "shown" : "hidden", a_reason));
		}

		void TrimHistory()
		{
			while (g_messages.size() > kHistoryCapacity) {
				g_messages.pop_front();
			}
		}

		nlohmann::json HistoryEvent()
		{
			auto messages = nlohmann::json::array();
			for (const auto& message : g_messages) {
				messages.push_back(message);
			}
			return { { "kind", "history" }, { "messages", messages } };
		}

		bool VanillaMenuOwnsScreen()
		{
			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}
			static const RE::BSFixedString kMenus[]{ "LooksMenu", "SPECIALMenu", "PipboyMenu", "ContainerMenu", "WorkshopMenu", "TerminalMenu", "DialogueMenu", "BarterMenu", "LockpickingMenu", "ExamineMenu", "LevelUpMenu", "BookMenu", "SleepWaitMenu" };
			for (const auto& name : kMenus) {
				if (ui->GetMenuOpen(name)) {
					return true;
				}
			}
			return false;
		}

		bool ScreenWantsChat()
		{
			return !F4T::PrismaUI::IsMainMenuVisible() && !F4T::PrismaUI::IsBrowserVisible() &&
				!F4T::PrismaUI::IsPauseMenuVisible() && !F4T::FactionView::IsVisible() &&
				!F4T::QuestView::IsVisible() && !F4T::CharacterSelectView::IsVisible() && !g_sessionLockoutActive && !VanillaMenuOwnsScreen();
		}

		bool WindowWanted()
		{
			return g_enabled && g_windowRequested && ScreenWantsChat();
		}

		bool FeedWanted()
		{
			return g_enabled && ScreenWantsChat() && !WindowWanted();
		}

		void PushWindowState()
		{
			InvokeWindow(EventScript(g_state));
			InvokeWindow(EventScript(HistoryEvent()));
		}

		void FinishFeedDomReady(PrismaView a_view)
		{
			if (!g_feedState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			g_feedState.AssignRoleIfNeeded();
			g_feedState.ApplyState("DomReady");
		}

		void OnFeedDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishFeedDomReady(a_view);
			});
		}

		void OnWindowAction(const char* a_json);

		void AssignWindowInputOwnership(PrismaView a_view)
		{
			if (g_ownsEscapeAssigned || a_view == 0 || !F4T::PrismaCapabilities::HasV10()) {
				return;
			}
			if (auto* api = F4T::PrismaCapabilities::GetV10()) {
				api->SetViewOwnsEscape(a_view, true);
				g_ownsEscapeAssigned = true;
			}
		}

		void FinishWindowDomReady(PrismaView a_view)
		{
			if (!g_windowState.MarkDomReady(a_view) || !g_api) {
				return;
			}
			g_windowState.AssignRoleIfNeeded();
			AssignWindowInputOwnership(a_view);
			g_api->BindUIEvent(a_view, "coChatAction", OnWindowAction);
			g_api->SetOrder(a_view, kWindowViewOrder);
			g_windowState.ApplyState("DomReady");
			WriteWindowCursor("DomReady");
			if (g_windowState.IsCurrentlyVisible()) {
				PushWindowState();
				InvokeWindow(kOpenScript);
			}
		}

		void OnWindowDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishWindowDomReady(a_view);
			});
		}

		void HandleEvent(const std::string& a_json)
		{
			try {
				const auto event = nlohmann::json::parse(a_json);
				const auto kind = event.at("kind").get<std::string>();
				if (kind == "state") {
					g_enabled = event.at("enabled").get<bool>();
					if (!g_enabled) {
						g_messages.clear();
						g_windowRequested = false;
					}
					g_state = event;
				} else if (kind == "message") {
					const auto& message = event.at("message");
					if (!message.is_object()) {
						LogWarn("Dropped chat message without a message object.");
						return;
					}
					g_messages.push_back(message);
					TrimHistory();
				} else if (kind == "history") {
					const auto& messages = event.at("messages");
					if (!messages.is_array()) {
						LogWarn("Dropped chat history without a messages array.");
						return;
					}
					g_messages.clear();
					for (const auto& message : messages) {
						if (message.is_object()) {
							g_messages.push_back(message);
						}
					}
					TrimHistory();
				} else if (kind != "rejected") {
					LogWarn(std::format("Dropped chat event with unknown kind '{}'.", kind));
					return;
				}

				const auto script = EventScript(event);
				InvokeWindow(script);
				if (kind == "message") {
					InvokeFeed(script);
				}
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed chat event: {}", ex.what()));
			}
		}

		void CloseInternal(std::string_view a_reason)
		{
			if (!g_windowRequested && !g_windowState.IsDesiredVisible()) {
				return;
			}
			LogInfo(std::format("Close reason={}", a_reason));
			g_windowRequested = false;
			g_windowState.SetDesired(false, false);
			InvokeWindow(kCloseScript);
			g_windowState.ApplyState(a_reason);
			WriteWindowCursor("Close");
			if (g_callbacks.SendAction) {
				g_callbacks.SendAction(kReleaseFocusJson);
			}
		}

		void OpenInternal(std::string_view a_reason)
		{
			if (!g_enabled) {
			LogInfo("Open ignored — chat is not available on this server.");
			return;
		}
		if (g_windowRequested && g_windowState.IsDesiredVisible()) {
			return;
		}
		if (!ScreenWantsChat()) {
			LogInfo(std::format("Open ignored — another screen owns the display. reason={}", a_reason));
				return;
			}
			if (!g_api) {
				LogWarn("Open ignored — Prisma API unavailable.");
				return;
			}

			LogInfo(std::format("Open reason={}", a_reason));
			g_windowRequested = true;
			g_windowState.SetDesired(true, true);
			if (g_windowState.EnsureCreated(kWindowHtmlPath, OnWindowDomReady, g_prismaReady) == 0) {
				return;
			}

			g_api->SetOrder(g_windowState.GetHandle(), kWindowViewOrder);
			AssignWindowInputOwnership(g_windowState.GetHandle());
			g_windowState.ApplyState(a_reason);
			WriteWindowCursor("Open");
			if (g_windowState.IsCurrentlyVisible()) {
				PushWindowState();
				InvokeWindow(kOpenScript);
			}
		}

		void HandleAction(const std::string& a_json)
		{
			std::string op;
			try {
				op = nlohmann::json::parse(a_json).at("op").get<std::string>();
			} catch (const std::exception& ex) {
				LogWarn(std::format("Dropped malformed chat action: {}", ex.what()));
				return;
			}
			if (op == "close") {
				CloseInternal("ChatCloseRequested");
				return;
			}
			if (op != "send" && op != "history" && op != "focus") {
				LogWarn(std::format("Dropped chat action with unknown op '{}'", op));
				return;
			}
			if (!g_callbacks.SendAction) {
				LogWarn(std::format("Dropped chat action '{}': no action callback", op));
				return;
			}
			g_callbacks.SendAction(a_json.c_str());
		}

		void OnWindowAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped chat action larger than 4 KiB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() {
				HandleAction(payload);
			});
		}
	}

	void Initialize()
	{
		g_feedState.SetApi(g_api);
		g_feedState.SetFocusArgs(false, true);
		g_feedState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_feedState.SetDesired(false, false);

		g_windowState.SetApi(g_api);
		g_windowState.SetFocusArgs(false, true);
		g_windowState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_windowState.SetDesired(false, false);
		g_windowRequested = false;
		g_windowDesired = false;
		g_ownsEscapeAssigned = false;
		g_cursorWritten = false;
		g_cursorVisible = false;
		g_cursorFocused = false;
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::ChatCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::ChatCallbacks{}]() {
			g_callbacks = callbacks;
		});
	}

	void PushEvent(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kEventMaxBytes + 1) > kEventMaxBytes) {
			LogWarn("Dropped chat event larger than 32 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			HandleEvent(payload);
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
		RunOnGameThread([reason = std::string(a_reason)]() {
			CloseInternal(reason);
		});
	}

	bool IsOpen()
	{
		return g_windowRequested;
	}

	void ReleaseInputFocus()
	{
		RunOnGameThread([]() {
			if (g_callbacks.SendAction) {
				g_callbacks.SendAction(kReleaseFocusJson);
			}
		});
	}

	void Tick()
	{
		if (!g_api) {
			return;
		}

		const bool focused = g_windowState.IsCurrentlyFocused();
		if (focused != g_cursorFocused) {
			g_cursorFocused = focused;
			g_cursorWritten = false;
			WriteWindowCursor("FocusChanged");
		}

		const auto feedWanted = FeedWanted();
		if (feedWanted && !g_feedState.IsHandleValid()) {
			if (g_feedState.EnsureCreated(kFeedHtmlPath, OnFeedDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(g_feedState.GetHandle(), kFeedViewOrder);
			}
		}
		if (feedWanted != g_feedDesired) {
			g_feedDesired = feedWanted;
			g_feedState.SetDesiredVisible(feedWanted);
			g_feedState.ApplyState(feedWanted ? "FeedShow" : "FeedHide");
		}

		if (!WindowWanted()) {
			if (g_windowDesired || g_windowState.IsDesiredVisible()) {
				g_windowDesired = false;
				g_windowRequested = false;
				CloseInternal("ChatWindowWantedFalse");
			}
			return;
		}
		g_windowDesired = true;

		if (!g_windowState.IsHandleValid()) {
			if (g_windowState.EnsureCreated(kWindowHtmlPath, OnWindowDomReady, g_prismaReady) == 0) {
				return;
			}
			g_api->SetOrder(g_windowState.GetHandle(), kWindowViewOrder);
			AssignWindowInputOwnership(g_windowState.GetHandle());
		}
		if (g_windowState.IsDomReady() && !g_windowState.IsCurrentlyVisible()) {
			g_windowState.ApplyState("ChatWindowPendingShow");
			WriteWindowCursor("ChatWindowPendingShow");
			if (g_windowState.IsCurrentlyVisible()) {
				PushWindowState();
				InvokeWindow(kOpenScript);
			}
		}
	}
}
