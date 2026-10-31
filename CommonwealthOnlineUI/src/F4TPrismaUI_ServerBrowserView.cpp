
#include "F4TPrismaUIInternal.h"

namespace F4T::PrismaUI::Internal
{
	void OnServerBrowserDomReady(PrismaView a_view)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		auto finish = [a_view]() {
			if (!g_standaloneBrowserState.MarkDomReady(a_view) || !g_api) {
				return;
			}

			g_standaloneBrowserState.SetDesired(false, false);
			g_standaloneBrowserState.ApplyState("StandaloneDomReadyKeepHidden");

			g_api->RegisterConsoleCallback(
				a_view,
				[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
					if (!a_message) {
						return;
					}

					const auto prefix = GetLogPrefix() + " [StandaloneBrowser JS]";
					switch (a_level) {
					case PRISMA_UI_API::ConsoleMessageLevel::Error:
						REX::CRITICAL("{} {}", prefix, a_message);
						break;
					case PRISMA_UI_API::ConsoleMessageLevel::Warning:
						REX::WARN("{} {}", prefix, a_message);
						break;
					default:
						REX::INFO("{} {}", prefix, a_message);
						break;
					}
				});
		};

		if (!taskInterface) {
			finish();
			return;
		}
		taskInterface->AddTask(finish);
	}

	void CreateServerBrowserView()
	{
		g_standaloneBrowserState.SetDesired(false, false);
		g_standaloneBrowserState.EnsureCreated(kServerBrowserHtmlPath, OnServerBrowserDomReady, g_prismaReady);
	}

	void DumpAllViewStateInternal()
	{
		LogInfo("── Prisma Diagnostics ──");
		LogInfo(std::format(
			"API version: {}  prismaReady={}  mainMenuOverlayReady={}  mainMenuBridgeReady={}  vanillaMainSuppressed={}  vanillaPauseSuppressed={}",
			F4T::PrismaCapabilities::GetVersionName(),
			g_prismaReady,
			g_mainMenuOverlayReady,
			g_mainMenuBridgeReady.load(),
			g_vanillaMainMenuSuppressed,
			g_vanillaPauseMenuSuppressed));

		const auto focused = F4T::PrismaCapabilities::TryGetFocusedView();
		LogInfo(std::format("Focused Prisma view: {}", focused));

		if (F4T::PrismaCapabilities::HasV10()) {
			LogInfo(std::format(
				"IsAnyPanelVisible(ignore=0)={} IsAnyPanelVisible(ignore=MainMenu)={} IsAnyPanelVisible(ignore=PauseMenu)={}",
				F4T::PrismaCapabilities::IsAnyExternalPanelVisible(0),
				F4T::PrismaCapabilities::IsAnyExternalPanelVisible(g_mainMenuState.GetHandle()),
				F4T::PrismaCapabilities::IsAnyExternalPanelVisible(g_pauseMenuState.GetHandle())));
		} else {
			LogInfo(std::format(
				"HasAnyActiveFocus={} (V6 fallback; cannot distinguish widgets from panels)",
				F4T::PrismaCapabilities::HasAnyActiveFocus()));
		}

		if (!g_lastPanelBlockReason.empty()) {
			LogInfo(std::format(
				"Last panel block: panel={} detail={}",
				PanelTypeName(g_lastBlockedPanel),
				g_lastPanelBlockReason));
		}

		g_mainMenuState.LogDiagnostics("F11");
		LogInfo(std::format(
			"ServerBrowser (embedded logical):\n"
			"  desiredVisible={}\n"
			"  currentlyVisible={}\n"
			"  hostMainMenuVisible={}\n"
			"  hostHandle={}\n"
			"  note=logical sub-panel of MainMenu kPanel; not a separate Prisma view",
			g_browserDesiredVisible,
			g_browserCurrentlyVisible,
			g_mainMenuState.IsCurrentlyVisible(),
			g_mainMenuState.GetHandle()));
		g_standaloneBrowserState.LogDiagnostics("F11");
		g_pauseMenuState.LogDiagnostics("F11");
		F4T::Alerts::DumpViewState();

		const auto checkSurface = [](const F4T::PrismaViewState::Controller& state) {
			if (state.IsFullyAppliedForVisibleSurface()) {
				LogInfo(std::format(
					"[{}] View state applied successfully but no visible surface was confirmed. Possible Prisma rendering/compositing failure.",
					state.GetName()));
			}
		};
		checkSurface(g_mainMenuState);
		checkSurface(g_pauseMenuState);
	}
}
