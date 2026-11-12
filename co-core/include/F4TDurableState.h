#pragma once

#include "F4TIdentityProtocol.h"
#include "F4TSaveProfileLogic.h"
#include "F4TSurvivalNeeds.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::DurableState
{
	inline constexpr std::uint32_t CurrentSchemaVersion = 6;
	inline constexpr std::size_t MaximumSaveAttestationsPerProfile = 64;
	inline constexpr std::size_t ProfileSecretLength = 32;
	inline constexpr std::size_t MaximumStartingInventoryItems = 2048;
	inline constexpr std::size_t MaximumStartingInventoryDumpBytes = 1024 * 1024;
	inline constexpr std::string_view kStartingGrantId = "starting-inventory";
	inline constexpr std::size_t kStartingGrantPartItems = 128;

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
	[[nodiscard]] std::optional<nlohmann::json> PendingStartingGrant(const nlohmann::json& a_root, std::string_view a_profileId);
	[[nodiscard]] std::optional<nlohmann::json> GetStartingGrant(const nlohmann::json& a_root, std::string_view a_profileId);
	[[nodiscard]] bool AckStartingGrant(nlohmann::json& a_root, std::string_view a_profileId, std::string_view a_grantId);
	[[nodiscard]] bool BeginStartingGrant(nlohmann::json& a_root, std::string_view a_profileId,
		std::string_view a_characterId, const F4T::SaveProfileLogic::AttestedSave& a_baseline);
	[[nodiscard]] bool ConsumeStartingGrantApproval(nlohmann::json& a_root, std::string_view a_profileId, const F4T::SaveProfileLogic::AttestedSave& a_save);
	[[nodiscard]] bool ResolveStartingGrant(nlohmann::json& a_root, std::string_view a_profileId,
		std::string_view a_characterId, const F4T::SaveProfileLogic::AttestedSave& a_save, std::string_view a_note, bool a_legacy);
	[[nodiscard]] bool HasCommittedFirstJoinState(const std::optional<nlohmann::json>& a_grant,
		const std::optional<nlohmann::json>& a_inventory, const std::optional<nlohmann::json>& a_playerState,
		bool a_saveAttested = true);

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
	[[nodiscard]] std::size_t SaveAttestationEvictionIndex(std::span<const std::optional<std::uint32_t>> a_slots);
	void RecordSaveAttestation(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save,
		double a_updatedAt);
	using SlotCharacterIds = std::vector<std::pair<std::uint32_t, std::string>>;
	[[nodiscard]] bool ScopeLegacySaveAttestations(
		nlohmann::json& a_root,
		std::string_view a_account,
		const SlotCharacterIds& a_slotCharacters,
		double a_updatedAt);
	[[nodiscard]] bool EraseSlotSaveAttestations(
		nlohmann::json& a_root,
		std::string_view a_account,
		std::uint32_t a_slot,
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
	[[nodiscard]] bool ProgressionRefused(
		const nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_nowSeconds);
	[[nodiscard]] PlayerStateMutationOutcome InspectPlayerStateUpdate(
		const nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_nowSeconds);
	[[nodiscard]] PlayerStateMutationOutcome UpdatePlayerState(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_updatedAt);

	[[nodiscard]] nlohmann::json GetWorldState(const nlohmann::json& a_root);
	void UpdateWorldState(nlohmann::json& a_root, const nlohmann::json& a_worldState);

	[[nodiscard]] F4T::SurvivalNeeds::Record GetSurvivalNeeds(const nlohmann::json& a_root, std::string_view a_profileId);
	void PutSurvivalNeeds(nlohmann::json& a_root, std::string_view a_profileId, F4T::SurvivalNeeds::Record a_record, double a_updatedAt);
	[[nodiscard]] F4T::SurvivalNeeds::Admission BeginSurvivalClaim(nlohmann::json& a_root, std::string_view a_profileId,
		F4T::SurvivalNeeds::Claim a_claim, std::uint32_t a_creditedDrops, double a_updatedAt);
	[[nodiscard]] F4T::SurvivalNeeds::Settled SettleSurvivalNeeds(nlohmann::json& a_root, std::string_view a_profileId, double a_now);
}
