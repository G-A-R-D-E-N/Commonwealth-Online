#include "pch.h"

#include "F4TUIClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TNetworking.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionWorldState.h"

#include <format>
#include <string>
#include <string_view>

namespace F4T::UIClient
{
	namespace
	{
		std::string GetLogPrefix()
		{
			return F4T::Networking::GetLocalPlayerLogPrefix() + " [UIClient]";
		}
	}

	void ShowDisconnectScreen(const char* a_code, const char* a_reason)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			return;
		}
		taskInterface->AddTask([code = std::string{ a_code ? a_code : "" }, reason = std::string{ a_reason ? a_reason : "" }]() {
			auto* ui = GetCachedUI();
			if (!ui || !ui->ShowSessionLockout || F4T::SessionWorldState::IsMainMenuOpen()) {
				return;
			}
			if (IsVoluntaryExitRequested() || F4T::SessionSaveGuard::IsSessionExitPending()) {
				return;
			}
			ui->ShowSessionLockout(code.c_str(), reason.c_str());
		});
	}

	void ShowSessionEndedNotification(const char* a_code, const char* a_reason)
	{
		ShowDisconnectScreen(a_code, a_reason);
		auto* ui = GetCachedUI();
		if (!ui || !ui->ShowAlert) {
			REX::WARN(
				"{} Cannot show session-ended notification because the alert API is unavailable.",
				GetLogPrefix());
			return;
		}

		const std::string_view code = a_code ? a_code : "";
		const bool banned = code == "banned";
		const bool serverFull = code == "server_full";
		const std::string title = banned ?
			"You Have Been Banned" :
			serverFull ? "Server Full" : "Disconnected From Server";
		std::string message = banned ?
			"You have been banned and disconnected from the server." :
			serverFull ?
				"The server is full. Try again when a slot opens." :
				"You have been kicked and disconnected from the server.";
		if (a_reason && a_reason[0]) {
			message += std::format("\nReason: {}", a_reason);
		}

		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = banned ? "session-banned" : serverFull ? "session-full" : "session-kicked";
		request.category = "disconnect";
		request.title = title.c_str();
		request.message = message.c_str();
		request.tone = "error";
		request.durationMs = 12000;
		if (!ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
			REX::WARN("{} Failed to show session-ended notification.", GetLogPrefix());
		}
	}
}
