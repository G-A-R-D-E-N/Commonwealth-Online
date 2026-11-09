#pragma once

#include <filesystem>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "F4TModpackManifest.h"

namespace F4T::ModOrganizer
{
	struct Filters
	{
		std::vector<std::string> suffixes{ ".mohidden" };
		std::vector<std::string> directories{ ".git" };
	};

	[[nodiscard]] std::optional<std::filesystem::path> ResolveFile(
		const std::vector<std::filesystem::path>& a_layers,
		const std::filesystem::path& a_relativePath,
		std::string& a_error,
		std::chrono::steady_clock::time_point a_deadline = std::chrono::steady_clock::time_point::max(),
		std::size_t* a_visited = nullptr);

	[[nodiscard]] std::optional<ModpackManifest::Manifest> BuildPluginManifest(
		const std::filesystem::path& a_profile,
		const std::filesystem::path& a_mods,
		const std::filesystem::path& a_overwrite,
		const std::filesystem::path& a_data,
		const std::filesystem::path& a_creationClubList,
		std::string& a_error,
		const Filters& a_filters = {},
		const std::filesystem::path& a_hashCacheFile = {});

	[[nodiscard]] std::optional<ModpackManifest::Manifest> BuildProfileManifest(
		const std::filesystem::path& a_instance,
		const std::string& a_profileName,
		std::string& a_error,
		const std::filesystem::path& a_hashCacheFile = {});
}
