#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace F4T::Ba2Inspect
{
	inline constexpr std::uint32_t kMaximumFiles = 1'000'000;
	inline constexpr std::size_t kMaximumPathBytes = 1024;
	inline constexpr std::size_t kMaximumListedRefusals = 8;

	enum class ArchiveType
	{
		general,
		textures
	};

	struct Report
	{
		std::uint32_t version{};
		ArchiveType type{ ArchiveType::general };
		std::uint32_t fileCount{};
		std::uint32_t scripts{};
		std::uint32_t interfaceFiles{};
		std::uint32_t refusedCount{};
		std::vector<std::string> refusedPaths;
	};

	[[nodiscard]] std::optional<Report> Inspect(std::istream& a_stream, std::uint64_t a_sizeBytes);
	[[nodiscard]] std::optional<Report> InspectFile(const std::filesystem::path& a_path);
	[[nodiscard]] bool IsAcceptable(const Report& a_report) noexcept;
	[[nodiscard]] bool IsRefusedInnerPath(std::string_view a_path);
}
