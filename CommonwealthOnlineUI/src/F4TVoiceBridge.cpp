#include "F4TPrismaUIInternal.h"
#include "F4TVoiceBridge.h"
#include "F4TVoiceBridgeState.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::VoiceBridge
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kHudHtmlPath = "CommonwealthOnline/voice-hud/index.html";
		constexpr int kHudViewOrder = 520;

		F4T::PrismaViewState::Controller g_hudState{ "VoiceHud" };
		COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks g_callbacks{};
		EventCache g_cache{};
		std::array<PrismaView, 4> g_views{};

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [Voice] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped voice task: game task interface unavailable.");
		}

		void InvokeSurfaces(const std::string& a_script)
		{
			if (!g_api) {
				return;
			}
			for (const auto view : g_views) {
				if (view != 0 && g_api->IsValid(view)) {
					g_api->Invoke(view, a_script.c_str());
				}
			}
		}

		void HandleEvent(const std::string& a_json)
		{
			const auto event = ParseEvent(a_json);
			if (!event) {
				LogWarn("Dropped voice event with an unknown kind or shape.");
				return;
			}
			g_cache.Store(*event);
			InvokeSurfaces(EventScript(*event));
		}

		void HandleAction(const std::string& a_json)
		{
			if (!AllowedActionOp(a_json)) {
				LogWarn("Dropped voice action with a disallowed op or shape.");
				return;
			}
			if (!g_callbacks.SendAction) {
				LogWarn("Dropped voice action: no action callback.");
				return;
			}
			g_callbacks.SendAction(a_json.c_str());
		}

		void OnAction(const char* a_json)
		{
			if (!a_json) {
				return;
			}
			if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {
				LogWarn("Dropped voice action larger than 4 KiB.");
				return;
			}
			RunOnGameThread([payload = std::string(a_json)]() {
				HandleAction(payload);
			});
		}

		void OnHudDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				if (!g_hudState.MarkDomReady(a_view) || !g_api) {
					return;
				}
				g_hudState.AssignRoleIfNeeded();
				g_hudState.ApplyState("DomReady");
				AttachView(Surface::Hud, a_view);
			});
		}
	}

	void Initialize()
	{
		g_hudState.SetApi(g_api);
		g_hudState.SetFocusArgs(false, true);
		g_hudState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_hudState.SetDesired(false, false);
	}

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks* a_callbacks)
	{
		RunOnGameThread([callbacks = a_callbacks ? *a_callbacks : COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks{}]() {
			g_callbacks = callbacks;
		});
	}

	void PushEvent(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kEventMaxBytes + 1) > kEventMaxBytes) {
			LogWarn("Dropped voice event larger than 32 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			HandleEvent(payload);
		});
	}

	void AttachView(Surface a_surface, PrismaView a_view)
	{
		if (!g_api) {
			return;
		}
		g_api->BindUIEvent(a_view, "coVoiceAction", OnAction);
		g_views[static_cast<std::size_t>(a_surface)] = a_view;
		for (const auto& script : g_cache.ReplayScripts()) {
			g_api->Invoke(a_view, script.c_str());
		}
	}

	void Tick()
	{
		if (!g_api) {
			return;
		}

		const auto wanted = HudWanted(
			g_cache,
			F4T::PrismaUI::IsMainMenuVisible(),
			F4T::PrismaUI::IsBrowserVisible(),
			g_sessionLockoutActive);
		if (wanted && !g_hudState.IsHandleValid()) {
			if (g_hudState.EnsureCreated(kHudHtmlPath, OnHudDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(g_hudState.GetHandle(), kHudViewOrder);
			}
		}
		if (wanted == g_hudState.IsDesiredVisible()) {
			return;
		}

		g_hudState.SetDesiredVisible(wanted);
		g_hudState.ApplyState(wanted ? "HudShow" : "HudHide");
	}
}
