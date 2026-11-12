#include "F4TPrismaUIInternal.h"

#include "DownedUiCallbacks.h"
#include "F4TDownedUiState.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::DownedUi
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;

		constexpr auto kOverlayHtmlPath = "CommonwealthOnline/downed/index.html";
		constexpr auto kRespawnHtmlPath = "CommonwealthOnline/downed/respawn.html";
		constexpr auto kOverlayEntryPoint = "coDownedSetState";
		constexpr auto kRespawnEntryPoint = "coRespawnSetState";
		constexpr int  kOverlayViewOrder = 540;
		constexpr int  kRespawnViewOrder = 610;

		constexpr struct
		{
			const char* button;
			const char* action;
		} kRespawnControllerBindings[] = {
			{ "A", "co.select" },
			{ "DUp", "co.navUp" },
			{ "DDown", "co.navDown" },
		};

		struct PushedView
		{
			F4T::PrismaViewState::Controller controller;
			std::string                   lastScript;
			PrismaView                    lastScriptHandle{ 0 };
			bool                          createRetryArmed{ false };
		};

		PushedView g_overlay{ F4T::PrismaViewState::Controller{ "DownedOverlay" }, {}, 0 };
		PushedView g_respawn{ F4T::PrismaViewState::Controller{ "RespawnMenu" }, {}, 0 };
		std::string g_lastStateJson;
		std::atomic<RespawnChosenFn> g_respawnChosen{ nullptr };
		State g_state{};
		bool g_respawnControllerBound = false;
		PrismaView g_respawnControllerView{ 0 };
		ControllerBindRetryTiming g_respawnControllerBind{};

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("[COUI] [Downed] {}", a_message);
		}

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Downed] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped downed task - game task interface unavailable.");
		}

		void InvokeState(PushedView& a_view, std::string_view a_entryPoint)
		{
			auto& controller = a_view.controller;
			if (!g_api || !controller.IsHandleValid() || !controller.IsDomReady()) {
				return;
			}
			const auto handle = controller.GetHandle();
			auto script = FormatSetStateScript(g_state, a_entryPoint);
			if (!ShouldPushStateScript(a_view.lastScript, a_view.lastScriptHandle, handle, script)) {
				return;
			}
			g_api->Invoke(handle, script.c_str());
			a_view.lastScriptHandle = handle;
			a_view.lastScript = std::move(script);
		}

		void PushStateToViews()
		{
			InvokeState(g_overlay, kOverlayEntryPoint);
			InvokeState(g_respawn, kRespawnEntryPoint);
		}

		void ArmViewCreateRetry(PushedView& a_view)
		{
			auto& controller = a_view.controller;
			if (!a_view.createRetryArmed && controller.DidCreateFail() && !controller.CanRetryCreate()) {
				a_view.createRetryArmed = true;
				controller.AllowCreateRetry();
			}
		}

		void ClearControllerActions()
		{
			if (g_respawnControllerBound) {
				if (auto* api = F4T::PrismaCapabilities::GetV12(); api && api->IsValid(g_respawnControllerView)) {
					api->ClearControllerActions(g_respawnControllerView);
				}
			}
			g_respawnControllerBound = false;
			g_respawnControllerView = 0;
			g_respawnControllerBind.Reset();
		}

		void EnsureControllerActions()
		{
			auto* api = F4T::PrismaCapabilities::GetV12();
			const auto view = g_respawn.controller.GetHandle();
			if (!api || !g_respawn.controller.IsHandleValid() || !api->IsValid(view)) {
				return;
			}

			if (view != g_respawnControllerView) {
				ClearControllerActions();
				g_respawnControllerView = view;
			}

			const auto now = ControllerBindRetryTiming::Clock::now();

			if (g_respawnControllerBound) {
				const auto bridgeState = api->GetControllerActionBridgeState(view);
				if (bridgeState == PRISMA_UI_API::ControllerActionBridgeState::Failed) {
					api->ClearControllerActions(view);
					g_respawnControllerBound = false;
					g_respawnControllerBind.ScheduleRetry(now);
				} else if (bridgeState == PRISMA_UI_API::ControllerActionBridgeState::Ready) {
					g_respawnControllerBind.Reset();
				}
				return;
			}

			if (!g_respawnControllerBind.CanAttempt(now)) {
				return;
			}
			g_respawnControllerBind.RecordAttempt(now);

			std::size_t bound = 0;
			for (const auto& binding : kRespawnControllerBindings) {
				if (api->BindControllerAction(view, binding.button, binding.action)) {
					++bound;
				}
			}
			if (bound != std::size(kRespawnControllerBindings)) {
				api->ClearControllerActions(view);
				g_respawnControllerBind.ScheduleRetry(now);
				return;
			}

			g_respawnControllerBound = true;
			g_respawnControllerBind.Reset();
		}

		void FinishOverlayDomReady(PrismaView a_view)
		{
			auto& controller = g_overlay.controller;
			if (!controller.MarkDomReady(a_view) || !g_api) {
				return;
			}
			controller.AssignRoleIfNeeded();
			controller.ApplyState("DomReady");
			g_overlay.lastScript.clear();
			PushStateToViews();
		}

		void OnOverlayDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishOverlayDomReady(a_view);
			});
		}

		void OnRespawnAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kMaxChoiceBytes + 1) > kMaxChoiceBytes) {
				LogWarn("Dropped respawn action larger than 1 KiB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() {
				const auto choice = ParseChoice(payload);
				if (!choice) {
					LogWarn("Dropped malformed respawn action.");
					return;
				}
				const auto callback = g_respawnChosen.load();
				if (!callback) {
					LogWarn("Dropped respawn action: no respawn callback registered.");
					return;
				}
				LogInfo(std::format("Respawn chosen point='{}' friend='{}'", choice->point, choice->friendId));
				callback(choice->point.c_str(), choice->friendId.c_str());
			});
		}

		void FinishRespawnDomReady(PrismaView a_view)
		{
			auto& controller = g_respawn.controller;
			if (!controller.MarkDomReady(a_view) || !g_api) {
				return;
			}
			g_api->BindUIEvent(a_view, "coDownedAction", OnRespawnAction);
			controller.AssignRoleIfNeeded();
			controller.ApplyState("DomReady");
			g_respawn.lastScript.clear();
			PushStateToViews();
		}

		void OnRespawnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				FinishRespawnDomReady(a_view);
			});
		}

		void ApplyState(const std::string& a_json)
		{
			if (a_json == g_lastStateJson) {
				return;
			}
			const auto parsed = ParseState(a_json);
			if (!parsed) {
				LogWarn("Kept previous downed state: malformed or unrecognized push.");
				return;
			}
			g_lastStateJson = a_json;
			g_state = *parsed;
			PushStateToViews();
		}
	}

	void Initialize()
	{
		g_overlay.controller.SetApi(g_api);
		g_overlay.controller.SetFocusArgs(false, true);
		g_overlay.controller.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_overlay.controller.SetDesired(false, false);

		g_respawn.controller.SetApi(g_api);
		g_respawn.controller.SetFocusArgs(false, true);
		g_respawn.controller.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		g_respawn.controller.SetDesired(false, false);
	}

	void EnsureViews()
	{
		if (!g_api) {
			return;
		}

		ArmViewCreateRetry(g_overlay);
		ArmViewCreateRetry(g_respawn);

		auto& overlay = g_overlay.controller;
		const bool overlayWanted = IsOverlayVisible(g_state);
		if (overlayWanted && !overlay.IsHandleValid()) {
			if (overlay.EnsureCreated(kOverlayHtmlPath, OnOverlayDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(overlay.GetHandle(), kOverlayViewOrder);
			}
		}
		if (overlayWanted != overlay.IsDesiredVisible()) {
			overlay.SetDesiredVisible(overlayWanted);
			overlay.ApplyState(overlayWanted ? "OverlayShow" : "OverlayHide");
		}
		if (overlayWanted &&
			overlay.IsDomReady() &&
			overlay.IsDesiredVisible() &&
			!overlay.IsCurrentlyVisible()) {
			overlay.ApplyState("OverlayPendingShow");
		}

		auto& respawn = g_respawn.controller;
		const bool menuWanted = IsRespawnMenuVisible(g_state);
		if (menuWanted && !respawn.IsHandleValid()) {
			if (respawn.EnsureCreated(kRespawnHtmlPath, OnRespawnDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(respawn.GetHandle(), kRespawnViewOrder);
			}
		}
		if (menuWanted != respawn.IsDesiredVisible()) {
			respawn.SetDesired(menuWanted, menuWanted);
			respawn.ApplyState(menuWanted ? "RespawnShow" : "RespawnHide");
			if (!menuWanted) {
				ClearControllerActions();
			}
		}
		if (menuWanted &&
			respawn.IsDomReady() &&
			respawn.IsDesiredVisible() &&
			!respawn.IsCurrentlyVisible()) {
			respawn.ApplyState("RespawnPendingShow");
		}
		if (menuWanted) {
			EnsureControllerActions();
		}
	}

	void SetRespawnChosenCallback(RespawnChosenFn a_callback) noexcept
	{
		g_respawnChosen.store(a_callback);
	}

	void AllowCreateRetry()
	{
		if (g_overlay.controller.DidCreateFail()) {
			g_overlay.controller.AllowCreateRetry();
		}
		if (g_respawn.controller.DidCreateFail()) {
			g_respawn.controller.AllowCreateRetry();
		}
	}

	bool DidCreateFail() noexcept
	{
		return g_overlay.controller.DidCreateFail() || g_respawn.controller.DidCreateFail();
	}

	void PushStateJson(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes) {
			LogWarn("Dropped downed state larger than 64 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			ApplyState(payload);
		});
	}
}
