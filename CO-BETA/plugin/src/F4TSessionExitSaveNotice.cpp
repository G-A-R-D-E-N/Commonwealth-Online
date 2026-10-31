#include "pch.h"

#include "F4TSessionExitSaveNotice.h"

#include "CommonwealthOnlineUI_API.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::SessionExitSaveNotice
{
	namespace
	{
		constexpr const char* kAlertId{ "co-session-exit-save" };

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1* AlertApi()
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert || !ui->DismissAlert) {
				REX::WARN("SessionExitSaveNotice: the alert API is unavailable; the session save notice was not shown");
				return nullptr;
			}
			return ui;
		}

		void PushPause(const char* a_state, std::string_view a_message)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI10>();
			if (!ui || !ui->NotifySessionExitSave) {
				REX::WARN("SessionExitSaveNotice: the pause menu save API is unavailable");
				return;
			}

			const std::string message{ a_message };
			ui->NotifySessionExitSave(a_state, message.c_str());
		}

		void ShowNotice(std::string_view a_title, std::string_view a_message, const char* a_tone, std::uint32_t a_durationMs)
		{
			auto* ui = AlertApi();
			if (!ui) {
				return;
			}

			const std::string title{ a_title };
			const std::string message{ a_message };
			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = kAlertId;
			request.category = "Session";
			request.title = title.c_str();
			request.message = message.c_str();
			request.tone = a_tone;
			request.durationMs = a_durationMs;
			if (!ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
				REX::WARN("SessionExitSaveNotice: failed to show the session save notice");
			}
		}
	}

	void ShowSaving()
	{
		constexpr auto message = "Please do not quit the game. Your session is being saved.";
		ShowNotice("Saving your session", message, "info", 0);
		PushPause("saving", message);
	}

	void ShowRetrying()
	{
		constexpr auto message = "Saving is taking longer than expected. Please do not quit the game.";
		ShowNotice("Saving your session", message, "warning", 0);
		PushPause("retrying", message);
	}

	void ShowSaved()
	{
		constexpr auto message = "Your session was saved.";
		ShowNotice("Session saved", message, "success", 3000);
		PushPause("saved", message);
	}

	void ShowFailed(std::string_view a_message)
	{
		ShowNotice("Your session was not saved", a_message, "error", 10000);
		PushPause("failed", a_message);
	}

	void Dismiss()
	{
		auto* ui = AlertApi();
		if (!ui) {
			return;
		}
		ui->DismissAlert(kAlertId);
	}
}
