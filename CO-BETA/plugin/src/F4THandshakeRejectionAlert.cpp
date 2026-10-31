#include "pch.h"

#include "F4TUIClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TIdentityProtocol.h"
#include "F4TModpackAutoFixClient.h"
#include "F4TNetworking.h"
#include "F4TServerProfileStore.h"

#include <array>
#include <cstdint>
#include <format>
#include <mutex>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace F4T::UIClient
{
	namespace
	{
		constexpr std::string_view kClientDownloadUrl =
			"https://github.com/G-A-R-D-E-N/CommonwealthOnline-Matrix/releases";
		bool g_retryFromAlertInFlight = false;
		bool g_retryRejectionSurfaced = false;

		struct RejectionAlertContext
		{
			std::uint64_t generation{ 0 };
			std::string serverKey;
			std::string modpackUrl;
		};

		std::mutex g_alertContextMutex;
		std::uint64_t g_alertGeneration = 0;
		std::unordered_map<std::uint64_t, RejectionAlertContext> g_alertContexts;

		struct Presentation
		{
			const char* id{ "session-rejected" };
			std::string title;
			std::string message;
			bool updateClass{ false };
		};

		std::string GetAlertLogPrefix()
		{
			return F4T::Networking::GetLocalPlayerLogPrefix() + " [UIClient]";
		}

		Presentation BuildPresentation(std::string_view a_code)
		{
			Presentation result{};
			if (a_code == "banned") {
				result.id = "session-banned";
				result.title = "You Have Been Banned";
				result.message = "You have been banned and disconnected from the server.";
			} else if (a_code == "profile_banned") {
				result.id = "session-profile-banned";
				result.title = "You Have Been Banned";
				result.message = "Your online profile has been banned by this server's administrators. The ban remains until an administrator unbans the profile.";
			} else if (a_code == "server_full") {
				result.id = "session-full";
				result.title = "Server Full";
				result.message = "The server is full. Try again when a slot opens.";
			} else if (a_code == "password_locked") {
				result.id = "session-password-locked";
				result.title = "Too Many Wrong Passwords";
				result.message = "Too many wrong join passwords came from your Steam account. Wait a few minutes, then try again with the right password.";
			} else if (a_code == "steam_friends_only") {
				result.id = "session-friends-only";
				result.title = "Friends Only";
				result.message = "This host only lets their Steam friends join.";
			} else if (a_code == "kicked") {
				result.id = "session-kicked";
				result.title = "Disconnected From Server";
				result.message = "You have been kicked and disconnected from the server.";
			} else if (a_code == "identity_required") {
				result.id = "session-identity";
				result.title = "Client Identity Required";
				result.message = "This server requires a verified Commonwealth Online client identity that this client did not provide. Update or reinstall the mod to join.";
				result.updateClass = true;
			} else if (a_code == "content_mismatch") {
				result.id = "session-content";
				result.title = "Content Mismatch";
				result.message = "Your installed content does not match this server. Check for updates or reinstall Commonwealth Online.";
				result.updateClass = true;
			} else if (a_code == "modpack_mismatch") {
				result.id = "session-modpack";
				result.title = "Modpack Mismatch";
				result.message = "Your installed modpack does not match this server.";
			} else if (a_code == "capability_mismatch") {
				result.id = "session-capability";
				result.title = "Client Outdated";
				result.message = "Your client version is missing features this server requires. Update Commonwealth Online to the latest version.";
				result.updateClass = true;
			} else if (a_code == "client_version_mismatch") {
				result.id = "session-client-version";
				result.title = "Client Version Mismatch";
				result.message = "This server requires a different Commonwealth Online version than yours. Update to the version the server requires and try again.";
				result.updateClass = true;
			} else if (a_code == "runtime_mismatch") {
				result.id = "session-runtime";
				result.title = "Fallout 4 Version Mismatch";
				result.message = "This server requires a different Fallout 4 version than yours. Use the Fallout 4 version the server requires and try again.";
			} else if (a_code == "resume_expired") {
				result.id = "session-resume-expired";
				result.title = "Session Expired";
				result.message = "Your saved connection expired and requires an administrator reset before you can reconnect.";
			} else if (a_code == "resume_required") {
				result.id = "session-resume-required";
				result.title = "Resume Required";
				result.message = "This server requires resuming your previous session, but none was found. Contact the server administrator.";
			} else if (a_code == "invalid_resume") {
				result.id = "session-resume-invalid";
				result.title = "Session Unavailable";
				result.message = "Your previous session could not be resumed; it may have been superseded by another connection.";
			} else if (a_code == "session_superseded") {
				result.id = "session-superseded";
				result.title = "Session Superseded";
				result.message = "Your session was replaced by a newer connection.";
			} else if (a_code == "rate_limited") {
				result.id = "session-rate";
				result.title = "Too Many Attempts";
				result.message = "You connected too many times in a short period. Wait a moment and try again.";
			} else {
				result.title = "Connection Rejected";
				result.message = std::format(
					"The server rejected the connection (code: {}).",
					a_code.empty() ? std::string_view{ "<none>" } : a_code);
			}
			return result;
		}

		void ShowRetryFailed(std::string_view a_error)
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert) {
				REX::WARN("{} Cannot show retry failure because the alert API is unavailable.", GetAlertLogPrefix());
				return;
			}

			std::string message = "The connection retry could not complete.";
			if (!a_error.empty()) {
				message += std::format("\nReason: {}", a_error);
			}

			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			request.id = "session-retry-failed";
			request.category = "disconnect";
			request.title = "Retry Failed";
			request.message = message.c_str();
			request.tone = "error";
			request.durationMs = 12000;
			if (!ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
				REX::WARN("{} Failed to show retry failure alert.", GetAlertLogPrefix());
			}
		}

		void OnRetryComplete(bool a_success, const char* a_error, void*)
		{
			const bool rejectionSurfaced = g_retryRejectionSurfaced;
			g_retryRejectionSurfaced = false;
			g_retryFromAlertInFlight = false;
			if (!a_success && !rejectionSurfaced) {
				ShowRetryFailed(a_error ? a_error : "");
			}
		}

		void RetryLastJoin()
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI2>();
			if (!ui || !ui->RetryLastJoin) {
				ShowRetryFailed("Retry is unavailable with the installed CommonwealthOnlineUI.dll.");
				return;
			}

			g_retryRejectionSurfaced = false;
			g_retryFromAlertInFlight = true;
			char errorBuf[512]{};
			if (!ui->RetryLastJoin(&OnRetryComplete, nullptr, errorBuf, sizeof(errorBuf))) {
				g_retryFromAlertInFlight = false;
				ShowRetryFailed(errorBuf[0] ? errorBuf : "Retry failed.");
			}
		}

		void OpenUrl(const std::wstring& a_url)
		{
			const auto result = reinterpret_cast<std::intptr_t>(
				ShellExecuteW(nullptr, L"open", a_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
			if (result <= 32) {
				REX::WARN("{} Failed to open the link (result={}).", GetAlertLogPrefix(), result);
			}
		}

		void OpenClientDownload()
		{
			OpenUrl(std::wstring{ kClientDownloadUrl.begin(), kClientDownloadUrl.end() });
		}

		void OpenModpackPage(const std::string& a_url)
		{
			if (a_url.empty()) {
				return;
			}
			OpenUrl(std::wstring{ a_url.begin(), a_url.end() });
		}

		void OnAlertResponse(const char*, const char* a_actionId, void* a_userData)
		{
			std::string boundServerKey;
			std::string modpackUrl;
			if (a_userData) {
				const auto generation = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(a_userData));
				std::scoped_lock lock(g_alertContextMutex);
				const auto it = g_alertContexts.find(generation);
				if (it != g_alertContexts.end()) {
					boundServerKey = std::move(it->second.serverKey);
					modpackUrl = std::move(it->second.modpackUrl);
					g_alertContexts.erase(it);
				}
			}
			if (!a_actionId || !a_actionId[0]) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				REX::WARN("{} Cannot handle alert response: task interface unavailable.", GetAlertLogPrefix());
				return;
			}
			const std::string action{ a_actionId };
			taskInterface->AddTask([action, boundServerKey = std::move(boundServerKey), modpackUrl = std::move(modpackUrl)]() {
				if (action == "clear_resume") {
					ClearRejectedResumeCredentialAndRetry(boundServerKey.c_str());
				} else if (action == "retry") {
					RetryLastJoin();
				} else if (action == "download") {
					OpenClientDownload();
				} else if (action == "modpack") {
					OpenModpackPage(modpackUrl);
				} else if (action == F4T::ModpackAutoFixClient::kOfferActionId) {
					F4T::ModpackAutoFixClient::Begin(boundServerKey);
				}
			});
		}

		bool ShowHandshakeRejection(std::string code, std::string reason, std::string serverKey = {}, std::string modpackUrl = {})
		{
			auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
			if (!ui || !ui->ShowAlert) {
				REX::WARN("{} Cannot show handshake rejection because the alert API is unavailable.", GetAlertLogPrefix());
				return false;
			}

			auto presentation = BuildPresentation(code);
			if (!reason.empty()) {
				presentation.message += std::format("\nReason: {}", reason);
			}

			std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 4> actions{};
			std::size_t actionCount = 0;
			bool hasClearResume = false;
			const bool hasModpackUrl = !modpackUrl.empty();
			const bool hasAutoFix = code == "modpack_mismatch" && F4T::ModpackAutoFixClient::CanOffer(serverKey);
			if (hasAutoFix) {
				presentation.message += "\nDownload Mods asks this server for the missing files.";
			}
			if (hasModpackUrl) {
				const auto host = F4T::Identity::ModpackUrlHost(modpackUrl);
				if (!host.empty()) {
					presentation.message += std::format("\nGet Modpack opens {}", host);
				}
			}
			auto* uiV2 = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI2>();
			if (uiV2 && uiV2->HasLastJoin && uiV2->RetryLastJoin && uiV2->HasLastJoin()) {
				if (code == "invalid_resume") {
					actions[actionCount++] = { "clear_resume", "Reset & Retry", nullptr };
					hasClearResume = true;
				} else {
					actions[actionCount++] = { "retry", "Retry", nullptr };
				}
			}
			if (presentation.updateClass && !kClientDownloadUrl.empty()) {
				actions[actionCount++] = { "download", "Get Update", nullptr };
			}
			if (hasModpackUrl && actionCount < actions.size()) {
				actions[actionCount++] = { "modpack", "Get Modpack", nullptr };
			}
			if (hasAutoFix && actionCount < actions.size()) {
				actions[actionCount++] = { F4T::ModpackAutoFixClient::kOfferActionId.data(), "Download Mods", nullptr };
			}

			std::uint64_t generation = 0;
			if (hasClearResume || hasModpackUrl || hasAutoFix) {
				std::scoped_lock lock(g_alertContextMutex);
				generation = ++g_alertGeneration;
				g_alertContexts[generation] = RejectionAlertContext{ generation, std::move(serverKey), std::move(modpackUrl) };
			}

			COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
			std::string alertId;
			if (generation > 0) {
				alertId = std::format("{}-{}", presentation.id, generation);
				request.id = alertId.c_str();
			} else {
				request.id = presentation.id;
			}
			request.category = "disconnect";
			request.title = presentation.title.c_str();
			request.message = presentation.message.c_str();
			request.tone = "error";
			request.durationMs = 12000;
			request.actions = actions.data();
			request.actionCount = actionCount;
			void* userData = (generation > 0)
				? reinterpret_cast<void*>(static_cast<std::uintptr_t>(generation))
				: nullptr;
			const bool shown = ui->ShowAlert(
				&request,
				actionCount > 0 ? &OnAlertResponse : nullptr,
				userData,
				nullptr,
				0);
			if (!shown) {
				if (generation > 0) {
					std::scoped_lock lock(g_alertContextMutex);
					g_alertContexts.erase(generation);
				}
				REX::WARN("{} Failed to show handshake rejection alert.", GetAlertLogPrefix());
			}
			return shown;
		}

	}

	void ClearRejectedResumeCredentialAndRetry(const char* a_serverKey)
	{
		if (!a_serverKey || !a_serverKey[0] ||
			!F4T::ServerProfileStore::ClearResumeCredential(a_serverKey)) {
			ShowRetryFailed("Failed to clear rejected resume credentials.");
			return;
		}
		RetryLastJoin();
	}

	void ShowHandshakeRejectionNotification(const char* a_code, const char* a_reason, const char* a_serverKey, const char* a_modpackUrl)
	{
		const bool shown = ShowHandshakeRejection(
			a_code ? a_code : "",
			a_reason ? a_reason : "",
			a_serverKey ? a_serverKey : "",
			a_modpackUrl ? a_modpackUrl : "");
		if (shown && g_retryFromAlertInFlight) {
			g_retryRejectionSurfaced = true;
		}
	}
}
