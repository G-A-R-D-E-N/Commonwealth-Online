#include "pch.h"

#include "F4TAlertSystem.h"
#include "F4TPrismaCapabilities.h"
#include "F4TPrismaUI.h"
#include "F4TPrismaViewState.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace F4T::Alerts
{
	namespace
	{
		constexpr auto kAlertsHtmlPath = "CommonwealthOnline/alerts/index.html";
		constexpr int  kAlertViewOrder = 500;

		struct PendingResponse
		{
			AlertResponseFn callback{ nullptr };
			void*           userData{ nullptr };
		};

		struct PendingShow
		{
			std::string     json;
			PendingResponse response;
			std::string     id;
		};

		PRISMA_UI_API::IVPrismaUI4* g_api = nullptr;
		F4T::PrismaViewState::Controller g_viewState{ "Alerts" };
		// Alerts are a passive transparent overlay: always desired-visible, never desired-focused
		// except temporary pointer hit-routing via FocusForPointer.
		bool g_pointerFocusActive = false;
		std::atomic_bool            g_menuInteraction{ false };
		std::atomic_bool            g_hasActionable{ false };

		std::mutex                              g_mutex;
		std::unordered_map<std::string, PendingResponse> g_responses;
		std::vector<PendingShow>                g_pendingShows;
		std::string                             g_lastShownId;
		std::uint32_t                           g_autoId = 0;

		struct HitRegion
		{
			bool          active{ false };
			std::int32_t  left{ 0 };
			std::int32_t  top{ 0 };
			std::int32_t  right{ 0 };
			std::int32_t  bottom{ 0 };
		};

		HitRegion g_hitRegion{};

		std::string EscapeForLog(std::string_view a_text)
		{
			return std::string{ a_text };
		}

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Alerts] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Alerts] {}", a_message);
		}

		void ApplyInputRegion(const HitRegion& a_region)
		{
			if (!F4T::PrismaCapabilities::HasInputRegions() || !g_viewState.IsHandleValid()) {
				return;
			}

			auto* apiV10 = F4T::PrismaCapabilities::GetV10();
			if (!apiV10) {
				return;
			}

			const PRISMA_UI_API::InputRegion region{ a_region.left, a_region.top, a_region.right - a_region.left, a_region.bottom - a_region.top };
			apiV10->SetInputRegions(g_viewState.GetHandle(), a_region.active ? &region : nullptr, a_region.active ? 1u : 0u);
		}

		std::string MakeAutoId()
		{
			++g_autoId;
			return std::format("co-alert-{}", g_autoId);
		}

		nlohmann::json BuildAlertJson(const AlertRequest& a_request, const std::string& a_id)
		{
			nlohmann::json payload{
				{ "id", a_id },
				{ "category", a_request.category && a_request.category[0] ? a_request.category : "ALERT" },
				{ "title", a_request.title ? a_request.title : "" },
				{ "message", a_request.message ? a_request.message : "" },
				{ "tone", a_request.tone && a_request.tone[0] ? a_request.tone : "info" },
				{ "durationMs", a_request.durationMs },
				{ "actions", nlohmann::json::array() }
			};

			if (a_request.actions && a_request.actionCount > 0) {
				for (std::size_t i = 0; i < a_request.actionCount; ++i) {
					const auto& action = a_request.actions[i];
					if (!action.id || !action.id[0]) {
						continue;
					}
					payload["actions"].push_back({
						{ "id", action.id },
						{ "label", action.label && action.label[0] ? action.label : action.id },
						{ "hotkey", action.hotkey ? action.hotkey : "" }
					});
				}
			}

			return payload;
		}

		void InvokeShow(const std::string& a_json)
		{
			if (!g_api || !g_viewState.IsHandleValid()) {
				return;
			}

			const auto script = std::format(
				"if(window.showCoAlert){{window.showCoAlert({});}}",
				a_json);
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
		}

		void InvokeInteractionMode()
		{
			if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
				return;
			}

			const auto mode = g_menuInteraction.load() ? "mouse" : "hotkey";
			const auto script = std::format(
				"if(window.setCoAlertInteractionMode){{window.setCoAlertInteractionMode('{}');}}",
				mode);
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
		}

		void DeliverResponse(const std::string& a_alertId, const std::string& a_actionId)
		{
			PendingResponse response{};
			{
				std::scoped_lock lock(g_mutex);
				const auto it = g_responses.find(a_alertId);
				if (it == g_responses.end()) {
					return;
				}
				response = it->second;
				g_responses.erase(it);
			}

			if (response.callback) {
				response.callback(a_alertId.c_str(), a_actionId.c_str(), response.userData);
			}
		}

		void OnAlertAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}

			try {
				const auto payload = nlohmann::json::parse(a_json);
				const auto id = payload.value("id", "");
				const auto action = payload.value("action", "");
				if (id.empty() || action.empty()) {
					return;
				}

				LogInfo(std::format("Alert '{}' -> '{}'", id, action));
				DeliverResponse(id, action);
			} catch (const std::exception& ex) {
				LogWarn(std::format("Failed to parse alert action: {}", ex.what()));
			}
		}

		void OnAlertHitRegion(const char* a_json)
		{
			if (!a_json) {
				g_hitRegion = {};
				ApplyInputRegion(g_hitRegion);
				return;
			}

			try {
				const auto payload = nlohmann::json::parse(a_json);
				HitRegion region{};
				region.active = payload.value("active", false);
				if (region.active) {
					region.left = payload.value("left", 0);
					region.top = payload.value("top", 0);
					region.right = payload.value("right", 0);
					region.bottom = payload.value("bottom", 0);
					if (region.right <= region.left || region.bottom <= region.top) {
						region = {};
					}
				}
				g_hitRegion = region;
				ApplyInputRegion(g_hitRegion);
			} catch (const std::exception& ex) {
				LogWarn(std::format("Failed to parse alert hit region: {}", ex.what()));
				g_hitRegion = {};
				ApplyInputRegion(g_hitRegion);
			}
		}

		void FlushPending()
		{
			std::vector<PendingShow> pending;
			{
				std::scoped_lock lock(g_mutex);
				pending.swap(g_pendingShows);
			}

			for (auto& item : pending) {
				{
					std::scoped_lock lock(g_mutex);
					g_responses[item.id] = item.response;
				}
				const auto payload = nlohmann::json::parse(item.json);
				if (payload.value("actions", nlohmann::json::array()).size() > 0) {
					g_hasActionable = true;
				}
				InvokeShow(item.json);
			}

			InvokeInteractionMode();
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
						REX::CRITICAL("[COUI] [Alerts][JS] {}", a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("[COUI] [Alerts][JS] {}", a_message);
						break;
					default:
						REX::INFO("[COUI] [Alerts][JS] {}", a_message);
						break;
					}
				});

			g_api->BindUIEvent(a_view, "coAlertAction", OnAlertAction);
			g_api->BindUIEvent(a_view, "coAlertHitRegion", OnAlertHitRegion);
			g_api->SetOrder(a_view, kAlertViewOrder);

			// Passive overlay: always visible, not focused for input unless pointer hit-routing.
			g_viewState.SetDesired(true, false);
			g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
			g_viewState.AssignRoleIfNeeded();
			g_viewState.ApplyState("DomReady");
			ApplyInputRegion(g_hitRegion);
			FlushPending();
		}

		void OnDomReady(PrismaView a_view)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				FinishDomReady(a_view);
				return;
			}
			taskInterface->AddTask([a_view]() {
				FinishDomReady(a_view);
			});
		}

		bool g_runtimeReady = false;

		void CreateView()
		{
			if (!g_api) {
				return;
			}

			// Alerts always want to be a visible passive overlay once ready.
			// Role is kWidget so this view never blocks interactive panels.
			g_viewState.SetDesired(true, false);
			g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
			const auto handle = g_viewState.EnsureCreated(kAlertsHtmlPath, OnDomReady, g_runtimeReady);
			if (handle == 0) {
				return;
			}

			g_api->SetOrder(handle, kAlertViewOrder);
			g_viewState.AssignRoleIfNeeded();
			if (g_viewState.IsDomReady()) {
				g_viewState.ApplyState("EnsureCreatedAlreadyReady");
			}
		}

		bool IsVirtualKeyDownEdge(int a_vk, bool& a_wasDown)
		{
			const bool down = (::GetAsyncKeyState(a_vk) & 0x8000) != 0;
			const bool edge = down && !a_wasDown;
			a_wasDown = down;
			return edge;
		}

		void CopyIdOut(const std::string& a_id, char* a_idOut, std::size_t a_idOutSize)
		{
			if (!a_idOut || a_idOutSize == 0) {
				return;
			}
			const auto copyLen = (std::min)(a_id.size(), a_idOutSize - 1);
			std::memcpy(a_idOut, a_id.data(), copyLen);
			a_idOut[copyLen] = '\0';
		}
	}

	void Initialize(PRISMA_UI_API::IVPrismaUI4* a_api)
	{
		g_api = a_api;
		g_viewState.SetApi(a_api);
		g_viewState.SetFocusArgs(false, true);
		g_viewState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_viewState.SetDesired(true, false);
		g_runtimeReady = false;
	}

	void EnsureView()
	{
		if (!g_api) {
			return;
		}

		g_runtimeReady = true;

		// One controlled retry after CreateView(0), independent of other panels.
		if (g_viewState.DidCreateFail() && !g_viewState.CanRetryCreate()) {
			static bool s_alertsRetryArmed = false;
			if (!s_alertsRetryArmed) {
				s_alertsRetryArmed = true;
				g_viewState.AllowCreateRetry();
			}
		}

		if (g_viewState.IsHandleValid()) {
			g_viewState.AssignRoleIfNeeded();
			if (g_viewState.IsDomReady() &&
				g_viewState.IsDesiredVisible() &&
				!g_viewState.IsCurrentlyVisible()) {
				g_viewState.ApplyState("EnsureViewPendingShow");
			}
			return;
		}

		if (g_viewState.DidCreateFail() && !g_viewState.CanRetryCreate()) {
			return;
		}

		if (g_viewState.GetHandle() != 0) {
			g_viewState.InvalidateApplied();
		}
		CreateView();
	}

	void AllowCreateRetry()
	{
		g_viewState.AllowCreateRetry();
	}

	bool HasView()
	{
		return g_viewState.IsHandleValid();
	}

	void Shutdown()
	{
		std::scoped_lock lock(g_mutex);
		g_responses.clear();
		g_pendingShows.clear();
		g_hasActionable = false;
		g_hitRegion = {};
		ApplyInputRegion(g_hitRegion);
		g_pointerFocusActive = false;
		g_viewState.SetDesired(false, false);
		g_viewState.ApplyState("Shutdown");
		g_viewState.ClearHandle();
		g_api = nullptr;
		g_viewState.SetApi(nullptr);
	}

	bool Show(
		const AlertRequest& a_request,
		AlertResponseFn     a_onResponse,
		void*               a_userData,
		char*               a_idOut,
		std::size_t         a_idOutSize)
	{
		std::string id =
			(a_request.id && a_request.id[0]) ? std::string{ a_request.id } : MakeAutoId();

		const auto payload = BuildAlertJson(a_request, id);
		const auto json = payload.dump();
		const bool hasActions = payload.value("actions", nlohmann::json::array()).size() > 0;

		{
			std::scoped_lock lock(g_mutex);
			g_responses[id] = PendingResponse{ a_onResponse, a_userData };
			g_lastShownId = id;
		}

		CopyIdOut(id, a_idOut, a_idOutSize);

		LogInfo(std::format(
			"Show '{}' — {} / {}",
			id,
			EscapeForLog(a_request.category ? a_request.category : "ALERT"),
			EscapeForLog(a_request.title ? a_request.title : "")));

		if (!g_api) {
			return false;
		}

		if (!g_viewState.IsHandleValid()) {
			if (g_runtimeReady) {
				CreateView();
			}
		}

		if (!g_viewState.IsDomReady()) {
			std::scoped_lock lock(g_mutex);
			g_pendingShows.push_back(PendingShow{ json, PendingResponse{ a_onResponse, a_userData }, id });
			if (hasActions) {
				g_hasActionable = true;
			}
			return true;
		}

		if (hasActions) {
			g_hasActionable = true;
		}
		InvokeShow(json);
		InvokeInteractionMode();
		// Keep the passive overlay shown via desired state (never Focus here).
		g_viewState.SetDesiredVisible(true);
		if (!g_pointerFocusActive) {
			g_viewState.SetDesiredFocused(false);
		}
		g_viewState.ApplyState("ShowAlert");
		return true;
	}

	void Dismiss(const char* a_alertId)
	{
		if (!a_alertId || !a_alertId[0]) {
			return;
		}

		{
			std::scoped_lock lock(g_mutex);
			g_responses.erase(a_alertId);
		}

		if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			return;
		}

		const auto script = std::format(
			"if(window.dismissCoAlert){{window.dismissCoAlert({});}}",
			nlohmann::json(a_alertId).dump());
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void DismissAll()
	{
		{
			std::scoped_lock lock(g_mutex);
			g_responses.clear();
			g_pendingShows.clear();
		}
		g_hasActionable = false;
		g_hitRegion = {};
		ApplyInputRegion(g_hitRegion);

		if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			return;
		}

		g_api->Invoke(
			g_viewState.GetHandle(),
			"if(window.dismissAllCoAlerts){window.dismissAllCoAlerts(true);}");
	}

	void SetMenuInteractionActive(bool a_active)
	{
		g_menuInteraction = a_active;
		if (g_viewState.IsDomReady()) {
			InvokeInteractionMode();
		}
		if (!a_active) {
			g_hitRegion = {};
			ApplyInputRegion(g_hitRegion);
			ClearPointerFocus("MenuInteractionOff");
		}
	}

	bool IsPointerOverInteractive(std::int32_t a_x, std::int32_t a_y)
	{
		if (!g_menuInteraction.load() || !g_hitRegion.active) {
			return false;
		}

		return a_x >= g_hitRegion.left && a_x < g_hitRegion.right &&
			a_y >= g_hitRegion.top && a_y < g_hitRegion.bottom;
	}

	PrismaView GetView()
	{
		if (!g_viewState.IsHandleValid()) {
			return 0;
		}
		return g_viewState.GetHandle();
	}

	void FocusForPointer(const char* a_reason)
	{
		if (!g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			return;
		}

		// Alerts must already be shown as the passive overlay before Focus.
		g_viewState.SetDesiredVisible(true);
		if (!g_viewState.IsCurrentlyVisible()) {
			g_viewState.ApplyState(a_reason ? a_reason : "FocusForPointerShow");
		}

		if (g_pointerFocusActive && g_viewState.IsCurrentlyFocused()) {
			return;
		}

		g_pointerFocusActive = true;
		g_viewState.SetDesiredFocused(true);
		g_viewState.ApplyState(a_reason ? a_reason : "FocusForPointer");
	}

	void ClearPointerFocus(const char* a_reason)
	{
		if (!g_pointerFocusActive && !g_viewState.IsDesiredFocused() && !g_viewState.IsCurrentlyFocused()) {
			return;
		}

		g_pointerFocusActive = false;
		g_viewState.SetDesiredFocused(false);
		if (g_viewState.IsHandleValid() && g_viewState.IsDomReady()) {
			g_viewState.ApplyState(a_reason ? a_reason : "ClearPointerFocus");
		}
	}

	void DumpViewState()
	{
		g_viewState.LogDiagnostics("F11");
		LogInfo(std::format(
			"Alerts extras: pointerFocusActive={} menuInteraction={} hasActionable={} runtimeReady={} role=kWidget",
			g_pointerFocusActive,
			g_menuInteraction.load(),
			g_hasActionable.load(),
			g_runtimeReady));

		if (g_viewState.IsFullyAppliedForVisibleSurface() && !g_pointerFocusActive) {
			LogInfo(
				"[Alerts] View state applied successfully (passive widget). "
				"If toasts are invisible, possible Prisma rendering/compositing failure.");
		}
	}

	bool TextInputOwnsKeyboard()
	{
		if (const auto* ui = RE::UI::GetSingleton();
			ui && ui->GetMenuOpen(RE::BSFixedString{ RE::Console::MENU_NAME })) {
			return true;
		}

		const auto focusedView = F4T::PrismaCapabilities::TryGetFocusedView();
		if (focusedView != 0) {
			return focusedView != g_viewState.GetHandle();
		}

		return F4T::PrismaCapabilities::HasAnyActiveFocus() && !g_viewState.IsCurrentlyFocused();
	}

	void PollHotkeys()
	{
		static bool wasY = false;
		static bool wasN = false;
		static bool wasF1 = false;
		static bool wasF2 = false;
		static bool wasT = false;

		struct HotkeyEdge
		{
			int         vk;
			bool*       wasDown;
			const char* name;
		};

		HotkeyEdge edges[] = {
			{ 'Y', &wasY, "Y" },
			{ 'N', &wasN, "N" },
			{ VK_F1, &wasF1, "F1" },
			{ VK_F2, &wasF2, "F2" },
			{ 'T', &wasT, "T" },
		};

		const char* pressed = nullptr;
		for (auto& edge : edges) {
			if (IsVirtualKeyDownEdge(edge.vk, *edge.wasDown) && !pressed) {
				pressed = edge.name;
			}
		}

		if (!pressed) {
			return;
		}

		if (!F4T::PrismaUI::IsGameForeground()) {
			return;
		}

		if (!g_hasActionable.load() || g_menuInteraction.load()) {
			return;
		}

		if (F4T::PrismaUI::IsMainMenuVisible() ||
			F4T::PrismaUI::IsBrowserVisible() ||
			F4T::PrismaUI::IsPauseMenuVisible()) {
			return;
		}

		if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			return;
		}

		if (TextInputOwnsKeyboard()) {
			return;
		}

		const auto script = std::format(
			"if(window.resolveCoAlertHotkey){{window.resolveCoAlertHotkey({});}}",
			nlohmann::json(pressed).dump());
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void PollDemoHotkey()
	{
		if (!F4T::PrismaUI::IsGameForeground()) {
			return;
		}

		static bool wasF10 = false;
		static std::size_t demoIndex = 0;

		const bool down = (::GetAsyncKeyState(VK_F10) & 0x8000) != 0;
		const bool edge = down && !wasF10;
		wasF10 = down;
		if (!edge) {
			return;
		}

		struct DemoAlert
		{
			const char* id;
			const char* category;
			const char* title;
			const char* message;
			const char* tone;
			std::uint32_t durationMs;
			bool actionable;
		};

		static constexpr DemoAlert kDemos[] = {
			{ "demo-join", "Social", "Player Requested To Join",
			  "VaultDweller_92 wants to join your session.", "invite", 0, true },
			{ "demo-friend", "Social", "Friend Invite Received",
			  "LoneWanderer invited you to Commonwealth Eastern.", "invite", 12000, true },
			{ "demo-connect", "Network", "Can't Connect To Server",
			  "Timed out reaching 192.168.1.40:7777.", "error", 10000, false },
			{ "demo-dc", "Network", "Disconnected From Server",
			  "Connection lost. Returning to local session.", "warning", 8000, false },
		};

		const auto& demo = kDemos[demoIndex % std::size(kDemos)];
		++demoIndex;

		AlertAction actions[2] = {
			{ "accept", "Accept", "Y" },
			{ "decline", "Decline", "N" }
		};

		AlertRequest request{};
		request.id = demo.id;
		request.category = demo.category;
		request.title = demo.title;
		request.message = demo.message;
		request.tone = demo.tone;
		request.durationMs = demo.durationMs;
		if (demo.actionable) {
			request.actions = actions;
			request.actionCount = 2;
		}

		Show(
			request,
			[](const char* a_alertId, const char* a_actionId, void*) {
				REX::INFO("[COUI] [Alerts] Demo response '{}' -> '{}'", a_alertId, a_actionId);
			},
			nullptr);
	}
}
