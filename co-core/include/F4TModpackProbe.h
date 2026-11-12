#pragma once

#include "F4TModpackManifest.h"

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackProbe
{
	inline constexpr std::uint32_t kMissingPluginOrder = 0xFFFFFFFFu;

	enum class FileHashStatus { complete, cancelled, limit, unreadable, changed };
	struct FileHashResult
	{
		FileHashStatus status{ FileHashStatus::unreadable };
		std::string sha256;
		std::uint64_t size{};
		std::int64_t modifiedTicks{};
	};

	[[nodiscard]] FileHashResult HashFile(
		const std::filesystem::path& a_path,
		std::uint64_t a_maxBytes,
		std::chrono::steady_clock::time_point a_deadline,
		const std::function<bool()>& a_cancelled = {},
		const std::function<void(std::uint64_t)>& a_progress = {});

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
