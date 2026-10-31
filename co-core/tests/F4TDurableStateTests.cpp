#include "F4TDurableState.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

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
	assert(root.at("schemaVersion") == 4);
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
	assert(migrated.root.at("schemaVersion") == 4);
	assert(migrated.root.at("serverInstanceId") == serverId);
	assert(migrated.root.at("profiles").contains("profile-one"));
	assert(migrated.root.at("worldState").at("timeHHmm") == "1200");
	assert(migrated.root.at("savedAt") == 2000.0);

	Json schema2 = root;
	schema2["schemaVersion"] = 2;
	schema2.erase("inventoryAuthorityBlocked");
	schema2.erase("drops");
	const auto upgraded = LoadAndMigrate(schema2, 2001.0);
	assert(upgraded.migrated && upgraded.root.at("schemaVersion") == 4);
	assert(upgraded.root.at("inventoryAuthorityBlocked") == false);
	Json newer = root;
	newer["schemaVersion"] = 5;
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
	assert(InspectPlayerStateUpdate(root, "profile-one", appearanceState) == PlayerStateMutationOutcome::Unchanged);
	assert(UpdatePlayerState(root, "profile-one", appearanceState, 1600.0) == PlayerStateMutationOutcome::Unchanged);
	const auto restored = GetPlayerState(root, "profile-one");
	assert(restored.has_value());
	assert(restored->at("characterName") == "Nomad");
	assert(restored->at("appearance").at("version") == 4);
	assert(restored->at("appearance").at("raceFormId") == "00000014");
	assert(restored->at("equippedItems")[0].at("formId") == "00001234");

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

	return 0;
}
