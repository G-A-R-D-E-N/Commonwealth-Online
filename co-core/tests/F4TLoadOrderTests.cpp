#include "F4TLoadOrder.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

namespace
{
	using F4T::LoadOrder::BuildPluginListPacket;
	using F4T::LoadOrder::BuildTranslationTable;
	using F4T::LoadOrder::DecodePluginList;
	using F4T::LoadOrder::EncodePluginList;
	using F4T::LoadOrder::Plugin;
	using F4T::LoadOrder::PluginKind;
	using F4T::LoadOrder::PluginList;
	using F4T::LoadOrder::Registry;

	Plugin Full(std::uint16_t a_index, std::string a_name)
	{
		return Plugin{ PluginKind::full, a_index, std::move(a_name) };
	}

	Plugin Light(std::uint16_t a_index, std::string a_name)
	{
		return Plugin{ PluginKind::light, a_index, std::move(a_name) };
	}

	PluginList SampleList()
	{
		PluginList list;
		list.full = {
			Full(0, "Fallout4.esm"),
			Full(1, "DLCCoast.esm"),
			Full(2, "MyMod.esp")
		};
		list.light = {
			Light(0, "ccA.esl"),
			Light(1, "ccB.esl")
		};
		return list;
	}

	void TestRoundTrip()
	{
		const auto sample = SampleList();
		const auto encoded = EncodePluginList(sample);
		assert(encoded.has_value());
		const auto decoded = DecodePluginList(*encoded);
		assert(decoded.has_value());
		assert(decoded->full.size() == sample.full.size());
		assert(decoded->light.size() == sample.light.size());
		for (std::size_t i = 0; i < sample.full.size(); ++i) {
			assert(decoded->full[i].index == sample.full[i].index);
			assert(decoded->full[i].name == sample.full[i].name);
		}
		for (std::size_t i = 0; i < sample.light.size(); ++i) {
			assert(decoded->light[i].index == sample.light[i].index);
			assert(decoded->light[i].name == sample.light[i].name);
		}
	}

	void TestPacketBuild()
	{
		const auto packet = BuildPluginListPacket(SampleList());
		assert(packet.has_value());
		const auto json = nlohmann::json::parse(*packet);
		assert(json.at("type") == "pluginList");
		const auto decoded = DecodePluginList(json.at("data").get<std::string>());
		assert(decoded.has_value());
		assert(decoded->full.size() == 3);
	}

	void TestRejectsMalformedLists()
	{
		assert(!DecodePluginList("").has_value());
		assert(!DecodePluginList("!!!!").has_value());
		assert(!DecodePluginList("AgEAAAA=").has_value());
		assert(!DecodePluginList("AQAAAAA=").has_value());
		assert(!DecodePluginList("AQEAAAA=").has_value());
		assert(!DecodePluginList("AQEAAAAAAUEA").has_value());
		assert(!DecodePluginList("AQEAAAD+AUE=").has_value());
		assert(!DecodePluginList("AQEBAAAAAUEAEAFC").has_value());

		PluginList duplicateName;
		duplicateName.full = { Full(0, "Fallout4.esm"), Full(1, "Fallout4.esm") };
		assert(!EncodePluginList(duplicateName).has_value());

		PluginList caseDuplicate;
		caseDuplicate.full = { Full(0, "Fallout4.esm"), Full(1, "fallout4.ESM") };
		assert(!EncodePluginList(caseDuplicate).has_value());

		PluginList duplicateSlot;
		duplicateSlot.full = { Full(0, "Fallout4.esm"), Full(0, "Other.esp") };
		assert(!EncodePluginList(duplicateSlot).has_value());

		PluginList noFull;
		noFull.light = { Light(0, "ccA.esl") };
		assert(!EncodePluginList(noFull).has_value());

		PluginList reservedSlot;
		reservedSlot.full = { Full(0xFE, "Fallout4.esm") };
		assert(!EncodePluginList(reservedSlot).has_value());

		PluginList lightOverflow;
		lightOverflow.full = { Full(0, "Fallout4.esm") };
		lightOverflow.light = { Light(0x1000, "ccA.esl") };
		assert(!EncodePluginList(lightOverflow).has_value());

		PluginList controlName;
		controlName.full = { Full(0, "Bad\nName.esp") };
		assert(!EncodePluginList(controlName).has_value());

		PluginList pathName;
		pathName.full = { Full(0, "Sub/Name.esp") };
		assert(!EncodePluginList(pathName).has_value());

		PluginList longName;
		longName.full = { Full(0, std::string(256, 'a')) };
		assert(!EncodePluginList(longName).has_value());
	}

