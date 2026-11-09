#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace F4T::PayloadCodec
{
	inline constexpr std::uint8_t FlagRaw = 0x00;
	inline constexpr std::uint8_t FlagZstd = 0x01;
	inline constexpr std::size_t CompressThresholdBytes = 4uz * 1024uz;
	inline constexpr std::size_t MaximumDecompressedBytes = 4uz * 1024uz * 1024uz;

	[[nodiscard]] std::vector<std::uint8_t> Encode(std::span<const std::uint8_t> a_payload);
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> Decode(std::span<const std::uint8_t> a_framed, std::size_t a_maxDecoded);
}
