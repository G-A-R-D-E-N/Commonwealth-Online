#include "F4TModpackProbe.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
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

	std::string LowerAscii(std::string_view a_name)
	{
		std::string lowered{ a_name };
		for (auto& character : lowered) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		return lowered;
	}

	std::unordered_set<std::string> NamesOfKind(const Manifest& a_manifest, EntryKind a_kind)
	{
		std::unordered_set<std::string> names;
		for (const auto& entry : a_manifest.entries) {
			if (entry.kind == a_kind) {
				names.insert(LowerAscii(entry.name));
			}
		}
		return names;
	}

	bool EndsWithIgnoreCase(std::string_view a_name, std::string_view a_suffix) noexcept
	{
		if (a_name.size() < a_suffix.size()) {
			return false;
		}
		const auto start = a_name.size() - a_suffix.size();
		for (std::size_t index = 0; index < a_suffix.size(); ++index) {
			auto character = a_name[start + index];
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
			if (character != a_suffix[index]) {
				return false;
			}
		}
		return true;
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
			const auto lowered = LowerAscii(name);
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
			const auto found = loadedRanks.find(LowerAscii(entry.name));
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
			const auto lowered = LowerAscii(name);
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
			const auto lowered = LowerAscii(name);
			if (requiredNames.contains(lowered) || !seen.insert(lowered).second) {
				continue;
			}
			extra.push_back(name);
		}
		return extra;
	}

	ModpackManifest::PluginType PluginTypeFromName(std::string_view a_name) noexcept
	{
		if (EndsWithIgnoreCase(a_name, ".esl")) {
			return ModpackManifest::PluginType::esl;
		}
		if (EndsWithIgnoreCase(a_name, ".esm")) {
			return ModpackManifest::PluginType::esm;
		}
		return ModpackManifest::PluginType::esp;
	}
}
