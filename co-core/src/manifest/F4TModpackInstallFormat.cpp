#include "F4TModpackInstallDetail.h"

#include "F4TContentManifest.h"
#include "F4TModpackContentPolicy.h"
#include "F4TPluginsTxt.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <utility>

namespace F4T::ModpackInstall
{
	using namespace Detail;

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
			std::uint64_t viewTotal = 0;
			for (const auto& file : *files) {
				if (!file.is_object()) {
					return std::nullopt;
				}
				const auto* name = StringField(file, "name");
				const auto* sha = StringField(file, "sha256");
				const auto size = file.find("size");
				const auto replaces = BoolField(file, "replaces");
				if (name == nullptr || !ModpackManifest::IsInstallableName(*name) || !names.insert(Lower(*name)).second ||
					sha == nullptr || !ContentManifest::IsValidFingerprint(*sha) ||
					size == file.end() || !size->is_number_unsigned() || !replaces) {
					return std::nullopt;
				}
				const auto bytes = size->get<std::uint64_t>();
				if (bytes == 0 || bytes > ModpackManifest::MaximumInstallableFileBytes(*name)) {
					return std::nullopt;
				}
				total += bytes;
				viewTotal += ModpackManifest::IsValidViewName(*name) ? bytes : 0;
				plan.files.push_back(PlannedFile{ *name, *sha, bytes, *replaces });
			}
			if (total > ModpackManifest::kMaximumAutoInstallTotalBytes || viewTotal > ModpackManifest::kMaximumViewTotalBytes) {
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
			{ "pluginsTxtExisted", a_journal.pluginsTxtExisted },
			{ "pluginsTxtInstalled", a_journal.pluginsTxtInstalled }
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
				if (name == nullptr || !ModpackManifest::IsInstallableName(*name) || !names.insert(Lower(*name)).second ||
					step == nullptr || !ParseStep(*step) || !hadOriginal) {
					return std::nullopt;
				}
				journal.ops.push_back(JournalOp{ *name, *ParseStep(*step), *hadOriginal });
			}
			if (const auto* installed = StringField(*root, "pluginsTxtInstalled")) {
				if (installed->size() > PluginsTxt::kMaximumFileBytes) return std::nullopt;
				journal.pluginsTxtInstalled = *installed;
			}
			return journal;
		} catch (...) {
			return std::nullopt;
		}
	}
}
