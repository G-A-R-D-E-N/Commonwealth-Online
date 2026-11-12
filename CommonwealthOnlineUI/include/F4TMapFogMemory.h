#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4SE
{
	class SerializationInterface;
}

namespace F4T::MapFogMemory
{
	inline constexpr std::uint32_t kGridSize = 256;
	inline constexpr std::uint32_t kRecordType = 'FOGM';

	void Clear();
	void MarkExplored(std::string_view a_mapKey, float a_xPct, float a_yPct);
	std::string ExploredBase64(std::string_view a_mapKey);
	void Save(const F4SE::SerializationInterface* a_intfc);
	void Load(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
}
