#include "F4TPrismaUIInternal.h"

namespace F4T::PrismaUI::Internal
{
	std::atomic<bool> g_menuServicesReady{ false };

	bool UsesLegacyMenuReadiness(PRISMA_UI_API::RuntimeState a_state)
	{
		return a_state == PRISMA_UI_API::RuntimeState::Unavailable || a_state == PRISMA_UI_API::RuntimeState::Failed;
	}

	bool IsMenuRuntimeReady()
	{
		const auto state = PRISMA_UI_API::GetRuntimeState();
		static bool s_logged = false;
		static auto s_lastState = PRISMA_UI_API::RuntimeState::Unavailable;
		if (!s_logged || state != s_lastState) {
			const auto message = std::format(
				"[Startup] provider runtime state {}{}",
				static_cast<int>(state),
				UsesLegacyMenuReadiness(state) ? " (using MainMenu advance gate)" : "");
			if (state == PRISMA_UI_API::RuntimeState::Failed) LogWarning(message); else LogInfo(message);
			s_logged = true;
			s_lastState = state;
		}
		if (UsesLegacyMenuReadiness(state)) return g_prismaReady;
		const bool ready = state == PRISMA_UI_API::RuntimeState::Ready;
		g_prismaReady = ready;
		return ready;
	}

	void TryFinalizeMainMenu()
	{
		if (!g_menuServicesReady || !g_mainMenuState.IsDomReady() || !g_mainMenuState.IsHandleValid()) return;
		BindMainMenuDomHandlers(g_mainMenuState.GetHandle());
		ReconcileMainMenuOverlayState("MenuServicesAndDomReady");
	}
}

namespace F4T::PrismaUI
{
	void PrepareMenuRuntime()
	{
		using namespace Internal;
		static bool started = false;
		const auto* tasks = F4SE::GetTaskInterface();
		if (started || !tasks) return;
		g_api = F4T::PrismaCapabilities::Acquire();
		if (!g_api) {
			LogWarning("[Startup] Prisma capability acquire failed; menu runtime not prepared");
			return;
		}
		started = true;
		g_mainMenuState.SetApi(g_api);
		g_mainMenuState.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
		InstallMainMenuCursorReleaseHook();
		F4T::F4SECapabilities::SchedulePermanentTask(tasks, F4SE::GetF4SEVersion(), [] {
			const auto* ui = RE::UI::GetSingleton();
			if (!g_menuServicesReady && ui && ui->GetMenuOpen<RE::MainMenu>() &&
				PRISMA_UI_API::GetRuntimeState() == PRISMA_UI_API::RuntimeState::Ready && IsMenuRuntimeReady()) {
				CreateMainMenuView();
			}
		});
	}
}
