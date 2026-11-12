#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::GnsSnapshotEnvelope
{
	inline constexpr std::size_t HeaderSize = 12;
	inline constexpr std::array<std::uint8_t, 4> Magic{ 'C', 'O', 'G', '2' };
	inline constexpr std::uint8_t Version = 1;

	enum class Family : std::uint8_t
	{
		Transform = 1,
		NpcState = 2,
		DroppedItem = 3,
		StableDroppedItem = 4
	};

	enum class DecodeResult : std::uint8_t
	{
		NotEnvelope,
		Valid,
		Truncated,
		InvalidHeader,
		UnknownFamily,
		ZeroSequence,
		EmptyPayload
	};

	struct Header
	{
		Family family{ Family::Transform };
		std::uint32_t sequence{};
	};

	inline std::uint32_t ReadSequence(const std::uint8_t* a_bytes) noexcept
	{
		return (static_cast<std::uint32_t>(a_bytes[0]) << 24U) |
		       (static_cast<std::uint32_t>(a_bytes[1]) << 16U) |
		       (static_cast<std::uint32_t>(a_bytes[2]) << 8U) |
		       static_cast<std::uint32_t>(a_bytes[3]);
	}

	inline void WriteSequence(std::uint8_t* a_bytes, std::uint32_t a_sequence) noexcept
	{
		a_bytes[0] = static_cast<std::uint8_t>((a_sequence >> 24U) & 0xFFU);
		a_bytes[1] = static_cast<std::uint8_t>((a_sequence >> 16U) & 0xFFU);
		a_bytes[2] = static_cast<std::uint8_t>((a_sequence >> 8U) & 0xFFU);
		a_bytes[3] = static_cast<std::uint8_t>(a_sequence & 0xFFU);
	}

	inline std::uint32_t NextSequence(std::uint32_t a_current) noexcept
	{
		const auto next = a_current + 1U;
		return next == 0 ? 1U : next;
	}

	inline bool IsNewer(std::uint32_t a_candidate, std::uint32_t a_baseline) noexcept
	{
		if (a_candidate == 0) {
			return false;
		}
		if (a_baseline == 0) {
			return true;
		}
		const auto delta = a_candidate - a_baseline;
		return delta > 0 && delta < 0x80000000U;
	}

	inline bool WriteHeader(std::span<std::uint8_t> a_output, Family a_family, std::uint32_t a_sequence) noexcept
	{
		if (a_output.size() < HeaderSize || a_sequence == 0) {
			return false;
		}
		std::copy(Magic.begin(), Magic.end(), a_output.begin());
		a_output[4] = Version;
		a_output[5] = static_cast<std::uint8_t>(a_family);
		a_output[6] = 0;
		a_output[7] = 0;
		WriteSequence(a_output.data() + 8, a_sequence);
		return true;
	}

	inline DecodeResult Decode(std::span<const std::uint8_t> a_message, Header& a_header) noexcept
	{
		if (a_message.size() < Magic.size() || !std::equal(Magic.begin(), Magic.end(), a_message.begin())) {
			return DecodeResult::NotEnvelope;
		}
		if (a_message.size() < HeaderSize) {
			return DecodeResult::Truncated;
		}
		if (a_message[4] != Version || a_message[6] != 0 || a_message[7] != 0) {
			return DecodeResult::InvalidHeader;
		}
		if (a_message[5] == static_cast<std::uint8_t>(Family::Transform)) {
			a_header.family = Family::Transform;
		} else if (a_message[5] == static_cast<std::uint8_t>(Family::NpcState)) {
			a_header.family = Family::NpcState;
		} else {
			return DecodeResult::UnknownFamily;
		}
		a_header.sequence = ReadSequence(a_message.data() + 8);
		if (a_header.sequence == 0) {
			return DecodeResult::ZeroSequence;
		}
		if (a_message.size() == HeaderSize) {
			return DecodeResult::EmptyPayload;
		}
		return DecodeResult::Valid;
	}
}
