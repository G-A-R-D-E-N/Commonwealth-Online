#pragma once

#include <cstdint>

namespace F4SE
{
	class SerializationInterface;
}

namespace F4T::VendorTradeSave
{
	inline constexpr std::uint32_t kRecordType = 'VTPK';
	inline constexpr std::uint32_t kRecordVersion = 1;

	void Save(const F4SE::SerializationInterface* a_intfc);
	void Load(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void Clear();
}
