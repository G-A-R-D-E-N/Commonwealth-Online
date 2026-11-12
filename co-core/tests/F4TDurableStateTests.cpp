#include "F4TDurableState.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	using Json = nlohmann::json;

	bool ThrowsValidation(const Json& a_root)
	{
		try {
			F4T::DurableState::ValidateRoot(a_root);
			return false;
		} catch (const std::invalid_argument&) {
			return true;
		}
	}
}

int main()
{
	using namespace F4T::DurableState;

	const std::string serverId = "0123456789abcdef0123456789abcdef";
	auto root = NewRoot(serverId, 1000.0);
	ValidateRoot(root);
	assert(root.at("schemaVersion") == CurrentSchemaVersion);
	assert(root.at("serverInstanceId") == serverId);
	assert(root.at("profiles").empty());
	assert(root.at("worldState").empty());

	Json schema1{
		{ "schemaVersion", 1 },
		{ "serverId", serverId },
		{ "players", Json{ { "profile-one", Json{ { "updatedAt", 2.0 } } } } },
		{ "worldState", Json{ { "timeHHmm", "1200" } } }
	};
	const auto migrated = LoadAndMigrate(schema1, 2000.0);
	assert(migrated.migrated);
	assert(migrated.root.at("schemaVersion") == CurrentSchemaVersion);
	assert(migrated.root.at("serverInstanceId") == serverId);
	assert(migrated.root.at("profiles").contains("profile-one"));
	assert(migrated.root.at("worldState").at("timeHHmm") == "1200");
	assert(migrated.root.at("savedAt") == 2000.0);

	Json schema2 = root;
	schema2["schemaVersion"] = 2;
	schema2.erase("inventoryAuthorityBlocked");
	schema2.erase("drops");
	const auto upgraded = LoadAndMigrate(schema2, 2001.0);
	assert(upgraded.migrated && upgraded.root.at("schemaVersion") == CurrentSchemaVersion);
	assert(upgraded.root.at("inventoryAuthorityBlocked") == false);
	Json newer = root;
	newer["schemaVersion"] = CurrentSchemaVersion + 1;
	try {
		(void)LoadAndMigrate(newer, 1.0);
		assert(false && "newer durable schema must fail");
	} catch (const std::invalid_argument&) {
	}

	Json invalid = root;
	invalid["serverInstanceId"] = "short";
	assert(ThrowsValidation(invalid));
	invalid = root;
	invalid["profiles"] = Json::array();
	assert(ThrowsValidation(invalid));
	invalid = root;
	invalid["worldState"] = "bad";
	assert(ThrowsValidation(invalid));
	invalid = root;
	invalid["profiles"]["profile-one"] = Json{ { "resume", Json{ { "tokenHash", "bad" }, { "expiresAt", 1.0 } } } };
	assert(ThrowsValidation(invalid));
	invalid = root;
	invalid["profiles"]["profile-one"] = Json{ { "playerState", Json{ { "characterName", "Nomad" }, { "actionEvents", Json::array() } } } };
	assert(ThrowsValidation(invalid));
	invalid = root;
	invalid["savedAt"] = std::numeric_limits<double>::infinity();
	assert(ThrowsValidation(invalid));
	invalid = root;
	invalid.erase("savedAt");
	assert(ThrowsValidation(invalid));

	F4T::Identity::TokenHash hash{};
	for (std::size_t index = 0; index < hash.size(); ++index) {
		hash[index] = static_cast<std::uint8_t>(index);
	}
	SaveResumeCredential(root, "profile-one", hash, std::nullopt, true, 5000.0, 1100.0);
	const auto resume = GetResumeCredential(root, "profile-one");
	assert(resume.has_value());
	assert(resume->tokenHash == hash);
	assert(resume->expiresAt == 5000.0);
	assert(!resume->previousTokenHash.has_value());

	F4T::Identity::TokenHash rotated{};
	for (std::size_t index = 0; index < rotated.size(); ++index) {
		rotated[index] = static_cast<std::uint8_t>(0xff - index);
	}
	SaveResumeCredential(root, "profile-one", rotated, hash, true, 6000.0, 1150.0);
	const auto rotatedRecord = GetResumeCredential(root, "profile-one");
	assert(rotatedRecord.has_value());
	assert(rotatedRecord->tokenHash == rotated);
	assert(rotatedRecord->previousTokenHash.has_value());
	assert(*rotatedRecord->previousTokenHash == hash);

	SaveResumeCredential(root, "profile-one", rotated, std::nullopt, true, 6000.0, 1160.0);
	assert(!GetResumeCredential(root, "profile-one")->previousTokenHash.has_value());
	SaveResumeCredential(root, "profile-one", hash, std::nullopt, true, 5000.0, 1100.0);
	assert(root["profiles"]["profile-one"]["resume"]["tokenHash"] == "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	assert(DeleteResumeCredential(root, "profile-one", 1200.0));
	assert(!GetResumeCredential(root, "profile-one").has_value());
	assert(!DeleteResumeCredential(root, "profile-one", 1201.0));

	const Json actionOnly{
		{ "type", "playerState" },
		{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
	};
	assert(UpdatePlayerState(root, "profile-one", actionOnly, 1300.0) == PlayerStateMutationOutcome::NoDurableData);
	assert(!GetPlayerState(root, "profile-one").has_value());

	const Json nameState{
		{ "type", "playerState" },
		{ "characterName", "Nomad" },
		{ "actionEvents", Json::array({ Json{ { "sequence", 2 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
	};
	assert(UpdatePlayerState(root, "profile-one", nameState, 1400.0) == PlayerStateMutationOutcome::Changed);
	const auto restoredName = GetPlayerState(root, "profile-one");
	assert(restoredName.has_value());
	assert(restoredName->at("type") == "playerState");
	assert(restoredName->at("characterName") == "Nomad");
	assert(!restoredName->contains("actionEvents"));
	assert(!restoredName->contains("updatedAt"));

	const Json appearanceState{
		{ "type", "playerState" },
		{ "appearance", Json{ { "raceFormId", "14" }, { "height", 1.0 } } },
		{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", "1234" } } }) }
	};
	assert(UpdatePlayerState(root, "profile-one", appearanceState, 1500.0) == PlayerStateMutationOutcome::Changed);
	assert(InspectPlayerStateUpdate(root, "profile-one", appearanceState, 1500.0) == PlayerStateMutationOutcome::Unchanged);
	assert(UpdatePlayerState(root, "profile-one", appearanceState, 1600.0) == PlayerStateMutationOutcome::Unchanged);
	const auto restored = GetPlayerState(root, "profile-one");
	assert(restored.has_value());
	assert(restored->at("characterName") == "Nomad");
	assert(restored->at("appearance").at("version") == 4);
	assert(restored->at("appearance").at("raceFormId") == "00000014");
	assert(restored->at("equippedItems")[0].at("formId") == "00001234");

	// TODO-verify: the 50 XP-per-level floor in F4TProgressionValidation.cpp is unconfirmed; confirm in game.
	const Json progressionState{
		{ "type", "playerState" },
		{ "progression", Json{
			{ "special", Json{ { "strength", 7 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
			{ "level", 2 },
			{ "experience", 200 },
			{ "perks", Json::array() }
		} }
	};
	assert(UpdatePlayerState(root, "profile-one", progressionState, 1700.0) == PlayerStateMutationOutcome::Changed);
	const auto leveled = GetPlayerState(root, "profile-one");
	assert(leveled.has_value());
	assert(leveled->at("progression").at("level") == 2);
	assert(leveled->at("progression").at("special").at("strength") == 7);
	assert(root.at("profiles").at("profile-one").at("playerState").at("progressionUpdatedAt") == 1700.0);

	const Json unearnedLevel{
		{ "type", "playerState" },
		{ "progression", Json{
			{ "special", Json{ { "strength", 7 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
			{ "level", 30 },
			{ "experience", 200 },
			{ "perks", Json::array() }
		} }
	};
	assert(UpdatePlayerState(root, "profile-one", unearnedLevel, 1800.0) == PlayerStateMutationOutcome::NoDurableData);
	assert(GetPlayerState(root, "profile-one")->at("progression").at("level") == 2);

	const Json droppedLevel{
		{ "type", "playerState" },
		{ "progression", Json{
			{ "special", Json{ { "strength", 7 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
			{ "level", 1 },
			{ "experience", 150 },
			{ "perks", Json::array() }
		} }
	};
	assert(ProgressionRefused(root, "profile-one", droppedLevel, 1900.0));
	assert(!ProgressionRefused(root, "profile-one", progressionState, 1900.0));
	assert(UpdatePlayerState(root, "profile-one", droppedLevel, 1900.0) == PlayerStateMutationOutcome::NoDurableData);
	assert(GetPlayerState(root, "profile-one")->at("progression").at("level") == 2);
	assert(UpdatePlayerState(root, "profile-one", progressionState, 2000.0) == PlayerStateMutationOutcome::Unchanged);
	assert(GetPlayerState(root, "profile-one")->at("progression").at("level") == 2);

	const auto progressionAt = [](std::int64_t a_level, std::int64_t a_experience) {
		return Json{
			{ "type", "playerState" },
			{ "progression", Json{
				{ "special", Json{ { "strength", 7 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
				{ "level", a_level },
				{ "experience", a_experience },
				{ "perks", Json::array() }
			} }
		};
	};
	const auto storedState = [&root](const char* a_profileId) -> const Json& {
		return root.at("profiles").at(a_profileId).at("playerState");
	};
	Json equippedMoved = storedState("profile-one").at("equippedItems");
	equippedMoved[0]["slot"] = "leftHand";
	const Json equipmentOnly{ { "type", "playerState" }, { "equippedItems", equippedMoved } };
	assert(UpdatePlayerState(root, "profile-one", equipmentOnly, 8900.0) == PlayerStateMutationOutcome::Changed);
	assert(storedState("profile-one").at("updatedAt") == 8900.0);
	assert(storedState("profile-one").at("progressionUpdatedAt") == 1700.0);
	assert(UpdatePlayerState(root, "profile-one", progressionAt(33, 200), 8900.0) == PlayerStateMutationOutcome::NoDurableData);
	assert(storedState("profile-one").at("progressionUpdatedAt") == 1700.0);
	assert(UpdatePlayerState(root, "profile-one", progressionAt(32, 200), 8900.0) == PlayerStateMutationOutcome::Changed);
	assert(GetPlayerState(root, "profile-one")->at("progression").at("level") == 32);
	assert(storedState("profile-one").at("progressionUpdatedAt") == 8900.0);
	assert(UpdatePlayerState(root, "profile-one", progressionAt(32, 400), 9000.0) == PlayerStateMutationOutcome::Changed);
	assert(storedState("profile-one").at("progressionUpdatedAt") == 8900.0);

	Json legacyState = progressionAt(2, 300);
	legacyState["updatedAt"] = 1000.0;
	root["profiles"]["profile-legacy"] = Json{ { "updatedAt", 1000.0 }, { "playerState", legacyState } };
	assert(UpdatePlayerState(root, "profile-legacy", progressionAt(33, 300), 8200.0) == PlayerStateMutationOutcome::NoDurableData);
	assert(!storedState("profile-legacy").contains("progressionUpdatedAt"));
	assert(UpdatePlayerState(root, "profile-legacy", progressionAt(32, 300), 8200.0) == PlayerStateMutationOutcome::Changed);
	assert(GetPlayerState(root, "profile-legacy")->at("progression").at("level") == 32);
	assert(storedState("profile-legacy").at("progressionUpdatedAt") == 8200.0);

	Json worldUpdate{
		{ "timeHHmm", "1800" },
		{ "weatherConsoleArg", "001ca7e4" },
		{ "ignored", "transient" }
	};
	UpdateWorldState(root, worldUpdate);
	auto world = GetWorldState(root);
	assert(world.at("timeHHmm") == "1800");
	assert(world.at("weatherConsoleArg") == "001ca7e4");
	assert(!world.contains("ignored"));
	UpdateWorldState(root, Json{ { "weatherFormId", "001CA7E4" } });
	world = GetWorldState(root);
	assert(world.at("timeHHmm") == "1800");
	assert(world.at("weatherFormId") == "001CA7E4");
	UpdateWorldState(root, Json{ { "weatherConsoleArg", nullptr }, { "weatherFormId", nullptr } });
	world = GetWorldState(root);
	assert(!world.contains("weatherConsoleArg"));
	assert(!world.contains("weatherFormId"));
	assert(world.at("timeHHmm") == "1800");

	ValidateRoot(root);

	const std::string attestationHash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const std::string otherHash = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	assert(GetSaveAttestations(root, "profile-attested").empty());
	RecordSaveAttestation(root, "profile-attested", { "Save1_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2", attestationHash, 10 }, 3000.0);
	RecordSaveAttestation(root, "profile-attested", { "Save2_AAAA1111_Nomad_Commonwealth_000002_20260814000202_1_2", otherHash, 20 }, 3001.0);
	auto attestations = GetSaveAttestations(root, "profile-attested");
	assert(attestations.size() == 2);
	assert(attestations.front().saveFile == "Save1_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2");
	assert(attestations.back().saveFile == "Save2_AAAA1111_Nomad_Commonwealth_000002_20260814000202_1_2");
	assert(attestations.back().sha256Hex == otherHash);
	assert(attestations.back().attestedAtUnix == 20);
	assert(GetSaveAttestations(root, "profile-other").empty());

	RecordSaveAttestation(root, "profile-attested", { "Save2_AAAA1111_Nomad_Commonwealth_000002_20260814000202_1_2", attestationHash, 30 }, 3002.0);
	attestations = GetSaveAttestations(root, "profile-attested");
	assert(attestations.size() == 2);
	assert(attestations.front().saveFile == "Save1_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2");
	assert(attestations.back().sha256Hex == attestationHash);
	assert(attestations.back().attestedAtUnix == 30);

	RecordSaveAttestation(root, "profile-attested", { "save2_AAAA1111_Nomad_Commonwealth_000002_20260814000202_1_2", otherHash, 40 }, 3003.0);
	attestations = GetSaveAttestations(root, "profile-attested");
	assert(attestations.size() == 2);
	assert(attestations.back().sha256Hex == otherHash);
	assert(attestations.back().attestedAtUnix == 40);
	assert(!attestations.back().slot.has_value());
	RecordSaveAttestation(root, "profile-attested", { "Save3_AAAA1111_Nomad_Commonwealth_000003_20260814000303_1_2", otherHash, 50, 2u }, 3004.0);
	assert(GetSaveAttestations(root, "profile-attested").back().slot == std::optional<std::uint32_t>{ 2u });
	assert(!GetSaveAttestations(root, "profile-attested").front().slot.has_value());

	for (std::size_t index = 0; index < MaximumSaveAttestationsPerProfile + 5; ++index) {
		RecordSaveAttestation(
			root,
			"profile-attested",
			{ "Save" + std::to_string(index) + "_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2", attestationHash, 50 + index },
			3100.0 + static_cast<double>(index));
	}
	attestations = GetSaveAttestations(root, "profile-attested");
	assert(attestations.size() == MaximumSaveAttestationsPerProfile);
	assert(attestations.back().saveFile == "Save" + std::to_string(MaximumSaveAttestationsPerProfile + 4) +
		"_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2");
	assert(root.at("profiles").at("profile-attested").at("updatedAt") == 3100.0 + static_cast<double>(MaximumSaveAttestationsPerProfile + 4));
	ValidateRoot(root);

	bool rejectedShortHash = false;
	try {
		RecordSaveAttestation(root, "profile-attested", { "Save9_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2", "abcd", 60 }, 3200.0);
	} catch (const std::invalid_argument&) {
		rejectedShortHash = true;
	}
	assert(rejectedShortHash);

	bool rejectedEmptyName = false;
	try {
		RecordSaveAttestation(root, "profile-attested", { "", attestationHash, 60 }, 3200.0);
	} catch (const std::invalid_argument&) {
		rejectedEmptyName = true;
	}
	assert(rejectedEmptyName);

	Json invalidAttestations = root;
	invalidAttestations["profiles"]["profile-attested"]["saveAttestations"] = Json::array({ Json{ { "saveFile", "Save1" } } });
	assert(ThrowsValidation(invalidAttestations));
	invalidAttestations["profiles"]["profile-attested"]["saveAttestations"] = "not-an-array";
	assert(ThrowsValidation(invalidAttestations));
	invalidAttestations["profiles"]["profile-attested"]["saveAttestations"] = Json::array();
	for (std::size_t index = 0; index < MaximumSaveAttestationsPerProfile + 1; ++index) {
		invalidAttestations["profiles"]["profile-attested"]["saveAttestations"].push_back(
			Json{ { "saveFile", "Save1_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2" }, { "sha256", attestationHash } });
	}
	assert(ThrowsValidation(invalidAttestations));

	const auto slotSave = [&](std::size_t a_index, std::optional<std::uint32_t> a_slot) {
		return F4T::SaveProfileLogic::AttestedSave{
			"Save" + std::to_string(a_index) + "_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2", attestationHash, 70 + a_index, a_slot };
	};
	auto slotted = NewRoot(serverId, 1000.0);
	RecordSaveAttestation(slotted, "profile-slots", slotSave(0, 2u), 4000.0);
	RecordSaveAttestation(slotted, "profile-slots", slotSave(1, std::nullopt), 4001.0);
	auto slotRecords = GetSaveAttestations(slotted, "profile-slots");
	assert(slotRecords.size() == 2 && slotRecords[0].slot == 2u && !slotRecords[1].slot.has_value());
	assert(!slotted.at("profiles").at("profile-slots").at("saveAttestations")[1].contains("slot"));
	const auto reloaded = LoadAndMigrate(slotted, 4002.0);
	assert(!reloaded.migrated);
	slotRecords = GetSaveAttestations(reloaded.root, "profile-slots");
	assert(slotRecords.size() == 2 && slotRecords[0].slot == 2u && !slotRecords[1].slot.has_value());

	auto legacySlots = NewRoot(serverId, 1000.0);
	legacySlots["schemaVersion"] = 5;
	legacySlots["profiles"]["profile-legacy"]["updatedAt"] = 10.0;
	legacySlots["profiles"]["profile-legacy"]["saveAttestations"] = Json::array({
		Json{ { "saveFile", "Save1_AAAA1111_Nomad_Commonwealth_000001_20260814000101_1_2" }, { "sha256", attestationHash }, { "attestedAt", 10.0 } } });
	const auto legacyLoaded = LoadAndMigrate(legacySlots, 4003.0);
	assert(legacyLoaded.migrated && legacyLoaded.root.at("schemaVersion") == CurrentSchemaVersion);
	slotRecords = GetSaveAttestations(legacyLoaded.root, "profile-legacy");
	assert(slotRecords.size() == 1 && !slotRecords.front().slot.has_value() && slotRecords.front().attestedAtUnix == 10);

	for (const Json& badSlot : { Json("two"), Json(-1), Json(true), Json(4294967296ULL) }) {
		auto malformed = slotted;
		malformed["profiles"]["profile-slots"]["saveAttestations"][0]["slot"] = badSlot;
		assert(ThrowsValidation(malformed));
		assert(!GetSaveAttestations(malformed, "profile-slots")[0].slot.has_value());
	}

	RecordSaveAttestation(slotted, "profile-slots", slotSave(0, 2u), 4004.0);
	assert(GetSaveAttestations(slotted, "profile-slots").back().slot == 2u);
	RecordSaveAttestation(slotted, "profile-slots", slotSave(0, 3u), 4005.0);
	slotRecords = GetSaveAttestations(slotted, "profile-slots");
	assert(slotRecords.size() == 2 && slotRecords.back().saveFile == slotSave(0, 3u).saveFile && !slotRecords.back().slot.has_value());
	ValidateRoot(slotted);

	RecordSaveAttestation(slotted, "profile-slot-cap", slotSave(1000, 1u), 4100.0);
	RecordSaveAttestation(slotted, "profile-slot-cap", slotSave(1001, std::nullopt), 4101.0);
	for (std::size_t index = 0; index < MaximumSaveAttestationsPerProfile + 5; ++index) {
		RecordSaveAttestation(slotted, "profile-slot-cap", slotSave(index, 2u), 4200.0 + static_cast<double>(index));
	}
	slotRecords = GetSaveAttestations(slotted, "profile-slot-cap");
	assert(slotRecords.size() == MaximumSaveAttestationsPerProfile);
	std::size_t slotOne = 0, unscoped = 0, slotTwo = 0;
	for (const auto& record : slotRecords) {
		if (!record.slot) ++unscoped; else if (*record.slot == 1u) ++slotOne; else if (*record.slot == 2u) ++slotTwo;
	}
	assert(slotOne == 1 && unscoped == 1 && slotTwo == MaximumSaveAttestationsPerProfile - 2);
	assert(slotRecords.back().saveFile == slotSave(MaximumSaveAttestationsPerProfile + 4, 2u).saveFile);
	ValidateRoot(slotted);

	const auto characterSave = [&](std::string_view a_character, std::size_t a_index, std::optional<std::uint32_t> a_slot) {
		return F4T::SaveProfileLogic::AttestedSave{
			"Save" + std::to_string(a_index) + "_" + std::string{ a_character } + "_Nomad_Commonwealth_000001_20260814000101_1_2", attestationHash, 90 + a_index, a_slot };
	};
	const SlotCharacterIds twoSlots{ { 0u, "AAAA1111" }, { 2u, "bbbb2222" } };
	auto scoping = NewRoot(serverId, 1000.0);
	RecordSaveAttestation(scoping, "acct", characterSave("AAAA1111", 1, std::nullopt), 5000.0);
	RecordSaveAttestation(scoping, "acct", characterSave("BBBB2222", 2, std::nullopt), 5001.0);
	RecordSaveAttestation(scoping, "acct", characterSave("CCCC3333", 3, std::nullopt), 5002.0);
	RecordSaveAttestation(scoping, "acct", characterSave("AAAA1111", 4, 1u), 5003.0);
	RecordSaveAttestation(scoping, "other", characterSave("AAAA1111", 5, std::nullopt), 5004.0);
	assert(ScopeLegacySaveAttestations(scoping, "acct", twoSlots, 5100.0));
	auto scoped = GetSaveAttestations(scoping, "acct");
	assert(scoped.size() == 4 && scoped[0].slot == 0u && scoped[1].slot == 2u && !scoped[2].slot.has_value() && scoped[3].slot == 1u);
	assert(scoped[0].attestedAtUnix == 91 && scoped[0].sha256Hex == attestationHash);
	assert(!GetSaveAttestations(scoping, "other").front().slot.has_value());
	const auto afterScoping = scoping;
	assert(!ScopeLegacySaveAttestations(scoping, "acct", twoSlots, 5200.0) && scoping == afterScoping);

	auto ambiguous = NewRoot(serverId, 1000.0);
	RecordSaveAttestation(ambiguous, "acct", characterSave("AAAA1111", 1, std::nullopt), 5000.0);
	const SlotCharacterIds sameCharacter{ { 0u, "AAAA1111" }, { 1u, "aaaa1111" } };
	assert(!ScopeLegacySaveAttestations(ambiguous, "acct", sameCharacter, 5100.0));
	assert(!GetSaveAttestations(ambiguous, "acct").front().slot.has_value());
	assert(!ScopeLegacySaveAttestations(ambiguous, "acct", {}, 5100.0));
	assert(!ScopeLegacySaveAttestations(ambiguous, "missing", twoSlots, 5100.0));

	ValidateRoot(scoping);
	const auto scopedReload = LoadAndMigrate(scoping, 5300.0);
	assert(!scopedReload.migrated && GetSaveAttestations(scopedReload.root, "acct")[1].slot == 2u);
	auto olderReader = scoping.at("profiles").at("acct").at("saveAttestations")[0];
	olderReader.erase("slot");
	assert(olderReader.at("saveFile") == characterSave("AAAA1111", 1, std::nullopt).saveFile && olderReader.at("sha256") == attestationHash);

	assert(EraseSlotSaveAttestations(scoping, "acct", 0u, 5400.0));
	scoped = GetSaveAttestations(scoping, "acct");
	assert(scoped.size() == 3 && scoped[0].slot == 2u && !scoped[1].slot.has_value() && scoped[2].slot == 1u);
	assert(!EraseSlotSaveAttestations(scoping, "acct", 0u, 5500.0));
	assert(!EraseSlotSaveAttestations(scoping, "missing", 2u, 5500.0));
	assert(GetSaveAttestations(scoping, "other").size() == 1);
	ValidateRoot(scoping);

	const std::array<std::optional<std::uint32_t>, 4> scopes{ 1u, std::nullopt, 2u, 2u };
	assert(SaveAttestationEvictionIndex(scopes) == 2);
	const std::array<std::optional<std::uint32_t>, 2> unique{ 1u, std::nullopt };
	assert(SaveAttestationEvictionIndex(unique) == 0);

	const std::string profileSecret{ "0123456789abcdef0123456789abcdef" };
	assert(!GetProfileSecret(root, "profile-secret").has_value());
	assert(EnsureProfileSecret(root, "profile-secret", profileSecret, 3300.0));
	assert(GetProfileSecret(root, "profile-secret") == profileSecret);
	assert(!EnsureProfileSecret(root, "profile-secret", std::string(32, 'f'), 3301.0));
	assert(GetProfileSecret(root, "profile-secret") == profileSecret);
	ValidateRoot(root);

	bool rejectedShortSecret = false;
	try {
		EnsureProfileSecret(root, "profile-secret-short", "abcd", 3302.0);
	} catch (const std::invalid_argument&) {
		rejectedShortSecret = true;
	}
	assert(rejectedShortSecret);

	Json invalidSecret = root;
	invalidSecret["profiles"]["profile-secret"]["profileSecret"] = "not-a-secret";
	assert(ThrowsValidation(invalidSecret));

	const std::string gluedCharacterId{ "F7CF8FE9M47656E6572616C204A6F7264616E204C" };
	const std::string plainCharacterId{ "F7CF8FE9" };
	const std::string gluedBaselineSave{ "Save2_F7CF8FE9M47656E6572616C204A6F7264616E204C_Commonwealth_000009_20261104034759_1_2" };
	const std::string plainResolvedSave{ "Save1_F7CF8FE9_47656E6572616C204A6F7264616E204C_Commonwealth_000012_20261105033905_1_2" };
	const std::string baselineHash(64, 'a');
	const std::string resolvedHash(64, 'b');
	const auto mixedShapeRoot = [&]() -> Json {
		auto candidate = NewRoot(serverId, 1000.0);
		F4T::Identity::TokenHash tokenHash{};
		SaveResumeCredential(candidate, "mixed", tokenHash, std::nullopt, true, 5000.0, 1000.0);
		assert(InitializeInventory(candidate, "mixed", Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 1u } } }), 1000.0));
		RecordSaveAttestation(candidate, "mixed", { gluedBaselineSave, baselineHash, 10 }, 1001.0);
		assert(BeginStartingGrant(candidate, "mixed", gluedCharacterId, { gluedBaselineSave, baselineHash, 10 }));
		return candidate;
	};

	auto mixed = mixedShapeRoot();
	assert(ResolveStartingGrant(mixed, "mixed", plainCharacterId, { plainResolvedSave, resolvedHash, 0 }, "shape check", false));
	assert(mixed.at("profiles").at("mixed").at("startingGrant").at("recovery").at("characterId") == plainCharacterId);
	assert(mixed.at("profiles").at("mixed").at("startingGrant").at("recovery").at("intent").at("characterId") == gluedCharacterId);
	ValidateRoot(mixed);

	auto otherCharacterResolve = mixedShapeRoot();
	assert(!ResolveStartingGrant(otherCharacterResolve, "mixed", "AAAA1111", { plainResolvedSave, resolvedHash, 0 }, "other character", false));
	assert(!ResolveStartingGrant(otherCharacterResolve, "mixed", "BBBB2222M47656E6572616C204A6F7264616E204C", { plainResolvedSave, resolvedHash, 0 }, "other prefix", false));

	Json otherRecoveryCharacter = mixed;
	otherRecoveryCharacter["profiles"]["mixed"]["startingGrant"]["recovery"]["characterId"] = "AAAA1111";
	assert(ThrowsValidation(otherRecoveryCharacter));

	Json otherIntentCharacter = mixed;
	otherIntentCharacter["profiles"]["mixed"]["startingGrant"]["recovery"]["intent"]["characterId"] = "AAAA1111M4E6F6D6164";
	assert(ThrowsValidation(otherIntentCharacter));

	const Json unstarted{ { "state", "unstarted" } };
	const Json grantUnstarted{ { "granted", false }, { "items", Json::object() }, { "recovery", unstarted } };
	const Json empty{ { "initialized", true }, { "revision", 0u }, { "items", Json::object() } };
	const Json named{ { "type", "playerState" }, { "characterName", "Hero" } };
	assert(!F4T::DurableState::HasCommittedFirstJoinState(std::nullopt, std::nullopt, std::nullopt));
	assert(!F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, named));
	assert(!F4T::DurableState::HasCommittedFirstJoinState(std::nullopt, empty, std::nullopt));
	assert(F4T::DurableState::HasCommittedFirstJoinState(Json{ { "granted", false }, { "items", Json::object() }, { "recovery", Json{ { "state", "intent" } } } }, empty, named));
	assert(F4T::DurableState::HasCommittedFirstJoinState(Json{ { "granted", false }, { "items", Json::object() } }, empty, named));
	assert(F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, Json{ { "initialized", true }, { "revision", 1u }, { "items", Json::object() } }, named));
	assert(!F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, Json{ { "type", "playerState" }, { "characterName", "Hero" }, { "appearance", Json::object() } }));
	assert(!F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, Json{ { "type", "playerState" }, { "characterName", "Hero" }, { "progression", Json::object() } }));
	assert(!F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, Json{ { "type", "playerState" }, { "characterName", "Hero" }, { "equippedItems", Json::array() } }));
	assert(!F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, Json{ { "type", "playerState" }, { "characterName", "Hero" }, { "powerArmor", Json::object() } }));
	assert(F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, Json{ { "type", "playerState" }, { "characterName", "Hero" }, { "unknownFutureField", Json::object() } }));
	const Json revisionEight{ { "initialized", true }, { "revision", 8u }, { "items", Json::object() } };
	const Json grantIntent{ { "granted", false }, { "items", Json::object() }, { "recovery", Json{ { "state", "intent" } } } };
	assert(!F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, revisionEight, named, false));
	assert(F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, revisionEight, named, true));
	assert(F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, revisionEight, named));
	assert(F4T::DurableState::HasCommittedFirstJoinState(grantIntent, empty, named, false));
	assert(F4T::DurableState::HasCommittedFirstJoinState(grantUnstarted, empty, Json{ { "type", "playerState" }, { "characterName", "Hero" }, { "unknownFutureField", Json::object() } }, false));

	auto corruptHistory = NewRoot(serverId, 1000.0);
	corruptHistory["deathBagGrantHistory"]["profile-a#0"]["bag:0123456789abcdef0123456789abcdef:x"] = Json{ { "deathId", "0123456789abcdef0123456789abcdef" },
		{ "operationId", "x" }, { "state", "attempted" }, { "attemptedAt", 1.0 } };
	bool refusedHistory = false;
	try { (void)LoadAndMigrate(corruptHistory, 1000.0); } catch (const std::invalid_argument&) { refusedHistory = true; }
	assert(refusedHistory);

	return 0;
}
