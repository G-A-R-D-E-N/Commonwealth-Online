#include "F4TPrismaUIInternal.h"

#include "QuestCallbacks.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::QuestView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_pauseMenuState;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;
		using F4T::PrismaUI::Internal::g_menuMouseCursorVisible;
		using F4T::PrismaUI::Internal::ApplyMenuFocusForPointerMode;

		constexpr auto kQuestHtmlPath = "CommonwealthOnline/quests/index.html";
		constexpr int  kQuestViewOrder = 620;
		constexpr std::size_t kActionMaxBytes = 1024;
		constexpr auto kRefreshActionJson = R"({"action":"refresh"})";

		F4T::PrismaViewState::Controller g_viewState{ "Quests" };
		bool g_pauseFocusHandedOff = false;

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Quests] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Quests] {}", a_message);
		}

		struct CallbacksHost final : F4T::Quests::IQuestHost
		{
			COMMONWEALTH_ONLINE_UI_API::QuestCallbacks callbacks{};

			bool GetStateJson(
				char*       a_jsonOut,
				std::size_t a_jsonOutSize,
				char*       a_errorBuf,
				std::size_t a_errorBufSize) override
			{
				if (!callbacks.GetStateJson) {
					constexpr std::string_view kNoStateCallback = "no quest state callback";
					if (a_errorBuf && a_errorBufSize > 0) {
						const auto length = (std::min)(kNoStateCallback.size(), a_errorBufSize - 1);
						std::memcpy(a_errorBuf, kNoStateCallback.data(), length);
						a_errorBuf[length] = '\0';
					}
					return false;
				}
				return callbacks.GetStateJson(a_jsonOut, a_jsonOutSize, a_errorBuf, a_errorBufSize);
			}

			void SendAction(const char* a_actionJson) override
			{
				if (!callbacks.SendAction) {
					LogWarn("Dropped quest action — no quest action callback registered.");
					return;
				}
				callbacks.SendAction(a_actionJson);
			}
		};

		CallbacksHost g_callbacksHost{};

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped quest task — game task interface unavailable.");
		}

		void PushState()
		{
			if (!g_api || !g_viewState.IsDesiredVisible() || !g_viewState.IsDomReady() || !g_viewState.IsHandleValid()) {
				return;
			}

			std::string error;
			const auto json = F4T::Quests::PullStateJson(error);
			if (!error.empty()) {
				LogWarn(std::format("State pull failed, showing not-available: {}", error));
			}

			g_api->Invoke(g_viewState.GetHandle(), F4T::Quests::FormatStateScript(json).c_str());
		}

		void SuspendPauseFocus()
		{
			if (g_pauseFocusHandedOff || !g_pauseMenuState.IsDesiredVisible() || !g_pauseMenuState.IsCurrentlyFocused()) {
				return;
			}
			g_pauseFocusHandedOff = true;
			g_pauseMenuState.SetDesiredFocused(false);
			g_pauseMenuState.ApplyState("QuestWindowOpened");
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
			g_viewState.SetDesired(false, false);
			g_viewState.ApplyState(a_reason);
			SyncEscapeOwnership();
			ResumePauseFocus();
		}

		void RequestRefresh()
		{
			F4T::Quests::ForwardAction(F4T::Quests::Action{ F4T::Quests::ActionKind::Refresh, F4T::Quests::Scope::Shared, {} });
		}

		void HandleAction(const std::string& a_json)
		{
			const auto action = F4T::Quests::ParseAction(a_json);
			if (!action) {
				LogWarn("Dropped malformed quest action.");
				return;
			}

			switch (action->kind) {
			case F4T::Quests::ActionKind::Close:
				CloseInternal("QuestCloseRequested");
				return;
			case F4T::Quests::ActionKind::Refresh:
				PushState();
				RequestRefresh();
				return;
			case F4T::Quests::ActionKind::SetScope:
				if (!F4T::Quests::GetHost()) {
					LogWarn("Dropped quest action: no quest host registered.");
					return;
				}
				F4T::Quests::ForwardAction(*action);
				return;
			}
		}

		void OnQuestAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped quest action larger than 1 KB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() {
				HandleAction(payload);
			});
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
						REX::CRITICAL("[COUI] [Quests][JS] {}", a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("[COUI] [Quests][JS] {}", a_message);
						break;
					default:
						REX::INFO("[COUI] [Quests][JS] {}", a_message);
						break;
					}
				});

			g_api->BindUIEvent(a_view, "coQuestAction", OnQuestAction);
			g_api->SetOrder(a_view, kQuestViewOrder);
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
			if (g_viewState.EnsureCreated(kQuestHtmlPath, OnDomReady, true) == 0) {
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

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::QuestCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::QuestCallbacks{}]() {
			g_callbacksHost.callbacks = callbacks;
			F4T::Quests::SetHost(callbacks.GetStateJson ? &g_callbacksHost : nullptr);
			PushState();
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
}
