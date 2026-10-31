#pragma once

#include "F4TIdentityProtocol.h"
#include "F4TSaveProfileLogic.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace F4T::DurableState
{
	inline constexpr std::uint32_t CurrentSchemaVersion = 4;
	inline constexpr std::size_t MaximumSaveAttestationsPerProfile = 64;
	inline constexpr std::size_t ProfileSecretLength = 32;

	struct LoadResult
	{
		nlohmann::json root;
		bool migrated{};
	};

	[[nodiscard]] nlohmann::json NormalizeStartingInventory(const nlohmann::json& a_catalog);
	void ValidateInventory(const nlohmann::json& a_inventory);
	[[nodiscard]] bool InitializeInventory(nlohmann::json& a_root, std::string_view a_profileId,
		const nlohmann::json& a_catalog, double a_updatedAt);
	[[nodiscard]] std::optional<nlohmann::json> GetInventory(const nlohmann::json& a_root, std::string_view a_profileId);

	[[nodiscard]] nlohmann::json NewRoot(std::string_view a_serverInstanceId, double a_savedAt);
	[[nodiscard]] LoadResult LoadAndMigrate(nlohmann::json a_root, double a_savedAt);
	void ValidateRoot(const nlohmann::json& a_root);

	[[nodiscard]] std::optional<F4T::Identity::PersistedResumeCredential> GetResumeCredential(
		const nlohmann::json& a_root,
		std::string_view a_profileId);
	void SaveResumeCredential(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const F4T::Identity::TokenHash& a_tokenHash,
		const std::optional<F4T::Identity::TokenHash>& a_previousTokenHash,
		bool a_confirmed,
		double a_expiresAt,
		double a_updatedAt);
	[[nodiscard]] bool DeleteResumeCredential(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		double a_updatedAt);

	[[nodiscard]] bool CarriesDurablePlayerState(const nlohmann::json& a_playerState);

	[[nodiscard]] std::optional<std::string> GetProfileSecret(
		const nlohmann::json& a_root,
		std::string_view a_profileId);
	[[nodiscard]] bool EnsureProfileSecret(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		std::string_view a_secret,
		double a_updatedAt);

	[[nodiscard]] std::vector<F4T::SaveProfileLogic::AttestedSave> GetSaveAttestations(
		const nlohmann::json& a_root,
		std::string_view a_profileId);
	void ValidateSaveAttestation(const F4T::SaveProfileLogic::AttestedSave& a_save);
	void RecordSaveAttestation(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save,
		double a_updatedAt);

	enum class PlayerStateMutationOutcome : std::uint8_t
	{
		NoDurableData,
		Unchanged,
		Changed
	};

	[[nodiscard]] std::optional<nlohmann::json> GetPlayerState(
		const nlohmann::json& a_root,
		std::string_view a_profileId);
	[[nodiscard]] PlayerStateMutationOutcome InspectPlayerStateUpdate(
		const nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState);
	[[nodiscard]] PlayerStateMutationOutcome UpdatePlayerState(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_updatedAt);

	[[nodiscard]] nlohmann::json GetWorldState(const nlohmann::json& a_root);
	void UpdateWorldState(nlohmann::json& a_root, const nlohmann::json& a_worldState);
}
