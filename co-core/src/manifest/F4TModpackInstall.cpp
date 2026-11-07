#include "F4TModpackInstallDetail.h"

#include "F4TModpackContentPolicy.h"
#include "F4TPluginsTxt.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::ModpackInstall
{
	using namespace Detail;

	std::filesystem::path Paths::PendingPlan() const
	{
		return installDirectory / "pending-install.json";
	}

	std::filesystem::path Paths::JournalFile() const
	{
		return installDirectory / "journal.json";
	}

	std::filesystem::path Paths::LastInstall() const
	{
		return installDirectory / "last-install.json";
	}

	std::filesystem::path Paths::RestoreRequest() const
	{
		return installDirectory / "restore-last-install";
	}

	std::filesystem::path Paths::StagingFor(std::string_view a_fingerprint) const
	{
		return stagingRoot / std::string{ a_fingerprint };
	}

	Paths PathsForGame(const std::filesystem::path& a_gameDirectory, const std::filesystem::path& a_pluginsTxt)
	{
		const auto root = a_gameDirectory / "CommonwealthOnline";
		return Paths{
			a_gameDirectory / "Data",
			root / "ModpackStaging",
			root / "ModpackBackups",
			root / "ModpackInstall",
			a_pluginsTxt,
			a_gameDirectory / "Fallout4.ccc"
		};
	}

	ModManager DetectModManager(bool a_virtualFileSystemLoaded, const std::vector<std::string>& a_dataFileNames)
	{
		if (a_virtualFileSystemLoaded) {
			return ModManager::modOrganizer;
		}
		for (const auto& name : a_dataFileNames) {
			const auto lowered = Lower(name);
			if (lowered.starts_with("vortex.deployment") && lowered.ends_with(".json")) {
				return ModManager::vortex;
			}
		}
		return ModManager::none;
	}

	std::string_view DescribeModManager(ModManager a_manager) noexcept
	{
		switch (a_manager) {
		case ModManager::modOrganizer:
			return "Mod Organizer 2";
		case ModManager::vortex:
			return "Vortex";
		case ModManager::none:
		default:
			return "no mod manager";
		}
	}

	Installer::Installer(FileSystem& a_fileSystem, Paths a_paths) :
		fileSystem(a_fileSystem),
		paths(std::move(a_paths))
	{}

	bool Installer::SaveJournal(const Journal& a_journal)
	{
		return fileSystem.WriteDurably(paths.JournalFile(), SerializeJournal(a_journal));
	}

	std::vector<std::string> Installer::CreationClubNames()
	{
		std::vector<std::string> names;
		const auto text = fileSystem.Read(paths.creationClubList, kMaximumCreationClubListBytes);
		if (!text) {
			return names;
		}
		std::size_t start = 0;
		while (start < text->size()) {
			auto end = text->find('\n', start);
			if (end == std::string::npos) {
				end = text->size();
			}
			auto line = std::string_view{ *text }.substr(start, end - start);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
				line.remove_suffix(1);
			}
			if (!line.empty()) {
				names.emplace_back(line);
			}
			start = end + 1;
		}
		return names;
	}

	std::optional<std::string> Installer::CheckTarget(const std::string& a_name)
	{
		const auto target = paths.dataDirectory / ModpackManifest::InstallRelativeName(a_name);
		if (!fileSystem.Exists(target)) {
			return std::nullopt;
		}
		if (!fileSystem.IsPlainFile(target)) {
			return a_name + " in Data is not a plain file";
		}
		const auto actual = fileSystem.ActualName(target);
		if (!actual || !EqualsIgnoringAsciiCase(*actual, target.filename().string())) {
			return a_name + " in Data resolves to a different file";
		}
		return std::nullopt;
	}

	bool Installer::HasPendingPlan()
	{
		return fileSystem.Exists(paths.PendingPlan());
	}

	bool Installer::HasRestoreRequest()
	{
		return fileSystem.Exists(paths.RestoreRequest());
	}

	bool Installer::IsVerifying()
	{
		const auto text = fileSystem.Read(paths.JournalFile(), kMaximumPlanBytes);
		const auto journal = text ? ParseJournal(*text) : std::nullopt;
		return journal && journal->state == JournalState::verifying;
	}

	bool Installer::WritePendingPlan(const InstallPlan& a_plan)
	{
		const auto serialized = SerializePlan(a_plan);
		if (!ParsePlan(serialized) || !fileSystem.CreateDirectories(paths.installDirectory)) {
			return false;
		}
		return fileSystem.WriteDurably(paths.PendingPlan(), serialized);
	}

	Result Installer::Recover()
	{
		if (fileSystem.Exists(paths.JournalFile())) {
			const auto text = fileSystem.Read(paths.JournalFile(), kMaximumPlanBytes);
			auto journal = text ? ParseJournal(*text) : std::nullopt;
			if (!journal) {
				return Result{ Outcome::refused, "The modpack install journal could not be read. Restore files by hand from " + paths.backupRoot.string() + "." };
			}
			if (journal->state == JournalState::applying || journal->state == JournalState::verifying) {
				const bool wasVerifying = journal->state == JournalState::verifying;
				auto result = Rollback(std::move(*journal), Outcome::rolledBack);
				if (result.outcome == Outcome::rolledBack) {
					result.detail = wasVerifying ?
						"The game did not finish loading after the last modpack install, so the install was undone." :
						"The last modpack install was interrupted, so it was undone.";
				}
				return result;
			}
			(void)fileSystem.Remove(paths.JournalFile());
		}
		if (HasRestoreRequest()) {
			(void)fileSystem.Remove(paths.RestoreRequest());
			return RestoreLastInstall();
		}
		return Result{};
	}

	Result Installer::ApplyPending(std::uint64_t a_uniqueSuffix)
	{
		if (fileSystem.Exists(paths.JournalFile())) {
			return Result{ Outcome::refused, "An earlier modpack install has not been finished or undone." };
		}
		if (!HasPendingPlan()) {
			return Result{};
		}
		const auto text = fileSystem.Read(paths.PendingPlan(), kMaximumPlanBytes);
		if (!fileSystem.Remove(paths.PendingPlan())) {
			return Result{ Outcome::refused, "The pending modpack install could not be cleared, so it was not applied." };
		}
		const auto plan = text ? ParsePlan(*text) : std::nullopt;
		if (!plan) {
			return Result{ Outcome::failed, "The pending modpack install was unreadable and was discarded." };
		}
		return Apply(*plan, a_uniqueSuffix);
	}

	Result Installer::Apply(const InstallPlan& a_plan, std::uint64_t a_uniqueSuffix)
	{
		Result refused{ Outcome::refused, {}, 0, {}, {}, a_plan.serverKey, a_plan.serverLabel, a_plan.endpoint };
		const auto staging = paths.StagingFor(a_plan.fingerprint);
		if (!fileSystem.IsPlainDirectory(paths.dataDirectory) || !fileSystem.IsPlainDirectory(staging) ||
			!fileSystem.CreateDirectories(paths.backupRoot) || !fileSystem.IsPlainDirectory(paths.backupRoot)) {
			refused.detail = "The Data, staging or backup folder is missing or is a link.";
			return refused;
		}
		if (!fileSystem.SameVolume(paths.dataDirectory, staging) || !fileSystem.SameVolume(paths.dataDirectory, paths.backupRoot)) {
			refused.detail = "The Data folder is on a different drive from the Commonwealth Online folder.";
			return refused;
		}

		Journal journal{ a_plan.fingerprint, a_plan.serverKey, a_plan.serverLabel, a_plan.endpoint, {}, JournalState::applying, {}, false, false };
		for (const auto& file : a_plan.files) {
			if (!fileSystem.Matches(staging / file.name, file.sha256Hex, file.sizeBytes)) {
				refused.outcome = Outcome::failed;
				refused.detail = file.name + " in staging no longer matches what was downloaded.";
				return refused;
			}
			if (ModpackManifest::IsValidViewName(file.name)) {
				const auto content = fileSystem.Read(staging / file.name, static_cast<std::size_t>(ModpackManifest::kMaximumViewFileBytes));
				if (!content || !ModpackManifest::IsSafeResourceViewContent(file.name, *content)) {
					refused.outcome = Outcome::failed;
					refused.detail = file.name + " is not a safe resource view; it references a remote origin or another folder.";
					return refused;
				}
			}
			if (const auto problem = CheckTarget(file.name)) {
				refused.detail = *problem + ".";
				return refused;
			}
			journal.ops.push_back(JournalOp{ file.name, OpStep::planned, fileSystem.Exists(paths.dataDirectory / ModpackManifest::InstallRelativeName(file.name)) });
		}

		std::optional<PluginsTxt::Activation> activation;
		std::string currentPluginsTxt;
		if (a_plan.activatePlugins) {
			journal.pluginsTxtExisted = fileSystem.Exists(paths.pluginsTxt);
			if (journal.pluginsTxtExisted) {
				const auto current = fileSystem.Read(paths.pluginsTxt, PluginsTxt::kMaximumFileBytes);
				if (!current) {
					refused.detail = "plugins.txt could not be read.";
					return refused;
				}
				currentPluginsTxt = *current;
			}
			activation = PluginsTxt::Activate(currentPluginsTxt, a_plan.loadOrder, CreationClubNames(), fileSystem.List(paths.dataDirectory));
			if (!activation) {
				refused.detail = "plugins.txt could not be updated.";
				return refused;
			}
		}

		journal.backupFolder = a_plan.fingerprint.substr(0, 12) + "-" + std::to_string(a_uniqueSuffix);
		const auto backup = paths.backupRoot / journal.backupFolder;
		if (!fileSystem.CreateNewDirectory(backup)) {
			refused.outcome = Outcome::failed;
			refused.detail = "A new backup folder could not be created.";
			return refused;
		}
		if (!SaveJournal(journal)) {
			(void)fileSystem.Remove(backup);
			refused.outcome = Outcome::failed;
			refused.detail = "The install journal could not be written.";
			return refused;
		}

		const auto abort = [&](std::string a_reason) {
			auto result = Rollback(journal, Outcome::failed);
			result.detail = std::move(a_reason) + (result.outcome == Outcome::failed && !result.detail.empty() ? " " + result.detail : std::string{});
			result.outcome = Outcome::failed;
			return result;
		};

		for (auto& op : journal.ops) {
			const auto target = paths.dataDirectory / ModpackManifest::InstallRelativeName(op.name);
			if (!fileSystem.CreateDirectories(target.parent_path()) || !fileSystem.CreateDirectories((backup / op.name).parent_path())) {
				return abort("Could not create the folders for " + op.name + "; the install was undone.");
			}
			if (op.hadOriginal) {
				if (!fileSystem.MoveNoReplace(target, backup / op.name)) {
					return abort("Could not move " + op.name + " to the backup folder; the install was undone.");
				}
				op.step = OpStep::backedUp;
				if (!SaveJournal(journal)) {
					return abort("The install journal could not be updated; the install was undone.");
				}
			}
			if (!fileSystem.MoveNoReplace(staging / op.name, target)) {
				return abort("Could not place " + op.name + " in Data; the install was undone.");
			}
			op.step = OpStep::placed;
			if (!SaveJournal(journal)) {
				return abort("The install journal could not be updated; the install was undone.");
			}
		}

		if (activation) {
			if (journal.pluginsTxtExisted && !fileSystem.WriteDurably(backup / std::string{ kPluginsTxtBackupName }, currentPluginsTxt)) {
				return abort("plugins.txt could not be backed up; the install was undone.");
			}
			journal.pluginsTxtReplaced = true;
			journal.pluginsTxtInstalled = activation->text;
			if (!SaveJournal(journal)) {
				return abort("The install journal could not be updated; the install was undone.");
			}
			if (!fileSystem.WriteDurably(paths.pluginsTxt, activation->text)) {
				return abort("plugins.txt could not be written; the install was undone.");
			}
		}

		journal.state = JournalState::verifying;
		if (!SaveJournal(journal)) {
			return abort("The install journal could not be updated; the install was undone.");
		}
		Result result{ Outcome::applied, {}, journal.ops.size(), {}, {}, a_plan.serverKey, a_plan.serverLabel, a_plan.endpoint };
		if (activation) {
			result.activated = activation->activated;
			result.deactivated = activation->deactivated;
		}
		return result;
	}
}
