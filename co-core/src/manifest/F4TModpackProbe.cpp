#include "F4TModpackProbe.h"

#include "F4TWindowsCaseFold.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	using F4T::ModpackManifest::EntryKind;
	using F4T::ModpackManifest::Manifest;

	std::string NameKey(std::string_view a_name)
	{
		auto key = F4T::WindowsCaseFold::Key(a_name);
		if (!key) throw std::invalid_argument("A mod file name has invalid UTF-8 or unsupported Windows case mapping");
		return std::move(*key);
	}

	std::unordered_set<std::string> NamesOfKind(const Manifest& a_manifest, EntryKind a_kind)
	{
		std::unordered_set<std::string> names;
		for (const auto& entry : a_manifest.entries) {
			if (entry.kind == a_kind) {
				names.insert(NameKey(entry.name));
			}
		}
		return names;
	}


}

namespace F4T::ModpackProbe
{

	std::vector<std::uint32_t> RankRequiredPlugins(
		const ModpackManifest::Manifest& a_required,
		const std::vector<std::string>& a_loadedPluginNames)
	{
		std::vector<std::uint32_t> ranks;
		const auto required = ModpackManifest::NormalizeManifest(a_required);
		if (!required) {
			return ranks;
		}
		const auto requiredNames = NamesOfKind(*required, EntryKind::plugin);
		std::unordered_map<std::string, std::uint32_t> loadedRanks;
		std::uint32_t rank = 0;
		for (const auto& name : a_loadedPluginNames) {
			const auto lowered = NameKey(name);
			if (!requiredNames.contains(lowered)) {
				continue;
			}
			if (loadedRanks.emplace(lowered, rank).second) {
				++rank;
			}
		}
		for (const auto& entry : required->entries) {
			if (entry.kind != EntryKind::plugin) {
				continue;
			}
			const auto found = loadedRanks.find(NameKey(entry.name));
			ranks.push_back(found == loadedRanks.end() ? kMissingPluginOrder : found->second);
		}
		return ranks;
	}

	std::vector<std::uint32_t> ContiguousOrdersFromRanks(const std::vector<std::uint32_t>& a_ranks)
	{
		std::vector<std::size_t> positions(a_ranks.size());
		std::iota(positions.begin(), positions.end(), std::size_t{ 0 });
		std::stable_sort(positions.begin(), positions.end(), [&a_ranks](std::size_t a_lhs, std::size_t a_rhs) {
			return a_ranks[a_lhs] < a_ranks[a_rhs];
		});
		std::vector<std::uint32_t> orders(a_ranks.size());
		for (std::size_t order = 0; order < positions.size(); ++order) {
			orders[positions[order]] = static_cast<std::uint32_t>(order);
		}
		return orders;
	}

	std::vector<std::string> FindExtraPlugins(
		const ModpackManifest::Manifest& a_required,
		const std::vector<std::string>& a_loadedPluginNames)
	{
		std::vector<std::string> extra;
		const auto required = ModpackManifest::NormalizeManifest(a_required);
		if (!required) {
			return extra;
		}
		const auto requiredNames = NamesOfKind(*required, EntryKind::plugin);
		std::unordered_set<std::string> seen;
		for (const auto& name : a_loadedPluginNames) {
			const auto lowered = NameKey(name);
			if (requiredNames.contains(lowered) || !seen.insert(lowered).second) {
				continue;
			}
			extra.push_back(name);
		}
		return extra;
	}

	std::vector<std::string> FindExtraNatives(
		const ModpackManifest::Manifest& a_required,
		const std::vector<std::string>& a_dataNativeNames)
	{
		std::vector<std::string> extra;
		const auto required = ModpackManifest::NormalizeManifest(a_required);
		if (!required) {
			return extra;
		}
		const auto requiredNames = NamesOfKind(*required, EntryKind::native);
		std::unordered_set<std::string> seen;
		for (const auto& name : a_dataNativeNames) {
			const auto lowered = NameKey(name);
			if (requiredNames.contains(lowered) || !seen.insert(lowered).second) {
				continue;
			}
			extra.push_back(name);
		}
		return extra;
	}


}
