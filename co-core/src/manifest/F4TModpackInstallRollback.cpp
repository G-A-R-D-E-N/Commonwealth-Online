#include "F4TModpackInstallDetail.h"

#include "F4TModpackContentPolicy.h"
#include "F4TPluginsTxt.h"

#include <string>
#include <utility>

namespace F4T::ModpackInstall
{
	using namespace Detail;

	Result Installer::Rollback(Journal a_journal, Outcome a_outcome)
	{
		Result result{ a_outcome, {}, 0, {}, {}, a_journal.serverKey, a_journal.serverLabel, a_journal.endpoint };
		const auto staging = paths.StagingFor(a_journal.fingerprint);
		const auto backup = paths.backupRoot / a_journal.backupFolder;
		(void)fileSystem.CreateDirectories(staging);
		std::string problems;
		for (auto op = a_journal.ops.rbegin(); op != a_journal.ops.rend(); ++op) {
			const auto target = paths.dataDirectory / ModpackManifest::InstallRelativeName(op->name);
			const auto staged = staging / op->name;
			const auto saved = backup / op->name;
			if (!fileSystem.CreateDirectories(staged.parent_path()) || !fileSystem.CreateDirectories(target.parent_path())) {
				problems += " " + op->name + " could not be undone because its folders could not be created.";
				continue;
			}
			const bool originalSaved = op->hadOriginal && fileSystem.Exists(saved);
			const bool targetExists = fileSystem.Exists(target);
			const bool stagedExists = fileSystem.Exists(staged);
			if (op->hadOriginal && !originalSaved && targetExists && stagedExists) {
				op->step = OpStep::planned;
				++result.files;
				continue;
			}
			if (targetExists && stagedExists) {
				problems += " " + op->name + " has a conflicting staged file; leaving installed file in place.";
				continue;
			}
			const bool ours = targetExists && !stagedExists && (!op->hadOriginal || originalSaved);
			if (targetExists && !ours) {
				problems += " " + op->name + " was modified after install; leaving it in place.";
				continue;
			}
			if (ours && !fileSystem.MoveNoReplace(target, staged)) {
				problems += " " + op->name + " could not be removed from Data.";
				continue;
			}
			if (originalSaved && !fileSystem.Exists(target) && !fileSystem.MoveNoReplace(saved, target)) {
				problems += " " + op->name + " could not be restored from " + backup.string() + ".";
				continue;
			}
			op->step = OpStep::planned;
			++result.files;
		}
		if (a_journal.pluginsTxtReplaced) {
			if (a_journal.pluginsTxtExisted) {
				const auto previous = fileSystem.Read(backup / std::string{ kPluginsTxtBackupName }, PluginsTxt::kMaximumFileBytes);
				if (!previous) {
					problems += " plugins.txt could not be restored from " + backup.string() + ".";
				} else {
					const auto current = fileSystem.Read(paths.pluginsTxt, PluginsTxt::kMaximumFileBytes);
					const bool edited = current && !a_journal.pluginsTxtInstalled.empty() && *current != a_journal.pluginsTxtInstalled && *current != *previous;
					if (edited) {
						problems += " plugins.txt was edited after install; leaving current file in place (original kept in " + backup.string() + ").";
					} else if (!fileSystem.WriteDurably(paths.pluginsTxt, *previous)) {
						problems += " plugins.txt could not be restored from " + backup.string() + ".";
					}
				}
			} else if (!fileSystem.Remove(paths.pluginsTxt)) {
				problems += " plugins.txt could not be removed.";
			}
		}
		if (!problems.empty()) {
			(void)SaveJournal(a_journal);
			result.outcome = Outcome::failed;
			result.detail = "The modpack install could not be fully undone:" + problems;
			return result;
		}
		a_journal.state = JournalState::rolledBack;
		(void)SaveJournal(a_journal);
		(void)fileSystem.Remove(paths.JournalFile());
		return result;
	}

	Result Installer::ConfirmVerified()
	{
		const auto text = fileSystem.Read(paths.JournalFile(), kMaximumPlanBytes);
		auto journal = text ? ParseJournal(*text) : std::nullopt;
		if (!journal || journal->state != JournalState::verifying) {
			return Result{};
		}
		journal->state = JournalState::applied;
		if (!fileSystem.WriteDurably(paths.LastInstall(), SerializeJournal(*journal)) || !fileSystem.Remove(paths.JournalFile())) {
			return Result{ Outcome::failed, "The finished modpack install could not be recorded." };
		}
		return Result{ Outcome::applied, {}, journal->ops.size(), {}, {}, journal->serverKey, journal->serverLabel, journal->endpoint };
	}

	Result Installer::RestoreLastInstall()
	{
		const auto text = fileSystem.Read(paths.LastInstall(), kMaximumPlanBytes);
		auto journal = text ? ParseJournal(*text) : std::nullopt;
		if (!journal || journal->state != JournalState::applied) {
			return Result{ Outcome::nothingToDo, "There is no finished modpack install to undo." };
		}
		journal->state = JournalState::applying;
		if (!SaveJournal(*journal)) {
			return Result{ Outcome::failed, "The restore could not be started." };
		}
		(void)fileSystem.Remove(paths.LastInstall());
		auto result = Rollback(std::move(*journal), Outcome::restored);
		if (result.outcome == Outcome::restored) {
			result.detail = "The files replaced by the last modpack install were restored.";
		}
		return result;
	}
}