	void TestTranslation()
	{
		auto local = SampleList();
		auto remote = SampleList();
		std::swap(remote.full[1], remote.full[2]);
		remote.full[1].index = 1;
		remote.full[2].index = 2;
		std::swap(remote.light[0], remote.light[1]);
		remote.light[0].index = 0;
		remote.light[1].index = 1;

		const auto table = BuildTranslationTable(local, remote);
		assert(!table.identical);
		assert(table.reordered);
		assert(table.differences == "Same mods, different load order (translated).");

		assert(table.ToLocal(0x00000000u) == 0x00000000u);
		assert(table.ToLocal(0x0001F4A6u) == 0x0001F4A6u);
		assert(table.ToLocal(0x0100ABCDu) == 0x0200ABCDu);
		assert(table.ToLocal(0x0200ABCDu) == 0x0100ABCDu);
		assert(table.ToLocal(0xFE000123u) == 0xFE001123u);
		assert(table.ToLocal(0xFE001123u) == 0xFE000123u);
		assert(table.ToLocal(0xFF001234u) == 0xFF001234u);

		auto missing = remote;
		missing.full.push_back(Full(3, "OnlyTheyHave.esp"));
		const auto withMissing = BuildTranslationTable(local, missing);
		assert(withMissing.ToLocal(0x0300BEEFu) == 0x00000000u);
		assert(withMissing.missingLocal.size() == 1);
		assert(withMissing.differences == "They have mods you don't: OnlyTheyHave.esp.");
	}

	void TestIdenticalAndMissingRemote()
	{
		const auto local = SampleList();
		const auto identical = BuildTranslationTable(local, local);
		assert(identical.identical);
		assert(!identical.reordered);
		assert(identical.differences.empty());
		assert(identical.ToLocal(0x0200ABCDu) == 0x0200ABCDu);

		PluginList extraLocal = local;
		extraLocal.full.push_back(Full(3, "OnlyWeHave.esp"));
		const auto table = BuildTranslationTable(extraLocal, local);
		assert(!table.identical);
		assert(!table.reordered);
		assert(table.missingRemote.size() == 1);
		assert(table.differences == "You have mods they don't: OnlyWeHave.esp.");
	}

	void TestDifferencesCap()
	{
		auto local = SampleList();
		PluginList remote = local;
		remote.full.push_back(Full(3, "A.esp"));
		remote.full.push_back(Full(4, "B.esp"));
		remote.full.push_back(Full(5, "C.esp"));
		remote.full.push_back(Full(6, "D.esp"));
		remote.full.push_back(Full(7, "E.esp"));
		const auto table = BuildTranslationTable(local, remote);
		assert(table.differences == "They have mods you don't: A.esp, B.esp, C.esp (+2 more).");
	}

	void TestRegistry()
	{
		Registry registry;
		assert(!registry.Known(7));
		assert(!registry.HasLocalList());
		assert(registry.ToLocal(7, 0x0100ABCDu) == 0x0100ABCDu);

		auto remote = SampleList();
		registry.SetRemoteList(7, remote);
		assert(!registry.Known(7));
		assert(registry.ToLocal(7, 0x0100ABCDu) == 0x0100ABCDu);

		auto local = SampleList();
		registry.SetLocalList(local);
		assert(registry.HasLocalList());
		assert(registry.Known(7));
		assert(registry.Identical(7));
		assert(registry.ToLocal(7, 0x0100ABCDu) == 0x0100ABCDu);

		auto reordered = SampleList();
		std::swap(reordered.full[1], reordered.full[2]);
		reordered.full[1].index = 1;
		reordered.full[2].index = 2;
		registry.SetRemoteList(7, reordered);
		assert(!registry.Identical(7));
		assert(registry.ToLocal(7, 0x0100ABCDu) == 0x0200ABCDu);
		assert(registry.ToLocal(7, 0xFF000001u) == 0xFF000001u);
		assert(registry.ToLocal(7, 0x00000000u) == 0x00000000u);

		auto missing = reordered;
		missing.full.push_back(Full(3, "OnlyTheyHave.esp"));
		registry.SetRemoteList(7, missing);
		const auto droppedBefore = registry.Dropped();
		assert(registry.ToLocal(7, 0x0300BEEFu) == 0u);
		assert(registry.Dropped() == droppedBefore + 1);
		assert(registry.Differences(7).starts_with("They have mods you don't:"));

		registry.Forget(7);
		assert(!registry.Known(7));
		assert(registry.ToLocal(7, 0x0100ABCDu) == 0x0100ABCDu);

		registry.SetRemoteList(8, remote);
		registry.Reset();
		assert(!registry.Known(8));
		assert(!registry.HasLocalList());
	}

	void TestReorderedLocalRebuild()
	{
		Registry registry;
		auto local = SampleList();
		auto remote = SampleList();
		std::swap(remote.full[1], remote.full[2]);
		remote.full[1].index = 1;
		remote.full[2].index = 2;
		registry.SetLocalList(local);
		registry.SetRemoteList(3, remote);
		assert(registry.ToLocal(3, 0x0100ABCDu) == 0x0200ABCDu);

		auto otherLocal = SampleList();
		std::swap(otherLocal.full[1], otherLocal.full[2]);
		otherLocal.full[1].index = 1;
		otherLocal.full[2].index = 2;
		registry.SetLocalList(otherLocal);
		assert(registry.ToLocal(3, 0x0100ABCDu) == 0x0100ABCDu);
	}
}

int main()
{
	TestRoundTrip();
	TestPacketBuild();
	TestRejectsMalformedLists();
	TestTranslation();
	TestIdenticalAndMissingRemote();
	TestDifferencesCap();
	TestRegistry();
	TestReorderedLocalRebuild();
	return 0;
}
