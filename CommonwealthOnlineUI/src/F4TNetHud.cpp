#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TNetHud.h"
#include "F4TMultiplayerProfile.h"
#include "F4TNetStats.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace F4T::NetHud
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kHtmlPath = "CommonwealthOnline/net-hud/index.html";
		constexpr auto kEntryPoint = "coNetHudSetState";
		constexpr int kViewOrder = 528;
		constexpr auto kRefreshInterval = std::chrono::seconds{ 1 };

		F4T::PrismaViewState::Controller g_hudState{ "NetHud" };
		std::string g_lastPayload;
		bool g_hasStats = false;
		std::chrono::steady_clock::time_point g_nextRefresh{};

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
			}
		}

		void PushScript()
		{
			if (!g_api || !g_hudState.IsDomReady() || !g_hudState.IsHandleValid()) {
				return;
			}
			const auto script = std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kEntryPoint,
				F4T::ViewJson::Dump(nlohmann::json(g_lastPayload)));
			g_api->Invoke(g_hudState.GetHandle(), script.c_str());
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				if (!g_hudState.MarkDomReady(a_view) || !g_api) {
					return;
				}
				g_hudState.AssignRoleIfNeeded();
				g_hudState.ApplyState("DomReady");
				PushScript();
			});
		}

		bool SurfaceClear()
		{
			if (F4T::PrismaUI::IsMainMenuVisible() || F4T::PrismaUI::IsBrowserVisible() ||
				F4T::PrismaUI::IsPauseMenuVisible() || g_sessionLockoutActive) {
				return false;
			}
			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return true;
			}
			return !ui->GetMenuOpen<RE::LoadingMenu>() && !ui->GetMenuOpen(RE::BSFixedString{ "PipboyMenu" });
		}

		void Refresh()
		{
			const auto stats = nlohmann::json::parse(F4T::NetStats::LatestJson(), nullptr, false);
			const auto prefs = F4T::MultiplayerProfile::GetPreferences();
			g_hasStats = stats.is_object() && (prefs.showPing || prefs.networkStatsOverlay);
			if (!g_hasStats) {
				return;
			}
			const nlohmann::json state{
				{ "showPing", prefs.showPing },
				{ "showStats", prefs.networkStatsOverlay },
				{ "stats", stats }
			};
			auto payload = F4T::ViewJson::Dump(state);
			if (payload == g_lastPayload) {
				return;
			}
			g_lastPayload = std::move(payload);
			PushScript();
		}
	}

	void Initialize()
	{
		g_hudState.SetApi(g_api);
		g_hudState.SetFocusArgs(false, true);
		g_hudState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_hudState.SetDesired(false, false);
	}

	void Tick()
	{
		if (!g_api) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= g_nextRefresh) {
			g_nextRefresh = now + kRefreshInterval;
			Refresh();
		}
		const auto wanted = g_hasStats && SurfaceClear();
		if (wanted && !g_hudState.IsHandleValid()) {
			if (g_hudState.EnsureCreated(kHtmlPath, OnDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(g_hudState.GetHandle(), kViewOrder);
			}
		}
		if (wanted == g_hudState.IsDesiredVisible()) {
			return;
		}
		g_hudState.SetDesiredVisible(wanted);
		g_hudState.ApplyState(wanted ? "HudShow" : "HudHide");
	}
}
