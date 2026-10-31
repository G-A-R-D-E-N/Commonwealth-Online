#include "F4TModpackAutoFix.h"

#include "F4TBa2Inspect.h"
#include "F4TContentManifest.h"

#include <algorithm>
#include <cstdio>
#include <system_error>
#include <utility>

namespace F4T::ModpackAutoFix
{
	namespace
	{
		constexpr std::size_t kListedNames = 6;

		[[nodiscard]] bool Cancelled(const std::atomic_bool* a_cancel) noexcept
		{
			return a_cancel != nullptr && a_cancel->load(std::memory_order_acquire);
		}

		[[nodiscard]] bool IsArchive(std::string_view a_name)
		{
			return a_name.size() > 4 && ModpackManifest::EqualsIgnoringAsciiCase(a_name.substr(a_name.size() - 4), ".ba2");
		}

		[[nodiscard]] std::string JoinNames(const std::vector<std::string>& a_names)
		{
			std::string text;
			for (std::size_t index = 0; index < a_names.size() && index < kListedNames; ++index) {
				if (!text.empty()) {
					text += ", ";
				}
				text += a_names[index];
			}
			if (a_names.size() > kListedNames) {
				text += " and " + std::to_string(a_names.size() - kListedNames) + " more";
			}
			return text;
		}

		[[nodiscard]] std::string Plural(std::size_t a_count, std::string_view a_word)
		{
			return std::to_string(a_count) + " " + std::string{ a_word } + (a_count == 1 ? "" : "s");
		}
	}

	std::optional<ModpackDownload::OfferRequest> MakeOfferRequest(
		std::string_view a_fingerprint,
		const ModpackManifest::NeedVerdict& a_verdict)
	{
		if (!ContentManifest::IsValidFingerprint(a_fingerprint) || !ModpackManifest::NeedVerdictCanDownload(a_verdict) ||
			a_verdict.download.size() > ModpackManifest::kMaximumAutoInstallFiles) {
			return std::nullopt;
		}
		ModpackDownload::OfferRequest request{ std::string{ a_fingerprint }, {} };
		for (const auto& file : a_verdict.download) {
			request.names.push_back(file.name);
		}
		return request;
	}

	bool HasRoomFor(std::optional<std::uint64_t> a_freeBytes, std::uint64_t a_neededBytes) noexcept
	{
		return a_freeBytes && a_neededBytes <= UINT64_MAX - kFreeSpaceMarginBytes && *a_freeBytes >= a_neededBytes + kFreeSpaceMarginBytes;
	}

	StageResult DownloadAndStage(
		const ModpackDownload::ConsentPlan& a_plan,
		const std::filesystem::path& a_stagingDirectory,
		Transport& a_transport,
		const std::function<void(const Progress&)>& a_onProgress,
		const std::atomic_bool* a_cancel)
	{
		StageResult result;
		if (!ModpackDownload::ConsentPlanCanInstall(a_plan)) {
			result.failure = "There is nothing this server can send.";
			return result;
		}
		std::uint64_t completed = 0;
		for (std::size_t index = 0; index < a_plan.files.size(); ++index) {
			const auto& file = a_plan.files[index];
			bool staged = false;
			for (std::size_t attempt = 0; attempt < kAttemptsPerFile && !staged; ++attempt) {
				if (Cancelled(a_cancel)) {
					result.cancelled = true;
					result.failure = "The download was cancelled.";
					return result;
				}
				ModpackDownload::StagedFileWriter writer(a_stagingDirectory, file);
				if (!writer.Open()) {
					result.failure = "Could not write " + file.name + " to " + a_stagingDirectory.string() + ".";
					return result;
				}
				const ModpackDownload::FileRequest request{ a_plan.fingerprint, file.name, file.sha256Hex, writer.Offset() };
				const auto report = [&](std::uint64_t a_fileBytes) {
					if (a_onProgress) {
						a_onProgress(Progress{ index + 1, a_plan.files.size(), file.name, completed + a_fileBytes, a_plan.totalBytes });
					}
				};
				report(writer.Offset());
				const auto end = a_transport.FetchFile(request, [&](const ModpackDownload::FileChunk& a_chunk) {
					if (Cancelled(a_cancel) || a_chunk.name != file.name ||
						writer.Append(a_chunk.offset, a_chunk.bytes) != ModpackDownload::WriteResult::accepted) {
						return false;
					}
					report(writer.Offset());
					return true;
				});
				if (end && writer.Finish(*end) == ModpackDownload::WriteResult::complete) {
					staged = true;
				}
			}
			if (Cancelled(a_cancel)) {
				result.cancelled = true;
				result.failure = "The download was cancelled.";
				return result;
			}
			if (!staged) {
				result.failure = "Could not download " + file.name + " from the server.";
				return result;
			}
			completed += file.sizeBytes;
			if (IsArchive(file.name)) {
				const auto path = a_stagingDirectory / file.name;
				const auto inspected = Ba2Inspect::InspectFile(path);
				if (!inspected || !Ba2Inspect::IsAcceptable(*inspected)) {
					std::error_code error;
					std::filesystem::remove(path, error);
					result.failure = file.name + (inspected && !inspected->refusedPaths.empty() ?
						" contains " + inspected->refusedPaths.front() + ", which is never installed automatically." :
						" is not an archive this game version can load.");
					return result;
				}
				result.scripts += inspected->scripts;
				result.interfaceFiles += inspected->interfaceFiles;
			}
		}
		result.ok = true;
		return result;
	}

