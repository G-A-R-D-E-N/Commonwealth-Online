#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace F4T::SaveProfileLogic
{
	std::optional<std::string> ExtractCharacterId(std::string_view a_saveFile);
	std::optional<std::uint64_t> ExtractSaveTimestamp(std::string_view a_saveFile);
	bool CharacterIdsEqual(std::string_view a_left, std::string_view a_right);
	bool EqualsIgnoreCase(std::string_view a_left, std::string_view a_right);
	std::optional<std::string> ResolveLatestProfileSave(
		std::string_view a_storedSaveFile,
		std::string_view a_storedCharacterId,
		std::span<const std::string> a_saveFiles);
	bool CanRebindProfileSave(std::string_view a_activeCharacterId, std::string_view a_saveFile);

	struct AttestedSave
	{
		std::string saveFile;
		std::string sha256Hex;
		std::uint64_t attestedAtUnix{ 0 };
	};

	enum class LineageVerdict
	{
		Attested,
		Unattested,
		Modified,
		Unverifiable
	};

	struct AttestedResolution
	{
		std::optional<std::string> saveFile;
		LineageVerdict verdict{ LineageVerdict::Unverifiable };
	};

	AttestedResolution ResolveLatestAttestedSave(
		std::string_view a_characterId,
		std::span<const std::string> a_saveFiles,
		std::span<const AttestedSave> a_attested,
		const std::function<std::optional<std::string>(std::string_view)>& a_hashSaveFile);

	inline constexpr std::string_view SaveNotMultiplayerCode = "save_not_multiplayer";

	struct SaveAdmission
	{
		bool allowed{};
		std::string code;
		std::string reason;
		bool recordClaim{};
		bool readmitted{};
	};

	struct SaveClaimStanding
	{
		bool isAdmin{};
		bool secretVerified{};
		bool profileKnown{};
		bool claimReadmission{};
	};

	bool SameAttestedSave(const AttestedSave& a_left, const AttestedSave& a_right);
	bool ContradictsRecordedSave(std::span<const AttestedSave> a_serverHistory, const AttestedSave& a_claim);

	SaveAdmission AdmitJoinSave(const AttestedResolution& a_resolution, bool a_isAdmin);

	SaveAdmission AdmitAttestedSaveClaim(
		std::span<const AttestedSave> a_serverHistory,
		const std::optional<AttestedSave>& a_claim,
		SaveClaimStanding a_standing);
}
