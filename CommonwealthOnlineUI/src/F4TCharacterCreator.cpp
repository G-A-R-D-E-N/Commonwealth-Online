#include "F4TCharacterCreatorInternal.h"

#include <cstring>

namespace F4T::CharacterCreator
{
	namespace
	{
		constexpr std::string_view kEnabledSetting = "bCOCharacterCreatorEnabled:Interface";

		// Constraint: the toggle is a kill-switch; a missing setting enables the creator, explicit binary 0 disables it.
		bool IsEnabled()
		{
			auto* ini = RE::INISettingCollection::GetSingleton();
			auto* setting = ini ? ini->GetSetting(kEnabledSetting) : nullptr;
			if (!setting) {
				return true;
			}
			return setting->GetType() != RE::Setting::SETTING_TYPE::kBinary || setting->GetBinary();
		}

		void SetVanillaSuppressed(bool a_suppress)
		{
			if (!Internal::g_api) {
				return;
			}

			if (!Internal::g_api->SuppressVanillaMenu(kMenuName, a_suppress)) {
				LogWarning(std::format(
					"SuppressVanillaMenu(\"{}\", {}) failed; vanilla chargen UI left as-is.",
					kMenuName,
					a_suppress));
				return;
			}

			LogInfo(std::format("Vanilla LooksMenu SWF {}.", a_suppress ? "suppressed" : "restored"));
		}

		void SetVanillaInputSuppressed(bool a_suppress)
		{
			if (a_suppress && !kSuppressVanillaInput) {
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(kMenuName) : nullptr;
			if (!menu) {
				return;
			}

			menu->inputEventHandlingEnabled = !a_suppress;
			LogInfo(std::format(
				"Vanilla LooksMenu input {}.",
				a_suppress ? "suppressed" : "restored"));
		}

		void ReconcileVanillaSuppression()
		{
			const Lifecycle::View view{
				g_state.menuOpen,
				Internal::g_api != nullptr,
				g_state.controller.IsHandleValid(),
				g_state.controller.IsDomReady(),
				g_state.controller.IsCurrentlyVisible(),
				g_state.controller.IsCurrentlyFocused(),
			};

			const auto decision = g_state.suppressionGate.Evaluate(view);
			if (decision.suppressRender) {
				SetVanillaSuppressed(true);
			}
			if (decision.suppressInput) {
				SetVanillaInputSuppressed(true);
				F4T::MenuInput::SetCharacterCreatorActive(true);
			}
			if (decision.restoreInput) {
				F4T::MenuInput::SetCharacterCreatorActive(false);
				SetVanillaInputSuppressed(false);
			}
			if (decision.restoreRender) {
				SetVanillaSuppressed(false);
			}
		}

		void ApplyCreatorState(std::string_view a_reason)
		{
			const auto result = g_state.controller.ApplyState(a_reason);
			if (result.showRequested) {
				Internal::InitializeBrowserPointerMode();
				if (Internal::g_api && g_state.controller.IsHandleValid()) {
					Internal::g_api->Invoke(
						g_state.controller.GetHandle(),
						"if(window.onCharacterCreatorShown){window.onCharacterCreatorShown();}");
				}
				PushState();
				LogInfo("Character creator view shown.");
			}

			if (result.hideRequested) {
				F4T::Alerts::ClearPointerFocus("CharacterCreatorHidden");
				Internal::RequestNativeCursorRestore();
			}

			ReconcileVanillaSuppression();
		}

		void ArmDesiredState()
		{
			g_state.controller.SetApi(Internal::g_api);
			g_state.controller.SetFocusArgs(false, true);
			g_state.controller.SetDesiredRole(PRISMA_UI_API::ViewRole::kPanel);
			g_state.controller.SetDesired(true, true);
		}

		void BindDomHandlers(PrismaView a_view)
		{
			Internal::g_api->RegisterConsoleCallback(
				a_view,
				[](PrismaView, PRISMA_UI_API::ConsoleMessageLevel a_level, const char* a_message) {
					if (!a_message) {
						return;
					}

					const auto prefix = Internal::GetLogPrefix() + " [CharacterCreator JS]";
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

			Internal::g_api->BindUIEvent(a_view, "coCreatorAction", [](const char* a_json) {
				if (!a_json) {
					return;
				}
				HandleCreatorAction(a_json);
			});
		}

		void OnDomReady(PrismaView a_view)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarning("Dropped character creator DOM-ready callback: game-thread task interface unavailable.");
				return;
			}

			taskInterface->AddTask([a_view]() {
				if (!g_state.controller.MarkDomReady(a_view) || !Internal::g_api) {
					return;
				}
				BindDomHandlers(a_view);
				PushState();
			});
		}

		void CreateView()
		{
			const auto previousHandle = g_state.controller.GetHandle();
			const auto handle = g_state.controller.EnsureCreated(
				kHtmlPath,
				OnDomReady,
				Internal::g_prismaReady);
			if (handle != 0 && handle != previousHandle) {
				LogInfo(std::format(
					"View created oldHandle={} newHandle={}",
					previousHandle,
					handle));
			}
		}

		void OnMenuOpened()
		{
			g_state.menuOpen = true;
			++g_state.generation;

			if (!Internal::g_api) {
				LogWarning("Prisma API unavailable; vanilla chargen UI left visible and usable.");
				ReconcileVanillaSuppression();
				return;
			}

			ArmDesiredState();

			const auto activePanel = Internal::GetActivePhysicalPanel();
			if (activePanel != Internal::PanelType::None && activePanel != Internal::PanelType::CharacterCreator) {
				LogWarning(std::format(
					"LooksMenu opened while panel '{}' is up; the engine forced chargen and owns "
					"precedence, so the creator overlay takes focus.",
					Internal::PanelTypeName(activePanel)));
			}

			CreateView();
			ApplyCreatorState("LooksMenuOpened");
		}

		void OnMenuClosed()
		{
			if (!g_state.menuOpen) {
				return;
			}

			g_state.menuOpen = false;
			g_state.controller.SetDesired(false, false);
			g_state.controller.SetFocusArgs(false, true);
			ApplyCreatorState("LooksMenuClosed");
			LogInfo(std::format("Character creator session generation {} closed.", g_state.generation.load()));
		}

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent&          a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto* menuName = a_event.menuName.c_str();
				if (!menuName || std::strcmp(menuName, kMenuName) != 0) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event.opening) {
					OnMenuOpened();
				} else {
					OnMenuClosed();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuSink g_menuSink;
	}