	void RemoveOtherStaging(const std::filesystem::path& a_stagingRoot, std::string_view a_keepFingerprint)
	{
		std::error_code error;
		std::vector<std::filesystem::path> stale;
		for (std::filesystem::directory_iterator it(a_stagingRoot, error), end; !error && it != end; it.increment(error)) {
			const auto name = it->path().filename().string();
			if (name != a_keepFingerprint && ContentManifest::IsValidFingerprint(name) && it->is_directory(error) && !it->is_symlink(error)) {
				stale.push_back(it->path());
			}
		}
		for (const auto& path : stale) {
			std::filesystem::remove_all(path, error);
		}
	}

	ModpackInstall::InstallPlan MakeInstallPlan(
		const ModpackDownload::ConsentPlan& a_plan,
		const ModpackManifest::Manifest& a_required,
		std::string a_serverKey,
		std::string a_serverLabel,
		ModpackInstall::ServerEndpoint a_endpoint)
	{
		ModpackInstall::InstallPlan plan;
		plan.fingerprint = a_plan.fingerprint;
		plan.serverKey = std::move(a_serverKey);
		plan.serverLabel = std::move(a_serverLabel);
		plan.endpoint = std::move(a_endpoint);
		for (const auto& file : a_plan.files) {
			plan.files.push_back(ModpackInstall::PlannedFile{ file.name, file.sha256Hex, file.sizeBytes, file.replaces });
		}
		std::vector<const ModpackManifest::Entry*> plugins;
		for (const auto& entry : a_required.entries) {
			if (entry.kind == ModpackManifest::EntryKind::plugin) {
				plugins.push_back(&entry);
			}
		}
		std::stable_sort(plugins.begin(), plugins.end(), [](const auto* a_left, const auto* a_right) {
			return a_left->order < a_right->order;
		});
		for (const auto* plugin : plugins) {
			plan.loadOrder.push_back(plugin->name);
		}
		return plan;
	}

	std::string FormatBytes(std::uint64_t a_bytes)
	{
		constexpr const char* kUnits[]{ "bytes", "KB", "MB", "GB", "TB" };
		double value = static_cast<double>(a_bytes);
		std::size_t unit = 0;
		while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
			value /= 1024.0;
			++unit;
		}
		char buffer[32]{};
		if (unit == 0) {
			std::snprintf(buffer, sizeof(buffer), "%llu bytes", static_cast<unsigned long long>(a_bytes));
		} else {
			std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
		}
		return buffer;
	}

	std::string DescribeConsent(const ModpackDownload::ConsentPlan& a_plan, std::string_view a_serverLabel)
	{
		std::vector<std::string> replaced;
		std::vector<std::string> added;
		for (const auto& file : a_plan.files) {
			(file.replaces ? replaced : added).push_back(file.name);
		}
		std::string text = std::string{ a_serverLabel } + " can send " + Plural(a_plan.files.size(), "file") + " (" + FormatBytes(a_plan.totalBytes) + ").";
		if (!added.empty()) {
			text += "\nNew: " + JoinNames(added);
		}
		if (!replaced.empty()) {
			text += "\nReplaces your copy of: " + JoinNames(replaced);
		}
		text += "\nThese files come from the server's operator, not from Commonwealth Online. Plugins and archives change how your game runs and can contain scripts.";
		text += "\nYour current files and load order are backed up, and the install is undone automatically if the game does not finish loading.";
		const auto manual = a_plan.manual.size() + a_plan.notOffered.size();
		if (manual > 0) {
			text += "\nStill install by hand: " + Plural(manual, "file") + ".";
		}
		return text;
	}

	std::string DescribeReady(
		const ModpackDownload::ConsentPlan& a_plan,
		const StageResult& a_staged,
		ModpackInstall::ModManager a_manager,
		const std::filesystem::path& a_stagingDirectory)
	{
		std::string text = "Downloaded " + Plural(a_plan.files.size(), "file") + " (" + FormatBytes(a_plan.totalBytes) + ").";
		if (a_staged.scripts > 0 || a_staged.interfaceFiles > 0) {
			text += "\nThe archives contain " + Plural(a_staged.scripts, "script") + " and " + Plural(a_staged.interfaceFiles, "interface file") + ".";
		}
		if (a_manager != ModpackInstall::ModManager::none) {
			text += "\n" + std::string{ ModpackInstall::DescribeModManager(a_manager) } + " manages your Data folder, so Commonwealth Online will not install them.";
			text += "\nThe files are in " + a_stagingDirectory.string() + ". Add them with " + std::string{ ModpackInstall::DescribeModManager(a_manager) } + ", enable the plugins in the server's order, then rejoin.";
			return text;
		}
		text += "\nThey are installed the next time Fallout 4 starts. Your plugins.txt is changed to match the server's load order, and plugins the server does not use are switched off.";
		text += "\nTo undo it later, create an empty file named restore-last-install in CommonwealthOnline\\ModpackInstall in the game folder and start the game.";
		return text;
	}

	std::string DescribeResult(const ModpackInstall::Result& a_result)
	{
		using ModpackInstall::Outcome;
		switch (a_result.outcome) {
		case Outcome::applied:
			{
				std::string text = "Installed " + Plural(a_result.files, "file") + (a_result.serverLabel.empty() ? std::string{} : " for " + a_result.serverLabel) + ".";
				if (!a_result.activated.empty()) {
					text += "\nSwitched on: " + JoinNames(a_result.activated);
				}
				if (!a_result.deactivated.empty()) {
					text += "\nSwitched off: " + JoinNames(a_result.deactivated);
				}
				return text;
			}
		case Outcome::rolledBack:
		case Outcome::restored:
			return a_result.detail;
		case Outcome::refused:
		case Outcome::failed:
			return "The modpack install did not run. " + a_result.detail;
		case Outcome::nothingToDo:
		default:
			return a_result.detail;
		}
	}
}
