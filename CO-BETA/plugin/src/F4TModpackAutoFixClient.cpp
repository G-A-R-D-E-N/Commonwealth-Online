#include "pch.h"

#include "F4TModpackAutoFixClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TGnsModpackDownload.h"
#include "F4TModpackAutoFix.h"
#include "F4TModpackInstall.h"
#include "F4TModpackNeed.h"
#include "F4TServerProfileStore.h"
#include "F4TUIClient.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace F4T::ModpackAutoFixClient
{
	namespace
	{
		constexpr const char* kConsentAlertId = "co-modpack-consent";
		constexpr const char* kProgressAlertId = "co-modpack-progress";
		constexpr const char* kResultAlertId = "co-modpack-result";
		constexpr auto kProgressInterval = std::chrono::milliseconds{ 1500 };

		struct Pending
		{
			std::string serverKey;
			std::string fingerprint;
			ModpackManifest::Manifest required;
			std::optional<ModpackManifest::NeedVerdict> verdict;
			std::string host;
			std::uint16_t port{};
			std::string joinId;
			std::string serverName;
			std::optional<std::string> serverPassword;
			bool hasEndpoint{};
		};

		std::mutex g_mutex;
		Pending g_pending;
		std::optional<ModpackDownload::ConsentPlan> g_consent;
		std::optional<ModpackAutoFix::StageResult> g_staged;
		std::optional<ModpackInstall::Result> g_startupResult;
		std::atomic_bool g_busy{ false };
		std::atomic_bool g_cancel{ false };

		void Wipe(std::optional<std::string>& a_password)
		{
			if (a_password) {
				std::fill(a_password->begin(), a_password->end(), '\0');
				a_password.reset();
			}
		}

		std::filesystem::path GameDirectory()
		{
			std::wstring buffer(MAX_PATH, L'\0');
			const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			buffer.resize(length);
			return std::filesystem::path{ buffer }.parent_path();
		}

		std::filesystem::path PluginsTxtPath()
		{
			wchar_t* raw = nullptr;
			std::size_t length = 0;
			std::filesystem::path base;
			if (_wdupenv_s(&raw, &length, L"LOCALAPPDATA") == 0 && raw != nullptr) {
				base = raw;
			}
			std::free(raw);
			return base / "Fallout4" / "plugins.txt";
		}

		ModpackInstall::Paths InstallPaths()
		{
			return ModpackInstall::PathsForGame(GameDirectory(), PluginsTxtPath());
		}

		bool IsElevated()
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
				return false;
			}
			TOKEN_ELEVATION elevation{};
			DWORD size = 0;
			const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated != 0;
			CloseHandle(token);
			return elevated;
		}

		ModpackInstall::ModManager DetectManager(ModpackInstall::FileSystem& a_fileSystem, const ModpackInstall::Paths& a_paths)
		{
			const bool virtualFileSystem = GetModuleHandleW(L"usvfs_x64.dll") != nullptr;
			return ModpackInstall::DetectModManager(virtualFileSystem, a_fileSystem.List(a_paths.dataDirectory));
		}

		std::string ServerLabel(const Pending& a_pending)
		{
			const auto endpoint = a_pending.host + ":" + std::to_string(a_pending.port);
			return a_pending.serverName.empty() ? endpoint : a_pending.serverName + " (" + endpoint + ")";
		}

		void OnAlertAction(const char*, const char* a_actionId, void*);

		void ShowAlert(const char* a_id, std::string a_title, std::string a_message, const char* a_tone, std::vector<COMMONWEALTH_ONLINE_UI_API::AlertAction> a_actions, std::uint32_t a_durationMs = 0)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			auto show = [a_id, title = std::move(a_title), message = std::move(a_message), a_tone, actions = std::move(a_actions), a_durationMs]() {
				auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
				if (!ui || !ui->ShowAlert) {
					REX::WARN("Modpack auto-fix: alert API unavailable: {}", title);
					return;
				}
				COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
				request.id = a_id;
				request.category = "modpack";
				request.title = title.c_str();
				request.message = message.c_str();
				request.tone = a_tone;
				request.durationMs = a_durationMs;
				request.actions = actions.empty() ? nullptr : actions.data();
				request.actionCount = actions.size();
				if (!ui->ShowAlert(&request, actions.empty() ? nullptr : &OnAlertAction, nullptr, nullptr, 0)) {
					REX::WARN("Modpack auto-fix: could not show alert: {}", title);
				}
			};
			if (taskInterface) {
				taskInterface->AddTask(std::move(show));
			} else {
				show();
			}
		}

		void Dismiss(const char* a_id)
		{
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([a_id]() {
					auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
					if (ui && ui->DismissAlert) {
						ui->DismissAlert(a_id);
					}
				});
			}
		}

		void Fail(std::string a_message)
		{
			REX::WARN("Modpack auto-fix: {}", a_message);
			Dismiss(kProgressAlertId);
			ShowAlert(kResultAlertId, "Modpack Download", std::move(a_message), "error", {}, 15000);
			g_busy.store(false);
		}

		void FetchOfferWorker()
		{
			Pending pending;
			{
				const std::scoped_lock lock(g_mutex);
				pending = g_pending;
			}
			const auto request = pending.verdict ? ModpackAutoFix::MakeOfferRequest(pending.fingerprint, *pending.verdict) : std::nullopt;
			if (!request) {
				Fail("Nothing here can be downloaded automatically.");
				return;
			}
			ShowAlert(kProgressAlertId, "Modpack Download", "Asking " + ServerLabel(pending) + " which files it can send...", "info", {});
			GnsModpackDownload::GnsTransport transport(pending.host, pending.port, pending.serverPassword, &g_cancel);
			const auto offer = transport.FetchOffer(*request);
			Wipe(pending.serverPassword);
			if (!offer) {
				Fail("The server cannot send its mods. " + transport.LastRefusal());
				return;
			}
			auto consent = ModpackDownload::BuildConsentPlan(pending.fingerprint, *pending.verdict, *offer);
			if (!consent || !ModpackDownload::ConsentPlanCanInstall(*consent)) {
				Fail(consent && consent->issue == ModpackDownload::ConsentIssue::totalTooLarge ?
						"The server's files are larger than the download limit. Install them by hand." :
						"The server does not offer the files you are missing. Install them by hand.");
				return;
			}
			auto fileSystem = ModpackInstall::MakeNativeFileSystem();
			if (!ModpackAutoFix::HasRoomFor(fileSystem->FreeBytes(GameDirectory()), consent->totalBytes)) {
				Fail("There is not enough free disk space for " + ModpackAutoFix::FormatBytes(consent->totalBytes) + " of mods.");
				return;
			}
			const auto message = ModpackAutoFix::DescribeConsent(*consent, ServerLabel(pending));
			{
				const std::scoped_lock lock(g_mutex);
				g_consent = std::move(consent);
			}
			Dismiss(kProgressAlertId);
			ShowAlert(kConsentAlertId, "Download Server Mods?", message, "warning", { { "modpack_download", "Download", nullptr }, { "modpack_cancel", "Cancel", nullptr } });
			g_busy.store(false);
		}

		void DownloadWorker()
		{
			Pending pending;
			std::optional<ModpackDownload::ConsentPlan> consent;
			{
				const std::scoped_lock lock(g_mutex);
				pending = g_pending;
				consent = g_consent;
			}
			if (!consent) {
				Fail("The download offer expired. Join the server again.");
				return;
			}
			const auto paths = InstallPaths();
			const auto staging = paths.StagingFor(consent->fingerprint);
			ModpackAutoFix::RemoveOtherStaging(paths.stagingRoot, consent->fingerprint);
			GnsModpackDownload::GnsTransport transport(pending.host, pending.port, pending.serverPassword, &g_cancel);
			Wipe(pending.serverPassword);
			auto lastShown = std::chrono::steady_clock::now() - kProgressInterval;
			const auto staged = ModpackAutoFix::DownloadAndStage(*consent, staging, transport, [&](const ModpackAutoFix::Progress& a_progress) {
				const auto now = std::chrono::steady_clock::now();
				if (now - lastShown < kProgressInterval) {
					return;
				}
				lastShown = now;
				ShowAlert(kProgressAlertId, "Downloading Server Mods",
					"File " + std::to_string(a_progress.fileIndex) + " of " + std::to_string(a_progress.fileCount) + ": " + a_progress.name +
						"\n" + ModpackAutoFix::FormatBytes(a_progress.doneBytes) + " of " + ModpackAutoFix::FormatBytes(a_progress.totalBytes),
					"info", { { "modpack_cancel", "Cancel", nullptr } });
			}, &g_cancel);
			if (!staged.ok) {
				Fail(staged.cancelled ? "The download was cancelled. Downloaded parts are kept and resume next time." : staged.failure + " " + transport.LastRefusal());
				return;
			}
			auto fileSystem = ModpackInstall::MakeNativeFileSystem();
			const auto manager = DetectManager(*fileSystem, paths);
			{
				const std::scoped_lock lock(g_mutex);
				g_staged = staged;
			}
			Dismiss(kProgressAlertId);
			std::vector<COMMONWEALTH_ONLINE_UI_API::AlertAction> actions;
			if (manager == ModpackInstall::ModManager::none) {
				actions = { { "modpack_install", "Install on Restart", nullptr }, { "modpack_cancel", "Cancel", nullptr } };
			}
			ShowAlert(kConsentAlertId, "Server Mods Downloaded", ModpackAutoFix::DescribeReady(*consent, staged, manager, staging), "warning", std::move(actions));
			g_busy.store(false);
		}

		void ScheduleInstall()
		{
			Pending pending;
			std::optional<ModpackDownload::ConsentPlan> consent;
			{
				const std::scoped_lock lock(g_mutex);
				pending = g_pending;
				consent = g_consent;
			}
			Wipe(pending.serverPassword);
			if (!consent) {
				Fail("The download offer expired. Join the server again.");
				return;
			}
			auto fileSystem = ModpackInstall::MakeNativeFileSystem();
			ModpackInstall::Installer installer(*fileSystem, InstallPaths());
			const auto plan = ModpackAutoFix::MakeInstallPlan(*consent, pending.required, pending.serverKey, ServerLabel(pending), { pending.host, pending.port, pending.joinId });
			if (!installer.WritePendingPlan(plan)) {
				Fail("The install could not be scheduled.");
				return;
			}
			ShowAlert(kResultAlertId, "Restart Fallout 4", "The server's mods are installed the next time Fallout 4 starts. Quit the game and start it again, then rejoin.", "success", {}, 0);
			g_busy.store(false);
		}

		void Rejoin(ModpackInstall::ServerEndpoint a_endpoint, std::string a_label)
		{
			if (!F4T::UIClient::RejoinServer(a_endpoint.host.c_str(), a_endpoint.port, a_label.c_str(), a_endpoint.joinId.c_str())) {
				ShowAlert(kResultAlertId, "Rejoin", "Join " + a_label + " from the server browser.", "info", {}, 12000);
			}
		}

		void OnAlertAction(const char*, const char* a_actionId, void*)
		{
			if (!a_actionId) {
				return;
			}
			const std::string action{ a_actionId };
			if (action == "modpack_cancel") {
				g_cancel.store(true);
				const std::scoped_lock lock(g_mutex);
				g_consent.reset();
				g_staged.reset();
				Wipe(g_pending.serverPassword);
				return;
			}
			if (action == "modpack_rejoin") {
				std::optional<ModpackInstall::Result> result;
				{
					const std::scoped_lock lock(g_mutex);
					result = g_startupResult;
				}
				if (result) {
					Rejoin(result->endpoint, result->serverLabel);
				}
				return;
			}
			if (g_busy.exchange(true)) {
				return;
			}
			g_cancel.store(false);
			if (action == "modpack_download") {
				std::thread(DownloadWorker).detach();
			} else if (action == "modpack_install") {
				std::thread(ScheduleInstall).detach();
			} else {
				g_busy.store(false);
			}
		}
	}

	void NoteMismatch(
		std::string_view a_serverKey,
		const std::string& a_fingerprint,
		const ModpackManifest::Manifest& a_required,
		const ModpackManifest::Manifest& a_client)
	{
		auto verdict = ModpackManifest::BuildNeedVerdict(a_required, a_client);
		const std::scoped_lock lock(g_mutex);
		if (g_pending.serverKey != a_serverKey) {
			Wipe(g_pending.serverPassword);
			g_pending = Pending{};
		}
		g_pending.serverKey = std::string{ a_serverKey };
		g_pending.fingerprint = a_fingerprint;
		g_pending.required = a_required;
		g_pending.verdict = std::move(verdict);
		g_consent.reset();
		g_staged.reset();
	}

	void NoteEndpoint(
		const std::string& a_serverKey,
		const std::string& a_host,
		std::uint16_t a_port,
		const std::string& a_joinId,
		const std::string& a_serverName,
		const std::optional<std::string>& a_serverPassword)
	{
		const std::scoped_lock lock(g_mutex);
		if (g_pending.serverKey != a_serverKey) {
			return;
		}
		g_pending.host = a_host;
		g_pending.port = a_port;
		g_pending.joinId = a_joinId;
		g_pending.serverName = a_serverName;
		Wipe(g_pending.serverPassword);
		g_pending.serverPassword = a_serverPassword;
		g_pending.hasEndpoint = !a_host.empty() && a_port != 0;
	}

	bool CanOffer(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_mutex);
		return !a_serverKey.empty() && g_pending.serverKey == a_serverKey && g_pending.hasEndpoint && g_pending.verdict &&
			ModpackManifest::NeedVerdictCanDownload(*g_pending.verdict);
	}

	void Begin(const std::string& a_serverKey)
	{
		if (!CanOffer(a_serverKey) || g_busy.exchange(true)) {
			return;
		}
		g_cancel.store(false);
		std::thread(FetchOfferWorker).detach();
	}

	void OnPluginLoad()
	{
		try {
			auto fileSystem = ModpackInstall::MakeNativeFileSystem();
			const auto paths = InstallPaths();
			ModpackInstall::Installer installer(*fileSystem, paths);
			auto recovered = installer.Recover();
			if (recovered.outcome != ModpackInstall::Outcome::nothingToDo) {
				REX::INFO("Modpack auto-fix: {}", recovered.detail);
				g_startupResult = std::move(recovered);
				return;
			}
			if (!installer.HasPendingPlan()) {
				return;
			}
			if (IsElevated()) {
				g_startupResult = ModpackInstall::Result{ ModpackInstall::Outcome::refused, "Fallout 4 is running as administrator, so the server's mods were not installed. Start the game normally." };
				return;
			}
			if (const auto manager = DetectManager(*fileSystem, paths); manager != ModpackInstall::ModManager::none) {
				g_startupResult = ModpackInstall::Result{ ModpackInstall::Outcome::refused, std::string{ ModpackInstall::DescribeModManager(manager) } + " manages your Data folder. Install the downloaded files with it; they are in " + paths.stagingRoot.string() + "." };
				return;
			}
			const auto unique = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			auto applied = installer.ApplyPending(unique);
			REX::INFO("Modpack auto-fix: install outcome {} {}", static_cast<int>(applied.outcome), applied.detail);
			g_startupResult = std::move(applied);
		} catch (...) {
			g_startupResult = ModpackInstall::Result{ ModpackInstall::Outcome::failed, "The modpack install check failed." };
		}
	}

	void OnGameDataReady()
	{
		std::optional<ModpackInstall::Result> result;
		try {
			auto fileSystem = ModpackInstall::MakeNativeFileSystem();
			ModpackInstall::Installer installer(*fileSystem, InstallPaths());
			if (installer.IsVerifying()) {
				const auto confirmed = installer.ConfirmVerified();
				if (confirmed.outcome == ModpackInstall::Outcome::failed) {
					REX::WARN("Modpack auto-fix: {}", confirmed.detail);
				}
			}
		} catch (...) {
		}
		{
			const std::scoped_lock lock(g_mutex);
			result = g_startupResult;
		}
		if (!result || result->outcome == ModpackInstall::Outcome::nothingToDo) {
			return;
		}
		const bool applied = result->outcome == ModpackInstall::Outcome::applied;
		std::vector<COMMONWEALTH_ONLINE_UI_API::AlertAction> actions;
		if (applied && !result->endpoint.host.empty() && result->endpoint.port != 0 &&
			F4T::ServerProfileStore::LookupProfileId(result->serverKey).has_value()) {
			actions.push_back({ "modpack_rejoin", "Rejoin", nullptr });
		}
		ShowAlert(kResultAlertId, applied ? "Server Mods Installed" : "Server Mods", ModpackAutoFix::DescribeResult(*result), applied ? "success" : "warning", std::move(actions), 0);
	}
}
