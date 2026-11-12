#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace F4T::MutationRing
{
	inline constexpr std::size_t Capacity = 4096;
	inline constexpr std::size_t DumpHeaderSize = 12;
	inline constexpr std::size_t EntryWireSize = 34;
	inline constexpr std::size_t MaxDumpSize = DumpHeaderSize + Capacity * EntryWireSize;
	inline constexpr std::uint32_t DumpVersion = 1;

	enum class MutationSite : std::uint16_t
	{
		kUnknown = 0,
		kPositionCorrection = 1,
		kTimeState = 2,
		kWeatherState = 3
	};

	// source:CO-BETA/plugin/lib/commonlibf4/include/RE/B/BSCoreTypes.h
	// source:co-core/include/F4TMutationRing.h CO-defined ring entry
	struct Entry
	{
		std::uint64_t seq{ 0 };
		std::uint64_t tick{ 0 };
		std::uint16_t site{ 0 };
		std::uint32_t formId{ 0 };
		std::uint64_t address{ 0 };
		std::uint32_t aux{ 0 };
	};

	void SetEnabled(bool a_enabled);
	bool IsEnabled();
	void Record(MutationSite a_site, std::uint32_t a_formId, std::uint64_t a_address, std::uint32_t a_aux);
	std::size_t Snapshot(std::span<Entry> a_out);
	std::size_t Encode(std::span<const Entry> a_entries, std::span<std::byte> a_out);
	bool Decode(std::span<const std::byte> a_bytes, std::vector<Entry>& a_out);
}
