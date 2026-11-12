#include "pch.h"
#include "F4TParkedSuitView.h"
#include "F4TViewJson.h"

#include "F4TAlertSystem.h"
#include "F4TPrismaCapabilities.h"
#include "F4TPrismaUI.h"
#include "F4TPrismaViewState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
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
		constexpr int  kAlertViewOrder = 660;
		constexpr std::int32_t kPointerLeaveMargin = 24;

		struct PendingResponse
		{
			AlertResponseFn callback{ nullptr };
			void*           userData{ nullptr };
			std::uint64_t   sequence{ 0 };
			std::string     json;
			std::chrono::steady_clock::time_point deadline{};
		};

		struct PendingShow
		{
			std::string     json;
			std::string     id;
			std::uint64_t   sequence{ 0 };
		};

		PRISMA_UI_API::IVPrismaUI4* g_api = nullptr;
		F4T::PrismaViewState::Controller g_viewState{ "Alerts" };
		// Alerts are a passive transparent overlay: always desired-visible, never desired-focused
		// except temporary pointer hit-routing via FocusForPointer.
		bool g_pointerFocusActive = false;
		std::atomic_bool            g_menuInteraction{ false };
		std::atomic_bool            g_hasActionable{ false };

		std::mutex                              g_mutex;
		std::recursive_mutex                    g_deliveryMutex;
		std::unordered_map<std::string, PendingResponse> g_responses;
		std::vector<PendingShow>                g_pendingShows;
		std::string                             g_lastShownId;
		std::atomic_uint32_t                    g_autoId{ 0 };
		std::uint64_t                           g_sequence = 0;

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
			const auto id = ++g_autoId;
			return std::format("co-alert-{}", id);
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

		std::string BuildFallbackAlertJson(const std::string& a_id)
		{
			nlohmann::json payload{
				{ "id", a_id },
				{ "category", "ALERT" },
				{ "title", "Connection Notice" },
				{ "message", "A connection message could not be displayed. See the Commonwealth Online log for details." },
				{ "tone", "info" },
				{ "durationMs", 12000 },
				{ "actions", nlohmann::json::array() }
			};
			return F4T::ViewJson::Dump(payload);
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

		void InvokeDismissAll(std::uint64_t a_sequence)
		{
			if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) return;
			const auto script = std::format("if(window.dismissAllCoAlerts){{window.dismissAllCoAlerts(true,{});}}", a_sequence);
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
		}

		void HideViewWhenIdle()
		{
			{
				std::scoped_lock lock(g_mutex);
				if (!g_responses.empty()) return;
				g_pointerFocusActive = false;
				g_viewState.SetDesiredFocused(false);
				g_viewState.SetDesiredVisible(false);
			}
			if (g_viewState.IsHandleValid() && g_viewState.IsDomReady()) g_viewState.ApplyState("AlertsIdle");
		}

		void DeliverResponse(const std::string& a_alertId, const std::string& a_actionId, std::uint64_t a_sequence)
		{
			PendingResponse response{};
			std::uint64_t idleSequence = 0;
			{
				std::scoped_lock lock(g_mutex);
				const auto it = g_responses.find(a_alertId);
				if (it == g_responses.end()) {
					LogInfo(std::format("[OnbDbg] response rejected: id '{}' action '{}' seq {}; no pending entry", a_alertId, a_actionId, a_sequence));
					return;
				}
				if (it->second.sequence != a_sequence) {
					LogInfo(std::format("[OnbDbg] response rejected: id '{}' action '{}' seq {} != pending seq {}", a_alertId, a_actionId, a_sequence, it->second.sequence));
					return;
				}
				LogInfo(std::format("[OnbDbg] response accepted: id '{}' action '{}' seq {} == pending seq {}; dismissing (reason={})", a_alertId, a_actionId, a_sequence, it->second.sequence, a_actionId));
				response = it->second;
				g_responses.erase(it);
				if (g_responses.empty()) {
					idleSequence = ++g_sequence;
					g_pendingShows.clear();
					g_hasActionable = false;
				}
			}
			if (idleSequence) {
				g_hitRegion = {};
				ApplyInputRegion(g_hitRegion);
				InvokeDismissAll(idleSequence);
				HideViewWhenIdle();
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
				const auto sequence = payload.value("sequence", std::uint64_t{ 0 });
				if (id.empty() || action.empty() || sequence == 0) {
					return;
				}

				LogInfo(std::format("Alert '{}' -> '{}'", id, action));
				DeliverResponse(id, action, sequence);
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

		void OnAlertCompact(const char*)
		{
			if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) return;
			std::uint64_t barrier;
			nlohmann::json active = nlohmann::json::array();
			{
				std::scoped_lock lock(g_mutex);
				barrier = ++g_sequence;
				g_pendingShows.clear();
				std::vector<const PendingResponse*> ordered;
				ordered.reserve(g_responses.size());
				for (const auto& [id, response] : g_responses) {
					(void)id;
					ordered.push_back(&response);
				}
				std::sort(ordered.begin(), ordered.end(), [](const auto* a_left, const auto* a_right) { return a_left->sequence < a_right->sequence; });
				const auto now = std::chrono::steady_clock::now();
				for (const auto* response : ordered) {
					auto payload = nlohmann::json::parse(response->json);
					payload["sequence"] = ++g_sequence;
					payload["actionSequence"] = response->sequence;
					if (response->deadline != std::chrono::steady_clock::time_point{}) {
						const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(response->deadline - now).count();
						payload["durationMs"] = (std::max)(std::int64_t{ 1 }, remaining);
					}
					active.push_back(std::move(payload));
				}
			}
			const auto script = std::format("if(window.compactCoAlerts){{window.compactCoAlerts({},{});}}", barrier, F4T::ViewJson::Dump(active));
			g_api->Invoke(g_viewState.GetHandle(), script.c_str());
		}

		void FlushPending()
		{
			const std::scoped_lock deliveryLock(g_deliveryMutex);
			std::vector<PendingShow> pending;
			{
				std::scoped_lock lock(g_mutex);
				pending.swap(g_pendingShows);
			}

			for (auto& item : pending) {
				{
					std::scoped_lock lock(g_mutex);
					const auto found = g_responses.find(item.id);
					if (found == g_responses.end() || found->second.sequence != item.sequence) continue;
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
			g_api->BindUIEvent(a_view, "coAlertCompact", OnAlertCompact);
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

		void LogHotkey(const char* a_key, const char* a_verdict)
		{
			std::scoped_lock lock(g_mutex);
			std::string pending;
			for (const auto& [id, response] : g_responses) {
				pending += std::format(" {}#{}", id, response.sequence);
			}
			LogInfo(std::format("[OnbDbg] hotkey '{}' {}; pending:{}", a_key, a_verdict, pending.empty() ? " none" : pending));
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
		const std::scoped_lock deliveryLock(g_deliveryMutex);
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
		const std::scoped_lock deliveryLock(g_deliveryMutex);
		std::string id =
			(a_request.id && a_request.id[0]) ? std::string{ a_request.id } : MakeAutoId();

		std::string json;
		bool hasActions = false;
		try {
			const auto payload = BuildAlertJson(a_request, id);
			json = F4T::ViewJson::Dump(payload);
			hasActions = payload.value("actions", nlohmann::json::array()).size() > 0;
		} catch (const std::exception& ex) {
			LogWarn(std::format("Dropped alert text after a serialisation failure: {}", ex.what()));
			id = MakeAutoId();
			json = BuildFallbackAlertJson(id);
		}

		auto payload = nlohmann::json::parse(json);
		const auto duration = payload.value("durationMs", std::uint32_t{ 0 });
		std::uint64_t sequence;
		{
			std::scoped_lock lock(g_mutex);
			sequence = ++g_sequence;
			payload["sequence"] = sequence;
			payload["actionSequence"] = sequence;
			json = F4T::ViewJson::Dump(payload);
			const auto deadline = duration ? std::chrono::steady_clock::now() + std::chrono::milliseconds{ duration } : std::chrono::steady_clock::time_point{};
			g_responses[id] = PendingResponse{ a_onResponse, a_userData, sequence, json, deadline };
			g_lastShownId = id;
			if (hasActions) g_hasActionable = true;
		}

		CopyIdOut(id, a_idOut, a_idOutSize);

		LogInfo(std::format(
			"Show '{}' — {} / {}",
			id,
			EscapeForLog(a_request.category ? a_request.category : "ALERT"),
			EscapeForLog(a_request.title ? a_request.title : "")));

		std::string hotkeys;
		for (const auto& action : payload["actions"]) {
			hotkeys += action.value("hotkey", "") + "|";
		}
		LogInfo(std::format("[OnbDbg] show: id '{}' seq {} hotkeys=[{}]", id, sequence, hotkeys));

		if (!g_api) {
			std::scoped_lock lock(g_mutex);
			const auto found = g_responses.find(id);
			if (found != g_responses.end() && found->second.sequence == sequence) g_responses.erase(found);
			if (g_responses.empty()) g_hasActionable = false;
			return false;
		}

		if (!g_viewState.IsHandleValid()) {
			if (g_runtimeReady) {
				CreateView();
			}
		}

		if (!g_viewState.IsDomReady()) {
			std::scoped_lock lock(g_mutex);
			const auto found = g_responses.find(id);
			if (found == g_responses.end() || found->second.sequence != sequence) return true;
			g_pendingShows.push_back(PendingShow{ json, id, sequence });
			return true;
		}

		{
			std::scoped_lock lock(g_mutex);
			const auto found = g_responses.find(id);
			if (found == g_responses.end() || found->second.sequence != sequence) return true;
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
		const std::scoped_lock deliveryLock(g_deliveryMutex);

		std::uint64_t sequence;
		bool idle;
		{
			std::scoped_lock lock(g_mutex);
			sequence = ++g_sequence;
			g_responses.erase(a_alertId);
			std::erase_if(g_pendingShows, [a_alertId](const PendingShow& a_pending) { return a_pending.id == a_alertId; });
			idle = g_responses.empty();
			if (idle) g_hasActionable = false;
			LogInfo(std::format("[OnbDbg] dismiss (api): id '{}' seq {} idle={}", a_alertId, sequence, idle));
		}
		if (idle) {
			g_hitRegion = {};
			ApplyInputRegion(g_hitRegion);
			InvokeDismissAll(sequence);
			HideViewWhenIdle();
			return;
		}

		if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			return;
		}

		const auto script = std::format(
			"if(window.dismissCoAlert){{window.dismissCoAlert({},{});}}",
			F4T::ViewJson::Dump(nlohmann::json(a_alertId)), sequence);
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void DismissAll()
	{
		const std::scoped_lock deliveryLock(g_deliveryMutex);
		std::uint64_t sequence;
		{
			std::scoped_lock lock(g_mutex);
			sequence = ++g_sequence;
			g_responses.clear();
			g_pendingShows.clear();
			g_hasActionable = false;
		}
		g_hitRegion = {};
		ApplyInputRegion(g_hitRegion);

		InvokeDismissAll(sequence);
		HideViewWhenIdle();
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

	bool IsPointerOverInteractive(std::int32_t a_x, std::int32_t a_y, bool a_buttonHeld)
	{
		if (!g_menuInteraction.load() || !g_hitRegion.active) {
			return false;
		}
		if (g_pointerFocusActive && a_buttonHeld) {
			return true;
		}

		const std::int32_t margin = g_pointerFocusActive ? kPointerLeaveMargin : 0;
		return a_x >= g_hitRegion.left - margin && a_x < g_hitRegion.right + margin &&
			a_y >= g_hitRegion.top - margin && a_y < g_hitRegion.bottom + margin;
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

	bool HotkeyAccepted(const char* a_key)
	{
		if (!F4T::PrismaUI::IsGameForeground()) {
			LogHotkey(a_key, "rejected: game window not foreground");
			return false;
		}

		if (!g_hasActionable.load()) {
			LogHotkey(a_key, "rejected: no actionable alert");
			return false;
		}

		if (g_menuInteraction.load()) {
			LogHotkey(a_key, "rejected: mouse interaction mode");
			return false;
		}

		if (F4T::PrismaUI::IsMainMenuVisible() ||
			F4T::PrismaUI::IsBrowserVisible() ||
			F4T::PrismaUI::IsPauseMenuVisible()) {
			LogHotkey(a_key, "rejected: main menu, browser or pause menu open");
			return false;
		}

		if (!g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			LogHotkey(a_key, "rejected: alert view not ready");
			return false;
		}

		if (TextInputOwnsKeyboard()) {
			LogHotkey(a_key, "rejected: text input owns keyboard");
			return false;
		}

		return true;
	}

	void ForwardHotkey(const char* a_key)
	{
		LogHotkey(a_key, "accepted: forwarded to alert view");
		const auto script = std::format(
			"if(window.resolveCoAlertHotkey){{window.resolveCoAlertHotkey({});}}",
			F4T::ViewJson::Dump(nlohmann::json(a_key)));
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void ResolveControllerButton(const char* a_button)
	{
		const char* name = nullptr;
		const char* hotkey = nullptr;
		if (std::strcmp(a_button, "x") == 0) {
			name = "X";
			hotkey = "Y";
		} else if (std::strcmp(a_button, "y") == 0) {
			name = "Y";
			hotkey = "N";
		} else if (std::strcmp(a_button, "a") == 0) {
			name = "A";
			hotkey = "A";
		} else {
			return;
		}

		if (HotkeyAccepted(name)) {
			ForwardHotkey(hotkey);
		}
	}

	bool VanillaMenuOwnsController()
	{
		const auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		static const RE::BSFixedString kMenus[]{ "LooksMenu", "SPECIALMenu", "MessageBoxMenu", "PipboyMenu", "ContainerMenu", "WorkshopMenu", "TerminalMenu", "DialogueMenu", "BarterMenu", "LockpickingMenu", "ExamineMenu", "LevelUpMenu", "BookMenu", "SleepWaitMenu" };
		for (const auto& name : kMenus) {
			if (ui->GetMenuOpen(name)) {
				return true;
			}
		}
		return false;
	}

	bool AcceptsControllerAnswers()
	{
		return g_hasActionable.load() && !VanillaMenuOwnsController();
	}

	void SyncAlertInputMode()
	{
		static bool s_sent = false;
		static bool s_gamepad = false;
		auto* api = F4T::PrismaCapabilities::GetV12();
		if (!api || !g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) {
			s_sent = false;
			return;
		}

		const bool gamepad = api->IsUsingGamepad();
		if (s_sent && gamepad == s_gamepad) {
			return;
		}
		s_sent = true;
		s_gamepad = gamepad;
		const auto script = std::format(
			"if(window.setCoAlertInputMode){{window.setCoAlertInputMode({},{});}}",
			gamepad ? "true" : "false",
			api->GetControllerStyle());
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void ResolveMenuHotkey(const char* a_key)
	{
		if (!a_key || (std::strcmp(a_key, "Y") != 0 && std::strcmp(a_key, "N") != 0) || !g_menuInteraction.load() || !g_hasActionable.load() || !g_api || !g_viewState.IsHandleValid() || !g_viewState.IsDomReady()) return;
		const auto script = std::format("if(window.resolveCoAlertHotkey){{window.resolveCoAlertHotkey({},true);}}", F4T::ViewJson::Dump(nlohmann::json(a_key)));
		g_api->Invoke(g_viewState.GetHandle(), script.c_str());
	}

	void PollHotkeys()
	{
		SyncAlertInputMode();
		static bool wasY = false;
		static bool wasN = false;
		static bool wasF1 = false;
		static bool wasF2 = false;
		static bool wasF3 = false;
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
			{ VK_F3, &wasF3, "F3" },
			{ 'T', &wasT, "T" },
		};

		const char* pressed = nullptr;
		for (auto& edge : edges) {
			if (IsVirtualKeyDownEdge(edge.vk, *edge.wasDown) && !pressed) {
				pressed = edge.name;
			}
		}

		if (pressed && std::strcmp(pressed, "F3") == 0) {
			F4T::ParkedSuitView::Open("Hotkey");
			return;
		}

		if (!pressed) {
			return;
		}

		if (!HotkeyAccepted(pressed)) {
			return;
		}

		ForwardHotkey(pressed);
	}

}
