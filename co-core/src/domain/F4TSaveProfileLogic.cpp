#include "F4TSaveProfileLogic.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

namespace
{
	constexpr std::size_t kSaveTimestampDigits = 14;

	constexpr bool IsLeapYear(std::uint64_t a_year)
	{
		return (a_year % 4ULL == 0ULL && a_year % 100ULL != 0ULL) || a_year % 400ULL == 0ULL;
	}

	constexpr std::uint64_t DaysInMonth(std::uint64_t a_year, std::uint64_t a_month)
	{
		switch (a_month) {
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
			return 31;
		case 4:
		case 6:
		case 9:
		case 11:
			return 30;
		case 2:
			return IsLeapYear(a_year) ? 29 : 28;
		default:
			return 0;
		}
	}

	bool TryParseSaveTimestampToken(std::string_view a_token, std::uint64_t& a_timestamp)
	{
		if (a_token.size() != kSaveTimestampDigits) {
			return false;
		}

		std::uint64_t parsed = 0;
		for (const auto ch : a_token) {
			const auto digit = static_cast<unsigned char>(ch);
			if (std::isdigit(digit) == 0) {
				return false;
			}
			parsed = (parsed * 10) + static_cast<std::uint64_t>(digit - '0');
		}

		const auto year = (parsed / 10000000000ULL) % 10000ULL;
		const auto month = (parsed / 100000000ULL) % 100ULL;
		const auto day = (parsed / 1000000ULL) % 100ULL;
		const auto hour = (parsed / 10000ULL) % 100ULL;
		const auto minute = (parsed / 100ULL) % 100ULL;
		const auto second = parsed % 100ULL;
		const auto daysInMonth = DaysInMonth(year, month);
		if (year < 2000 || daysInMonth == 0 || day < 1 || day > daysInMonth ||
			hour > 23 || minute > 59 || second > 59) {
			return false;
		}

		a_timestamp = parsed;
		return true;
	}

	struct SaveCandidate
	{
		std::size_t index{ 0 };
		std::optional<std::uint64_t> timestamp;
	};

	bool IsPreferredSave(const SaveCandidate& a_candidate, const SaveCandidate& a_other)
	{
		if (a_candidate.timestamp && a_other.timestamp && *a_candidate.timestamp != *a_other.timestamp) {
			return *a_candidate.timestamp > *a_other.timestamp;
		}
		if (a_candidate.timestamp.has_value() != a_other.timestamp.has_value()) {
			return a_candidate.timestamp.has_value();
		}
		return a_candidate.index > a_other.index;
	}

	const F4T::SaveProfileLogic::AttestedSave* FindAttestedSave(
		std::span<const F4T::SaveProfileLogic::AttestedSave> a_attested,
		std::string_view a_saveFile)
	{
		for (const auto& entry : a_attested) {
			if (F4T::SaveProfileLogic::EqualsIgnoreCase(entry.saveFile, a_saveFile)) {
				return std::addressof(entry);
			}
		}
		return nullptr;
	}
}

