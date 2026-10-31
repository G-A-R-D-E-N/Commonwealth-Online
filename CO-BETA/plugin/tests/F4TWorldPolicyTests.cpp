#include "F4TWorldPolicy.h"
#include "F4TWorldPolicyClient.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using F4T::WorldPolicy::Doors;
	using F4T::WorldPolicy::FormKey;
	using F4T::WorldPolicy::Mode;
	using F4T::WorldPolicy::Policy;
	using F4T::WorldPolicy::Vats;
	using F4T::WorldPolicyClient::DecideForQuest;
	using F4T::WorldPolicyClient::DecideRefAction;
	using F4T::WorldPolicyClient::IsRemovalTarget;
	using F4T::WorldPolicyClient::IsStoryQuestType;
	using F4T::WorldPolicyClient::QuestAction;
	using F4T::WorldPolicyClient::RefAction;
	using F4T::WorldPolicyClient::RefClass;
	using F4T::WorldPolicyClient::RefState;

	FormKey Key(std::uint32_t a_localId, std::string a_plugin)
	{
		return FormKey{ std::move(a_plugin), a_localId };
	}

	Policy FreeRoamPolicy()
	{
		Policy policy;
		policy.mode = Mode::FreeRoam;
		policy.version = 7;
		policy.doors = Doors::Shared;
		policy.suppressQuests = true;
		policy.disable = { Key(0x01A2B3, "Fallout4.esm"), Key(0x000800, "StoryChars.esp") };
		return policy;
	}

	void TestModes()
	{
		assert(F4T::WorldPolicy::ParseMode("coop-story") == Mode::CoopStory);
		assert(F4T::WorldPolicy::ParseMode("free-roam") == Mode::FreeRoam);
		assert(F4T::WorldPolicy::ParseMode(" Free-Roam ") == Mode::FreeRoam);
		assert(!F4T::WorldPolicy::ParseMode("faction-war").has_value());
		assert(!F4T::WorldPolicy::ParseMode("").has_value());

		assert(F4T::WorldPolicy::ParseMode(F4T::WorldPolicy::ModeName(Mode::CoopStory)) == Mode::CoopStory);
		assert(F4T::WorldPolicy::ParseMode(F4T::WorldPolicy::ModeName(Mode::FreeRoam)) == Mode::FreeRoam);
		assert(F4T::WorldPolicy::ModeName(Mode::CoopStory) == "coop-story");
		assert(F4T::WorldPolicy::ModeName(Mode::FreeRoam) == "free-roam");
		assert(F4T::WorldPolicy::DoorsName(Doors::Shared) == "shared");
		assert(F4T::WorldPolicy::DoorsName(Doors::Vanilla) == "vanilla");
	}

	void TestQuestTypes()
	{
		assert(!IsStoryQuestType(0));
		assert(IsStoryQuestType(1));
		assert(IsStoryQuestType(2));
		assert(IsStoryQuestType(3));
		assert(IsStoryQuestType(4));
		assert(IsStoryQuestType(5));
		assert(!IsStoryQuestType(6));
		assert(IsStoryQuestType(7));
		assert(IsStoryQuestType(8));
		assert(IsStoryQuestType(9));
		assert(IsStoryQuestType(10));
		assert(!IsStoryQuestType(11));
		assert(!IsStoryQuestType(-1));
	}

	void TestSuppression()
	{
		Policy coop;
		coop.mode = Mode::CoopStory;
		assert(!coop.suppressQuests);
		assert(coop.doors == Doors::Shared);

		Policy freeRoam = coop;
		freeRoam.mode = Mode::FreeRoam;
		assert(!freeRoam.suppressQuests);

		freeRoam.suppressQuests = true;
		assert(freeRoam.suppressQuests);

		Policy doorsOnly = coop;
		doorsOnly.doors = Doors::Vanilla;
		assert(doorsOnly.doors == Doors::Vanilla);
		assert(coop.doors == Doors::Shared);

		Policy defaultPolicy;
		assert(defaultPolicy.doors == Doors::Shared);
	}

	void TestRemovalTargets()
	{
		const auto policy = FreeRoamPolicy();
		Policy noList = policy;
		noList.disable.clear();

		assert(IsRemovalTarget(policy, RefClass::World, true, false));
		assert(IsRemovalTarget(policy, RefClass::World, false, true));
		assert(IsRemovalTarget(policy, RefClass::Actor, true, false));
		assert(IsRemovalTarget(policy, RefClass::Actor, true, true));

		assert(!IsRemovalTarget(policy, RefClass::World, false, false));
		assert(!IsRemovalTarget(noList, RefClass::World, true, true));
		assert(!IsRemovalTarget(noList, RefClass::Actor, true, false));

		assert(!IsRemovalTarget(policy, RefClass::Player, true, true));
		assert(!IsRemovalTarget(policy, RefClass::ProxyActor, true, true));
		assert(!IsRemovalTarget(policy, RefClass::VendorActor, true, true));
		assert(!IsRemovalTarget(policy, RefClass::VendorActor, false, true));
	}

	void TestRefActions()
	{
		assert(DecideRefAction(RefState::Unknown) == RefAction::Skip);
		assert(DecideRefAction(RefState::Deleted) == RefAction::Skip);
		assert(DecideRefAction(RefState::DisabledNotDeleted) == RefAction::Skip);
		assert(DecideRefAction(RefState::Live) == RefAction::Disable);
	}

	void TestIdempotentReapplication()
	{
		RefState state = RefState::Live;
		std::vector<RefAction> observed;

		for (int pass = 0; pass < 5; ++pass) {
			const auto action = DecideRefAction(state);
			observed.push_back(action);

			if (action == RefAction::Disable) {
				state = RefState::DisabledNotDeleted;
			}
		}

		assert(observed.size() == 5);
		assert(observed[0] == RefAction::Disable);
		assert(observed[1] == RefAction::Skip);
		assert(observed[2] == RefAction::Skip);
		assert(observed[3] == RefAction::Skip);
		assert(observed[4] == RefAction::Skip);
		assert(state == RefState::DisabledNotDeleted);
	}

	void TestQuestDecisions()
	{
		const auto policy = FreeRoamPolicy();
		assert(DecideForQuest(policy, false, 1) == QuestAction::Keep);
		assert(DecideForQuest(policy, true, 0) == QuestAction::Keep);
		assert(DecideForQuest(policy, true, 6) == QuestAction::Keep);
		assert(DecideForQuest(policy, true, 11) == QuestAction::Keep);
		assert(DecideForQuest(policy, true, 1) == QuestAction::Stop);
		assert(DecideForQuest(policy, true, 2) == QuestAction::Stop);
		assert(DecideForQuest(policy, true, 5) == QuestAction::Stop);
		assert(DecideForQuest(policy, true, 7) == QuestAction::Stop);
		assert(DecideForQuest(policy, true, 10) == QuestAction::Stop);

		Policy coop;
		coop.mode = Mode::CoopStory;
		assert(DecideForQuest(coop, true, 1) == QuestAction::Keep);

		Policy coopExplicit = coop;
		coopExplicit.suppressQuests = true;
		assert(DecideForQuest(coopExplicit, true, 1) == QuestAction::Stop);
	}

	void TestPendingCellOrdering()
	{
		F4T::WorldPolicyClient::PendingCellQueue queue;
		assert(queue.Size() == 0);
		assert(queue.Drain().empty());

		queue.Push(0x0000A001);
		queue.Push(0x0000A002);
		queue.Push(0x0000A003);
		assert(queue.Size() == 3);
		assert(queue.Contains(0x0000A002));
		assert(!queue.Contains(0x0000A004));

		queue.Push(0x0000A002);
		queue.Push(0);
		assert(queue.Size() == 3);

		const auto drained = queue.Drain();
		assert(drained.size() == 3);
		assert(drained[0] == 0x0000A001);
		assert(drained[1] == 0x0000A002);
		assert(drained[2] == 0x0000A003);
		assert(queue.Size() == 0);

		F4T::WorldPolicyClient::PendingCellQueue bounded;
		for (std::uint32_t index = 1; index <= 200; ++index) {
			bounded.Push(0x0000B000 + index);
		}
		assert(bounded.Size() == F4T::WorldPolicyClient::kMaximumPendingCells);
		assert(bounded.Contains(0x0000B001));
		assert(bounded.Contains(0x0000B040));
		assert(!bounded.Contains(0x0000B041));
		assert(!bounded.Contains(0x0000B0C8));

		bounded.Clear();
		assert(bounded.Size() == 0);
		assert(bounded.Drain().empty());
	}

	void TestFormKeyCodec()
	{
		using F4T::WorldPolicy::FormatFormKey;
		using F4T::WorldPolicy::ParseFormKey;

		const auto parsed = ParseFormKey("01A2B3:Fallout4.esm");
		assert(parsed.has_value());
		assert(parsed->localId == 0x01A2B3);
		assert(parsed->plugin == "Fallout4.esm");
		assert(FormatFormKey(*parsed) == "01A2B3:Fallout4.esm");

		const auto lowercase = ParseFormKey("000abc:Mod.esp");
		assert(lowercase.has_value());
		assert(lowercase->localId == 0x000ABC);
		assert(FormatFormKey(*lowercase) == "000ABC:Mod.esp");

		assert(!ParseFormKey("").has_value());
		assert(!ParseFormKey("01A2B3").has_value());
		assert(!ParseFormKey("01A2B:Mod.esp").has_value());
		assert(!ParseFormKey("01A2B33:Mod.esp").has_value());
		assert(!ParseFormKey("01A2BG:Mod.esp").has_value());
		assert(!ParseFormKey("000000:Mod.esp").has_value());
		assert(!ParseFormKey("01A2B3:").has_value());
		assert(!ParseFormKey("01A2B3:Mod.esp\n").has_value());
		assert(!ParseFormKey("01A2B3:Mod\t.esp").has_value());
		assert(!ParseFormKey("01A2B3:Mod:esp").has_value());

		const std::string longPlugin(F4T::WorldPolicy::kMaximumPluginNameBytes, 'a');
		assert(ParseFormKey("01A2B3:" + longPlugin).has_value());
		assert(!ParseFormKey("01A2B3:" + longPlugin + "a").has_value());
	}

	void TestInboundOrdering()
	{
		using F4T::WorldPolicyClient::InboundPolicyState;
		using F4T::WorldPolicyClient::kNoSessionGeneration;

		Policy early = FreeRoamPolicy();
		early.version = 1;
		Policy later = FreeRoamPolicy();
		later.version = 2;
		later.doors = Doors::Vanilla;

		{
			InboundPolicyState state;
			assert(state.Store(early, kNoSessionGeneration));
			Policy adopted;
			assert(!state.TakeAdoptable(kNoSessionGeneration, adopted));
			state.OnSessionGeneration(5);
			assert(state.TakeAdoptable(5, adopted));
			assert(adopted.version == 1);
			assert(adopted.mode == Mode::FreeRoam);
			assert(!state.TakeAdoptable(5, adopted));
		}

		{
			InboundPolicyState state;
			assert(state.Store(early, 5));
			Policy adopted;
			assert(state.TakeAdoptable(5, adopted));
			assert(!state.TakeAdoptable(5, adopted));
			state.OnSessionGeneration(kNoSessionGeneration);
			assert(!state.TakeAdoptable(5, adopted));
			state.OnSessionGeneration(6);
			assert(!state.TakeAdoptable(6, adopted));
		}

		{
			InboundPolicyState state;
			assert(state.Store(early, 5));
			state.OnSessionGeneration(6);
			Policy adopted;
			assert(!state.TakeAdoptable(6, adopted));
		}

		{
			InboundPolicyState state;
			assert(state.Store(later, 5));
			Policy adopted;
			assert(state.TakeAdoptable(5, adopted));
			assert(adopted.version == 2);
			assert(!state.Store(early, 5));
			assert(!state.Store(later, 5));
			Policy newer = later;
			newer.version = 3;
			assert(state.Store(newer, 5));
			assert(state.TakeAdoptable(5, adopted));
			assert(adopted.version == 3);
		}

		{
			InboundPolicyState state;
			assert(state.Store(early, 5));
			Policy adopted;
			assert(!state.TakeAdoptable(6, adopted));
			assert(state.TakeAdoptable(5, adopted));
			assert(adopted.version == 1);
		}

		{
			InboundPolicyState state;
			assert(state.Store(early, kNoSessionGeneration));
			state.OnSessionGeneration(kNoSessionGeneration);
			state.OnSessionGeneration(7);
			Policy adopted;
			assert(!state.TakeAdoptable(7, adopted));
		}

		{
			InboundPolicyState state;
			Policy stale = FreeRoamPolicy();
			stale.version = 5;
			Policy fresh = FreeRoamPolicy();
			fresh.version = 1;
			assert(state.Store(stale, kNoSessionGeneration));
			state.OnSessionGeneration(8);
			assert(state.Store(fresh, 8));
			Policy adopted;
			assert(state.TakeAdoptable(8, adopted));
			assert(adopted.version == 1);
			Policy older = FreeRoamPolicy();
			older.version = 0;
			assert(!state.Store(older, 8));
			assert(!state.Store(fresh, 8));
		}

		{
			InboundPolicyState state;
			Policy stale = FreeRoamPolicy();
			stale.version = 5;
			Policy fresh = FreeRoamPolicy();
			fresh.version = 1;
			assert(state.Store(stale, kNoSessionGeneration));
			assert(state.Store(fresh, kNoSessionGeneration));
			state.OnSessionGeneration(8);
			Policy adopted;
			assert(state.TakeAdoptable(8, adopted));
			assert(adopted.version == 1);
		}

		{
			InboundPolicyState state;
			state.OnSessionGeneration(8);
			Policy late = FreeRoamPolicy();
			late.version = 3;
			assert(state.Store(late, kNoSessionGeneration));
			Policy adopted;
			assert(!state.TakeAdoptable(kNoSessionGeneration, adopted));
			assert(state.TakeAdoptable(8, adopted));
			assert(adopted.version == 3);
			assert(!state.TakeAdoptable(8, adopted));
		}
	}

	void TestTrackingCapacity()
	{
		using F4T::WorldPolicyClient::CanTrackDisabledRef;

		assert(CanTrackDisabledRef(0, 2, false));
		assert(CanTrackDisabledRef(1, 2, false));
		assert(!CanTrackDisabledRef(2, 2, false));
		assert(CanTrackDisabledRef(2, 2, true));
	}

	nlohmann::json WirePolicy(const Policy& a_policy)
	{
		nlohmann::json disable = nlohmann::json::array();
		for (const auto& key : a_policy.disable) {
			disable.push_back(F4T::WorldPolicy::FormatFormKey(key));
		}
		return nlohmann::json{
			{ "type", "worldPolicy" },
			{ "mode", std::string{ F4T::WorldPolicy::ModeName(a_policy.mode) } },
			{ "version", a_policy.version },
			{ "doors", std::string{ F4T::WorldPolicy::DoorsName(a_policy.doors) } },
			{ "suppressQuests", a_policy.suppressQuests },
			{ "vats", std::string{ F4T::WorldPolicy::VatsName(a_policy.vats) } },
			{ "disable", disable }
		};
	}

	void TestPolicyPacket()
	{
		using F4T::WorldPolicy::DecodeMessage;
		using F4T::WorldPolicy::ParsePolicy;
		using Json = nlohmann::json;

		const auto policy = FreeRoamPolicy();
		const auto wire = WirePolicy(policy);

		const auto roundTrip = ParsePolicy(wire);
		assert(roundTrip.has_value());
		assert(*roundTrip == policy);
		assert(DecodeMessage(wire)->parts == 1);

		assert(!ParsePolicy(Json{ { "type", "worldPolicy" } }).has_value());
		assert(!ParsePolicy(Json::array()).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldState" } }).has_value());

		Json withoutMode = wire;
		withoutMode.erase("mode");
		assert(!ParsePolicy(withoutMode).has_value());

		Json withoutVersion = wire;
		withoutVersion.erase("version");
		assert(!ParsePolicy(withoutVersion).has_value());

		Json withoutDoors = wire;
		withoutDoors.erase("doors");
		assert(!ParsePolicy(withoutDoors).has_value());

		Json withoutSuppress = wire;
		withoutSuppress.erase("suppressQuests");
		assert(!ParsePolicy(withoutSuppress).has_value());

		Json withoutDisable = wire;
		withoutDisable.erase("disable");
		assert(!ParsePolicy(withoutDisable).has_value());

		Json vanillaDoors = wire;
		vanillaDoors["doors"] = "vanilla";
		const auto vanilla = ParsePolicy(vanillaDoors);
		assert(vanilla.has_value());
		assert(vanilla->doors == Doors::Vanilla);

		Json legacyWire = wire;
		legacyWire.erase("vats");
		const auto legacy = ParsePolicy(legacyWire);
		assert(legacy.has_value());
		assert(legacy->vats == Vats::Vanilla);

		Json freeRoamNoSuppression = wire;
		freeRoamNoSuppression["suppressQuests"] = false;
		const auto unsuppressed = ParsePolicy(freeRoamNoSuppression);
		assert(unsuppressed.has_value());
		assert(unsuppressed->mode == Mode::FreeRoam);
		assert(!unsuppressed->suppressQuests);

		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "mode", "sandbox" } }).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "mode", 3 } }).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "version", -1 } }).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "version", "7" } }).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "doors", "open" } }).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "suppressQuests", "true" } }).has_value());
		assert(!ParsePolicy(Json{ { "type", "worldPolicy" }, { "disable", "000800:A.esp" } }).has_value());

		Json oneBadKey = wire;
		oneBadKey["disable"] = Json::array({ "01A2B3:Fallout4.esm", "800:A.esp", "000800:StoryChars.esp" });
		assert(!ParsePolicy(oneBadKey).has_value());

		Json nonStringKey = wire;
		nonStringKey["disable"] = Json::array({ "01A2B3:Fallout4.esm", 5, "000800:StoryChars.esp" });
		assert(!ParsePolicy(nonStringKey).has_value());

		Json duplicateKey = wire;
		duplicateKey["disable"] = Json::array({ "01A2B3:Fallout4.esm", "01A2B3:Fallout4.esm" });
		assert(!ParsePolicy(duplicateKey).has_value());

		Json extraKey = wire;
		extraKey["allowQuests"] = Json::array({ "01F0A1:Allowed.esp" });
		assert(!ParsePolicy(extraKey).has_value());

		Json maxVersion = wire;
		maxVersion["version"] = static_cast<std::uint64_t>(F4T::WorldPolicy::kMaximumDataVersion);
		const auto parsedMaxVersion = ParsePolicy(maxVersion);
		assert(parsedMaxVersion.has_value());
		assert(parsedMaxVersion->version == F4T::WorldPolicy::kMaximumDataVersion);
	}

	void TestChunkedReassembly()
	{
		using F4T::WorldPolicy::DecodeMessage;
		using F4T::WorldPolicy::EncodeMessages;
		using F4T::WorldPolicy::kMaximumDisableEntriesPerMessage;
		using F4T::WorldPolicy::kMaximumMessageParts;
		using F4T::WorldPolicy::kMaximumTotalDisableEntries;
		using F4T::WorldPolicy::ParsePolicy;
		using F4T::WorldPolicy::Reassembler;

		assert(kMaximumDisableEntriesPerMessage == 128);
		assert(kMaximumTotalDisableEntries == 131072);
		assert(kMaximumMessageParts * kMaximumDisableEntriesPerMessage == kMaximumTotalDisableEntries);

		Policy large = FreeRoamPolicy();
		large.disable.clear();
		const std::size_t entries = kMaximumDisableEntriesPerMessage * 2 + 5;
		for (std::size_t index = 0; index < entries; ++index) {
			large.disable.push_back(Key(static_cast<std::uint32_t>(index + 1), "Big.esp"));
		}

		const auto parts = EncodeMessages(large);
		assert(parts.size() == 3);
		assert(!ParsePolicy(parts.front()).has_value());

		Reassembler reassembler;
		std::optional<Policy> complete;
		for (std::size_t index = 0; index < parts.size(); ++index) {
			assert(parts[index].at("type") == std::string{ F4T::WorldPolicy::kPartialMessageType });
			assert(parts[index].at("part") == index);
			assert(parts[index].at("parts") == parts.size());
			assert(parts[index].at("disable").size() <= kMaximumDisableEntriesPerMessage);
			assert(DecodeMessage(parts[index]).has_value());
			if (const auto applied = reassembler.Apply(parts[index])) {
				complete = applied;
			}
		}
		assert(complete && *complete == large);

		Reassembler restarted;
		assert(!restarted.Apply(parts.at(1)).has_value());
		std::optional<Policy> resumed;
		for (const auto& part : parts) {
			if (const auto applied = restarted.Apply(part)) {
				resumed = applied;
			}
		}
		assert(resumed && *resumed == large);

		assert(reassembler.Apply(WirePolicy(large)).has_value());

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
	}
}

int main()
{
	TestModes();
	TestQuestTypes();
	TestSuppression();
	TestRemovalTargets();
	TestRefActions();
	TestIdempotentReapplication();
	TestQuestDecisions();
	TestPendingCellOrdering();
	TestFormKeyCodec();
	TestInboundOrdering();
	TestTrackingCapacity();
	TestPolicyPacket();
	TestChunkedReassembly();
	return 0;
}
