#pragma once

#include "F4TServerConfiguration.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ServerSettings
{
	enum class PasswordUpdate
	{
		Keep,
		Set,
		Clear
	};

	struct Editable
	{
		std::string serverName;
		std::string serverDescription;
		std::optional<std::string> serverPassword;
		PasswordUpdate passwordUpdate{ PasswordUpdate::Keep };
		std::size_t maximumPlayers{};
		std::string worldMode;
		std::string vats;
		bool doorsShared{ true };
		bool friendlyFire{ true };
		bool factionFriendlyFire{};
		std::uint32_t downedSeconds{};
		bool enableChat{ true };
		bool enableFactions{ true };
		bool enablePersistence{ true };
		bool enableObjectState{ true };
		bool enableLanDiscovery{ true };
		bool publicListing{};
		std::uint32_t resumeTokenLifetimeHours{};
	};

	struct SaveOutcome
	{
		bool saved{ false };
		bool liveApplied{ false };
		nlohmann::json liveResult{ nullptr };
		std::vector<std::string> errors;
	};

	struct WorldModeVerdict
	{
		std::string mode;
		std::filesystem::path policyPath;
		std::string refusal;

		[[nodiscard]] bool Servable() const { return refusal.empty(); }
	};

	struct SavePlan
	{
		nlohmann::json document;
		nlohmann::json previousDocument;
		std::string worldMode;
		std::string previousWorldMode;
		std::filesystem::path worldPolicyPath;
		std::string worldModeRefusal;
		std::vector<std::string> errors;

		[[nodiscard]] bool Ready() const { return errors.empty(); }
		[[nodiscard]] bool ChangesWorldMode() const { return Ready() && worldMode != previousWorldMode; }
	};

	enum class LiveApplyResult
	{
		Applied,
		Unavailable,
		Refused
	};

	using LiveSettingsApplier = std::function<LiveApplyResult(
		const nlohmann::json& a_document,
		nlohmann::json& a_result,
		std::string& a_error)>;

	[[nodiscard]] Editable ReadEditable(const ServerConfiguration::Options& a_options);
	[[nodiscard]] nlohmann::json ReadDocument(const std::filesystem::path& a_path);
	[[nodiscard]] nlohmann::json ApplyEditable(nlohmann::json a_root, const Editable& a_values);
	[[nodiscard]] nlohmann::json ToPayload(const Editable& a_values, bool a_passwordConfigured);
	[[nodiscard]] bool FromPayload(const nlohmann::json& a_payload, Editable& a_values, std::string& a_error);
	[[nodiscard]] WorldModeVerdict JudgeWorldMode(
		const ServerConfiguration::Options& a_options,
		const std::filesystem::path& a_baseDirectory);
	[[nodiscard]] SavePlan PrepareSave(const std::filesystem::path& a_path, const Editable& a_values);
	[[nodiscard]] SaveOutcome CommitSave(const std::filesystem::path& a_path, const SavePlan& a_plan);
	[[nodiscard]] SaveOutcome SaveSettings(
		const std::filesystem::path& a_path,
		const Editable& a_values,
		const LiveSettingsApplier& a_liveSettings,
		const nlohmann::json& a_runningDocument = nullptr);
	void WriteDocument(const std::filesystem::path& a_path, const nlohmann::json& a_root);
}
