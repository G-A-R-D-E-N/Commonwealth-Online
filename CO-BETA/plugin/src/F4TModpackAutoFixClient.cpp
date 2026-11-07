#include "pch.h"

#include "F4TModpackAutoFixClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TContentManifest.h"
#include "F4TGnsModpackDownload.h"
#include "F4TModpackAutoFix.h"
#include "F4TModpackInstall.h"
#include "F4TModpackNeed.h"
#include "F4TNexusSources.h"
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
#include <shellapi.h>

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
		std::vector<NexusSources::Source> g_nexusSources;
		std::optional<NexusSources::Collection> g_nexusCollection;
		std::size_t g_nexusIndex{};
		bool g_nexusUnmatched{};
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

		void ShowNexusSource()
		{
			NexusSources::Source source;
			std::size_t index{};
			std::size_t count{};
			bool serverFiles{};
			bool unmatched{};
			bool collection{};
			{
				const std::scoped_lock lock(g_mutex);
				index = g_nexusIndex;
				count = g_nexusSources.size();
				if (index < count) source = g_nexusSources[index];
				serverFiles = g_consent && ModpackDownload::ConsentPlanCanInstall(*g_consent);
				unmatched = g_nexusUnmatched;
				collection = g_nexusCollection.has_value();
			}
			std::vector<COMMONWEALTH_ONLINE_UI_API::AlertAction> actions;
			if (index < count) actions.push_back({ "modpack_nexus", "Open Nexus", nullptr });
			if (collection) actions.push_back({ "modpack_collection", "Open Vortex Collection", nullptr });
			if (serverFiles) actions.push_back({ "modpack_review", "Review Server Files", nullptr });
			actions.push_back({ "modpack_cancel", "Close", nullptr });
			auto message = index < count ?
				"File " + std::to_string(index + 1) + " of " + std::to_string(count) + ": " + source.name +
				"\nOpen Nexus, sign in, expand the linked file, and choose Mod Manager Download to send it to MO2 or Vortex." :
				std::string{ collection ? "Open the collection and install and deploy it with Vortex. " : "Install and enable the files in MO2 or Vortex. " } +
				"Restart Fallout 4 through your mod manager and rejoin. The server will check the files again." +
				(unmatched ? " Some required files may need to be installed separately; check the mismatch details after rejoining." : "");
			if (collection) message += " To install the collection, set Vortex to handle Nexus links.";
			ShowAlert(kConsentAlertId, "Get Server Mods", message, "info", std::move(actions));
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
			std::optional<ModpackDownload::Offer> offer{ ModpackDownload::Offer{ request->fingerprint, {} } };
			std::size_t fileOffset = 0;
			std::size_t nexusOffset = 0;
			bool firstBatch = true;
			while (firstBatch || fileOffset < request->names.size() || nexusOffset < request->nexusNames.size()) {
				ModpackDownload::OfferRequest batch{ request->fingerprint, {} };
				batch.wantCollection = firstBatch;
				firstBatch = false;
				const auto limit = ModpackManifest::kMaximumAutoInstallFiles;
				const auto fileEnd = (std::min)(request->names.size(), fileOffset + limit);
				const auto nexusEnd = (std::min)(request->nexusNames.size(), nexusOffset + limit);
				batch.names.assign(request->names.begin() + fileOffset, request->names.begin() + fileEnd);
				batch.nexusNames.assign(request->nexusNames.begin() + nexusOffset, request->nexusNames.begin() + nexusEnd);
				const auto part = transport.FetchOffer(batch);
				if (!part) { offer.reset(); break; }
				if (part->nexusCollection) {
					if (offer->nexusCollection && (offer->nexusCollection->slug != part->nexusCollection->slug || offer->nexusCollection->revision != part->nexusCollection->revision)) { offer.reset(); break; }
					offer->nexusCollection = part->nexusCollection;
				}
				offer->files.insert(offer->files.end(), part->files.begin(), part->files.end());
				if (!part->nexusSources.is_null()) {
					if (offer->nexusSources.is_null()) offer->nexusSources = { { "version", 1 }, { "files", nlohmann::json::array() } };
					for (const auto& source : part->nexusSources.at("files")) offer->nexusSources["files"].push_back(source);
				}
				fileOffset = fileEnd;
				nexusOffset = nexusEnd;
			}
			Wipe(pending.serverPassword);
			if (!offer) {
				Fail(request->names.empty() && request->nexusNames.empty() ?
					"The server could not provide a collection. Use the mismatch details to adjust your mod list." :
					"The server cannot send its mods. " + transport.LastRefusal());
				return;
			}
			if (offer->nexusCollection && std::any_of(pending.required.entries.begin(), pending.required.entries.end(), [](const auto& entry) {
					return !ContentManifest::IsValidFingerprint(entry.sha256Hex);
				})) {
				Fail("The server's collection cannot be verified because its required manifest is missing file hashes.");
				return;
			}
			auto consent = ModpackDownload::BuildConsentPlan(pending.fingerprint, *pending.verdict, *offer);
			bool insufficientSpace = false;
			if (consent && ModpackDownload::ConsentPlanCanInstall(*consent)) {
				auto fileSystem = ModpackInstall::MakeNativeFileSystem();
				if (!ModpackAutoFix::HasRoomFor(fileSystem->FreeBytes(GameDirectory()), consent->totalBytes)) {
					consent.reset();
					insufficientSpace = true;
				}
			}
			std::vector<NexusSources::Source> nexus;
			std::vector<std::string> matched;
			if (!offer->nexusSources.is_null()) {
				const auto parsed = NexusSources::Parse(offer->nexusSources.dump(), pending.required);
				if (!parsed) { Fail("The server sent invalid Nexus source information."); return; }
				for (const auto& source : *parsed) {
					const bool requested = std::find(request->nexusNames.begin(), request->nexusNames.end(), source.name) != request->nexusNames.end();
					const bool serverOffered = consent && ModpackDownload::ConsentPlanCanInstall(*consent) &&
						std::any_of(consent->files.begin(), consent->files.end(), [&](const auto& file) { return file.name == source.name; });
					const bool duplicate = std::any_of(nexus.begin(), nexus.end(), [&](const auto& file) { return file.modId == source.modId && file.fileId == source.fileId; });
					if (requested) matched.push_back(source.name);
					if (requested && !serverOffered && !duplicate) nexus.push_back(source);
				}
			}
			if (!nexus.empty() || offer->nexusCollection) {
				if (insufficientSpace) ShowAlert(kResultAlertId, "Server Mods", "There is not enough free disk space for the server-provided files.", "warning", {}, 15000);
				{
					const std::scoped_lock lock(g_mutex);
					g_consent = std::move(consent);
					g_nexusSources = std::move(nexus);
					g_nexusCollection = offer->nexusCollection;
					g_nexusIndex = 0;
					g_nexusUnmatched = std::any_of(request->nexusNames.begin(), request->nexusNames.end(), [&](const auto& name) {
						return std::find(matched.begin(), matched.end(), name) == matched.end() &&
							(!g_consent || !ModpackDownload::ConsentPlanCanInstall(*g_consent) ||
								std::none_of(g_consent->files.begin(), g_consent->files.end(), [&](const auto& file) { return file.name == name; }));
					});
				}
				Dismiss(kProgressAlertId);
				ShowNexusSource();
				g_busy.store(false);
				return;
			}
			if (!consent || !ModpackDownload::ConsentPlanCanInstall(*consent)) {
				Fail(request->names.empty() && request->nexusNames.empty() ? "The server does not offer a collection. Use the mismatch details to adjust your mod list." :
					insufficientSpace ? "There is not enough free disk space for the server-provided files." :
					consent && consent->issue == ModpackDownload::ConsentIssue::totalTooLarge ?
						"The server's files are larger than the download limit. Install them by hand." :
						"The server does not offer the files you are missing. Install them by hand.");
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
			if (action == "modpack_nexus") {
				NexusSources::Source source;
				{
					const std::scoped_lock lock(g_mutex);
					if (g_nexusIndex >= g_nexusSources.size()) return;
					source = g_nexusSources[g_nexusIndex++];
				}
				const auto url = NexusSources::FilePage(source);
				const std::wstring wide{ url.begin(), url.end() };
				if (reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
					{ const std::scoped_lock lock(g_mutex); if (g_nexusIndex > 0) --g_nexusIndex; }
					ShowAlert(kResultAlertId, "Nexus", "Could not open the Nexus page. Check your browser settings.", "error", {}, 15000);
				}
				ShowNexusSource();
				return;
			}
			if (action == "modpack_collection") {
				std::optional<NexusSources::Collection> collection;
				{ const std::scoped_lock lock(g_mutex); collection = g_nexusCollection; }
				if (!collection) return;
				const auto url = NexusSources::CollectionPage(*collection);
				const std::wstring wide{ url.begin(), url.end() };
				if (url.empty() || reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
					ShowAlert(kResultAlertId, "Nexus", "Could not open the collection page. Check your browser settings.", "error", {}, 15000);
				ShowNexusSource();
				return;
			}
			if (action == "modpack_review") {
				std::optional<ModpackDownload::ConsentPlan> consent;
				Pending pending;
				{
					const std::scoped_lock lock(g_mutex);
					consent = g_consent;
					pending = g_pending;
				}
				Wipe(pending.serverPassword);
				if (consent && ModpackDownload::ConsentPlanCanInstall(*consent)) {
					ShowAlert(kConsentAlertId, "Download Server Mods?", ModpackAutoFix::DescribeConsent(*consent, ServerLabel(pending)), "warning", { { "modpack_download", "Download", nullptr }, { "modpack_cancel", "Cancel", nullptr } });
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
		g_nexusSources.clear();
		g_nexusCollection.reset();
		g_nexusIndex = 0;
		g_nexusUnmatched = false;
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
			ModpackAutoFix::MakeOfferRequest(g_pending.fingerprint, *g_pending.verdict).has_value();
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
