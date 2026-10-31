#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackProbeCache
{
	inline constexpr std::size_t kMaximumFiles = 8192;
	inline constexpr std::size_t kMaximumPlugins = 4351;
	inline constexpr std::size_t kMaximumBlobBytes = 2 * 1024 * 1024;
	inline constexpr std::size_t kMaximumPathBytes = 260;
	inline constexpr std::size_t kMaximumPluginNameBytes = 255;
	inline constexpr std::uint64_t kMissingFileSize = 0xFFFFFFFFFFFFFFFFULL;

	struct FileFact
	{
		std::string path;
		std::uint64_t sizeBytes{};
		std::int64_t modifiedTicks{};
	};

	struct Entry
	{
		std::string fingerprint;
		std::vector<FileFact> files;
		std::vector<std::string> loadedPlugins;
	};

	enum class Match
	{
		hit,
		empty,
		missingFile,
		changedFile,
		changedPlugins
	};

	[[nodiscard]] std::optional<std::string> SerializeEntry(const Entry& a_entry);
	[[nodiscard]] std::optional<Entry> ParseEntry(std::string_view a_text);
	[[nodiscard]] Match Validate(
		const Entry& a_entry,
		const std::vector<FileFact>& a_currentFiles,
		const std::vector<std::string>& a_currentPlugins);
}
