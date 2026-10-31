#include "F4TSaveProfileLogic.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Hashes = std::map<std::string, std::string>;

	std::optional<std::string> HashFrom(const Hashes& a_hashes, std::string_view a_saveFile)
	{
		const auto found = a_hashes.find(std::string{ a_saveFile });
		if (found == a_hashes.end()) {
			return std::nullopt;
		}
		return found->second;
	}
}

int main()
{
	using namespace F4T::SaveProfileLogic;

	const std::vector<std::string> saves{
		"Quicksave0_AAAA1111_Nomad_Vault111Cryo_000001_20260814000101_1_2",
		"Autosave0_BBBB2222_Other_Commonwealth_000010_20260814000202_1_2",
		"Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2",
		"Exitsave0_BBBB2222_Other_Commonwealth_000030_20260814000404_1_2",
		"Exitsave0_AAAA1111_Nomad_Commonwealth_000040_20260814000505_1_2"
	};

	const auto characterId = ExtractCharacterId(saves.front());
	assert(characterId.has_value());
	assert(*characterId == "AAAA1111");
	assert(CharacterIdsEqual("aaaa1111", "AAAA1111"));
	assert(!CharacterIdsEqual("AAAA1111", "BBBB2222"));

	const auto legacyResolved = ResolveLatestProfileSave(saves.front(), "", saves);
	assert(legacyResolved.has_value());
	assert(*legacyResolved == saves.back());

	const auto persistedResolved = ResolveLatestProfileSave(saves.front(), "aaaa1111", saves);
	assert(persistedResolved.has_value());
	assert(*persistedResolved == saves.back());

	const auto persistedMalformedResolved = ResolveLatestProfileSave("malformed-save-name", "AAAA1111", saves);
	assert(persistedMalformedResolved.has_value());
	assert(*persistedMalformedResolved == saves.back());

	const auto otherResolved = ResolveLatestProfileSave(saves[1], "BBBB2222", saves);
	assert(otherResolved.has_value());
	assert(*otherResolved == saves[3]);

	assert(!ResolveLatestProfileSave("malformed-save-name", "", saves).has_value());
	assert(CanRebindProfileSave("aaaa1111", saves[2]));
	assert(!CanRebindProfileSave("AAAA1111", saves[1]));
	assert(!CanRebindProfileSave("AAAA1111", "malformed-save-name"));
	assert(!CanRebindProfileSave("", saves[2]));

	std::vector<std::string> reversedSaves{ saves.rbegin(), saves.rend() };
	assert(ResolveLatestProfileSave("", "AAAA1111", reversedSaves) == std::optional<std::string>{ saves.back() });
	assert(ResolveLatestProfileSave("", "BBBB2222", reversedSaves) == std::optional<std::string>{ saves[3] });

	const std::vector<std::string> shuffledSaves{ saves[2], saves[4], saves[0], saves[3], saves[1] };
	assert(ResolveLatestProfileSave("", "AAAA1111", shuffledSaves) == std::optional<std::string>{ saves.back() });

	assert(ExtractSaveTimestamp(saves.front()) == std::optional<std::uint64_t>{ 20260814000101ULL });
	assert(!ExtractSaveTimestamp("Quicksave0_AAAA1111_Nomad_Commonwealth_000001_1_2").has_value());
	assert(!ExtractSaveTimestamp("Quicksave0_AAAA1111_Nomad_99999999999999_1_2").has_value());
	assert(!ExtractSaveTimestamp("Save9_AAAA1111_Nomad_Commonwealth_20260229010203_1_2").has_value());
	assert(!ExtractSaveTimestamp("Save9_AAAA1111_Nomad_Commonwealth_20260230010203_1_2").has_value());
	assert(!ExtractSaveTimestamp("Save9_AAAA1111_Nomad_Commonwealth_20260431010203_1_2").has_value());
	assert(ExtractSaveTimestamp("Save9_AAAA1111_Nomad_Commonwealth_20240229010203_1_2") ==
		   std::optional<std::uint64_t>{ 20240229010203ULL });
	assert(ExtractSaveTimestamp("Save9_AAAA1111_No_mad_Sanctuary_Hills_000020_20260814010203_1_2") ==
		   std::optional<std::uint64_t>{ 20260814010203ULL });

	const std::vector<std::string> mixedSaves{
		"Save7_AAAA1111_Nomad_Commonwealth_000050_20260814000606_1_2",
		"Save8_AAAA1111_Nomad_Commonwealth"
	};
	assert(ResolveLatestProfileSave("", "AAAA1111", mixedSaves) == std::optional<std::string>{ mixedSaves.front() });
	const std::vector<std::string> mixedSavesReversed{ mixedSaves[1], mixedSaves[0] };
	assert(ResolveLatestProfileSave("", "AAAA1111", mixedSavesReversed) ==
		   std::optional<std::string>{ mixedSaves.front() });
	assert(ResolveLatestProfileSave("", "AAAA1111", { &mixedSaves[1], 1 }) ==
		   std::optional<std::string>{ mixedSaves[1] });

	const std::string lineageOlder = "Save3_AAAA1111_Nomad_Commonwealth_000020_20260814000303_1_2";
	const std::string lineageMiddle = "Save5_AAAA1111_Nomad_Commonwealth_000030_20260814000404_1_2";
	const std::string lineageNewest = "Exitsave0_AAAA1111_Nomad_Commonwealth_000040_20260814000505_1_2";
	const std::string lineageCopy = "Save9_AAAA1111_Nomad_Commonwealth_000050_20260814000707_1_2";
	const std::string lineageOffline = "Quicksave0_AAAA1111_Nomad_Commonwealth_000060_20260814000808_1_2";
	const std::string lineageOther = "Autosave0_BBBB2222_Other_Commonwealth_000010_20260814000606_1_2";
	const std::vector<AttestedSave> attestedAll{
		{ lineageOlder, "older-hash", 1 },
		{ lineageMiddle, "middle-hash", 2 },
		{ lineageNewest, "newest-hash", 3 },
		{ lineageOther, "other-hash", 4 }
	};
	const Hashes currentHashes{
		{ lineageOlder, "older-hash" },
		{ lineageMiddle, "middle-hash" },
		{ lineageNewest, "newest-hash" },
		{ lineageOffline, "offline-hash" },
		{ lineageCopy, "middle-hash" },
		{ lineageOther, "other-hash" }
	};
	const auto hashCurrent = [&currentHashes](std::string_view a_saveFile) { return HashFrom(currentHashes, a_saveFile); };

	const std::vector<std::string> attestedLatestSaves{ lineageOlder, lineageOther, lineageNewest, lineageMiddle };
	const auto attestedLatest = ResolveLatestAttestedSave("AAAA1111", attestedLatestSaves, attestedAll, hashCurrent);
	assert(attestedLatest.verdict == LineageVerdict::Attested);
	assert(attestedLatest.saveFile == std::optional<std::string>{ lineageNewest });
	assert(attestedLatest.saveFile == ResolveLatestProfileSave("", "AAAA1111", attestedLatestSaves));

	const std::vector<std::string> offlineSaves{ lineageOffline, lineageNewest, lineageMiddle, lineageOlder };
	const std::vector<AttestedSave> attestedWithoutOffline{ attestedAll[0], attestedAll[1], attestedAll[2] };
	const auto offlineFallback = ResolveLatestAttestedSave("AAAA1111", offlineSaves, attestedWithoutOffline, hashCurrent);
	assert(offlineFallback.verdict == LineageVerdict::Unattested);
	assert(offlineFallback.saveFile == std::optional<std::string>{ lineageNewest });

	Hashes editedNewest = currentHashes;
	editedNewest[lineageNewest] = "edited-hash";
	const auto editedNewestHash = [&editedNewest](std::string_view a_saveFile) { return HashFrom(editedNewest, a_saveFile); };
	const std::vector<std::string> modifiedSaves{ lineageNewest, lineageMiddle, lineageOlder };
	const auto modified = ResolveLatestAttestedSave("AAAA1111", modifiedSaves, attestedAll, editedNewestHash);
	assert(modified.verdict == LineageVerdict::Modified);
	assert(modified.saveFile == std::optional<std::string>{ lineageMiddle });

	Hashes editedNewestAndMiddle = editedNewest;
	editedNewestAndMiddle[lineageMiddle] = "edited-hash";
	const auto rollbackHash = [&editedNewestAndMiddle](std::string_view a_saveFile) { return HashFrom(editedNewestAndMiddle, a_saveFile); };
	const auto rollback = ResolveLatestAttestedSave("AAAA1111", modifiedSaves, attestedAll, rollbackHash);
	assert(rollback.verdict == LineageVerdict::Modified);
	assert(rollback.saveFile == std::optional<std::string>{ lineageOlder });

	const std::vector<std::string> copiedSaves{ lineageCopy, lineageMiddle, lineageOlder };
	const auto copied = ResolveLatestAttestedSave("AAAA1111", copiedSaves, attestedAll, hashCurrent);
	assert(copied.verdict == LineageVerdict::Unattested);
	assert(copied.saveFile == std::optional<std::string>{ lineageMiddle });

	const std::vector<std::string> mixedCharacterSaves{ lineageOther, lineageNewest, lineageMiddle };
	const auto characterA = ResolveLatestAttestedSave("AAAA1111", mixedCharacterSaves, attestedAll, hashCurrent);
	const auto characterB = ResolveLatestAttestedSave("BBBB2222", mixedCharacterSaves, attestedAll, hashCurrent);
	assert(characterA.saveFile == std::optional<std::string>{ lineageNewest });
	assert(characterB.verdict == LineageVerdict::Attested);
	assert(characterB.saveFile == std::optional<std::string>{ lineageOther });

	const auto noServerLineage = ResolveLatestAttestedSave("AAAA1111", offlineSaves, std::span<const AttestedSave>{}, hashCurrent);
	assert(noServerLineage.verdict == LineageVerdict::Unattested);
	assert(!noServerLineage.saveFile.has_value());

	Hashes withoutNewest = currentHashes;
	withoutNewest.erase(lineageNewest);
	const auto withoutNewestHash = [&withoutNewest](std::string_view a_saveFile) { return HashFrom(withoutNewest, a_saveFile); };
	const auto unverifiable = ResolveLatestAttestedSave("AAAA1111", modifiedSaves, attestedAll, withoutNewestHash);
	assert(unverifiable.verdict == LineageVerdict::Unverifiable);
	assert(!unverifiable.saveFile.has_value());

	const auto firstRun = ResolveLatestAttestedSave("AAAA1111", attestedLatestSaves, std::span<const AttestedSave>{}, hashCurrent);
	assert(firstRun.verdict == LineageVerdict::Unattested);
	assert(!firstRun.saveFile.has_value());

	const std::vector<AttestedSave> attestedMixed{ { mixedSaves.front(), "mixed-hash", 5 } };
	const Hashes mixedHashes{ { mixedSaves.front(), "mixed-hash" } };
	const auto mixedHash = [&mixedHashes](std::string_view a_saveFile) { return HashFrom(mixedHashes, a_saveFile); };
	const auto mixedLatest = ResolveLatestAttestedSave("AAAA1111", mixedSaves, attestedMixed, mixedHash);
	assert(mixedLatest.verdict == LineageVerdict::Attested);
	assert(mixedLatest.saveFile == ResolveLatestProfileSave("", "AAAA1111", mixedSaves));

	const std::vector<std::string> soloNewestSaves{ lineageNewest, lineageOlder };
	const std::vector<AttestedSave> soloModifiedAttested{ attestedAll[2] };
	const auto soloModified = ResolveLatestAttestedSave("AAAA1111", soloNewestSaves, soloModifiedAttested, editedNewestHash);
	assert(soloModified.verdict == LineageVerdict::Modified);
	assert(!soloModified.saveFile.has_value());

	const std::vector<AttestedSave> soloUnattestedAttested{ attestedAll[3] };
	const auto soloUnattested = ResolveLatestAttestedSave("AAAA1111", soloNewestSaves, soloUnattestedAttested, hashCurrent);
	assert(soloUnattested.verdict == LineageVerdict::Unattested);
	assert(!soloUnattested.saveFile.has_value());

	const auto attestedJoin = AdmitJoinSave(attestedLatest, false);
	assert(attestedJoin.allowed);
	assert(attestedJoin.code.empty());
	assert(AdmitJoinSave(offlineFallback, false).allowed);
	assert(!AdmitJoinSave(soloUnattested, false).allowed);
	assert(AdmitJoinSave(soloUnattested, false).code == std::string{ SaveNotMultiplayerCode });
	assert(!AdmitJoinSave(soloModified, false).allowed);
	assert(!AdmitJoinSave(unverifiable, false).allowed);
	assert(!AdmitJoinSave(firstRun, false).allowed);
	assert(AdmitJoinSave(soloUnattested, true).allowed);
	assert(AdmitJoinSave(unverifiable, true).allowed);

	const AttestedSave serverRecord{ lineageNewest, "newest-hash", 3 };
	const std::vector<AttestedSave> serverHistory{ attestedAll[1], serverRecord };
	assert(SameAttestedSave(serverRecord, AttestedSave{ lineageNewest, "newest-hash", 99 }));
	assert(!SameAttestedSave(serverRecord, AttestedSave{ lineageNewest, "other-hash", 3 }));
	assert(!SameAttestedSave(serverRecord, AttestedSave{ lineageMiddle, "newest-hash", 3 }));
	assert(ContradictsRecordedSave(serverHistory, AttestedSave{ lineageNewest, "edited-hash", 0 }));
	assert(!ContradictsRecordedSave(serverHistory, AttestedSave{ lineageNewest, "newest-hash", 0 }));
	assert(!ContradictsRecordedSave(serverHistory, AttestedSave{ lineageOffline, "edited-hash", 0 }));
	assert(ContradictsRecordedSave(serverHistory, AttestedSave{ lineageMiddle, "other-hash", 0 }));

	const SaveClaimStanding unverified{};
	const SaveClaimStanding verified{ false, true, true };
	const SaveClaimStanding readmission{ false, false, true, true };
	const SaveClaimStanding neverSeen{};

	const auto matchingClaim = AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "newest-hash", 0 }, unverified);
	assert(!matchingClaim.allowed);
	assert(matchingClaim.code == std::string{ SaveNotMultiplayerCode });
	assert(AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "newest-hash", 0 }, verified).allowed);
	const auto readmittedClaim = AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "newest-hash", 0 }, readmission);
	assert(readmittedClaim.allowed);
	assert(!readmittedClaim.recordClaim);
	assert(readmittedClaim.readmitted);
	assert(!AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageOffline, "offline-hash", 0 }, readmission).allowed);
	const auto caseInsensitiveClaim =
		AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageMiddle, "MIDDLE-HASH", 0 }, readmission);
	assert(caseInsensitiveClaim.allowed);

	const auto unknownSaveClaim = AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageOffline, "offline-hash", 0 }, unverified);
	assert(!unknownSaveClaim.allowed);
	assert(unknownSaveClaim.code == std::string{ SaveNotMultiplayerCode });
	assert(!unknownSaveClaim.reason.empty());
	assert(AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageOffline, "offline-hash", 0 }, verified).allowed);
	const auto tamperedClaim = AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "edited-hash", 0 }, unverified);
	assert(!tamperedClaim.allowed);
	const auto verifiedTamper = AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "edited-hash", 0 }, verified);
	assert(!verifiedTamper.allowed);
	assert(verifiedTamper.code == std::string{ SaveNotMultiplayerCode });
	assert(!AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "edited-hash", 0 }, readmission).allowed);
	const auto missingClaim = AdmitAttestedSaveClaim(serverHistory, std::nullopt, unverified);
	assert(!missingClaim.allowed);
	assert(AdmitAttestedSaveClaim(serverHistory, std::nullopt, verified).allowed);

	const auto firstJoinClaim = AdmitAttestedSaveClaim(std::span<const AttestedSave>{}, std::nullopt, unverified);
	assert(firstJoinClaim.allowed);
	const auto grandfatherClaim =
		AdmitAttestedSaveClaim(std::span<const AttestedSave>{}, AttestedSave{ lineageNewest, "newest-hash", 0 }, neverSeen);
	assert(grandfatherClaim.allowed);
	assert(grandfatherClaim.recordClaim);
	assert(!AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageOffline, "offline-hash", 0 }, neverSeen).allowed);
	const auto knownProfileClaim = AdmitAttestedSaveClaim(
		std::span<const AttestedSave>{},
		AttestedSave{ lineageNewest, "newest-hash", 0 },
		SaveClaimStanding{ false, false, true });
	assert(!knownProfileClaim.allowed);
	assert(knownProfileClaim.code == std::string{ SaveNotMultiplayerCode });
	assert(!knownProfileClaim.reason.empty());
	const auto restoredClaim =
		AdmitAttestedSaveClaim(std::span<const AttestedSave>{}, AttestedSave{ lineageNewest, "newest-hash", 0 }, verified);
	assert(restoredClaim.allowed);
	assert(restoredClaim.recordClaim);
	const SaveClaimStanding adminVerified{ true, true, true };
	assert(AdmitAttestedSaveClaim(serverHistory, std::nullopt, adminVerified).allowed);
	assert(AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageOffline, "offline-hash", 0 }, adminVerified).allowed);
	assert(AdmitAttestedSaveClaim(serverHistory, AttestedSave{ lineageNewest, "edited-hash", 0 }, adminVerified).allowed);

	return 0;
}
