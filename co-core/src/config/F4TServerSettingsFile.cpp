#include "F4TServerSettingsFile.h"

#include "F4TWorldPolicy.h"
#include "F4TUtf8Path.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace F4T::ServerSettings
{
	namespace
	{
		using Json = nlohmann::json;

		enum class FieldKind
		{
			Text,
			Flag,
			Count,
			Size
		};

		struct Field
		{
			std::string_view key;
			std::string_view documentKey;
			std::string_view alias;
			std::string_view legacy;
			std::string_view legacyAlias;
			FieldKind kind;
			std::string Editable::* text;
			bool Editable::* flag;
			std::uint32_t Editable::* count;
			std::size_t Editable::* size;
		};

		constexpr Field kFields[]{
			{ .key = "serverName", .documentKey = "server_name", .alias = "serverName", .kind = FieldKind::Text, .text = &Editable::serverName },
			{ .key = "serverDescription", .documentKey = "server_description", .alias = "serverDescription", .kind = FieldKind::Text, .text = &Editable::serverDescription },
			{ .key = "maxPlayers", .documentKey = "max_players", .alias = "maxPlayers", .kind = FieldKind::Size, .size = &Editable::maximumPlayers },
			{ .key = "worldMode", .documentKey = "worldMode", .alias = "world_mode", .legacy = "questMode", .legacyAlias = "quest_mode", .kind = FieldKind::Text, .text = &Editable::worldMode },
			{ .key = "vats", .documentKey = "vats", .alias = "vats_mode", .kind = FieldKind::Text, .text = &Editable::vats },
			{ .key = "doorsShared", .documentKey = "doorsShared", .alias = "doors_shared", .kind = FieldKind::Flag, .flag = &Editable::doorsShared },
			{ .key = "friendlyFire", .documentKey = "friendly_fire", .alias = "friendlyFire", .kind = FieldKind::Flag, .flag = &Editable::friendlyFire },
			{ .key = "factionFriendlyFire", .documentKey = "faction_friendly_fire", .alias = "factionFriendlyFire", .kind = FieldKind::Flag, .flag = &Editable::factionFriendlyFire },
			{ .key = "downedSeconds", .documentKey = "downed_seconds", .alias = "downedSeconds", .kind = FieldKind::Count, .count = &Editable::downedSeconds },
			{ .key = "enableChat", .documentKey = "enable_chat", .alias = "enableChat", .kind = FieldKind::Flag, .flag = &Editable::enableChat },
			{ .key = "enableFactions", .documentKey = "enable_factions", .alias = "enableFactions", .kind = FieldKind::Flag, .flag = &Editable::enableFactions },
			{ .key = "enablePersistence", .documentKey = "enable_persistence", .alias = "enablePersistence", .kind = FieldKind::Flag, .flag = &Editable::enablePersistence },
			{ .key = "enableObjectState", .documentKey = "enable_object_state", .alias = "enableObjectState", .kind = FieldKind::Flag, .flag = &Editable::enableObjectState },
			{ .key = "lanDiscoveryEnabled", .documentKey = "lan_discovery_enabled", .alias = "lanDiscoveryEnabled", .kind = FieldKind::Flag, .flag = &Editable::enableLanDiscovery },
			{ .key = "publicListing", .documentKey = "public_listing", .alias = "publicListing", .kind = FieldKind::Flag, .flag = &Editable::publicListing },
			{ .key = "resumeTokenLifetimeHours", .documentKey = "resume_token_lifetime_hours", .alias = "resumeTokenLifetimeHours", .kind = FieldKind::Count, .count = &Editable::resumeTokenLifetimeHours }
		};

		constexpr std::string_view kPasswordKeys[]{ "server_password", "serverPassword" };

		void EraseSpellings(Json& a_root, const Field& a_field)
		{
			for (const auto spelling : { a_field.documentKey, a_field.alias, a_field.legacy, a_field.legacyAlias }) {
				if (!spelling.empty()) {
					a_root.erase(std::string{ spelling });
				}
			}
		}

		void ErasePassword(Json& a_root)
		{
			for (const auto key : kPasswordKeys) {
				a_root.erase(std::string{ key });
			}
		}

		std::string DeclaredWorldMode(const Json& a_root)
		{
			for (const auto key : { "worldMode", "world_mode", "questMode", "quest_mode" }) {
				const auto found = a_root.find(key);
				if (found != a_root.end() && found->is_string()) {
					return found->get<std::string>();
				}
			}
			return {};
		}

		std::string CanonicalMode(std::string_view a_mode)
		{
			const auto parsed = F4T::WorldPolicy::ParseMode(a_mode);
			return parsed ? std::string{ F4T::WorldPolicy::ModeName(*parsed) } : std::string{ a_mode };
		}

		std::string CanonicalVats(std::string_view a_vats)
		{
			const auto parsed = F4T::WorldPolicy::ParseVats(a_vats);
			return parsed ? std::string{ F4T::WorldPolicy::VatsName(*parsed) } : std::string{ a_vats };
		}

		std::string UnchangedMessage(const std::string& a_reason, std::string_view a_tail)
		{
			return a_reason + " Nothing was changed, " + std::string{ a_tail };
		}

		std::vector<std::string> AppliedFields(const nlohmann::json& a_result)
		{
			std::vector<std::string> fields;
			if (!a_result.is_object()) {
				return fields;
			}
			const nlohmann::json* source = &a_result;
			if (const auto data = a_result.find("data"); data != a_result.end() && data->is_object()) {
				source = &*data;
			}
			const auto found = source->find("applied");
			if (found == source->end() || !found->is_array()) {
				return fields;
			}
			for (const auto& field : *found) {
				if (field.is_string()) {
					fields.push_back(field.get<std::string>());
				}
			}
			return fields;
		}

		std::string JoinedFields(const std::vector<std::string>& a_fields)
		{
			std::string joined;
			for (const auto& field : a_fields) {
				if (!joined.empty()) {
					joined += ", ";
				}
				joined += field;
			}
			return joined;
		}

		void RevertLiveSettings(
			const LiveSettingsApplier& a_liveSettings,
			const nlohmann::json& a_baselineDocument,
			std::vector<std::string>& a_errors)
		{
			if (!a_liveSettings) {
				return;
			}
			if (a_baselineDocument.is_null() || a_baselineDocument.empty()) {
				a_errors.push_back(
					"The running server had already applied part of the change and there was no previous settings "
					"document to put back, so its live settings are left as they are.");
				return;
			}
			nlohmann::json revertResult;
			std::string revertError;
			if (a_liveSettings(a_baselineDocument, revertResult, revertError) != LiveApplyResult::Applied) {
				a_errors.push_back(
					"The running server keeps the settings it was just given; "
					"the file on disk was left as it was.");
			}
		}

		std::string TypeErrorMessage(std::string_view a_key, std::string_view a_expected)
		{
			return "Setting '" + std::string{ a_key } + "' must be " + std::string{ a_expected } + ".";
		}

		void WriteField(Json& a_target, const Field& a_field, const Editable& a_values, std::string_view a_key)
		{
			switch (a_field.kind) {
			case FieldKind::Text:
				a_target[std::string{ a_key }] = a_values.*(a_field.text);
				break;
			case FieldKind::Flag:
				a_target[std::string{ a_key }] = a_values.*(a_field.flag);
				break;
			case FieldKind::Count:
				a_target[std::string{ a_key }] = a_values.*(a_field.count);
				break;
			case FieldKind::Size:
				a_target[std::string{ a_key }] = a_values.*(a_field.size);
				break;
			}
		}

		bool ReadField(const Json& a_source, const Field& a_field, Editable& a_values, std::string& a_error)
		{
			const auto found = a_source.find(std::string{ a_field.key });
			switch (a_field.kind) {
			case FieldKind::Text:
				if (found == a_source.end() || !found->is_string()) {
					a_error = TypeErrorMessage(a_field.key, "text");
					return false;
				}
				a_values.*(a_field.text) = found->get<std::string>();
				return true;
			case FieldKind::Flag:
				if (found == a_source.end() || !found->is_boolean()) {
					a_error = TypeErrorMessage(a_field.key, "true or false");
					return false;
				}
				a_values.*(a_field.flag) = found->get<bool>();
				return true;
			case FieldKind::Count:
			case FieldKind::Size: {
				if (found == a_source.end() || !found->is_number_integer()) {
					a_error = TypeErrorMessage(a_field.key, "a whole number");
					return false;
				}
				const auto value = found->get<std::int64_t>();
				if (value < 0 || value > static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())) {
					a_error = "Setting '" + std::string{ a_field.key } + "' is out of range.";
					return false;
				}
				if (a_field.kind == FieldKind::Count) {
					a_values.*(a_field.count) = static_cast<std::uint32_t>(value);
				} else {
					a_values.*(a_field.size) = static_cast<std::size_t>(value);
				}
				return true;
			}
			}
			return false;
		}

		template <typename Visit>
		bool WalkFields(Visit&& a_visit)
		{
			for (const auto& field : kFields) {
				if (!a_visit(field)) {
					return false;
				}
			}
			return true;
		}
	}

	Editable ReadEditable(const ServerConfiguration::Options& a_options)
	{
		return Editable{
			.serverName = a_options.serverName,
			.serverDescription = a_options.serverDescription,
			.serverPassword = a_options.serverPassword,
			.passwordUpdate = PasswordUpdate::Keep,
			.maximumPlayers = a_options.maximumPlayers,
			.worldMode = CanonicalMode(a_options.worldMode),
			.vats = CanonicalVats(a_options.vats),
			.doorsShared = a_options.doorsShared,
			.friendlyFire = a_options.friendlyFire,
			.factionFriendlyFire = a_options.factionFriendlyFire,
			.downedSeconds = a_options.downedSeconds,
			.enableChat = a_options.enableChat,
			.enableFactions = a_options.enableFactions,
			.enablePersistence = a_options.enablePersistence,
			.enableObjectState = a_options.enableObjectState,
			.enableLanDiscovery = a_options.enableLanDiscovery,
			.publicListing = a_options.publicListing,
			.resumeTokenLifetimeHours = a_options.resumeTokenLifetimeHours
		};
	}

	Json ReadDocument(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Could not open server configuration '" + F4T::Utf8Path::Encode(a_path) + "'.");
		}
		Json root;
		try {
			input >> root;
		} catch (const Json::exception& exception) {
			throw std::runtime_error("Server configuration is not valid JSON: " + std::string{ exception.what() });
		}
		if (!root.is_object()) {
			throw std::runtime_error("Server configuration root must be a JSON object.");
		}
		return root;
	}

	Json ApplyEditable(Json a_root, const Editable& a_values)
	{
		if (!a_root.is_object()) {
			a_root = Json::object();
		}
		WalkFields([&](const Field& field) {
			EraseSpellings(a_root, field);
			WriteField(a_root, field, a_values, field.documentKey);
			return true;
		});

		if (a_values.passwordUpdate == PasswordUpdate::Clear) {
			ErasePassword(a_root);
		} else if (a_values.passwordUpdate == PasswordUpdate::Set) {
			ErasePassword(a_root);
			if (a_values.serverPassword) {
				a_root[std::string{ kPasswordKeys[0] }] = *a_values.serverPassword;
			}
		}
		return a_root;
	}

	Json ToPayload(const Editable& a_values, bool a_passwordConfigured)
	{
		Json payload = Json::object();
		WalkFields([&](const Field& field) {
			WriteField(payload, field, a_values, field.key);
			return true;
		});
		payload["passwordConfigured"] = a_passwordConfigured;
		return payload;
	}

	bool FromPayload(const Json& a_payload, Editable& a_values, std::string& a_error)
	{
		if (!a_payload.is_object()) {
			a_error = "Server settings must be an object.";
			return false;
		}
		return WalkFields([&](const Field& field) { return ReadField(a_payload, field, a_values, a_error); });
	}

	WorldModeVerdict JudgeWorldMode(
		const ServerConfiguration::Options& a_options,
		const std::filesystem::path& a_baseDirectory)
	{
		WorldModeVerdict verdict{ .mode = CanonicalMode(a_options.worldMode) };
		verdict.policyPath = F4T::Utf8Path::Decode(a_options.worldPolicyPath);
		if (verdict.policyPath.is_relative()) {
			verdict.policyPath = a_baseDirectory / verdict.policyPath;
		}
		const auto mode = F4T::WorldPolicy::ParseMode(verdict.mode);
		verdict.refusal = mode ? F4T::WorldPolicy::UnservableModeError(verdict.policyPath, *mode) :
			std::string{ "That world mode is not a server mode name." };
		return verdict;
	}

	SavePlan PrepareSave(const std::filesystem::path& a_path, const Editable& a_values)
	{
		Json root = Json::object();
		try {
			if (std::filesystem::exists(a_path)) {
				root = ReadDocument(a_path);
			}
		} catch (const std::exception& exception) {
			return SavePlan{ .errors = { exception.what() } };
		}

		SavePlan plan;
		plan.previousDocument = root;
		plan.previousWorldMode = CanonicalMode(DeclaredWorldMode(root));
		try {
			plan.document = ApplyEditable(std::move(root), a_values);
			const auto options = ServerConfiguration::Parse(plan.document, a_path.parent_path());
			const auto verdict = JudgeWorldMode(options, a_path.parent_path());
			plan.worldMode = verdict.mode;
			plan.worldPolicyPath = verdict.policyPath;
			plan.worldModeRefusal = verdict.refusal;
			plan.errors = ServerConfiguration::Validate(options);
			if (const auto modpackError = ServerConfiguration::RequiredModpackError(plan.document, a_path.parent_path())) {
				plan.errors.push_back(*modpackError);
			}
		} catch (const std::exception& exception) {
			plan.errors = { exception.what() };
		}
		return plan;
	}

	SaveOutcome CommitSave(const std::filesystem::path& a_path, const SavePlan& a_plan)
	{
		if (!a_plan.Ready()) {
			return SaveOutcome{ .saved = false, .errors = a_plan.errors };
		}
		try {
			WriteDocument(a_path, a_plan.document);
		} catch (const std::exception& exception) {
			return SaveOutcome{ .saved = false, .errors = { exception.what() } };
		}
		return SaveOutcome{ .saved = true, .errors = {} };
	}

	SaveOutcome SaveSettings(
		const std::filesystem::path& a_path,
		const Editable& a_values,
		const LiveSettingsApplier& a_liveSettings,
		const nlohmann::json& a_runningDocument,
		const DocumentAdjuster& a_adjustDocument)
	{
		auto plan = PrepareSave(a_path, a_values);
		if (!plan.Ready()) {
			return SaveOutcome{ .saved = false, .liveApplied = false, .errors = plan.errors };
		}

		if (!plan.worldModeRefusal.empty()) {
			return SaveOutcome{
				.saved = false,
				.liveApplied = false,
				.errors = { UnchangedMessage(plan.worldModeRefusal, "so the file on disk was left as it was.") }
			};
		}

		if (a_adjustDocument) {
			a_adjustDocument(plan.document);
		}

		bool liveApplied = false;
		nlohmann::json liveResult = nullptr;
		if (a_liveSettings) {
			std::string liveError;
			switch (a_liveSettings(plan.document, liveResult, liveError)) {
			case LiveApplyResult::Applied:
				liveApplied = true;
				break;
			case LiveApplyResult::Refused: {
				std::vector<std::string> errors;
				bool revertedCleanly = false;
				const auto applied = AppliedFields(liveResult);
				if (!applied.empty()) {
					const auto& baseline = !a_runningDocument.is_null() && a_runningDocument.is_object() && !a_runningDocument.empty() ?
						a_runningDocument : plan.previousDocument;
					const bool canRevert = a_liveSettings && !baseline.is_null() && !baseline.empty();
					if (canRevert) {
						RevertLiveSettings(a_liveSettings, baseline, errors);
						revertedCleanly = errors.empty();
					}
					if (!canRevert || !errors.empty()) {
						errors.insert(errors.begin(), liveError);
						errors.push_back(
							canRevert ? "The running server had already applied " + JoinedFields(applied) +
									"; putting its previous settings back failed, so the file on disk was left as it was." :
								"The running server had already applied " + JoinedFields(applied) +
									"; there was no previous settings document to put back, so the file on disk was left as it was.");
					}
				}
				if (errors.empty()) {
					errors.push_back(UnchangedMessage(
						liveError,
						revertedCleanly ? "so the file on disk was left as it was and the running server was asked to restore its previous settings." :
							"so the file still matches the running server."));
				}
				return SaveOutcome{
					.saved = false,
					.liveApplied = false,
					.liveResult = std::move(liveResult),
					.errors = std::move(errors)
				};
			}
			case LiveApplyResult::Unavailable:
				break;
			}
		}

		auto outcome = CommitSave(a_path, plan);
		if (!outcome.saved) {
			if (liveApplied) {
				RevertLiveSettings(
					a_liveSettings,
					!a_runningDocument.is_null() && a_runningDocument.is_object() && !a_runningDocument.empty() ? a_runningDocument : plan.previousDocument,
					outcome.errors);
			}
			return outcome;
		}
		outcome.liveApplied = liveApplied;
		outcome.liveResult = std::move(liveResult);
		return outcome;
	}

	void WriteDocument(const std::filesystem::path& a_path, const nlohmann::json& a_root)
	{
		auto temporary = a_path;
		temporary += ".tmp";
		const auto text = a_root.dump(2) + "\n";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output) {
				throw std::runtime_error("Could not write server configuration '" + F4T::Utf8Path::Encode(temporary) + "'.");
			}
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			output.flush();
			if (!output) {
				throw std::runtime_error("Could not write server configuration '" + F4T::Utf8Path::Encode(temporary) + "'.");
			}
		}
		std::error_code error;
		std::filesystem::rename(temporary, a_path, error);
		if (error) {
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			throw std::runtime_error("Could not replace server configuration '" + F4T::Utf8Path::Encode(a_path) + "': " + error.message());
		}
	}
}
