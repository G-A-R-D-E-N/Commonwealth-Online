#include "F4TModpackManifest.h"
#include "F4TModpackProbe.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using F4T::ModpackManifest::Entry;
	using F4T::ModpackManifest::EntryKind;
	using F4T::ModpackManifest::Manifest;
	using F4T::ModpackManifest::ManifestDiff;
	using F4T::ModpackManifest::PluginType;
	using F4T::ModpackProbe::kMissingPluginOrder;

	constexpr std::string_view kHashA = "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73";
	constexpr std::string_view kHashB = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";

	Entry Plugin(std::string a_name, std::uint32_t a_order)
	{
		return Entry{ EntryKind::plugin, PluginType::esp, std::move(a_name), a_order, std::string{ kHashA }, std::nullopt };
	}

	Manifest Required()
	{
		return Manifest{ { Plugin("fallout4.esm", 0), Plugin("a.esp", 1), Plugin("b.esp", 2), Plugin("c.esp", 3) } };
	}

	Manifest Normalized(Manifest a_manifest)
	{
		const auto normalized = F4T::ModpackManifest::NormalizeManifest(std::move(a_manifest));
		assert(normalized.has_value());
		return *normalized;
	}

	ManifestDiff Diff(const Manifest& a_required, const Manifest& a_client)
	{
		const auto diff = F4T::ModpackManifest::DiffManifests(a_required, a_client);
		assert(diff.has_value());
		return *diff;
	}

	void TestRankUsesActualLoadOrder()
	{
		const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(
			Required(),
			{ "Fallout4.esm", "C.esp", "A.esp", "B.esp" });
		assert(ranks.size() == 4);
		assert(ranks[0] == 0);
		assert(ranks[1] == 2);
		assert(ranks[2] == 3);
		assert(ranks[3] == 1);
	}

	void TestRankMarksMissingPlugins()
	{
		const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(
			Required(),
			{ "fallout4.esm", "a.esp", "c.esp" });
		assert(ranks.size() == 4);
		assert(ranks[0] == 0);
		assert(ranks[1] == 1);
		assert(ranks[2] == kMissingPluginOrder);
		assert(ranks[3] == 2);
	}

	void TestClientOrdersFollowTheLoadedOrder()
	{
		const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(
			Required(),
			{ "fallout4.esm", "c.esp", "a.esp", "b.esp" });
		const auto orders = F4T::ModpackProbe::ContiguousOrdersFromRanks(ranks);
		assert((orders == std::vector<std::uint32_t>{ 0, 2, 3, 1 }));
		assert(orders != (std::vector<std::uint32_t>{ 0, 1, 2, 3 }));
	}

	void TestOrdersStayContiguousWhenARequiredPluginIsMissing()
	{
		const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(
			Required(),
			{ "fallout4.esm", "c.esp", "b.esp" });
		std::vector<std::uint32_t> present;
		for (const auto rank : ranks) {
			if (rank != kMissingPluginOrder) {
				present.push_back(rank);
			}
		}
		const auto orders = F4T::ModpackProbe::ContiguousOrdersFromRanks(present);
		assert((orders == std::vector<std::uint32_t>{ 0, 2, 1 }));
	}

	void TestOrdersCloseGapsLeftByDroppedPlugins()
	{
		const auto orders = F4T::ModpackProbe::ContiguousOrdersFromRanks({ 4, 1, 9 });
		assert((orders == std::vector<std::uint32_t>{ 1, 0, 2 }));
	}

	void TestExtraPluginsAreListedCaseInsensitively()
	{
		const auto extra = F4T::ModpackProbe::FindExtraPlugins(
			Required(),
			{ "FALLOUT4.ESM", "a.esp", "b.esp", "c.esp", "DLC.esp", "dlc.esp" });
		assert(extra.size() == 1);
		assert(extra[0] == "DLC.esp");
	}

	void TestExtraNativesAreListed()
	{
		Manifest required;
		required.entries.push_back(Entry{ EntryKind::native, PluginType::none, "f4se/plugins/co.dll", 0, std::string{ kHashA }, std::nullopt });
		const auto extra = F4T::ModpackProbe::FindExtraNatives(
			required,
			{ "f4se/plugins/co.dll", "f4se/plugins/addresslibrary.dll" });
		assert(extra.size() == 1);
		assert(extra[0] == "f4se/plugins/addresslibrary.dll");
	}

	void TestPluginTypeFromName()
	{
		assert(F4T::ModpackProbe::PluginTypeFromName("Fallout4.esm") == PluginType::esm);
		assert(F4T::ModpackProbe::PluginTypeFromName("Mod.ESP") == PluginType::esp);
		assert(F4T::ModpackProbe::PluginTypeFromName("Light.ESL") == PluginType::esl);
	}

	void TestSwappedOrderIsADifferenceAndBreaksTheFingerprint()
	{
		const auto required = Normalized(Required());
		const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(required, { "c.esp", "b.esp", "a.esp", "fallout4.esm" });
		Manifest client;
		std::uint32_t next = 0;
		for (const auto& entry : required.entries) {
			auto copy = entry;
			copy.order = ranks[next++];
			client.entries.push_back(std::move(copy));
		}
		const auto normalized = F4T::ModpackManifest::NormalizeManifest(client);
		assert(normalized.has_value());
		const auto diff = Diff(required, *normalized);
		assert(!diff.wrongOrder.empty());
		assert(diff.missing.empty() && diff.unexpected.empty());
		assert(!F4T::ModpackManifest::MatchingFingerprint(required, *normalized).has_value());
	}

	void TestExtraPluginIsUnexpectedAndBreaksTheFingerprint()
	{
		const auto required = Normalized(Required());
		Manifest client = required;
		client.entries.push_back(Entry{ EntryKind::plugin, PluginType::esp, "extra.esp", 4, {}, std::uint64_t{ 128 } });
		const auto normalized = F4T::ModpackManifest::NormalizeManifest(client);
		assert(normalized.has_value());
		const auto diff = Diff(required, *normalized);
		assert(diff.unexpected.size() == 1);
		assert(diff.unexpected[0].name == "extra.esp");
		assert(diff.wrongOrder.empty());
		assert(!F4T::ModpackManifest::MatchingFingerprint(required, *normalized).has_value());
	}

	void TestMissingPluginKeepsTheRelativeOrderIntact()
	{
		const auto required = Normalized(Required());
		const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(required, { "fallout4.esm", "a.esp", "c.esp" });
		Manifest client;
		std::size_t index = 0;
		for (const auto& entry : required.entries) {
			if (ranks[index] != kMissingPluginOrder) {
				auto copy = entry;
				copy.order = ranks[index];
				client.entries.push_back(std::move(copy));
			}
			++index;
		}
		const auto normalized = F4T::ModpackManifest::NormalizeManifest(client);
		assert(normalized.has_value());
		const auto diff = Diff(required, *normalized);
		assert(diff.missing.size() == 1 && diff.missing[0].name == "b.esp");
		assert(diff.wrongOrder.empty());
	}
}

int main()
{
	TestRankUsesActualLoadOrder();
	TestRankMarksMissingPlugins();
	TestClientOrdersFollowTheLoadedOrder();
	TestOrdersStayContiguousWhenARequiredPluginIsMissing();
	TestOrdersCloseGapsLeftByDroppedPlugins();
	TestExtraPluginsAreListedCaseInsensitively();
	TestExtraNativesAreListed();
	TestPluginTypeFromName();
	TestSwappedOrderIsADifferenceAndBreaksTheFingerprint();
	TestExtraPluginIsUnexpectedAndBreaksTheFingerprint();
	TestMissingPluginKeepsTheRelativeOrderIntact();
	return 0;
}
