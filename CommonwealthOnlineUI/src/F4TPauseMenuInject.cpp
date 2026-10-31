#include "pch.h"

#include "F4TPauseMenuInject.h"
#include "F4TPrismaUI.h"
#include "SessionCallbacks.h"

#include <string_view>

namespace F4T::PauseMenuInject
{
	namespace
	{
		bool g_hooksInstalled = false;

		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [PauseMenuInject]";
		}

		void LogInfo(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}

		void LogWarning(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::WARN(std::string_view{ message });
		}

		class PauseMenuOpenCloseSink :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override
			{
				(void)a_source;

				if (a_event.menuName != RE::PauseMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event.opening) {
					LogInfo("Fallout PauseMenu opened");
					F4T::PrismaUI::ShowPauseMenu();
				} else {
					LogInfo("Fallout PauseMenu closed");
					F4T::PrismaUI::HidePauseMenu();
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		PauseMenuOpenCloseSink g_pauseMenuOpenCloseSink;

		void TryCatchUpOpenPauseMenu()
		{
			const auto* ui = RE::UI::GetSingleton();
			if (!ui || !ui->GetMenuOpen<RE::PauseMenu>()) {
				return;
			}

			if (F4T::PrismaUI::IsPauseMenuVisible()) {
				return;
			}

			LogInfo("Running catch-up show for already-open PauseMenu.");
			F4T::PrismaUI::ShowPauseMenu();
		}
	}

	void SetPauseMenuInputSuppressed(bool a_suppressed)
	{
		const auto* ui = RE::UI::GetSingleton();
		const auto pauseMenu = ui ? ui->GetMenu<RE::PauseMenu>() : nullptr;
		if (!pauseMenu) {
			return;
		}

		pauseMenu->inputEventHandlingEnabled = !a_suppressed;
	}

	void Install()
	{
		if (g_hooksInstalled) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (ui) {
			ui->RegisterSink(std::addressof(g_pauseMenuOpenCloseSink));
			LogInfo("PauseMenu open/close listener registered.");
		} else {
			LogWarning("UI singleton unavailable; PauseMenu open/close listener not registered.");
		}

		const auto* taskInterface = F4SE::GetTaskInterface();
		if (taskInterface) {
			taskInterface->AddTask([]() {
				TryCatchUpOpenPauseMenu();
			});
		}

		g_hooksInstalled = true;
	}
}