namespace F4T::SaveProfileLogic
{
	std::optional<std::string> ExtractCharacterId(std::string_view a_saveFile)
	{
		const auto firstSeparator = a_saveFile.find('_');
		if (firstSeparator == std::string_view::npos) {
			return std::nullopt;
		}

		const auto secondSeparator = a_saveFile.find('_', firstSeparator + 1);
		if (secondSeparator == std::string_view::npos || secondSeparator == firstSeparator + 1) {
			return std::nullopt;
		}

		return std::string{ a_saveFile.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1) };
	}

	std::optional<std::uint64_t> ExtractSaveTimestamp(std::string_view a_saveFile)
	{
		std::optional<std::uint64_t> latest;

		std::size_t tokenStart = 0;
		while (tokenStart <= a_saveFile.size()) {
			auto tokenEnd = a_saveFile.find('_', tokenStart);
			if (tokenEnd == std::string_view::npos) {
				tokenEnd = a_saveFile.size();
			}

			std::uint64_t timestamp = 0;
			if (TryParseSaveTimestampToken(a_saveFile.substr(tokenStart, tokenEnd - tokenStart), timestamp)) {
				latest = timestamp;
			}

			tokenStart = tokenEnd + 1;
		}

		return latest;
	}

	bool EqualsIgnoreCase(std::string_view a_left, std::string_view a_right)
	{
		if (a_left.size() != a_right.size()) {
			return false;
		}

		for (std::size_t i = 0; i < a_left.size(); ++i) {
			const auto left = static_cast<unsigned char>(a_left[i]);
			const auto right = static_cast<unsigned char>(a_right[i]);
			if (std::tolower(left) != std::tolower(right)) {
				return false;
			}
		}

		return true;
	}

	bool CharacterIdsEqual(std::string_view a_left, std::string_view a_right)
	{
		return EqualsIgnoreCase(a_left, a_right);
	}

	bool CanRebindProfileSave(std::string_view a_activeCharacterId, std::string_view a_saveFile)
	{
		if (a_activeCharacterId.empty()) {
			return false;
		}

		const auto characterId = ExtractCharacterId(a_saveFile);
		return characterId && CharacterIdsEqual(a_activeCharacterId, *characterId);
	}

	std::optional<std::string> ResolveLatestProfileSave(
		std::string_view a_storedSaveFile,
		std::string_view a_storedCharacterId,
		std::span<const std::string> a_saveFiles)
	{
		std::string characterId;
		if (!a_storedCharacterId.empty()) {
			characterId = a_storedCharacterId;
		} else {
			const auto parsedCharacterId = ExtractCharacterId(a_storedSaveFile);
			if (!parsedCharacterId) {
				return std::nullopt;
			}
			characterId = *parsedCharacterId;
		}

		const std::string* best = nullptr;
		std::optional<std::uint64_t> bestTimestamp;

		for (const auto& saveFile : a_saveFiles) {
			if (!CanRebindProfileSave(characterId, saveFile)) {
				continue;
			}

			const auto timestamp = ExtractSaveTimestamp(saveFile);
			if (!best ||
				(timestamp && (!bestTimestamp || *timestamp >= *bestTimestamp)) ||
				(!timestamp && !bestTimestamp)) {
				best = std::addressof(saveFile);
				bestTimestamp = timestamp;
			}
		}

		if (!best) {
			return std::nullopt;
		}

		return *best;
	}

	AttestedResolution ResolveLatestAttestedSave(
		std::string_view a_characterId,
		std::span<const std::string> a_saveFiles,
		std::span<const AttestedSave> a_attested,
		const std::function<std::optional<std::string>(std::string_view)>& a_hashSaveFile)
	{
		std::vector<SaveCandidate> candidates;
		for (std::size_t index = 0; index < a_saveFiles.size(); ++index) {
			if (CanRebindProfileSave(a_characterId, a_saveFiles[index])) {
				candidates.push_back(SaveCandidate{ index, ExtractSaveTimestamp(a_saveFiles[index]) });
			}
		}
		if (candidates.empty()) {
			return {};
		}
		std::sort(candidates.begin(), candidates.end(), IsPreferredSave);

		AttestedResolution resolution;
		const std::string_view newestSaveFile = a_saveFiles[candidates.front().index];
		const auto newestHash = a_hashSaveFile(newestSaveFile);
		if (!newestHash) {
			return resolution;
		}
		const auto* newestAttested = FindAttestedSave(a_attested, newestSaveFile);
		if (!newestAttested) {
			resolution.verdict = LineageVerdict::Unattested;
		} else if (newestAttested->sha256Hex == *newestHash) {
			resolution.verdict = LineageVerdict::Attested;
		} else {
			resolution.verdict = LineageVerdict::Modified;
		}

		for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
			const auto& saveFile = a_saveFiles[candidates[rank].index];
			const auto* attested = FindAttestedSave(a_attested, saveFile);
			if (!attested) {
				continue;
			}
			const auto hash = rank == 0 ? newestHash : a_hashSaveFile(saveFile);
			if (hash && *hash == attested->sha256Hex) {
				resolution.saveFile = saveFile;
				break;
			}
		}

		return resolution;
	}

	bool SameAttestedSave(const AttestedSave& a_left, const AttestedSave& a_right)
	{
		return EqualsIgnoreCase(a_left.saveFile, a_right.saveFile) &&
		       EqualsIgnoreCase(a_left.sha256Hex, a_right.sha256Hex);
	}

	bool ContradictsRecordedSave(std::span<const AttestedSave> a_serverHistory, const AttestedSave& a_claim)
	{
		const auto* recorded = FindAttestedSave(a_serverHistory, a_claim.saveFile);
		return recorded && !SameAttestedSave(*recorded, a_claim);
	}

	SaveAdmission AdmitJoinSave(const AttestedResolution& a_resolution, bool a_isAdmin)
	{
		if (a_isAdmin || a_resolution.saveFile.has_value()) {
			return SaveAdmission{ true, {}, {} };
		}

		return SaveAdmission{
			false,
			std::string{ SaveNotMultiplayerCode },
			"Only Commonwealth Online multiplayer saves can join this server. Start a new multiplayer save for this "
			"server or load a multiplayer save that was created here."
		};
	}

	SaveAdmission AdmitAttestedSaveClaim(
		std::span<const AttestedSave> a_serverHistory,
		const std::optional<AttestedSave>& a_claim,
		SaveClaimStanding a_standing)
	{
		if (a_standing.isAdmin) {
			return SaveAdmission{ true, {}, {} };
		}
		if (a_claim && ContradictsRecordedSave(a_serverHistory, *a_claim)) {
			return SaveAdmission{
				false,
				std::string{ SaveNotMultiplayerCode },
				"This save name is already recorded for this profile with a different hash. Start a new multiplayer "
				"save for this server or load the multiplayer save that was recorded here."
			};
		}
		const bool recorded = a_claim.has_value() && std::any_of(
			a_serverHistory.begin(),
			a_serverHistory.end(),
			[&a_claim](const AttestedSave& a_entry) { return SameAttestedSave(a_entry, *a_claim); });
		const bool verified = a_standing.secretVerified;
		if (recorded && (verified || a_standing.claimReadmission)) {
			return SaveAdmission{ true, {}, {}, false, !verified };
		}
		if (!a_claim && (verified || a_serverHistory.empty())) {
			return SaveAdmission{ true, {}, {} };
		}
		if (a_claim && (verified || (a_serverHistory.empty() && !a_standing.profileKnown))) {
			return SaveAdmission{ true, {}, {}, true };
		}

		return SaveAdmission{
			false,
			std::string{ SaveNotMultiplayerCode },
			"This profile's save is not a Commonwealth Online multiplayer save for this server. Start a new "
			"multiplayer save for this server or load a multiplayer save that was created here."
		};
	}
}
