#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SaveFilePlugins
{
	inline constexpr std::string_view kSaveMagic = "FO4_SAVEGAME";
	inline constexpr std::uint32_t kMinimumSaveVersion = 11;
	inline constexpr std::uint32_t kMaximumSaveVersion = 15;
	inline constexpr std::size_t kMaximumPluginNameBytes = 260;
	inline constexpr std::size_t kMaximumHeaderStringBytes = 1024;
	inline constexpr std::size_t kMaximumNamedPlugins = 5;

	struct PluginList
	{
		std::vector<std::string> full;
		std::vector<std::string> light;
	};

	[[nodiscard]] std::optional<PluginList> ParseSavePlugins(std::span<const std::uint8_t> a_headerBytes);
	[[nodiscard]] std::vector<std::string> FindMissingPlugins(
		const PluginList& a_saved,
		const PluginList& a_loaded);
	[[nodiscard]] std::string BuildMissingPluginsMessage(const std::vector<std::string>& a_missing);
}