	void LogInfo(std::string_view a_message)
	{
		Internal::LogInfo(std::string("[CharacterCreator] ") + std::string(a_message));
	}

	void LogWarning(std::string_view a_message)
	{
		Internal::LogWarning(std::string("[CharacterCreator] ") + std::string(a_message));
	}

	void DispatchControllerInput(const char* a_button, const char* a_phase)
	{
		if (!a_button || !a_phase) {
			return;
		}

		PushEvent(nlohmann::json{
			{ "type", "creatorController" },
			{ "button", a_button },
			{ "phase", a_phase }
		});
	}

	bool IsOverlayUp()
	{
		return g_state.controller.IsCurrentlyVisible();
	}

	bool IsOverlayFocused()
	{
		return g_state.controller.IsCurrentlyFocused();
	}

	void Pump()
	{
		if (!g_state.menuOpen || !Internal::g_api || !Internal::g_prismaReady) {
			return;
		}

		if (!g_state.controller.IsDesiredVisible()) {
			ArmDesiredState();
		}

		if (!g_state.controller.IsHandleValid()) {
			CreateView();
		}

		if (g_state.controller.IsDesiredVisible() &&
			(!g_state.controller.IsCurrentlyVisible() || !g_state.controller.IsCurrentlyFocused()) &&
			g_state.controller.IsDomReady()) {
			ApplyCreatorState("GameThreadPendingFlush");
			return;
		}

		ReconcileVanillaSuppression();
	}

	void Install()
	{
		if (g_state.installed || !IsEnabled()) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			LogWarning("RE::UI unavailable; LooksMenu observation not installed.");
			return;
		}

		if (!g_state.sinkRegistered) {
			ui->RegisterSink(std::addressof(g_menuSink));
			g_state.sinkRegistered = true;
		}

		g_state.installed = true;
		LogInfo("LooksMenu observation installed.");

		if (ui->GetMenuOpen(kMenuName)) {
			OnMenuOpened();
		}
	}
}
