#include "F4TWorldPolicy.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::WorldPolicy;

	const char* kSample = R"({
		"version": 4,
		"game": "Fallout4.esm",
		"classes": {
			"story": ["01CA6E:Fallout4.esm", "01CA6F:Fallout4.esm"],
			"companion": ["01D162:Fallout4.esm"],
			"vendor": ["01CA70:Fallout4.esm"],
			"town": [],
			"settler": [],
			"enemy": ["01CA73:Fallout4.esm"],
			"animal": []
		},
		"doors": ["01CA75:Fallout4.esm"]
	})";

	Json SampleRoot()
	{
		return Json::parse(kSample);
	}

	void TestFormKeyRoundTrip()
	{
		const auto key = ParseFormKey("01CA6E:Fallout4.esm");
		assert(key);
		assert(key->plugin == "Fallout4.esm");
		assert(key->localId == 0x1CA6Eu);
		assert(FormatFormKey(*key) == "01CA6E:Fallout4.esm");

		const auto shortId = ParseFormKey("00000F:DLCworkshop03.esm");
		assert(shortId && shortId->localId == 0xFu);
		assert(FormatFormKey(*shortId) == "00000F:DLCworkshop03.esm");

		assert(!ParseFormKey("F:DLCworkshop03.esm").has_value());
		assert(!ParseFormKey("0F:DLCworkshop03.esm").has_value());
		assert(!ParseFormKey("0000F:DLCworkshop03.esm").has_value());
		assert(!ParseFormKey("00000F0:DLCworkshop03.esm").has_value());
		assert(!ParseFormKey("0000000F:DLCworkshop03.esm").has_value());
		assert(ParseFormKey("FFFFFF:Fallout4.esm")->localId == kMaximumLocalId);

		const std::string longestPlugin(kMaximumPluginNameBytes, 'p');
		assert(ParseFormKey("00000F:" + longestPlugin).has_value());
		assert(!ParseFormKey("00000F:" + longestPlugin + "p").has_value());

		assert(!ParseFormKey("").has_value());
		assert(!ParseFormKey("01CA6E").has_value());
		assert(!ParseFormKey(":Fallout4.esm").has_value());
		assert(!ParseFormKey("01CA6E:").has_value());
		assert(!ParseFormKey("0000000:Fallout4.esm").has_value());
		assert(!ParseFormKey("00000000:Fallout4.esm").has_value());
		assert(!ParseFormKey("000000:Fallout4.esm").has_value());
		assert(!ParseFormKey("ZZZZZZ:Fallout4.esm").has_value());
		assert(!ParseFormKey("01CA6E:Fallout4:esm").has_value());
		assert(!ParseFormKey("01CA6E:Fallout4\t.esm").has_value());
		assert(!ParseFormKey("01CA6E:Fallout4\".esm").has_value());
		assert(!ParseFormKey("01CA6E:Fallout4\\.esm").has_value());
		assert(!ParseFormKey("1000000:Fallout4.esm").has_value());

		const auto spaced = ParseFormKey("01CA6E:Unofficial Fallout 4 Patch.esp");
		assert(spaced && spaced->plugin == "Unofficial Fallout 4 Patch.esp");
	}

	void TestClassNames()
	{
		assert(ClassName(ClassId::Story) == "story");
		assert(ClassName(ClassId::Animal) == "animal");
		assert(ParseClass("companion") == ClassId::Companion);
		assert(!ParseClass("stories").has_value());
		assert(kClassCount == 7);
	}

	void TestParseDataValid()
	{
		const auto data = ParseData(kSample);
		assert(data);
		assert(data->version == 4u);
		assert(data->game == "Fallout4.esm");
		assert(data->classes[static_cast<std::size_t>(ClassId::Story)].size() == 2);
		assert(data->classes[static_cast<std::size_t>(ClassId::Companion)].size() == 1);
		assert(data->classes[static_cast<std::size_t>(ClassId::Town)].empty());
		assert(data->doors.size() == 1);
		assert(data->EntryCount() == 6);

		auto withoutDoors = SampleRoot();
		withoutDoors.erase("doors");
		const auto parsed = ParseData(withoutDoors.dump());
		assert(parsed && parsed->doors.empty());
	}

	void TestParseDataRejectsMalformed()
	{
		assert(!ParseData("").has_value());
		assert(!ParseData("   ").has_value());
		assert(!ParseData("not json").has_value());
		assert(!ParseData("[]").has_value());
		assert(!ParseData(std::string(kMaximumPolicyFileBytes + 1, ' ')).has_value());

		const auto mutate = [](const char* a_key, const Json& a_value) {
			auto root = SampleRoot();
			root[a_key] = a_value;
			return root.dump();
		};
		assert(!ParseData(mutate("version", 0)).has_value());
		assert(!ParseData(mutate("version", "1")).has_value());
		assert(!ParseData(mutate("version", true)).has_value());
		assert(!ParseData(mutate("version", kMaximumDataVersion + 1)).has_value());
		assert(!ParseData(mutate("game", "")).has_value());
		assert(!ParseData(mutate("game", 4)).has_value());
		assert(!ParseData(mutate("unexpected", 1)).has_value());

		auto missingClass = SampleRoot();
		missingClass["classes"].erase("animal");
		assert(!ParseData(missingClass.dump()).has_value());

		auto extraClass = SampleRoot();
		extraClass["classes"]["raider"] = Json::array();
		assert(!ParseData(extraClass.dump()).has_value());

		auto badEntry = SampleRoot();
		badEntry["classes"]["story"] = Json::array({ "01CA6E", 7 });
		assert(!ParseData(badEntry.dump()).has_value());

		auto duplicate = SampleRoot();
		duplicate["classes"]["story"] = Json::array({ "01CA6E:Fallout4.esm", "01CA6E:Fallout4.esm" });
		assert(!ParseData(duplicate.dump()).has_value());

		auto sameIdDifferentPlugin = SampleRoot();
		sameIdDifferentPlugin["classes"]["story"] = Json::array({ "01CA6E:Fallout4.esm", "01CA6E:DLC.esm" });
		assert(ParseData(sameIdDifferentPlugin.dump()).has_value());

		auto doorsNotArray = SampleRoot();
		doorsNotArray["doors"] = "01CA75:Fallout4.esm";
		assert(!ParseData(doorsNotArray.dump()).has_value());
	}

	void TestLoadDataFromFile()
	{
		const auto directory = std::filesystem::temp_directory_path() / "co-world-policy-tests";
		std::filesystem::create_directories(directory);
		const auto validPath = directory / "world-policy.json";
		{
			std::ofstream stream{ validPath, std::ios::binary };
			stream << kSample;
		}
		const auto loaded = LoadData(validPath);
		assert(loaded && loaded->version == 4u && loaded->EntryCount() == 6);

		const auto invalidPath = directory / "world-policy-bad.json";
		{
			std::ofstream stream{ invalidPath, std::ios::binary };
			stream << "{ \"version\": 1 }";
		}
		assert(!LoadData(invalidPath).has_value());
		assert(!LoadData(directory / "world-policy-missing.json").has_value());

		std::error_code error;
		std::filesystem::remove_all(directory, error);
	}

	void TestModeNames()
	{
		assert(ModeName(Mode::CoopStory) == "coop-story");
		assert(ModeName(Mode::FreeRoam) == "free-roam");
		assert(ParseMode("free-roam") == Mode::FreeRoam);
		assert(ParseMode(" Free-Roam ") == Mode::FreeRoam);
		assert(ParseMode("COOP-STORY") == Mode::CoopStory);
		assert(ParseMode("questMode") == std::nullopt);
		assert(ParseMode("faction-war") == std::nullopt);
		assert(ParseMode("instanced-story") == std::nullopt);
		assert(ParseMode("") == std::nullopt);
		assert(ParseMode("   ") == std::nullopt);
		assert(ParseMode("free roam") == std::nullopt);
		assert(ParseMode("free-roam-extra-long-name") == std::nullopt);
		assert(DoorsName(Doors::Shared) == "shared");
		assert(DoorsName(Doors::Vanilla) == "vanilla");
	}

	void TestSampleMarker()
	{
		auto root = SampleRoot();
		assert(!ParseData(root.dump())->sample);
		root["sample"] = true;
		const auto flagged = ParseData(root.dump());
		assert(flagged && flagged->sample);
		root["sample"] = "true";
		assert(!ParseData(root.dump()).has_value());
	}

	void TestCanServeMode()
	{
		const auto data = ParseData(kSample);
		assert(data);
		assert(CanServeMode(*data, Mode::CoopStory));
		assert(CanServeMode(*data, Mode::FreeRoam));

		const Data empty;
		assert(CanServeMode(empty, Mode::CoopStory));
		assert(!CanServeMode(empty, Mode::FreeRoam));

		auto placeholder = *data;
		placeholder.sample = true;
		assert(!CanServeMode(placeholder, Mode::FreeRoam));
		assert(CanServeMode(placeholder, Mode::CoopStory));

		auto companionOnly = *data;
		companionOnly.classes[static_cast<std::size_t>(ClassId::Story)].clear();
		assert(CanServeMode(companionOnly, Mode::FreeRoam));

		auto storyOnly = *data;
		storyOnly.classes[static_cast<std::size_t>(ClassId::Companion)].clear();
		assert(CanServeMode(storyOnly, Mode::FreeRoam));
	}

	void TestDerivePolicyPerMode()
	{
		const auto data = ParseData(kSample);
		assert(data);

		const auto coopStory = DerivePolicy(*data, Mode::CoopStory, true, Vats::Disabled);
		assert(coopStory.mode == Mode::CoopStory);
		assert(coopStory.version == 4u);
		assert(coopStory.disable.empty());
		assert(coopStory.doors == Doors::Shared);
		assert(!coopStory.suppressQuests);
		assert(coopStory.vats == Vats::Disabled);

		const auto freeRoam = DerivePolicy(*data, Mode::FreeRoam, true, Vats::RealTime);
		assert(freeRoam.suppressQuests);
		assert(freeRoam.disable.size() == 3);
		assert(FormatFormKey(freeRoam.disable[0]) == "01CA6E:Fallout4.esm");
		assert(FormatFormKey(freeRoam.disable[1]) == "01CA6F:Fallout4.esm");
		assert(FormatFormKey(freeRoam.disable[2]) == "01D162:Fallout4.esm");

		assert(freeRoam.vats == Vats::RealTime);
		const auto vanillaDoors = DerivePolicy(*data, Mode::CoopStory, false, Vats::Vanilla);
		assert(vanillaDoors.doors == Doors::Vanilla);
		assert(vanillaDoors.vats == Vats::Vanilla);
	}

	void TestDerivePolicyServerDefault()
	{
		const auto data = ParseData(kSample);
		assert(data);
		assert(ServerDefaultVats() == Vats::Disabled);
		assert(DerivePolicy(*data, Mode::CoopStory, true).vats == Vats::Disabled);
		SetServerDefaultVats(Vats::RealTime);
		assert(DerivePolicy(*data, Mode::CoopStory, true).vats == Vats::RealTime);
		SetServerDefaultVats(Vats::Disabled);
		assert(DerivePolicy(*data, Mode::CoopStory, true).vats == Vats::Disabled);
	}

	void TestDerivePolicyDeduplicates()
	{
		auto root = SampleRoot();
		root["classes"]["companion"] = Json::array({ "01CA6E:Fallout4.esm", "01D162:Fallout4.esm" });
		const auto data = ParseData(root.dump());
		assert(data);
		const auto policy = DerivePolicy(*data, Mode::FreeRoam, true, Vats::Disabled);
		assert(policy.vats == Vats::Disabled);
		assert(policy.disable.size() == 3);
		assert(FormatFormKey(policy.disable[0]) == "01CA6E:Fallout4.esm");
		assert(FormatFormKey(policy.disable[2]) == "01D162:Fallout4.esm");
	}

	void TestPolicyWireRoundTrip()
	{
		const auto data = ParseData(kSample);
		assert(data);
		const auto policy = DerivePolicy(*data, Mode::FreeRoam, false, Vats::Disabled);
		const auto packet = ToJson(policy);
		assert(packet.at("type") == "worldPolicy");
		assert(packet.at("mode") == "free-roam");
		assert(packet.at("version") == 4u);
		assert(packet.at("doors") == "vanilla");
		assert(packet.at("suppressQuests") == true);
		assert(packet.at("disable").size() == 3);
		assert(packet.at("disable").at(0) == "01CA6E:Fallout4.esm");
		assert(packet.at("vats") == "disabled");

		const auto parsed = ParsePolicy(packet);
		assert(parsed && *parsed == policy);
	}

	void TestVatsNames()
	{
		assert(VatsName(Vats::Disabled) == "disabled");
		assert(VatsName(Vats::RealTime) == "real-time");
		assert(VatsName(Vats::Vanilla) == "vanilla");
		assert(ParseVats("disabled") == Vats::Disabled);
		assert(ParseVats("real-time") == Vats::RealTime);
		assert(ParseVats("realtime") == Vats::RealTime);
		assert(ParseVats("vanilla") == Vats::Vanilla);
		assert(ParseVats(" Disabled ") == Vats::Disabled);
		assert(ParseVats("REAL-TIME") == Vats::RealTime);
		assert(!ParseVats("").has_value());
		assert(!ParseVats("slow-mo").has_value());
		assert(!ParseVats("coop-story").has_value());
	}

	void TestVatsWireCompatibility()
	{
		const auto data = ParseData(kSample);
		assert(data);
		for (const auto vats : { Vats::Disabled, Vats::RealTime, Vats::Vanilla }) {
			const auto policy = DerivePolicy(*data, Mode::CoopStory, true, vats);
			const auto packet = ToJson(policy);
			assert(packet.at("vats") == std::string{ VatsName(vats) });
			const auto parsed = ParsePolicy(packet);
			assert(parsed && parsed->vats == vats);
			assert(parsed && *parsed == policy);
		}
		auto legacy = ToJson(DerivePolicy(*data, Mode::CoopStory, true, Vats::Disabled));
		legacy.erase("vats");
		const auto legacyParsed = ParsePolicy(legacy);
		assert(legacyParsed && legacyParsed->vats == Vats::Vanilla);
		auto aliased = legacy;
		aliased["vatsMode"] = "real-time";
		const auto aliasedParsed = ParsePolicy(aliased);
		assert(aliasedParsed && aliasedParsed->vats == Vats::RealTime);
		auto conflicted = ToJson(DerivePolicy(*data, Mode::CoopStory, true, Vats::Disabled));
		conflicted["vatsMode"] = "vanilla";
		assert(!ParsePolicy(conflicted).has_value());
		auto unknown = legacy;
		unknown["vats"] = "bullet-time";
		assert(!ParsePolicy(unknown).has_value());
		auto nonString = legacy;
		nonString["vats"] = 1;
		assert(!ParsePolicy(nonString).has_value());

		Policy chunked;
		chunked.mode = Mode::FreeRoam;
		chunked.version = 3u;
		chunked.suppressQuests = true;
		chunked.vats = Vats::RealTime;
		for (std::size_t index = 0; index < kMaximumDisableEntriesPerMessage + 1; ++index) {
			chunked.disable.push_back(FormKey{ "p.esm", static_cast<std::uint32_t>(index + 1) });
		}
		const auto parts = EncodeMessages(chunked);
		assert(parts.size() == 2);
		Reassembler reassembler;
		std::optional<Policy> complete;
		for (const auto& part : parts) {
			assert(part.at("vats") == "real-time");
			if (const auto applied = reassembler.Apply(part)) {
				complete = applied;
			}
		}
		assert(complete && complete->vats == Vats::RealTime);
		assert(complete && *complete == chunked);
	}

	void TestChunkedMessages()
	{
		Policy small;
		small.mode = Mode::FreeRoam;
		small.version = 7u;
		small.suppressQuests = true;
		small.disable = { FormKey{ "Fallout4.esm", 0x1CA6Eu } };

		const auto single = EncodeMessages(small);
		assert(single.size() == 1);
		assert(single.front().at("type") == "worldPolicy");
		assert(!single.front().contains("part"));
		assert(ParsePolicy(single.front()) == small);
		assert(DecodeMessage(single.front())->parts == 1);

		Policy large = small;
		large.disable.clear();
		const std::string plugin(kMaximumPluginNameBytes, 'p');
		for (std::size_t index = 0; index < kMaximumDisableEntriesPerMessage * 3 + 1; ++index) {
			large.disable.push_back(FormKey{ plugin, static_cast<std::uint32_t>(index + 1) });
		}

		const auto parts = EncodeMessages(large);
		assert(parts.size() == 4);
		assert(!ParsePolicy(parts.front()).has_value());

		Reassembler reassembler;
		std::optional<Policy> complete;
		for (std::size_t index = 0; index < parts.size(); ++index) {
			const auto& part = parts.at(index);
			assert(part.at("type") == std::string{ kPartialMessageType });
			assert(part.at("part") == index);
			assert(part.at("parts") == parts.size());
			assert(part.at("disable").size() <= kMaximumDisableEntriesPerMessage);
			assert(part.dump().size() < F4T::ReceiveFrame::DefaultMaximumFrameBytes);
			const auto applied = reassembler.Apply(part);
			if (index + 1 < parts.size()) {
				assert(!applied.has_value());
			} else {
				complete = applied;
			}
		}
		assert(complete && *complete == large);

		Reassembler restart;
		assert(!restart.Apply(parts.at(1)).has_value());
		std::optional<Policy> restarted;
		for (std::size_t index = 0; index < parts.size(); ++index) {
			if (const auto applied = restart.Apply(parts.at(index))) {
				restarted = applied;
			}
		}
		assert(restarted && *restarted == large);

		Reassembler mismatched;
		assert(!mismatched.Apply(parts.at(0)).has_value());
		auto otherBase = parts.at(1);
		otherBase["version"] = 8u;
		assert(!mismatched.Apply(otherBase).has_value());
		auto resumed = mismatched.Apply(parts.at(0));
		assert(!resumed.has_value());
		for (std::size_t index = 1; index < parts.size(); ++index) {
			if (const auto applied = mismatched.Apply(parts.at(index))) {
				resumed = applied;
			}
		}
		assert(resumed && *resumed == large);

		auto bad = parts.at(0);
		bad["parts"] = 1;
		assert(!DecodeMessage(bad).has_value());
		bad = parts.at(0);
		bad["part"] = parts.size();
		assert(!DecodeMessage(bad).has_value());
		bad = parts.at(0);
		bad["extra"] = 1;
		assert(!DecodeMessage(bad).has_value());
		bad = parts.at(0);
		bad.erase("part");
		assert(!DecodeMessage(bad).has_value());
		bad = parts.at(0);
		bad["type"] = "somethingElse";
		assert(!DecodeMessage(bad).has_value());
		bad = single.front();
		bad["part"] = 0;
		assert(!DecodeMessage(bad).has_value());
		bad = single.front();
		bad["disable"] = Json::array({ "00000F:Fallout4.esm", "00000F:Fallout4.esm" });
		assert(!DecodeMessage(bad).has_value());
	}

	Json PartialPacket(std::size_t a_part, std::size_t a_parts, std::size_t a_entries)
	{
		Json packet{
			{ "type", std::string{ kPartialMessageType } },
			{ "part", a_part },
			{ "parts", a_parts },
			{ "mode", "free-roam" },
			{ "version", 3u },
			{ "doors", "shared" },
			{ "suppressQuests", true },
			{ "disable", Json::array() }
		};
		for (std::size_t index = 0; index < a_entries; ++index) {
			packet["disable"].push_back(FormatFormKey(FormKey{ "p.esm", static_cast<std::uint32_t>(index + 1) }));
		}
		return packet;
	}

	void TestChunkedEntryCaps()
	{
		assert(kMaximumMessageParts * kMaximumDisableEntriesPerMessage == kMaximumTotalDisableEntries);

		assert(DecodeMessage(PartialPacket(0, 2, kMaximumDisableEntriesPerMessage)).has_value());
		assert(!DecodeMessage(PartialPacket(0, 2, kMaximumDisableEntriesPerMessage + 1)).has_value());
		assert(!DecodeMessage(PartialPacket(0, kMaximumMessageParts + 1, 1)).has_value());
		assert(!DecodeMessage(PartialPacket(0, 2, kMaximumClassEntries)).has_value());

		Policy atCap;
		atCap.mode = Mode::FreeRoam;
		atCap.version = 3u;
		atCap.suppressQuests = true;
		atCap.disable.reserve(kMaximumTotalDisableEntries);
		for (std::size_t index = 0; index < kMaximumTotalDisableEntries; ++index) {
			atCap.disable.push_back(FormKey{ "p.esm", static_cast<std::uint32_t>(index + 1) });
		}

		const auto parts = EncodeMessages(atCap);
		assert(parts.size() == kMaximumMessageParts);
		Reassembler reassembler;
		std::optional<Policy> complete;
		for (const auto& part : parts) {
			assert(part.at("disable").size() == kMaximumDisableEntriesPerMessage);
			if (const auto applied = reassembler.Apply(part)) {
				complete = applied;
			}
		}
		assert(complete && complete->disable.size() == kMaximumTotalDisableEntries);
		assert(*complete == atCap);

		Policy overCap = atCap;
		overCap.disable.push_back(FormKey{ "p.esm", 1u });
		const auto overParts = EncodeMessages(overCap);
		assert(overParts.size() == kMaximumMessageParts + 1);
		Reassembler rejected;
		for (const auto& part : overParts) {
			assert(!rejected.Apply(part).has_value());
		}
	}

	void TestParsePolicyRejectsMalformed()
	{
		const auto data = ParseData(kSample);
		assert(data);
		const auto packet = ToJson(DerivePolicy(*data, Mode::CoopStory, true, Vats::Disabled));
		assert(ParsePolicy(packet).has_value());
		assert(!ParsePolicy(Json::array()).has_value());

		const auto mutate = [&](const char* a_key, const Json& a_value) {
			auto copy = packet;
			copy[a_key] = a_value;
			return copy;
		};
		assert(!ParsePolicy(mutate("mode", "sandbox")).has_value());
		assert(!ParsePolicy(mutate("mode", 1)).has_value());
		assert(!ParsePolicy(mutate("version", -1)).has_value());
		assert(!ParsePolicy(mutate("version", kMaximumDataVersion + 1)).has_value());
		assert(ParsePolicy(mutate("version", 0)).has_value());
		assert(!ParsePolicy(mutate("doors", "open")).has_value());
		assert(!ParsePolicy(mutate("suppressQuests", "true")).has_value());
		assert(!ParsePolicy(mutate("disable", Json::array({ "nope" }))).has_value());
		assert(!ParsePolicy(mutate("vats", "slow-mo")).has_value());
		assert(!ParsePolicy(mutate("vats", 1)).has_value());
		assert(ParsePolicy(mutate("vats", "real-time"))->vats == Vats::RealTime);
		assert(!ParsePolicy(mutate("extra", 1)).has_value());
		assert(!ParsePolicy(mutate("allowQuests", Json::array())).has_value());

		const char* const contractKeys[] = { "type", "mode", "version", "disable", "doors", "suppressQuests", "vats" };
		assert(packet.size() == sizeof(contractKeys) / sizeof(contractKeys[0]));
		for (const char* key : contractKeys) {
			assert(packet.count(key) == 1);
		}
		assert(ParsePolicy(packet).has_value());

		auto missing = packet;
		missing.erase("doors");
		assert(!ParsePolicy(missing).has_value());

		auto wrongType = packet;
		wrongType["type"] = "notWorldPolicy";
		assert(!ParsePolicy(wrongType).has_value());

		auto noType = packet;
		noType.erase("type");
		assert(ParsePolicy(noType).has_value());

		auto nullVersion = packet;
		nullVersion["version"] = nullptr;
		assert(!ParsePolicy(nullVersion).has_value());
	}
}

int main()
{
	TestFormKeyRoundTrip();
	TestClassNames();
	TestParseDataValid();
	TestParseDataRejectsMalformed();
	TestLoadDataFromFile();
	TestModeNames();
	TestSampleMarker();
	TestCanServeMode();
	TestDerivePolicyPerMode();
	TestDerivePolicyServerDefault();
	TestDerivePolicyDeduplicates();
	TestPolicyWireRoundTrip();
	TestVatsNames();
	TestVatsWireCompatibility();
	TestChunkedMessages();
	TestChunkedEntryCaps();
	TestParsePolicyRejectsMalformed();
	return 0;
}
