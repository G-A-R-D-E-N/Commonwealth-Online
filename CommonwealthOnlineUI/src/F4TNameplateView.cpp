#include "F4TPrismaUIInternal.h"
#include "F4TViewJson.h"
#include "F4TNameplateView.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace F4T::NameplateView
{
	namespace
	{
		using F4T::PrismaUI::Internal::g_api;
		using F4T::PrismaUI::Internal::g_prismaReady;
		using F4T::PrismaUI::Internal::g_sessionLockoutActive;

		constexpr auto kHtmlPath = "CommonwealthOnline/nameplates/index.html";
		constexpr auto kEntryPoint = "coNameplatesSet";
		constexpr auto kEmptyJson = "[]";
		constexpr int kViewOrder = 529;
		constexpr std::size_t kMaxJsonBytes = 4096;

		F4T::PrismaViewState::Controller g_nameplateState{ "Nameplates" };
		std::string g_lastJson{ kEmptyJson };

		void RunOnGameThread(std::function<void()> a_task)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_task));
			}
		}

		void PushScript()
		{
			if (!g_api || !g_nameplateState.IsDomReady() || !g_nameplateState.IsHandleValid()) {
				return;
			}
			const auto script = std::format(
				"if(window.{0}){{window.{0}({1});}}",
				kEntryPoint,
				F4T::ViewJson::Dump(nlohmann::json(g_lastJson)));
			g_api->Invoke(g_nameplateState.GetHandle(), script.c_str());
		}

		void OnDomReady(PrismaView a_view)
		{
			RunOnGameThread([a_view]() {
				if (!g_nameplateState.MarkDomReady(a_view) || !g_api) {
					return;
				}
				g_nameplateState.AssignRoleIfNeeded();
				g_nameplateState.ApplyState("DomReady");
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
		g_nameplateState.SetApi(g_api);
		g_nameplateState.SetFocusArgs(false, true);
		g_nameplateState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);
		g_nameplateState.SetDesired(false, false);
	}

	void PushStateJson(const char* a_json)
	{
		if (!a_json) {
			return;
		}
		const auto oversize = strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes;
		RunOnGameThread([payload = std::string(oversize ? kEmptyJson : a_json)]() {
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
		const auto wanted = g_lastJson != kEmptyJson && SurfaceClear();
		if (wanted && !g_nameplateState.IsHandleValid()) {
			if (g_nameplateState.EnsureCreated(kHtmlPath, OnDomReady, g_prismaReady) != 0) {
				g_api->SetOrder(g_nameplateState.GetHandle(), kViewOrder);
			}
		}
		if (wanted == g_nameplateState.IsDesiredVisible()) {
			return;
		}
		g_nameplateState.SetDesiredVisible(wanted);
		g_nameplateState.ApplyState(wanted ? "NameplatesShow" : "NameplatesHide");
	}
}
