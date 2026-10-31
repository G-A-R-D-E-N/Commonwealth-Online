#pragma once

#include "F4TModpackManifest.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackProbe
{
	inline constexpr std::uint32_t kMissingPluginOrder = 0xFFFFFFFFu;

	[[nodiscard]] std::vector<std::uint32_t> RankRequiredPlugins(
		const ModpackManifest::Manifest& a_required,
		const std::vector<std::string>& a_loadedPluginNames);

	[[nodiscard]] std::vector<std::uint32_t> ContiguousOrdersFromRanks(const std::vector<std::uint32_t>& a_ranks);

	[[nodiscard]] std::vector<std::string> FindExtraPlugins(
		const ModpackManifest::Manifest& a_required,
		const std::vector<std::string>& a_loadedPluginNames);

	[[nodiscard]] std::vector<std::string> FindExtraNatives(
		const ModpackManifest::Manifest& a_required,
		const std::vector<std::string>& a_dataNativeNames);

	[[nodiscard]] ModpackManifest::PluginType PluginTypeFromName(std::string_view a_name) noexcept;
}
