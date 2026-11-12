#include "pch.h"

#include "F4TMainMenuInject.h"
#include "F4TMainMenuInjectInternal.h"
#include "F4TPrismaUI.h"

#include "Scaleform/G/GFx_FunctionHandler.h"
#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace F4T::MainMenuInject::Internal
{
	namespace
	{
		constexpr auto kResyncThrottleFrames = 30;
		constexpr auto kItemPressListenerPriority = 100;
		constexpr std::array<std::string_view, 3> kItemPressEventCandidates{
			"itemPress",
			"listItemPress",
			"listPress"
		};

		bool g_itemPressListenerRegistered = false;
		int g_resyncFrameCounter = 0;
		constexpr auto kMultiplayerOpenDebounce = 500ms;
		std::chrono::steady_clock::time_point g_lastMultiplayerOpenTime{};

		bool EnsureMainMenuMainState(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->menuObj.IsObject()) {
				return false;
			}

			if (IsMainStateForMenu(a_mainMenu)) {
				return true;
			}

			constexpr auto kMaxCancelAttempts = 12;
			const std::array<Scaleform::GFx::Value, 2> cancelArgs{
				Scaleform::GFx::Value{ true },
				Scaleform::GFx::Value{ false }
			};

			for (int attempt = 0; attempt < kMaxCancelAttempts; ++attempt) {
				if (IsMainStateForMenu(a_mainMenu)) {
					return true;
				}

				a_mainMenu->menuObj.Invoke("onCancelPress", nullptr, cancelArgs.data(), cancelArgs.size());
			}

			return IsMainStateForMenu(a_mainMenu);
		}

		bool OpenMultiplayerMenu(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu) {
				return false;
			}

			const auto now = std::chrono::steady_clock::now();
			if (g_lastMultiplayerOpenTime.time_since_epoch().count() != 0 &&
				now - g_lastMultiplayerOpenTime < kMultiplayerOpenDebounce) {
				return false;
			}

			if (!EnsureMainMenuMainState(a_mainMenu)) {
				LogWarning("Could not return MainMenu to MAIN_STATE before opening multiplayer browser.");
				return false;
			}

			g_lastMultiplayerOpenTime = now;

			if (a_mainMenu->uiMovie) {
				Scaleform::GFx::Value bgsCode;
				if (a_mainMenu->menuObj.IsObject() &&
					a_mainMenu->menuObj.GetMember("BGSCodeObj", &bgsCode) &&
					bgsCode.IsObject()) {
					bgsCode.Invoke("PlayOKSound");
				} else if (a_mainMenu->uiMovie->GetVariable(&bgsCode, "BGSCodeObj") && bgsCode.IsObject()) {
					bgsCode.Invoke("PlayOKSound");
				}
			}

			const auto* taskInterface = F4SE::GetTaskInterface();
			if (taskInterface) {
				taskInterface->AddTask([]() {
					F4T::PrismaUI::ShowServerBrowser();
				});
				return true;
			}

			F4T::PrismaUI::ShowServerBrowser();
			return true;
		}

		bool IsMultiplayerRowSelected(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie || IsPauseMode(a_mainMenu->uiMovie.get())) {
				return false;
			}

			if (!IsMainStateForMenu(a_mainMenu)) {
				return false;
			}

			Scaleform::GFx::Value listWidget;
			Scaleform::GFx::Value entryList;
			if (!ResolveMainList(a_mainMenu, listWidget, entryList)) {
				return false;
			}

			Scaleform::GFx::Value selectedEntry;
			if (!listWidget.GetMember("selectedEntry", &selectedEntry) || !selectedEntry.IsObject()) {
				return false;
			}

			const auto selectedIndex = GetEntryLogicalIndex(selectedEntry);
			return selectedIndex.has_value() && *selectedIndex == kMultiplayerIndex;
		}

		std::optional<std::string> TryReadClassItemPressEvent(
			Scaleform::GFx::Movie* a_movie,
			const char* a_className)
		{
			if (!a_movie || a_className == nullptr) {
				return std::nullopt;
			}

			const std::array<Scaleform::GFx::Value, 1> classNameArg{ Scaleform::GFx::Value{ a_className } };
			Scaleform::GFx::Value classDef;
			if (!a_movie->Invoke(
					"flash.utils.getDefinitionByName",
					&classDef,
					classNameArg.data(),
					static_cast<std::uint32_t>(classNameArg.size())) ||
				!classDef.IsObject()) {
				return std::nullopt;
			}

			Scaleform::GFx::Value itemPressEvent;
			if (!classDef.GetMember("ITEM_PRESS", &itemPressEvent) || !itemPressEvent.IsString()) {
				return std::nullopt;
			}

			const char* eventName = itemPressEvent.GetString();
			if (eventName == nullptr || *eventName == '\0') {
				return std::nullopt;
			}

			return std::string{ eventName };
		}

		std::vector<std::string> ResolveItemPressEventNames(Scaleform::GFx::Movie* a_movie)
		{
			std::vector<std::string> eventNames;
			eventNames.reserve(kItemPressEventCandidates.size() + 1);

			const std::array<const char*, 2> kClassNames{
				"Shared.AS3.BSScrollingList",
				"BSScrollingList"
			};

			for (const auto* className : kClassNames) {
				if (const auto resolved = TryReadClassItemPressEvent(a_movie, className)) {
					if (std::find(eventNames.begin(), eventNames.end(), *resolved) == eventNames.end()) {
						eventNames.push_back(*resolved);
					}
				}
			}

			Scaleform::GFx::Value itemPressEvent;
			if (a_movie &&
				a_movie->GetVariable(&itemPressEvent, "Shared.AS3.BSScrollingList.ITEM_PRESS") &&
				itemPressEvent.IsString() &&
				itemPressEvent.GetString() != nullptr) {
				const auto variableName = std::string{ itemPressEvent.GetString() };
				if (std::find(eventNames.begin(), eventNames.end(), variableName) == eventNames.end()) {
					eventNames.push_back(variableName);
				}
			}

			for (const auto candidate : kItemPressEventCandidates) {
				const auto candidateName = std::string{ candidate };
				if (std::find(eventNames.begin(), eventNames.end(), candidateName) == eventNames.end()) {
					eventNames.push_back(candidateName);
				}
			}

			return eventNames;
		}

		class ItemPressHandler :
			public Scaleform::GFx::FunctionHandler
		{
		public:
			void Call(const Params& a_params) override
			{
				if (a_params.argCount < 1) {
					return;
				}

				auto* mainMenu = static_cast<RE::MainMenu*>(a_params.userData);
				if (!mainMenu || !mainMenu->uiMovie) {
					return;
				}

				Scaleform::GFx::Value target;
				if (!a_params.args[0].GetMember("target", &target) || !target.IsObject()) {
					return;
				}

				Scaleform::GFx::Value selectedEntry;
				if (!target.GetMember("selectedEntry", &selectedEntry) || !selectedEntry.IsObject()) {
					return;
				}

				const auto selectedIndex = GetEntryLogicalIndex(selectedEntry);
				if (!selectedIndex.has_value() || *selectedIndex != kMultiplayerIndex) {
					return;
				}

				if (OpenMultiplayerMenu(mainMenu)) {
					LogInfo("Opened multiplayer menu from ITEM_PRESS handler.");
				}
				a_params.args[0].Invoke("stopImmediatePropagation");
			}
		};

		ItemPressHandler g_itemPressHandler;

	}

		bool TryHandleMultiplayerSelection(RE::MainMenu* a_mainMenu)
		{
			if (!IsMultiplayerRowSelected(a_mainMenu)) {
				return false;
			}

			if (OpenMultiplayerMenu(a_mainMenu)) {
				LogInfo("Opened multiplayer menu from MainMenu selection.");
			}
			return true;
		}

		void RegisterItemPressListener(RE::MainMenu* a_mainMenu)
		{
			if (!a_mainMenu || !a_mainMenu->uiMovie || g_itemPressListenerRegistered) {
				return;
			}

			if (!a_mainMenu->menuObj.IsObject()) {
				const auto* taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([a_mainMenu]() {
						RegisterItemPressListener(a_mainMenu);
					});
				}
				return;
			}

			Scaleform::GFx::Value listWidget;
			Scaleform::GFx::Value entryList;
			if (!ResolveMainList(a_mainMenu, listWidget, entryList)) {
				LogWarning(
					"Optional ITEM_PRESS integration unavailable: could not resolve "
					"MainPanel_mc.List_mc. Overlay readiness is unaffected.");
				return;
			}

			const auto eventNames = ResolveItemPressEventNames(a_mainMenu->uiMovie.get());

			Scaleform::GFx::Value handler;
			a_mainMenu->uiMovie->CreateFunction(&handler, &g_itemPressHandler, a_mainMenu);

			std::size_t registeredListeners = 0;
			for (const auto& eventName : eventNames) {
				const std::array<Scaleform::GFx::Value, 4> args{
					Scaleform::GFx::Value{ eventName.c_str() },
					handler,
					Scaleform::GFx::Value{ true },
					Scaleform::GFx::Value{ kItemPressListenerPriority }
				};

				if (a_mainMenu->menuObj.Invoke("addEventListener", nullptr, args.data(), args.size())) {
					++registeredListeners;
				}

				if (listWidget.Invoke("addEventListener", nullptr, args.data(), args.size())) {
					++registeredListeners;
				}
			}

			if (registeredListeners == 0) {
				LogWarning("Failed to register ITEM_PRESS listeners on MainMenu.");
				return;
			}

			g_itemPressListenerRegistered = true;
			LogInfo(std::format(
				"Registered {} ITEM_PRESS listener(s); candidates: {}",
				registeredListeners,
				eventNames.size()));
		}

	void ResetItemPressListener()
	{
		g_itemPressListenerRegistered = false;
	}

	void PollMultiplayerActivation()
	{
		static bool lastAcceptDown = false;
		static bool lastLeftButtonDown = false;
		const auto acceptDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
		const auto acceptPressed = acceptDown && !lastAcceptDown;
		lastAcceptDown = acceptDown;
		const auto leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const auto clickFallback = leftButtonDown && !lastLeftButtonDown && !g_itemPressListenerRegistered &&
			F4T::PrismaUI::IsGameForeground();
		lastLeftButtonDown = leftButtonDown;

		if (F4T::PrismaUI::IsCustomMenuInputCaptured()) {
			return;
		}

		const auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->GetMenuOpen<RE::MainMenu>() || !IsMainMenuOnMainPanel(ui)) {
			return;
		}

			const auto mainMenu = ui->GetMenu<RE::MainMenu>();
			if (!mainMenu || !IsMultiplayerRowSelected(mainMenu.get())) {
				return;
			}

			if (!acceptPressed && !clickFallback) {
				return;
			}

			if (OpenMultiplayerMenu(mainMenu.get())) {
				LogInfo(clickFallback && !acceptPressed ?
					"Opened multiplayer menu from click fallback (ITEM_PRESS listener not registered)." :
					"Opened multiplayer menu from activation poll.");
			}
		}

	void ResyncMultiplayerRow()
	{
		if (++g_resyncFrameCounter < kResyncThrottleFrames) {
			return;
		}

		g_resyncFrameCounter = 0;

		const auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->GetMenuOpen<RE::MainMenu>() || !IsMainMenuOnMainPanel(ui)) {
			return;
		}

			const auto mainMenu = ui->GetMenu<RE::MainMenu>();
			if (!mainMenu || !mainMenu->uiMovie || IsPauseMode(mainMenu->uiMovie.get())) {
				return;
			}

			Scaleform::GFx::Value listWidget;
			Scaleform::GFx::Value entryList;
			if (!ResolveMainList(mainMenu.get(), listWidget, entryList) || !entryList.IsArray()) {
				return;
			}

			if (!EntryListHasIndex(entryList, kMultiplayerIndex)) {
				InjectMultiplayerRow(mainMenu.get());
			}

			if (!g_itemPressListenerRegistered) {
				RegisterItemPressListener(mainMenu.get());
			}
		}
}
