#include "F4TModpackInstall.h"

#include "F4TContentManifest.h"
#include "F4TModpackDownload.h"
#include "F4TPluginsTxt.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif

namespace F4T::ModpackInstall
{
	namespace
	{
		using ModpackManifest::AutoInstallRefusal;
		using ModpackManifest::CheckAutoInstallName;
		using ModpackManifest::EqualsIgnoringAsciiCase;

		constexpr int kFormatVersion = 1;
		constexpr std::size_t kMaximumLabelBytes = 256;
		constexpr std::size_t kMaximumJoinIdBytes = 128;
		constexpr std::size_t kMaximumLoadOrderNameBytes = 256;
		constexpr std::size_t kMaximumCreationClubListBytes = 256 * 1024;
		constexpr std::string_view kPluginsTxtBackupName = "plugins.txt";

		[[nodiscard]] bool IsPrintable(std::string_view a_text, std::size_t a_maximumBytes, bool a_allowEmpty)
		{
			if ((!a_allowEmpty && a_text.empty()) || a_text.size() > a_maximumBytes) {
				return false;
			}
			for (const auto character : a_text) {
				const auto byte = static_cast<unsigned char>(character);
				if (byte < 0x20 || byte == 0x7F) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string lowered{ a_text };
			for (auto& character : lowered) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				}
			}
			return lowered;
		}

