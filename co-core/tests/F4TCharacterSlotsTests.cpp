#include "F4TCharacterSlotProtocol.h"
#include "F4TCharacterSlots.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::CharacterSlots;

	Json RootWith(Json a_profiles)
	{
		return Json{ { "profiles", std::move(a_profiles) } };
	}

	void TestAccountKey()
	{
		assert(AccountKey("legacy-acct-a") == std::string{ "legacy-acct-a" });
		assert(!AccountKey("has space"));
		assert(!AccountKey("has#hash"));
		assert(!AccountKey(""));
		assert(!AccountKey("short"));
	}

	void TestSlotKeyRoundTrip()
	{
		const auto key = SlotProfileKey("legacy-acct-a", 2);
		assert(key == "legacy-acct-a#2");
		const auto parsed = ParseSlotKey(key);
		assert(parsed && parsed->first == "legacy-acct-a" && parsed->second == 2);
		assert(!ParseSlotKey("legacy-acct-a"));
		assert(!ParseSlotKey("legacy-acct-a#"));
		assert(!ParseSlotKey("legacy-acct-a#x"));
		assert(!ParseSlotKey("legacy-acct-a#1#2"));
		assert(!ParseSlotKey("#1"));
		assert(!ParseSlotKey("legacy-acct-a#007"));
		assert(!ParseSlotKey("legacy-acct-a#00"));
		const auto zero = SlotProfileKey("legacy-acct-a", 0);
		assert(zero == "legacy-acct-a#0");
		const auto parsedZero = ParseSlotKey(zero);
		assert(parsedZero && parsedZero->first == "legacy-acct-a" && parsedZero->second == 0);
		assert(SlotProfileKey("bad account!", 0).empty());
		assert(IsValidSlotCount(1) && IsValidSlotCount(8));
		assert(!IsValidSlotCount(0) && !IsValidSlotCount(9));
		assert(IsValidSlot(0, 3) && IsValidSlot(2, 3));
		assert(!IsValidSlot(3, 3));
	}

	void TestNormalizeNameAndSlot()
	{
		using namespace F4T::CharacterSlotProtocol;
		assert(NormalizeName("Hero") == std::string{ "Hero" });
		assert(!NormalizeName("  padded"));
		assert(!NormalizeName("padded  "));
		assert(!NormalizeName(""));
		assert(!NormalizeName(std::string(49, 'x')));
		assert(!NormalizeName("Bad#Name"));
		assert(!NormalizeName("Bad\tName"));
				// Constraint: the wire lexer yields unsigned slots, so the literal must be unsigned too.
		assert(NormalizeSlot(Json{ { "slot", 1u } }) == 1);
		assert(!NormalizeSlot(Json{ { "slot", -1 } }));
		assert(!NormalizeSlot(Json{ { "slot", std::uint64_t{ 0x1'0000'0000ULL } } }));
		assert(!NormalizeSlot(Json{}));
	}

	void TestMigrationMovesCharacterDataOnly()
	{
		Json profiles = Json::object();
		profiles["legacy-acct-a"] = Json{
			{ "playerState", Json{ { "characterName", "Legacy" } } },
			{ "inventory", Json{ { "initialized", true } } },
			{ "inventoryOperations", Json::array() },
			{ "resume", Json{ { "tokenHash", "abc" } } },
			{ "profileSecret", "secret" },
		};
		auto root = RootWith(std::move(profiles));
		const auto result = MigrateLegacyProfiles(root, 1000.0);
		assert(result.moved == 1 && !result.conflict);
		const auto& moved = root["profiles"]["legacy-acct-a#0"];
		assert(moved["playerState"]["characterName"] == "Legacy");
		assert(moved["inventory"]["initialized"] == true);
		assert(moved["updatedAt"] == 1000.0);
		const auto& account = root["profiles"]["legacy-acct-a"];
		assert(account.contains("resume") && account.contains("profileSecret"));
		assert(!account.contains("playerState") && !account.contains("inventory"));
		assert(!root["profiles"]["legacy-acct-a#0"].contains("resume"));
	}

	void TestMigrationSkipsIdentityOnlyNodes()
	{
		Json profiles = Json::object();
		profiles["legacy-acct-b"] = Json{ { "resume", Json{ { "tokenHash", "abc" } } } };
		auto root = RootWith(std::move(profiles));
		const auto first = MigrateLegacyProfiles(root, 1000.0);
		assert(first.moved == 0 && !first.conflict);
		assert(root["profiles"].contains("legacy-acct-b") && !root["profiles"].contains("legacy-acct-b#0"));
		const auto second = MigrateLegacyProfiles(root, 2000.0);
		assert(second.moved == 0 && !second.conflict);
	}

	void TestMigrationConflictNeedsCharacterDataOnBothSides()
	{
		Json profiles = Json::object();
		profiles["legacy-acct-c"] = Json{ { "playerState", Json{ { "characterName", "Original" } } } };
		profiles["legacy-acct-c#0"] = Json{ { "resume", Json{ { "tokenHash", "abc" } } } };
		auto root = RootWith(std::move(profiles));
		const auto merged = MigrateLegacyProfiles(root, 1000.0);
		assert(!merged.conflict && merged.moved == 1);
		assert(root["profiles"]["legacy-acct-c#0"]["playerState"]["characterName"] == "Original");
		assert(root["profiles"]["legacy-acct-c#0"].contains("resume"));

		Json clash = Json::object();
		clash["legacy-acct-d"] = Json{ { "playerState", Json{ { "characterName", "Original" } } } };
		clash["legacy-acct-d#0"] = Json{ { "playerState", Json{ { "characterName", "HalfRun" } } } };
		auto clashRoot = RootWith(std::move(clash));
		const auto before = clashRoot.dump();
		const auto conflict = MigrateLegacyProfiles(clashRoot, 1000.0);
		assert(conflict.conflict && conflict.conflictKey == "legacy-acct-d");
		assert(clashRoot.dump() == before);
	}

	void TestMigrationRemapsDropOwners()
	{
		Json profiles = Json::object();
		profiles["legacy-acct-e"] = Json{ { "playerState", Json{ { "characterName", "Legacy" } } } };
		auto root = RootWith(std::move(profiles));
		root["drops"] = Json{ { "live", Json{ { "7", Json{ { "ownerProfileId", "legacy-acct-e" } } } } } };
		const auto result = MigrateLegacyProfiles(root, 1000.0);
		assert(result.moved == 1);
		assert(root["drops"]["live"]["7"]["ownerProfileId"] == "legacy-acct-e#0");
	}

	void TestMigrationRejectsBadInput()
	{
		auto root = RootWith(Json::object());
		bool threw = false;
		try {
			(void)MigrateLegacyProfiles(root, std::numeric_limits<double>::quiet_NaN());
		} catch (const std::invalid_argument&) {
			threw = true;
		}
		assert(threw);
	}
}

int main()
{
	TestAccountKey();
	TestSlotKeyRoundTrip();
	TestNormalizeNameAndSlot();
	TestMigrationMovesCharacterDataOnly();
	TestMigrationSkipsIdentityOnlyNodes();
	TestMigrationConflictNeedsCharacterDataOnBothSides();
	TestMigrationRemapsDropOwners();
	TestMigrationRejectsBadInput();
	std::cout << "All character slot core tests passed.\n";
	return 0;
}
