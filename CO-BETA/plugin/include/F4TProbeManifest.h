#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ProbeManifest
{
	inline constexpr std::uint32_t FormatVersion = 1;
	inline constexpr std::uint32_t DefaultSampling = 1;
	inline constexpr std::uint32_t DefaultPerProbeCap = 256;
	inline constexpr std::uint32_t MaxSampling = 1'000'000;
	inline constexpr std::uint32_t MaxPerProbeCap = 1'000'000;

	struct Entry
	{
		std::string name;
		bool enabled{ false };
		std::uint32_t sampling{ DefaultSampling };
		std::uint32_t perProbeCap{ DefaultPerProbeCap };
	};

	struct Document
	{
		std::vector<Entry> probes;
	};

	[[nodiscard]] bool Parse(std::string_view a_text, Document& a_out, std::string& a_error);
	[[nodiscard]] bool Load(const std::filesystem::path& a_path, Document& a_out, std::string& a_error);
}
