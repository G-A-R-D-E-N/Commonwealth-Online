#include "pch.h"

#include "F4TMainMenuController.h"
#include "F4TMainMenuInjectInternal.h"
#include "F4TMenuInput.h"
#include "F4TPrismaUI.h"
#include "F4TVtableHookGuard.h"

namespace F4T::MainMenuInject::Internal
{
	namespace
	{
		// source:CommonLib RE/B/BSInputEventUser.h names slots 04/05/06/08; TODO-verify the base offset (unchanged from HEAD).
			constexpr auto kBSInputEventUserOffset = 0x10;
		constexpr auto kBSInputOnThumbstickVtableIndex = 0x04;
		constexpr auto kBSInputOnCursorMoveVtableIndex = 0x05;
		constexpr auto kBSInputOnMouseMoveVtableIndex = 0x06;
		constexpr auto kBSInputOnButtonVtableIndex = 0x08;

		bool g_mainMenuInputHooksInstalled = false;

		using InitMainListFn = void (*)(RE::StartMenuBase*);
		InitMainListFn g_originalInitMainList = nullptr;

		void InitMainListHook(RE::StartMenuBase* a_this)
		{
			if (g_originalInitMainList) {
				g_originalInitMainList(a_this);
			}

			if (!a_this) {
				return;
			}

			if (!a_this->GetIsMenuReady()) {
				const auto* taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([a_this]() {
						if (TryInjectForStartMenu(a_this)) {
							MarkMainMenuInjectionComplete("InitMainListInjectionComplete");
						}
					});
				}
				return;
			}

			if (TryInjectForStartMenu(a_this)) {
				MarkMainMenuInjectionComplete("InitMainListInjectionComplete");
			}
		}

		std::uintptr_t g_originalMainMenuOnButtonEvent = 0;
		std::uintptr_t g_originalMainMenuOnCursorMoveEvent = 0;
		std::uintptr_t g_originalMainMenuOnMouseMoveEvent = 0;

		RE::IMenu* MainMenuFromInputUser(RE::BSInputEventUser* a_inputUser)
		{
			if (!a_inputUser) {
				return nullptr;
			}

			return reinterpret_cast<RE::IMenu*>(reinterpret_cast<char*>(a_inputUser) - kBSInputEventUserOffset);
		}

		using MainMenuOnButtonEventFn = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);
		using MainMenuOnCursorMoveEventFn = void (*)(RE::BSInputEventUser*, const RE::CursorMoveEvent*);
		using MainMenuOnMouseMoveEventFn = void (*)(RE::BSInputEventUser*, const RE::MouseMoveEvent*);

		void MainMenuOnButtonEventHook(RE::BSInputEventUser* a_this, const RE::ButtonEvent* a_event)
		{
			if (F4T::PrismaUI::IsCustomMenuInputCaptured()) {
				F4T::MenuInput::ForwardButtonEvent(a_event);
				return;
			}

			if (a_this && a_event && a_event->QJustPressed() && !a_event->QDisabled()) {
				const auto& userEvent = a_event->QUserEvent();
				if (userEvent == "Accept"sv) {
					const auto* ui = RE::UI::GetSingleton();
					const auto mainMenu = ui ? ui->GetMenu<RE::MainMenu>() : nullptr;
					const auto* menu = MainMenuFromInputUser(a_this);
					if (mainMenu && mainMenu.get() == menu && TryHandleMultiplayerSelection(mainMenu.get())) {
						return;
					}
				}
			}

			if (g_originalMainMenuOnButtonEvent != 0) {
				reinterpret_cast<MainMenuOnButtonEventFn>(g_originalMainMenuOnButtonEvent)(a_this, a_event);
			}
		}

		void MainMenuOnCursorMoveEventHook(RE::BSInputEventUser* a_this, const RE::CursorMoveEvent* a_event)
		{
			if (F4T::PrismaUI::IsCustomMenuInputCaptured()) {
				return;
			}

			if (g_originalMainMenuOnCursorMoveEvent != 0) {
				reinterpret_cast<MainMenuOnCursorMoveEventFn>(g_originalMainMenuOnCursorMoveEvent)(a_this, a_event);
			}
		}

		void MainMenuOnMouseMoveEventHook(RE::BSInputEventUser* a_this, const RE::MouseMoveEvent* a_event)
		{
			if (F4T::PrismaUI::IsCustomMenuInputCaptured()) {
				return;
			}

			if (g_originalMainMenuOnMouseMoveEvent != 0) {
				reinterpret_cast<MainMenuOnMouseMoveEventFn>(g_originalMainMenuOnMouseMoveEvent)(a_this, a_event);
			}
		}
	}

	bool InstallInitMainListHook()
	{
		// source:CommonLib RE/S/StartMenuBase.h InitMainList slot 0x15.
		REL::Relocation<std::uintptr_t> startMenuVtable{ RE::StartMenuBase::VTABLE[0] };
		g_originalInitMainList = reinterpret_cast<InitMainListFn>(
			VtableHookGuard::TryWriteVfunc(
				startMenuVtable,
				0x15,
				REX::UNRESTRICTED_CAST<std::uintptr_t>(&InitMainListHook),
				"InitMainList"));
		return g_originalInitMainList != nullptr;
	}

	bool InstallMainMenuInputHooks()
	{
		if (g_mainMenuInputHooksInstalled) {
			return true;
		}

		REL::Relocation<std::uintptr_t> inputVtable{ RE::MainMenu::VTABLE[1] };
		g_originalMainMenuOnThumbstickEvent = VtableHookGuard::TryWriteVfunc(
			inputVtable,
			kBSInputOnThumbstickVtableIndex,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&MainMenuOnThumbstickEventHook),
			"MainMenu thumbstick");
		g_originalMainMenuOnCursorMoveEvent = VtableHookGuard::TryWriteVfunc(
			inputVtable,
			kBSInputOnCursorMoveVtableIndex,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&MainMenuOnCursorMoveEventHook),
			"MainMenu cursor move");
		g_originalMainMenuOnMouseMoveEvent = VtableHookGuard::TryWriteVfunc(
			inputVtable,
			kBSInputOnMouseMoveVtableIndex,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&MainMenuOnMouseMoveEventHook),
			"MainMenu mouse move");
		g_originalMainMenuOnButtonEvent = VtableHookGuard::TryWriteVfunc(
			inputVtable,
			kBSInputOnButtonVtableIndex,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&MainMenuOnButtonEventHook),
			"MainMenu button");

		if (g_originalMainMenuOnButtonEvent == 0) {
			LogWarning("MainMenu input OnButtonEvent hook failed to install.");
			return false;
		}

		g_mainMenuInputHooksInstalled = true;
		LogInfo("Installed MainMenu input hooks on BSInputEventUser vtable (thumbstick, cursor-move, mouse-move, button).");
		return true;
	}

	bool AreMainMenuInputHooksInstalled()
	{
		return g_mainMenuInputHooksInstalled;
	}
}
