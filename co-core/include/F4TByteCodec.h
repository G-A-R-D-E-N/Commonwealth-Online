#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::ByteCodec
{
	inline void PutLittleEndian(std::span<std::byte> a_out, std::size_t& a_offset, std::uint64_t a_value, std::size_t a_width)
	{
		for (std::size_t index = 0; index < a_width; ++index) {
			a_out[a_offset++] = static_cast<std::byte>((a_value >> (8 * index)) & 0xFFU);
		}
	}

	inline std::uint64_t GetLittleEndian(std::span<const std::byte> a_in, std::size_t& a_offset, std::size_t a_width)
	{
		std::uint64_t value = 0;
		for (std::size_t index = 0; index < a_width; ++index) {
			value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(a_in[a_offset++])) << (8 * index);
		}
		return value;
	}
}
