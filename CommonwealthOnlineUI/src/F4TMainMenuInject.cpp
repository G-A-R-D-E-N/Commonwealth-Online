#include "pch.h"

#include "F4TMainMenuInject.h"
#include "F4TMainMenuInjectInternal.h"
#include "F4TMenuInput.h"
#include "SessionCallbacks.h"
#include "F4TPrismaUI.h"
#include "F4TF4SEPermanentTask.h"

#include "Scaleform/G/GFx_FunctionHandler.h"
#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::MainMenuInject
{
	namespace Internal
	{
		constexpr auto kLoadIndex = 4;
		constexpr auto kEntryListPath = "MainPanel_mc.List_mc.entryList";
		constexpr auto kListWidgetPath = "MainPanel_mc.List_mc";

		bool g_hooksInstalled = false;
		bool g_resyncTaskStarted = false;
		int g_injectFailLogCooldown = 0;


		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [MainMenuInject]";
		}

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("{} {}", GetLogPrefix(), a_message);
		}

		void LogWarning(std::string_view a_message)
		{
			REX::WARN("{} {}", GetLogPrefix(), a_message);
		}

		void LogInjectFailure(std::string_view a_reason)
		{
			if (g_injectFailLogCooldown > 0) {
				--g_injectFailLogCooldown;
				return;
			}

			g_injectFailLogCooldown = 120;
			LogWarning(a_reason);
		}

		std::optional<std::int32_t> GetEntryLogicalIndex(const Scaleform::GFx::Value& a_entry)
		{
			Scaleform::GFx::Value indexValue;
			if (!a_entry.GetMember("index", &indexValue)) {
				return std::nullopt;
			}

			if (indexValue.IsInt()) {
				return indexValue.GetInt();
			}

			if (indexValue.IsUInt()) {
				return static_cast<std::int32_t>(indexValue.GetUInt());
			}

			if (indexValue.IsNumber()) {
				return static_cast<std::int32_t>(indexValue.GetNumber());
			}

			return std::nullopt;
		}

		bool IsPauseMode(Scaleform::GFx::Movie* a_movie)
		{
			if (!a_movie) {
				return true;
			}

			Scaleform::GFx::Value pauseMode;
			if (!a_movie->GetVariable(&pauseMode, "PauseMode") || !pauseMode.IsBoolean()) {
				return false;
			}

			return pauseMode.GetBoolean();
		}

		int g_titleMenuFrames = 0;
		RE::IMenu* g_lastTitleMenu = nullptr;

		constexpr std::string_view kPlayBinkMenuName{ "PlayBinkMenu" };

		void TickTitleMenuFrames()
		{
			const auto* ui = RE::UI::GetSingleton();
			const auto mainMenu = ui ? ui->GetMenu<RE::MainMenu>() : nullptr;
			if (mainMenu && ui->GetMenuOpen<RE::MainMenu>()) {
				if (g_lastTitleMenu != mainMenu.get()) {
					g_titleMenuFrames = 0;
					g_lastTitleMenu = mainMenu.get();
				} else {
					++g_titleMenuFrames;
				}
			} else {
				g_titleMenuFrames = 0;
				g_lastTitleMenu = nullptr;
			}
		}

		std::optional<std::string_view> TryGetMainMenuCurrentState(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie) {
				return std::nullopt;
			}

			Scaleform::GFx::Value state;
			if (a_mainMenu->menuObj.IsObject() && a_mainMenu->menuObj.GetMember("currentState", &state)) {
				if (state.IsString()) {
					const char* currentState = state.GetString();
					if (currentState != nullptr) {
						return std::string_view{ currentState };
					}
				}
			}

			if (a_mainMenu->uiMovie->GetVariable(&state, "currentState") && state.IsString()) {
				const char* currentState = state.GetString();
				if (currentState != nullptr) {
					return std::string_view{ currentState };
				}
			}

			return std::nullopt;
		}

		bool IsTitleIntroBinkPlaying(const RE::UI* a_ui)
		{
			return a_ui && a_ui->GetMenuOpen(RE::BSFixedString(kPlayBinkMenuName));
		}

		bool IsMainPanelVisible(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie) {
				return false;
			}

			Scaleform::GFx::Value mainPanel;
			if (a_mainMenu->menuObj.IsObject()) {
				a_mainMenu->menuObj.GetMember("MainPanel_mc", &mainPanel);
			}
			if (!mainPanel.IsObject()) {
				a_mainMenu->uiMovie->GetVariable(&mainPanel, "MainPanel_mc");
			}
			if (!mainPanel.IsObject()) {
				return false;
			}

			Scaleform::GFx::Value visible;
			if (mainPanel.GetMember("_visible", &visible) && visible.IsBoolean()) {
				return visible.GetBoolean();
			}

			return false;
		}

		bool IsMainMenuMainListReady(RE::MainMenu* a_mainMenu);
		bool IsMainMenuIntroComplete(RE::MainMenu* a_mainMenu);
		bool IsMainStateForMenu(RE::MainMenu* a_mainMenu);

		bool IsTitleScreenMainMenu(RE::StartMenuBase* a_menu)
		{
			if (!a_menu || !a_menu->uiMovie) {
				return false;
			}

			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}

			const auto mainMenu = ui->GetMenu<RE::MainMenu>();
			if (!mainMenu || mainMenu.get() != a_menu) {
				return false;
			}

			return !IsPauseMode(a_menu->uiMovie.get());
		}

		bool EntryHasIndex(const Scaleform::GFx::Value& a_entry, std::int32_t a_index)
		{
			const auto logicalIndex = GetEntryLogicalIndex(a_entry);
			return logicalIndex.has_value() && *logicalIndex == a_index;
		}

		bool EntryListHasIndex(const Scaleform::GFx::Value& a_entryList, std::int32_t a_index)
		{
			if (!a_entryList.IsArray()) {
				return false;
			}

			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				Scaleform::GFx::Value entry;
				if (a_entryList.GetElement(i, &entry) && EntryHasIndex(entry, a_index)) {
					return true;
				}
			}

			return false;
		}

		std::optional<std::uint32_t> FindEntryArrayIndex(const Scaleform::GFx::Value& a_entryList, std::int32_t a_index)
		{
			if (!a_entryList.IsArray()) {
				return std::nullopt;
			}

			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				Scaleform::GFx::Value entry;
				if (a_entryList.GetElement(i, &entry) && EntryHasIndex(entry, a_index)) {
					return i;
				}
			}

			return std::nullopt;
		}

		bool ResolveMainList(
			RE::MainMenu* a_mainMenu,
			Scaleform::GFx::Value& a_outListWidget,
			Scaleform::GFx::Value& a_outEntryList)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie) {
				return false;
			}

			if (a_mainMenu->menuObj.IsObject()) {
				Scaleform::GFx::Value mainPanel;
				if (a_mainMenu->menuObj.GetMember("MainPanel_mc", &mainPanel) && mainPanel.IsObject()) {
					Scaleform::GFx::Value listWidget;
					if (mainPanel.GetMember("List_mc", &listWidget) && listWidget.IsObject()) {
						Scaleform::GFx::Value entryList;
						if (listWidget.GetMember("entryList", &entryList) &&
							(entryList.IsArray() || entryList.IsObject())) {
							a_outListWidget = listWidget;
							a_outEntryList = entryList;
							return true;
						}
					}
				}
			}

			if (!a_mainMenu->uiMovie->GetVariable(&a_outListWidget, kListWidgetPath) || !a_outListWidget.IsObject()) {
				return false;
			}

			if (!a_outListWidget.GetMember("entryList", &a_outEntryList) &&
				!a_mainMenu->uiMovie->GetVariable(&a_outEntryList, kEntryListPath)) {
				return false;
			}

			return a_outEntryList.IsArray() || a_outEntryList.IsObject();
		}

		bool IsMainMenuMainListReady(RE::MainMenu* a_mainMenu)
		{
			Scaleform::GFx::Value listWidget;
			Scaleform::GFx::Value entryList;
			if (!ResolveMainList(a_mainMenu, listWidget, entryList)) {
				return false;
			}

			if (!entryList.IsArray()) {
				return false;
			}

			return entryList.GetArraySize() > 0;
		}

		bool IsMainMenuIntroComplete(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu) {
				return false;
			}

			if (a_mainMenu->userEngaged) {
				return true;
			}

			if (a_mainMenu->creditScreen) {
				return false;
			}

			const auto* ui = RE::UI::GetSingleton();
			if (IsTitleIntroBinkPlaying(ui)) {
				return false;
			}

			// Title screen is up once the vanilla main panel/list is visible and populated.
			if (IsMainPanelVisible(a_mainMenu) && IsMainMenuMainListReady(a_mainMenu)) {
				return true;
			}

			if (a_mainMenu->mainBinkShown) {
				return true;
			}

			// Short safety net only if GFx visibility checks are unavailable.
			constexpr int kFallbackFramesAfterOpen = 45;
			return g_titleMenuFrames >= kFallbackFramesAfterOpen && a_mainMenu->GetIsMenuReady();
		}

		bool IsMainStateForMenu(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie) {
				return false;
			}

			const auto currentState = TryGetMainMenuCurrentState(a_mainMenu);
			if (currentState.has_value()) {
				return *currentState == "MAIN_STATE";
			}

			// Title screen often leaves currentState unset even on the main list.
			// Accept that once intro playback is finished.
			return IsMainMenuIntroComplete(a_mainMenu);
		}

		bool InsertEntryAfterIndex(
			Scaleform::GFx::Movie* a_movie,
			Scaleform::GFx::Value& a_entryList,
			std::int32_t a_afterLogicalIndex,
			const Scaleform::GFx::Value& a_newEntry)
		{
			const auto insertAt = FindEntryArrayIndex(a_entryList, a_afterLogicalIndex);
			if (!insertAt.has_value()) {
				return false;
			}

			const auto targetIndex = *insertAt + 1;
			const auto oldSize = a_entryList.GetArraySize();

			for (std::uint32_t i = oldSize; i > targetIndex; --i) {
				Scaleform::GFx::Value entry;
				if (!a_entryList.GetElement(i - 1, &entry)) {
					return false;
				}

				if (!a_entryList.SetElement(i, entry)) {
					return false;
				}
			}

			(void)a_movie;
			return a_entryList.SetElement(targetIndex, a_newEntry);
		}

		bool InjectMultiplayerRow(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie || IsPauseMode(a_mainMenu->uiMovie.get())) {
				LogInjectFailure("Injection skipped: MainMenu movie unavailable or pause mode active.");
				return false;
			}

			Scaleform::GFx::Value listWidget;
			Scaleform::GFx::Value entryList;
			if (!ResolveMainList(a_mainMenu, listWidget, entryList)) {
				LogInjectFailure("Injection skipped: could not resolve MainPanel_mc.List_mc.entryList.");
				return false;
			}

			if (!entryList.IsArray()) {
				LogInjectFailure("Injection skipped: entryList is not a GFx array.");
				return false;
			}

			if (EntryListHasIndex(entryList, kMultiplayerIndex)) {
				return true;
			}

			Scaleform::GFx::Value newEntry;
			a_mainMenu->uiMovie->CreateObject(&newEntry);
			if (!newEntry.IsObject()) {
				LogInjectFailure("Injection skipped: failed to create GFx entry object.");
				return false;
			}

			Scaleform::GFx::Value textValue{ "MULTIPLAYER" };
			Scaleform::GFx::Value indexValue{ kMultiplayerIndex };
			if (!newEntry.SetMember("text", textValue) || !newEntry.SetMember("index", indexValue)) {
				LogInjectFailure("Injection skipped: failed to populate Multiplayer entry members.");
				return false;
			}

			if (!InsertEntryAfterIndex(a_mainMenu->uiMovie.get(), entryList, kLoadIndex, newEntry)) {
				LogInjectFailure(std::format(
					"Injection skipped: could not find Load row (logical index {}).",
					kLoadIndex));
				return false;
			}

			listWidget.Invoke("InvalidateData");
			LogInfo("Injected Multiplayer row after Load.");
			return true;
		}

	bool TryInjectForStartMenu(RE::StartMenuBase* a_menu)
	{
		if (!IsTitleScreenMainMenu(a_menu)) {
			return false;
		}

		InjectMultiplayerRow(static_cast<RE::MainMenu*>(a_menu));
		return true;
	}

	void MarkMainMenuInjectionComplete(std::string_view a_reason)
	{
		if (AreMainMenuInputHooksInstalled()) {
			// The HTML overlay only requires the MainMenu instance and native input hooks.
			// ITEM_PRESS/list-row integration is optional and must not gate display.
			F4T::PrismaUI::SetMainMenuOverlayReady(true, a_reason);
			return;
		}

		LogWarning("MainMenu injection cannot become ready: native input hooks are unavailable.");
		F4T::PrismaUI::SetMainMenuOverlayReady(
			false,
			"MainMenuInjectionFailed:MainMenuInputHooksUnavailable");
	}


	void TryInjectOpenMainMenu()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			LogWarning("Catch-up injection failed: UI singleton unavailable.");
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"CatchUpInjectionFailed:UISingletonUnavailable");
			return;
		}

		const auto mainMenu = ui->GetMenu<RE::MainMenu>();
		if (!mainMenu) {
			if (ui->GetMenuOpen<RE::MainMenu>()) {
				LogWarning("Catch-up injection failed: Fallout MainMenu is open but its instance is unavailable.");
				F4T::PrismaUI::SetMainMenuOverlayReady(
					false,
					"CatchUpInjectionFailed:MainMenuInstanceUnavailable");
			}
			return;
		}

		if (!ui->GetMenuOpen<RE::MainMenu>() && !mainMenu->menuCanBeVisible) {
			return;
		}

		LogInfo("Running catch-up injection for already-open MainMenu.");
		F4T::PrismaUI::SetMainMenuOverlayReady(false, "CatchUpInjectionStarted");
		// The normal MenuOpenClose event occurred before our sink was installed. Replay
		// that transition so desired state cannot remain at its default false value.
		F4T::PrismaUI::OnFalloutMainMenuOpened();

		if (!mainMenu->uiMovie) {
			LogWarning("Catch-up injection failed: MainMenu movie unavailable.");
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"CatchUpInjectionFailed:MainMenuMovieUnavailable");
			return;
		}

		RegisterItemPressListener(mainMenu.get());
		InjectMultiplayerRow(mainMenu.get());

		if (!AreMainMenuInputHooksInstalled()) {
			LogWarning("Catch-up injection failed: MainMenu native input hooks are unavailable.");
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"CatchUpInjectionFailed:MainMenuInputHooksUnavailable");
			return;
		}

		F4T::PrismaUI::SetMainMenuOverlayReady(true, "CatchUpInjectionComplete");
	}

		class MainMenuOpenCloseSink :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override
			{
				(void)a_source;

				if (a_event.menuName == RE::MessageBoxMenu::MENU_NAME) {
					F4T::PrismaUI::SetVanillaMessageBoxOpen(a_event.opening);
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event.menuName != RE::MainMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event.opening) {
					F4T::PrismaUI::SetMainMenuOverlayReady(
						false,
						"FalloutMainMenuOpened:WaitingForInjection");
					F4T::PrismaUI::OnFalloutMainMenuOpened();

					const auto* ui = RE::UI::GetSingleton();
					if (!ui) {
						LogWarning("MainMenu injection failed: UI singleton unavailable after menu-open event.");
						F4T::PrismaUI::SetMainMenuOverlayReady(
							false,
							"MainMenuInjectionFailed:UISingletonUnavailable");
						return RE::BSEventNotifyControl::kContinue;
					}

					const auto mainMenu = ui->GetMenu<RE::MainMenu>();
					if (!mainMenu) {
						LogWarning("MainMenu injection failed: menu instance unavailable after menu-open event.");
						F4T::PrismaUI::SetMainMenuOverlayReady(
							false,
							"MainMenuInjectionFailed:MainMenuInstanceUnavailable");
						return RE::BSEventNotifyControl::kContinue;
					}

					ResetItemPressListener();

					const auto* taskInterface = F4SE::GetTaskInterface();
					if (taskInterface) {
						taskInterface->AddTask([mainMenu = mainMenu.get()]() {
							RegisterItemPressListener(mainMenu);
							(void)TryInjectForStartMenu(mainMenu);
							MarkMainMenuInjectionComplete("MainMenuOpenInjectionComplete");
						});
					} else {
						LogWarning("MainMenu injection failed: F4SE task interface unavailable.");
						F4T::PrismaUI::SetMainMenuOverlayReady(
							false,
							"MainMenuInjectionFailed:TaskInterfaceUnavailable");
					}
				} else {
					ResetItemPressListener();
					F4T::PrismaUI::SetMainMenuOverlayReady(false, "FalloutMainMenuClosed");
					F4T::PrismaUI::OnFalloutMainMenuClosed();
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MainMenuOpenCloseSink g_mainMenuOpenCloseSink;

		bool IsTitleFlowOpenForCustomOverlay(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie) {
				return false;
			}

			if (IsPauseMode(a_mainMenu->uiMovie.get()) || a_mainMenu->creditScreen) {
				return false;
			}

			const auto currentState = TryGetMainMenuCurrentState(a_mainMenu);
			if (!currentState.has_value() || currentState->empty()) {
				// Pre-MAIN_STATE title screens (including "Press Any Button") often have no state.
				return true;
			}

			if (*currentState == "MAIN_STATE") {
				return true;
			}

			// Tear down only when vanilla navigates into a sub-panel we don't own.
			static constexpr std::array kAwayStates{
				"SETTINGS_STATE"sv,
				"LOAD_STATE"sv,
				"HELP_STATE"sv,
				"CREDITS_STATE"sv,
				"MODS_STATE"sv,
				"DLC_STATE"sv,
				"CHARACTER_CREATION_STATE"sv,
			};
			for (const auto away : kAwayStates) {
				if (*currentState == away) {
					return false;
				}
			}

			// Unknown early/idle title states: keep the custom overlay.
			return true;
		}
	}

	using namespace Internal;

	bool IsMainMenuOnMainPanel(const RE::UI* a_ui)
	{
		if (!a_ui || !a_ui->GetMenuOpen<RE::MainMenu>()) {
			return false;
		}

		const auto mainMenu = a_ui->GetMenu<RE::MainMenu>();
		return IsTitleFlowOpenForCustomOverlay(mainMenu.get());
	}

	void SetMainMenuInputSuppressed(bool a_suppressed)
	{
		const auto* ui = RE::UI::GetSingleton();
		const auto mainMenu = ui ? ui->GetMenu<RE::MainMenu>() : nullptr;
		if (!mainMenu) {
			return;
		}

		mainMenu->inputEventHandlingEnabled = !a_suppressed;
	}

	void Install()
	{
		if (g_hooksInstalled) {
			return;
		}

		if (!InstallInitMainListHook()) {
			LogWarning("InitMainList vtable hook failed to install.");
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"MainMenuInjectionFailed:InitMainListHookUnavailable");
			return;
		}

		if (!InstallMainMenuInputHooks()) {
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"MainMenuInjectionFailed:MainMenuInputHooksUnavailable");
		}

		auto* ui = RE::UI::GetSingleton();
		if (ui) {
			ui->RegisterSink(std::addressof(g_mainMenuOpenCloseSink));
		} else {
			LogWarning("UI singleton unavailable; MainMenu open/close listener not registered.");
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"MainMenuInjectionFailed:OpenCloseListenerUnavailable");
		}

		const auto* taskInterface = F4SE::GetTaskInterface();
		if (taskInterface) {
			if (!g_resyncTaskStarted) {
				F4T::F4SECapabilities::SchedulePermanentTask(taskInterface, F4SE::GetF4SEVersion(), []() {
					TickTitleMenuFrames();
					PollMultiplayerActivation();
					ResyncMultiplayerRow();

				});
				g_resyncTaskStarted = true;
			}

			taskInterface->AddTask([]() {
				TryInjectOpenMainMenu();
			});
		} else {
			LogWarning("F4SE task interface unavailable; catch-up MainMenu injection cannot run.");
			F4T::PrismaUI::SetMainMenuOverlayReady(
				false,
				"CatchUpInjectionFailed:TaskInterfaceUnavailable");
		}

		g_hooksInstalled = true;
		LogInfo("InitMainList hook and MainMenu injection tasks installed.");
	}
}
