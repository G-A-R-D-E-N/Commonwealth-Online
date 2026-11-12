#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TNeedsHud.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::NeedsHud
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kHtmlPath = "CommonwealthOnline/needs-hud/index.html";
		constexpr auto kEntryPoint = "coNeedsSetState";
		constexpr int kViewOrder = 527;
		constexpr std::size_t kMaxJsonBytes = 1024;

		F4T::PrismaViewState::Controller g_hudState{ "NeedsHud" };
		std::string g_lastJson;
		bool g_hasState = false;
		bool g_enabled = false;

		void LogWarn(std::string_view a_message)
		{
			REX::WARN("[COUI] [NeedsHud] {}", a_message);
		}

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
				return;
			}
			LogWarn("Dropped needs HUD task: game task interface unavailable.");
		}

		void PushScript()
		{
			if (!g_api || !g_hudState.IsDomReady() || !g_hudState.IsHandleValid()) {
				return;
			}
			const auto script = std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kEntryPoint,
				F4T::ViewJson::Dump(nlohmann::json(g_lastJson)));
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
	}

	void Initialize()
	{
		g_hudState.SetApi(g_api);
		g_hudState.SetFocusArgs(false, true);
		g_hudState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_hudState.SetDesired(false, false);
	}

	void PushStateJson(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		if (strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes) {
			LogWarn("Dropped needs HUD state larger than 1 KiB.");
			return;
		}
		RunOnGameThread([payload = std::string(a_json)]() {
			const auto state = nlohmann::json::parse(payload, nullptr, false);
			const auto enabled = state.find("enabled");
			if (state.is_discarded() || !state.is_object() || enabled == state.end() || !enabled->is_boolean()) {
				LogWarn("Dropped malformed needs HUD state.");
				return;
			}
			g_enabled = enabled->get<bool>();
			g_hasState = true;
			if (payload == g_lastJson) {
				return;
			}
			g_lastJson = payload;
			PushScript();
		});
	}

	void Tick()
	{
		if (!g_api) {
			return;
		}
		const auto wanted = g_hasState && g_enabled && SurfaceClear();
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