		[[nodiscard]] bool IsBackupFolderName(std::string_view a_name)
		{
			if (a_name.empty() || a_name.size() > 64) {
				return false;
			}
			for (const auto character : a_name) {
				const bool allowed = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || character == '-';
				if (!allowed) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] std::string_view StepName(OpStep a_step) noexcept
		{
			switch (a_step) {
			case OpStep::backedUp:
				return "backedUp";
			case OpStep::placed:
				return "placed";
			case OpStep::planned:
			default:
				return "planned";
			}
		}

		[[nodiscard]] std::optional<OpStep> ParseStep(std::string_view a_text) noexcept
		{
			if (a_text == "planned") {
				return OpStep::planned;
			}
			if (a_text == "backedUp") {
				return OpStep::backedUp;
			}
			if (a_text == "placed") {
				return OpStep::placed;
			}
			return std::nullopt;
		}

		[[nodiscard]] std::string_view StateName(JournalState a_state) noexcept
		{
			switch (a_state) {
			case JournalState::verifying:
				return "verifying";
			case JournalState::applied:
				return "applied";
			case JournalState::rolledBack:
				return "rolledBack";
			case JournalState::applying:
			default:
				return "applying";
			}
		}

		[[nodiscard]] std::optional<JournalState> ParseState(std::string_view a_text) noexcept
		{
			if (a_text == "applying") {
				return JournalState::applying;
			}
			if (a_text == "verifying") {
				return JournalState::verifying;
			}
			if (a_text == "applied") {
				return JournalState::applied;
			}
			if (a_text == "rolledBack") {
				return JournalState::rolledBack;
			}
			return std::nullopt;
		}

		[[nodiscard]] const std::string* StringField(const nlohmann::json& a_object, const char* a_key)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end() || !found->is_string()) {
				return nullptr;
			}
			return &found->get_ref<const std::string&>();
		}

		[[nodiscard]] std::optional<bool> BoolField(const nlohmann::json& a_object, const char* a_key)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end() || !found->is_boolean()) {
				return std::nullopt;
			}
			return found->get<bool>();
		}

		[[nodiscard]] bool HasVersion(const nlohmann::json& a_root)
		{
			const auto version = a_root.find("version");
			return version != a_root.end() && version->is_number_integer() && version->get<int>() == kFormatVersion;
		}

		[[nodiscard]] nlohmann::json EndpointObject(const ServerEndpoint& a_endpoint)
		{
			return nlohmann::json{
				{ "host", a_endpoint.host },
				{ "port", a_endpoint.port },
				{ "joinId", a_endpoint.joinId }
			};
		}

		[[nodiscard]] std::optional<ServerEndpoint> ParseEndpoint(const nlohmann::json& a_root)
		{
			const auto endpoint = a_root.find("endpoint");
			if (endpoint == a_root.end() || !endpoint->is_object()) {
				return std::nullopt;
			}
			const auto* host = StringField(*endpoint, "host");
			const auto* joinId = StringField(*endpoint, "joinId");
			const auto port = endpoint->find("port");
			if (host == nullptr || !IsPrintable(*host, kMaximumLabelBytes, true) ||
				joinId == nullptr || !IsPrintable(*joinId, kMaximumJoinIdBytes, true) ||
				port == endpoint->end() || !port->is_number_unsigned() || port->get<std::uint64_t>() > 65535) {
				return std::nullopt;
			}
			return ServerEndpoint{ *host, static_cast<std::uint16_t>(port->get<std::uint64_t>()), *joinId };
		}

		[[nodiscard]] std::optional<nlohmann::json> ParseObject(std::string_view a_text)
		{
			if (a_text.empty() || a_text.size() > kMaximumPlanBytes) {
				return std::nullopt;
			}
			auto root = nlohmann::json::parse(a_text, nullptr, false);
			if (root.is_discarded() || !root.is_object() || !HasVersion(root)) {
				return std::nullopt;
			}
			return root;
		}
	}

	std::string SerializePlan(const InstallPlan& a_plan)
	{
		auto files = nlohmann::json::array();
		for (const auto& file : a_plan.files) {
			files.push_back(nlohmann::json{
				{ "name", file.name },
				{ "sha256", file.sha256Hex },
				{ "size", file.sizeBytes },
				{ "replaces", file.replaces }
			});
		}
		return nlohmann::json{
			{ "version", kFormatVersion },
			{ "fingerprint", a_plan.fingerprint },
			{ "serverKey", a_plan.serverKey },
			{ "serverLabel", a_plan.serverLabel },
			{ "endpoint", EndpointObject(a_plan.endpoint) },
			{ "activatePlugins", a_plan.activatePlugins },
			{ "files", std::move(files) },
			{ "loadOrder", a_plan.loadOrder }
		}.dump(1, '\t');
	}

	std::optional<InstallPlan> ParsePlan(std::string_view a_text)
	{
		try {
			const auto root = ParseObject(a_text);
			if (!root) {
				return std::nullopt;
			}
			const auto* fingerprint = StringField(*root, "fingerprint");
			const auto* serverKey = StringField(*root, "serverKey");
			const auto* serverLabel = StringField(*root, "serverLabel");
			const auto activate = BoolField(*root, "activatePlugins");
			const auto endpoint = ParseEndpoint(*root);
			const auto files = root->find("files");
			const auto loadOrder = root->find("loadOrder");
			if (fingerprint == nullptr || !ContentManifest::IsValidFingerprint(*fingerprint) ||
				serverKey == nullptr || !IsPrintable(*serverKey, kMaximumServerKeyBytes, true) ||
				serverLabel == nullptr || !IsPrintable(*serverLabel, kMaximumLabelBytes, true) || !activate || !endpoint ||
				files == root->end() || !files->is_array() || files->empty() || files->size() > ModpackManifest::kMaximumAutoInstallFiles ||
				loadOrder == root->end() || !loadOrder->is_array() || loadOrder->size() > kMaximumLoadOrderEntries) {
				return std::nullopt;
			}
			InstallPlan plan{ *fingerprint, *serverKey, *serverLabel, *endpoint, {}, {}, *activate };
			std::unordered_set<std::string> names;
			std::uint64_t total = 0;
			for (const auto& file : *files) {
				if (!file.is_object()) {
					return std::nullopt;
				}
				const auto* name = StringField(file, "name");
				const auto* sha = StringField(file, "sha256");
				const auto size = file.find("size");
				const auto replaces = BoolField(file, "replaces");
				if (name == nullptr || CheckAutoInstallName(*name) != AutoInstallRefusal::none || !names.insert(Lower(*name)).second ||
					sha == nullptr || !ContentManifest::IsValidFingerprint(*sha) ||
					size == file.end() || !size->is_number_unsigned() || !replaces) {
					return std::nullopt;
				}
				const auto bytes = size->get<std::uint64_t>();
				if (bytes == 0 || bytes > ModpackManifest::kMaximumAutoInstallFileBytes) {
					return std::nullopt;
				}
				total += bytes;
				plan.files.push_back(PlannedFile{ *name, *sha, bytes, *replaces });
			}
			if (total > ModpackManifest::kMaximumAutoInstallTotalBytes) {
				return std::nullopt;
			}
			for (const auto& entry : *loadOrder) {
				if (!entry.is_string() || !IsPrintable(entry.get_ref<const std::string&>(), kMaximumLoadOrderNameBytes, false)) {
					return std::nullopt;
				}
				plan.loadOrder.push_back(entry.get<std::string>());
			}
			return plan;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::string SerializeJournal(const Journal& a_journal)
	{
		auto ops = nlohmann::json::array();
		for (const auto& op : a_journal.ops) {
			ops.push_back(nlohmann::json{
				{ "name", op.name },
				{ "step", std::string{ StepName(op.step) } },
				{ "hadOriginal", op.hadOriginal }
			});
		}
		return nlohmann::json{
			{ "version", kFormatVersion },
			{ "fingerprint", a_journal.fingerprint },
			{ "serverKey", a_journal.serverKey },
			{ "serverLabel", a_journal.serverLabel },
			{ "endpoint", EndpointObject(a_journal.endpoint) },
			{ "backupFolder", a_journal.backupFolder },
			{ "state", std::string{ StateName(a_journal.state) } },
			{ "ops", std::move(ops) },
			{ "pluginsTxtReplaced", a_journal.pluginsTxtReplaced },
			{ "pluginsTxtExisted", a_journal.pluginsTxtExisted }
		}.dump(1, '\t');
	}

	std::optional<Journal> ParseJournal(std::string_view a_text)
	{
		try {
			const auto root = ParseObject(a_text);
			if (!root) {
				return std::nullopt;
			}
			const auto* fingerprint = StringField(*root, "fingerprint");
			const auto* serverKey = StringField(*root, "serverKey");
			const auto* serverLabel = StringField(*root, "serverLabel");
			const auto* backupFolder = StringField(*root, "backupFolder");
			const auto endpoint = ParseEndpoint(*root);
			const auto* state = StringField(*root, "state");
			const auto ops = root->find("ops");
			const auto replaced = BoolField(*root, "pluginsTxtReplaced");
			const auto existed = BoolField(*root, "pluginsTxtExisted");
			if (fingerprint == nullptr || !ContentManifest::IsValidFingerprint(*fingerprint) ||
				serverKey == nullptr || !IsPrintable(*serverKey, kMaximumServerKeyBytes, true) ||
				serverLabel == nullptr || !IsPrintable(*serverLabel, kMaximumLabelBytes, true) ||
				backupFolder == nullptr || !IsBackupFolderName(*backupFolder) || !endpoint ||
				state == nullptr || !ParseState(*state) || !replaced || !existed ||
				ops == root->end() || !ops->is_array() || ops->size() > ModpackManifest::kMaximumAutoInstallFiles) {
				return std::nullopt;
			}
			Journal journal{ *fingerprint, *serverKey, *serverLabel, *endpoint, *backupFolder, *ParseState(*state), {}, *replaced, *existed };
			std::unordered_set<std::string> names;
			for (const auto& op : *ops) {
				if (!op.is_object()) {
					return std::nullopt;
				}
				const auto* name = StringField(op, "name");
				const auto* step = StringField(op, "step");
				const auto hadOriginal = BoolField(op, "hadOriginal");
				if (name == nullptr || CheckAutoInstallName(*name) != AutoInstallRefusal::none || !names.insert(Lower(*name)).second ||
					step == nullptr || !ParseStep(*step) || !hadOriginal) {
					return std::nullopt;
				}
				journal.ops.push_back(JournalOp{ *name, *ParseStep(*step), *hadOriginal });
			}
			return journal;
		} catch (...) {
			return std::nullopt;
		}
	}

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
		const auto target = paths.dataDirectory / a_name;
		if (!fileSystem.Exists(target)) {
			return std::nullopt;
		}
		if (!fileSystem.IsPlainFile(target)) {
			return a_name + " in Data is not a plain file";
		}
		const auto actual = fileSystem.ActualName(target);
		if (!actual || !EqualsIgnoringAsciiCase(*actual, a_name)) {
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
			if (const auto problem = CheckTarget(file.name)) {
				refused.detail = *problem + ".";
				return refused;
			}
			journal.ops.push_back(JournalOp{ file.name, OpStep::planned, fileSystem.Exists(paths.dataDirectory / file.name) });
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
			const auto target = paths.dataDirectory / op.name;
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

	Result Installer::Rollback(Journal a_journal, Outcome a_outcome)
	{
		Result result{ a_outcome, {}, 0, {}, {}, a_journal.serverKey, a_journal.serverLabel, a_journal.endpoint };
		const auto staging = paths.StagingFor(a_journal.fingerprint);
		const auto backup = paths.backupRoot / a_journal.backupFolder;
		(void)fileSystem.CreateDirectories(staging);
		std::string problems;
		for (auto op = a_journal.ops.rbegin(); op != a_journal.ops.rend(); ++op) {
			const auto target = paths.dataDirectory / op->name;
			const auto staged = staging / op->name;
			const auto saved = backup / op->name;
			const bool originalSaved = op->hadOriginal && fileSystem.Exists(saved);
			const bool ours = fileSystem.Exists(target) && !fileSystem.Exists(staged) && (!op->hadOriginal || originalSaved);
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
				if (!previous || !fileSystem.WriteDurably(paths.pluginsTxt, *previous)) {
					problems += " plugins.txt could not be restored from " + backup.string() + ".";
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

	namespace
	{
		[[nodiscard]] std::uint64_t FileSizeOrMax(const std::filesystem::path& a_path)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(a_path, error);
			return error ? std::numeric_limits<std::uint64_t>::max() : size;
		}

#ifdef _WIN32
		[[nodiscard]] std::wstring Extended(const std::filesystem::path& a_path)
		{
			std::error_code error;
			auto absolute = std::filesystem::absolute(a_path, error);
			if (error) {
				absolute = a_path;
			}
			std::wstring text = absolute.lexically_normal().native();
			if (text.starts_with(L"\\\\?\\")) {
				return text;
			}
			if (text.starts_with(L"\\\\")) {
				return L"\\\\?\\UNC\\" + text.substr(2);
			}
			return L"\\\\?\\" + text;
		}

		[[nodiscard]] DWORD Attributes(const std::filesystem::path& a_path)
		{
			return GetFileAttributesW(Extended(a_path).c_str());
		}

		[[nodiscard]] bool HasReparseAncestor(const std::filesystem::path& a_path)
		{
			std::error_code error;
			auto current = std::filesystem::absolute(a_path, error).lexically_normal();
			while (!current.empty() && current != current.root_path()) {
				const auto attributes = Attributes(current);
				if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
					return true;
				}
				current = current.parent_path();
			}
			return false;
		}

		class NativeFileSystem final : public FileSystem
		{
		public:
			bool Exists(const std::filesystem::path& a_path) override
			{
				return Attributes(a_path) != INVALID_FILE_ATTRIBUTES;
			}

			bool IsPlainFile(const std::filesystem::path& a_path) override
			{
				const auto attributes = Attributes(a_path);
				return attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
					!HasReparseAncestor(a_path.parent_path());
			}

			bool IsPlainDirectory(const std::filesystem::path& a_path) override
			{
				const auto attributes = Attributes(a_path);
				return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
					(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && !HasReparseAncestor(a_path);
			}

			bool CreateDirectories(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				std::filesystem::create_directories(a_path, error);
				return IsPlainDirectory(a_path);
			}

			bool CreateNewDirectory(const std::filesystem::path& a_path) override
			{
				return !HasReparseAncestor(a_path.parent_path()) && CreateDirectoryW(Extended(a_path).c_str(), nullptr) != FALSE;
			}

			bool MoveNoReplace(const std::filesystem::path& a_from, const std::filesystem::path& a_to) override
			{
				if (HasReparseAncestor(a_from) || HasReparseAncestor(a_to.parent_path())) {
					return false;
				}
				return MoveFileExW(Extended(a_from).c_str(), Extended(a_to).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}

			bool WriteDurably(const std::filesystem::path& a_path, std::string_view a_bytes) override
			{
				if (HasReparseAncestor(a_path.parent_path()) || a_bytes.size() > std::numeric_limits<DWORD>::max()) {
					return false;
				}
				auto temporary = a_path;
				temporary += L".co-tmp";
				const auto handle = CreateFileW(Extended(temporary).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (handle == INVALID_HANDLE_VALUE) {
					return false;
				}
				DWORD written = 0;
				const bool ok = WriteFile(handle, a_bytes.data(), static_cast<DWORD>(a_bytes.size()), &written, nullptr) != FALSE &&
					written == a_bytes.size() && FlushFileBuffers(handle) != FALSE;
				CloseHandle(handle);
				if (!ok) {
					DeleteFileW(Extended(temporary).c_str());
					return false;
				}
				return MoveFileExW(Extended(temporary).c_str(), Extended(a_path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}

			std::optional<std::string> Read(const std::filesystem::path& a_path, std::size_t a_maximumBytes) override
			{
				if (!IsPlainFile(a_path) || FileSizeOrMax(a_path) > a_maximumBytes) {
					return std::nullopt;
				}
				std::ifstream stream(a_path, std::ios::binary);
				if (!stream) {
					return std::nullopt;
				}
				return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
			}

			bool Remove(const std::filesystem::path& a_path) override
			{
				const auto attributes = Attributes(a_path);
				if (attributes == INVALID_FILE_ATTRIBUTES) {
					return true;
				}
				if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
					return RemoveDirectoryW(Extended(a_path).c_str()) != FALSE;
				}
				return DeleteFileW(Extended(a_path).c_str()) != FALSE;
			}

			bool Matches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes) override
			{
				return IsPlainFile(a_path) && ModpackDownload::StagedFileMatches(a_path, a_sha256Hex, a_sizeBytes);
			}

			std::optional<std::string> ActualName(const std::filesystem::path& a_path) override
			{
				WIN32_FIND_DATAW found{};
				const auto handle = FindFirstFileW(Extended(a_path).c_str(), &found);
				if (handle == INVALID_HANDLE_VALUE) {
					return std::nullopt;
				}
				FindClose(handle);
				return std::filesystem::path{ found.cFileName }.string();
			}

			bool SameVolume(const std::filesystem::path& a_left, const std::filesystem::path& a_right) override
			{
				const auto volume = [](const std::filesystem::path& a_path) -> std::optional<DWORD> {
					const auto handle = CreateFileW(Extended(a_path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
					if (handle == INVALID_HANDLE_VALUE) {
						return std::nullopt;
					}
					BY_HANDLE_FILE_INFORMATION information{};
					const bool ok = GetFileInformationByHandle(handle, &information) != FALSE;
					CloseHandle(handle);
					return ok ? std::optional<DWORD>{ information.dwVolumeSerialNumber } : std::nullopt;
				};
				const auto left = volume(a_left);
				const auto right = volume(a_right);
				return left && right && *left == *right;
			}

			std::optional<std::uint64_t> FreeBytes(const std::filesystem::path& a_path) override
			{
				ULARGE_INTEGER available{};
				if (GetDiskFreeSpaceExW(Extended(a_path).c_str(), &available, nullptr, nullptr) == FALSE) {
					return std::nullopt;
				}
				return available.QuadPart;
			}

			std::vector<std::string> List(const std::filesystem::path& a_directory) override
			{
				std::vector<std::string> names;
				std::error_code error;
				for (std::filesystem::directory_iterator it(a_directory, error), end; !error && it != end; it.increment(error)) {
					if (it->is_regular_file(error)) {
						names.push_back(it->path().filename().string());
					}
				}
				return names;
			}
		};
#else
		[[nodiscard]] bool HasLinkAncestor(const std::filesystem::path& a_path)
		{
			std::error_code error;
			auto current = std::filesystem::absolute(a_path, error).lexically_normal();
			while (!current.empty() && current != current.root_path()) {
				if (std::filesystem::is_symlink(std::filesystem::symlink_status(current, error))) {
					return true;
				}
				current = current.parent_path();
			}
			return false;
		}

		class NativeFileSystem final : public FileSystem
		{
		public:
			bool Exists(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return std::filesystem::exists(std::filesystem::symlink_status(a_path, error));
			}

			bool IsPlainFile(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return std::filesystem::is_regular_file(std::filesystem::symlink_status(a_path, error)) && !HasLinkAncestor(a_path.parent_path());
			}

			bool IsPlainDirectory(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return std::filesystem::is_directory(std::filesystem::symlink_status(a_path, error)) && !HasLinkAncestor(a_path);
			}

			bool CreateDirectories(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				std::filesystem::create_directories(a_path, error);
				return IsPlainDirectory(a_path);
			}

			bool CreateNewDirectory(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				return !HasLinkAncestor(a_path.parent_path()) && std::filesystem::create_directory(a_path, error) && !error;
			}

			bool MoveNoReplace(const std::filesystem::path& a_from, const std::filesystem::path& a_to) override
			{
				std::error_code error;
				if (HasLinkAncestor(a_from) || HasLinkAncestor(a_to.parent_path()) || Exists(a_to)) {
					return false;
				}
				std::filesystem::rename(a_from, a_to, error);
				return !error;
			}

			bool WriteDurably(const std::filesystem::path& a_path, std::string_view a_bytes) override
			{
				if (HasLinkAncestor(a_path.parent_path())) {
					return false;
				}
				auto temporary = a_path;
				temporary += ".co-tmp";
				{
					std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
					stream.write(a_bytes.data(), static_cast<std::streamsize>(a_bytes.size()));
					stream.flush();
					if (!stream) {
						return false;
					}
				}
				std::error_code error;
				std::filesystem::rename(temporary, a_path, error);
				return !error;
			}

			std::optional<std::string> Read(const std::filesystem::path& a_path, std::size_t a_maximumBytes) override
			{
				if (!IsPlainFile(a_path) || FileSizeOrMax(a_path) > a_maximumBytes) {
					return std::nullopt;
				}
				std::ifstream stream(a_path, std::ios::binary);
				if (!stream) {
					return std::nullopt;
				}
				return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
			}

			bool Remove(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				std::filesystem::remove(a_path, error);
				return !error;
			}

			bool Matches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes) override
			{
				return IsPlainFile(a_path) && ModpackDownload::StagedFileMatches(a_path, a_sha256Hex, a_sizeBytes);
			}

			std::optional<std::string> ActualName(const std::filesystem::path& a_path) override
			{
				return Exists(a_path) ? std::optional<std::string>{ a_path.filename().string() } : std::nullopt;
			}

			bool SameVolume(const std::filesystem::path&, const std::filesystem::path&) override
			{
				return true;
			}

			std::optional<std::uint64_t> FreeBytes(const std::filesystem::path& a_path) override
			{
				std::error_code error;
				const auto space = std::filesystem::space(a_path, error);
				return error ? std::nullopt : std::optional<std::uint64_t>{ space.available };
			}

			std::vector<std::string> List(const std::filesystem::path& a_directory) override
			{
				std::vector<std::string> names;
				std::error_code error;
				for (std::filesystem::directory_iterator it(a_directory, error), end; !error && it != end; it.increment(error)) {
					if (it->is_regular_file(error)) {
						names.push_back(it->path().filename().string());
					}
				}
				return names;
			}
		};
#endif
	}

	std::unique_ptr<FileSystem> MakeNativeFileSystem()
	{
		return std::make_unique<NativeFileSystem>();
	}
}
