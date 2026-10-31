#include "pch.h"

#include "F4TServerSettingsBridge.h"

#include "F4TServerBrowserBridgeInternal.h"
#include "F4TServerConfiguration.h"
#include "F4TServerLive.h"
#include "F4TServerSettingsFile.h"
#include "F4TUserPaths.h"
#include "F4TUtf8Path.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::ServerSettings
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr const char* kConfigFileName = "commonwealth-server.json";
		constexpr const char* kServerExecutableName = "CommonwealthOnline.Server.exe";
		constexpr const char* kLogPrefix = "[COUI] [ServerSettings]";
		constexpr std::size_t kMaximumStoredPathLength = 1024;

		std::filesystem::path StorePath()
		{
			const auto documents = F4T::UserPaths::CommonwealthOnlineDocumentsDirectory();
			if (documents.empty()) {
				return {};
			}
			return documents / "host-server-folder.json";
		}

		std::filesystem::path LocalAppDataServerDirectory()
		{
			const auto localAppData = F4T::UserPaths::EnvironmentValue("LOCALAPPDATA");
			if (localAppData.empty()) {
				return {};
			}
			return std::filesystem::path{ localAppData } / "CommonwealthOnline" / "Server";
		}

		bool LooksLikeServerPackage(const std::filesystem::path& a_directory)
		{
			std::error_code error;
			return std::filesystem::exists(a_directory / kConfigFileName, error) ||
				std::filesystem::exists(a_directory / kServerExecutableName, error);
		}

		std::filesystem::path ReadStoredDirectory()
		{
			const auto path = StorePath();
			if (path.empty()) {
				return {};
			}
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				return {};
			}
			Json root;
			try {
				input >> root;
			} catch (const Json::exception&) {
				return {};
			}
			if (!root.is_object()) {
				return {};
			}
			const auto found = root.find("serverDirectory");
			if (found == root.end() || !found->is_string()) {
				return {};
			}
			const auto value = found->get<std::string>();
			if (value.empty() || value.size() > kMaximumStoredPathLength) {
				return {};
			}
			return F4T::Utf8Path::Decode(value);
		}

		void WriteStoredDirectory(const std::filesystem::path& a_directory)
		{
			const auto path = StorePath();
			if (path.empty()) {
				return;
			}
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output) {
				REX::WARN("{} Could not remember the server folder", kLogPrefix);
				return;
			}
			output << Json{ { "serverDirectory", F4T::Utf8Path::Encode(a_directory) } }.dump(2) << '\n';
		}

		std::filesystem::path RememberedDirectory()
		{
			const auto stored = ReadStoredDirectory();
			if (!stored.empty() && LooksLikeServerPackage(stored)) {
				return stored;
			}
			const auto packaged = F4T::UserPaths::PackagedServerDirectory();
			if (!packaged.empty() && LooksLikeServerPackage(packaged)) {
				return packaged;
			}
			const auto localAppData = LocalAppDataServerDirectory();
			if (!localAppData.empty() && LooksLikeServerPackage(localAppData)) {
				return localAppData;
			}
			return stored;
		}

		struct RunningServerConfig
		{
			std::optional<std::filesystem::path> directory;
			Json document;
			bool linkFailed{};
			std::string error;
		};

		RunningServerConfig QueryRunningServer();

		std::filesystem::path ChosenDirectory(const Json& a_request, const RunningServerConfig& a_running, std::string& a_error)
		{
			const auto requested = a_request.value("serverDirectory", std::string{});
			if (a_running.linkFailed) {
				a_error = a_running.error.empty() ?
					"The running server did not answer, so the panel did not touch the settings file." : a_running.error;
				return {};
			}
			if (a_running.directory && !a_running.directory->empty()) {
				if (!requested.empty() && F4T::Utf8Path::Encode(F4T::Utf8Path::Decode(requested)) != F4T::Utf8Path::Encode(*a_running.directory)) {
					a_error = "The running server reads " + F4T::Utf8Path::Encode(*a_running.directory / kConfigFileName) +
						". Stop hosting to edit another folder.";
					return {};
				}
				return *a_running.directory;
			}
			if (!requested.empty()) {
				if (requested.size() > kMaximumStoredPathLength) {
					a_error = "That server folder path is too long.";
					return {};
				}
				const auto directory = F4T::Utf8Path::Decode(requested);
				std::error_code error;
				if (!std::filesystem::is_directory(directory, error)) {
					a_error = "That server folder does not exist.";
					return {};
				}
				if (!LooksLikeServerPackage(directory)) {
					a_error = "That folder has no commonwealth-server.json or CommonwealthOnline.Server.exe.";
					return {};
				}
				return directory;
			}
			const auto directory = RememberedDirectory();
			if (directory.empty()) {
				a_error = "Set the server folder that holds CommonwealthOnline.Server.exe.";
			}
			return directory;
		}

		Json LimitsJson()
		{
			return Json{
				{ "maximumPlayers", F4T::RemotePlayerCapacity::MaximumSessionPlayers },
				{ "worldModes", Json::array({ "coop-story", "free-roam" }) },
				{ "vatsModes", Json::array({ "disabled", "real-time", "vanilla" }) }
			};
		}

		LiveApplyResult ApplyLiveSettings(const Json& a_document, Json& a_result, std::string& a_error)
		{
			switch (F4T::ServerLive::ApplySettings(
				Json{ { "action", "settings" }, { "document", a_document } }, a_result, a_error)) {
			case F4T::ServerLive::LiveApply::Applied:
				return LiveApplyResult::Applied;
			case F4T::ServerLive::LiveApply::Refused:
				return LiveApplyResult::Refused;
			case F4T::ServerLive::LiveApply::Unavailable:
				break;
			}
			return LiveApplyResult::Unavailable;
		}

		RunningServerConfig QueryRunningServer()
		{
			RunningServerConfig running;
			Json reply;
			switch (F4T::ServerLive::QueryRunningConfig(reply, running.error)) {
			case F4T::ServerLive::LiveApply::Applied:
				break;
			case F4T::ServerLive::LiveApply::Unavailable:
				running.error.clear();
				return running;
			case F4T::ServerLive::LiveApply::Refused:
				running.linkFailed = true;
				if (running.error.empty()) {
					running.error = "The running server did not answer the settings link, so the panel did not touch the file.";
				}
				return running;
			}
			const auto data = reply.find("data");
			if (data == reply.end() || !data->is_object()) {
				return running;
			}
			if (const auto document = data->find("document"); document != data->end() && document->is_object()) {
				running.document = *document;
			}
			const auto configPath = data->find("configPath");
			if (configPath == data->end() || !configPath->is_string()) {
				return running;
			}
			const auto path = F4T::Utf8Path::Decode(configPath->get<std::string>()).parent_path();
			if (!path.empty()) {
				running.directory = path;
			}
			return running;
		}

		void Reply(const Json& a_request, Json a_data, bool a_success, std::vector<std::string> a_errors)
		{
			Json event{
				{ "type", "serverSettingsResult" },
				{ "requestId", a_request.value("requestId", "") },
				{ "action", a_request.value("action", "") },
				{ "success", a_success }
			};
			if (!a_data.is_null()) {
				event["data"] = std::move(a_data);
			}
			if (!a_errors.empty()) {
				event["errors"] = std::move(a_errors);
			}
			F4T::ServerBrowser::detail::DispatchEvent(event);
		}

		Json LoadPayload(const std::filesystem::path& a_directory, std::string_view a_folderError)
		{
			Json data{
				{ "serverDirectory", a_directory.empty() ? std::string{} : F4T::Utf8Path::Encode(a_directory) },
				{ "configPath", a_directory.empty() ? std::string{} : F4T::Utf8Path::Encode(a_directory / kConfigFileName) },
				{ "limits", LimitsJson() }
			};

			ServerConfiguration::Options options;
			bool readable = true;
			std::string worldModeRefusal;
			Json errors = Json::array();
			if (!a_folderError.empty()) {
				errors.push_back(std::string{ a_folderError });
			}
			if (!a_directory.empty()) {
				const auto configPath = a_directory / kConfigFileName;
				if (std::filesystem::exists(configPath)) {
					try {
						const auto document = ReadDocument(configPath);
						options = ServerConfiguration::Parse(document, a_directory);
						for (const auto& error : ServerConfiguration::Validate(options)) {
							errors.push_back(error);
						}
						if (const auto modpackError = ServerConfiguration::RequiredModpackError(document, a_directory)) {
							errors.push_back(*modpackError);
						}
						worldModeRefusal = JudgeWorldMode(options, a_directory).refusal;
					} catch (const std::exception& exception) {
						errors.push_back(exception.what());
						readable = false;
					}
				}
			}
			data["errors"] = std::move(errors);
			data["worldModeRefusal"] = std::move(worldModeRefusal);
			if (readable) {
				data["values"] = ToPayload(ReadEditable(options), options.serverPassword.has_value());
			} else {
				data["readable"] = false;
			}
			return data;
		}

		void ProcessGet(const Json& a_request)
		{
			std::string folderError;
			const auto directory = ChosenDirectory(a_request, QueryRunningServer(), folderError);
			Reply(a_request, LoadPayload(directory, folderError), true, {});
		}

		void ProcessSave(const Json& a_request)
		{
			const auto running = QueryRunningServer();
			std::string folderError;
			const auto directory = ChosenDirectory(a_request, running, folderError);
			if (directory.empty()) {
				Reply(a_request, Json(), false, { folderError.empty() ? std::string{ "Set the server folder." } : folderError });
				return;
			}

			Editable values;
			std::string readError;
			if (!FromPayload(a_request.value("values", Json::object()), values, readError)) {
				Reply(a_request, Json(), false, { readError });
				return;
			}

			const auto passwordMode = a_request.value("passwordMode", std::string{ "keep" });
			if (passwordMode == "set") {
				values.passwordUpdate = PasswordUpdate::Set;
				values.serverPassword = a_request.value("password", std::string{});
			} else if (passwordMode == "clear") {
				values.passwordUpdate = PasswordUpdate::Clear;
			} else if (passwordMode != "keep") {
				Reply(a_request, Json(), false, { "Unknown password mode." });
				return;
			}

			const auto configPath = directory / kConfigFileName;
			const auto outcome = SaveSettings(configPath, values, &ApplyLiveSettings, running.document);
			if (!outcome.saved) {
				Reply(a_request, Json(), false, outcome.errors);
				return;
			}
			try {
				WriteStoredDirectory(directory);
			} catch (const std::exception& exception) {
				REX::WARN("{} Could not remember the server folder: {}", kLogPrefix, exception.what());
			}
			REX::INFO("{} Saved {}", kLogPrefix, F4T::Utf8Path::Encode(configPath));
			auto payload = LoadPayload(directory, {});
			payload["liveApplied"] = outcome.liveApplied;
			payload["liveResult"] = outcome.liveResult.is_object() && outcome.liveResult.contains("data") ?
				outcome.liveResult["data"] : outcome.liveResult;
			Reply(a_request, std::move(payload), true, {});
		}

		void ProcessAction(const Json& a_action)
		{
			const auto action = a_action.value("action", "");
			try {
				if (action == "serverSettingsGet") {
					ProcessGet(a_action);
					return;
				}
				if (action == "serverSettingsSave") {
					ProcessSave(a_action);
					return;
				}
				Reply(a_action, Json(), false, { "Unknown server settings action '" + action + "'." });
			} catch (const std::exception& exception) {
				REX::WARN("{} Failed {}: {}", kLogPrefix, action, exception.what());
				Reply(a_action, Json(), false, { exception.what() });
			}
		}
	}

	void HandleAction(const nlohmann::json& a_action)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (tasks == nullptr) {
			Reply(a_action, Json(), false, { "The game task interface is unavailable." });
			return;
		}
		tasks->AddTask([action = a_action]() { ProcessAction(action); });
	}
}
