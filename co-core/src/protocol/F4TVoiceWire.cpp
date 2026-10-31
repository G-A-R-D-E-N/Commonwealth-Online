#include "F4TVoiceWire.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace F4T::Voice
{
	namespace
	{
		constexpr std::array<std::byte, 4> kMagic{ std::byte{ 'C' }, std::byte{ 'O' }, std::byte{ 'V' }, std::byte{ '1' } };
		constexpr std::size_t kProfileOffset = 4;
		constexpr std::size_t kFlagsOffset = 5;
		constexpr std::size_t kSpeakerOffset = 6;
		constexpr std::size_t kSequenceOffset = 8;
		constexpr std::uint8_t kIllegalTier = 3;
		constexpr std::uint8_t kVoiceFlagTierMask = 0x03;
		constexpr std::uint8_t kVoiceFlagBurstStart = 0x04;
		constexpr std::uint8_t kVoiceFlagBurstEnd = 0x08;
		constexpr std::uint8_t kVoiceFlagReservedMask = 0xF0;

		std::uint32_t ReadBigEndian(std::span<const std::byte> a_bytes, std::size_t a_offset, std::size_t a_width) noexcept
		{
			std::uint32_t value = 0;
			for (std::size_t i = 0; i < a_width; ++i) {
				value = (value << 8U) | std::to_integer<std::uint32_t>(a_bytes[a_offset + i]);
			}
			return value;
		}

		void WriteBigEndian(std::span<std::byte> a_bytes, std::size_t a_offset, std::size_t a_width, std::uint32_t a_value) noexcept
		{
			for (std::size_t i = 0; i < a_width; ++i) {
				a_bytes[a_offset + i] = static_cast<std::byte>((a_value >> (8U * (a_width - 1 - i))) & 0xFFU);
			}
		}

		std::uint8_t EncodeFlags(const VoiceHeader& a_header) noexcept
		{
			auto flags = static_cast<std::uint8_t>(a_header.tier);
			if (a_header.burstStart) {
				flags |= kVoiceFlagBurstStart;
			}
			if (a_header.burstEnd) {
				flags |= kVoiceFlagBurstEnd;
			}
			return flags;
		}
	}

	bool HasVoicePrefix(std::span<const std::byte> a_datagram) noexcept
	{
		if (a_datagram.size() < kMagic.size()) {
			return false;
		}
		return std::ranges::equal(a_datagram.first(kMagic.size()), kMagic);
	}

	std::size_t EncodeVoicePacket(
		const VoiceHeader& a_header,
		std::span<const std::byte> a_payload,
		std::span<std::byte> a_output) noexcept
	{
		if (a_payload.empty() || a_payload.size() > kVoiceMaxPayloadBytes || a_header.sequence == 0) {
			return 0;
		}
		const auto total = kVoiceHeaderBytes + a_payload.size();
		if (a_output.size() < total) {
			return 0;
		}
		std::ranges::copy(kMagic, a_output.begin());
		a_output[kProfileOffset] = static_cast<std::byte>(a_header.profile);
		a_output[kFlagsOffset] = static_cast<std::byte>(EncodeFlags(a_header));
		WriteBigEndian(a_output, kSpeakerOffset, 2, a_header.speakerId);
		WriteBigEndian(a_output, kSequenceOffset, 4, a_header.sequence);
		std::copy(a_payload.begin(), a_payload.end(), a_output.subspan(kVoiceHeaderBytes).begin());
		return total;
	}

	VoiceDecodeResult DecodeVoicePacket(
		std::span<const std::byte> a_datagram,
		VoiceDirection a_direction,
		VoicePacket& a_out) noexcept
	{
		if (!HasVoicePrefix(a_datagram)) {
			return VoiceDecodeResult::NotVoice;
		}
		if (a_datagram.size() <= kVoiceHeaderBytes || a_datagram.size() > kVoiceMaxDatagramBytes) {
			return VoiceDecodeResult::BadSize;
		}
		const auto profile = std::to_integer<std::uint8_t>(a_datagram[kProfileOffset]);
		if (profile != kVoiceProfileOpus48kMono20ms) {
			return VoiceDecodeResult::BadProfile;
		}
		const auto flags = std::to_integer<std::uint8_t>(a_datagram[kFlagsOffset]);
		if ((flags & kVoiceFlagReservedMask) != 0) {
			return VoiceDecodeResult::BadFlags;
		}
		const auto tier = static_cast<std::uint8_t>(flags & kVoiceFlagTierMask);
		if (tier == kIllegalTier) {
			return VoiceDecodeResult::BadTier;
		}
		const auto speakerId = static_cast<std::uint16_t>(ReadBigEndian(a_datagram, kSpeakerOffset, 2));
		if (a_direction == VoiceDirection::ClientToServer && speakerId != 0) {
			return VoiceDecodeResult::BadSpeaker;
		}
		const auto sequence = ReadBigEndian(a_datagram, kSequenceOffset, 4);
		if (sequence == 0) {
			return VoiceDecodeResult::BadSequence;
		}
		a_out.header.profile = profile;
		a_out.header.tier = static_cast<Tier>(tier);
		a_out.header.burstStart = (flags & kVoiceFlagBurstStart) != 0;
		a_out.header.burstEnd = (flags & kVoiceFlagBurstEnd) != 0;
		a_out.header.speakerId = speakerId;
		a_out.header.sequence = sequence;
		a_out.payload = a_datagram.subspan(kVoiceHeaderBytes);
		return VoiceDecodeResult::Ok;
	}

	bool WriteSpeakerId(std::span<std::byte> a_datagram, std::uint16_t a_speakerId) noexcept
	{
		if (a_datagram.size() < kVoiceHeaderBytes || !HasVoicePrefix(a_datagram)) {
			return false;
		}
		WriteBigEndian(a_datagram, kSpeakerOffset, 2, a_speakerId);
		return true;
	}

	std::uint32_t NextTick(std::uint32_t a_sequence) noexcept
	{
		return GnsSnapshotEnvelope::NextSequence(a_sequence);
	}

	std::uint32_t TickDistance(std::uint32_t a_from, std::uint32_t a_to) noexcept
	{
		return a_to >= a_from ? a_to - a_from : a_to - a_from - 1U;
	}

	SequenceVerdict Check(const SequenceWindow& a_window, std::uint32_t a_sequence) noexcept
	{
		if (a_sequence == 0) {
			return SequenceVerdict::Zero;
		}
		if (GnsSnapshotEnvelope::IsNewer(a_sequence, a_window.highWater)) {
			return SequenceVerdict::Accept;
		}
		const auto behind = TickDistance(a_sequence, a_window.highWater);
		if (behind >= kVoiceSequenceWindowTicks) {
			return SequenceVerdict::TooOld;
		}
		return (a_window.seen & (1ULL << behind)) != 0 ? SequenceVerdict::Duplicate : SequenceVerdict::Accept;
	}

	void Commit(SequenceWindow& a_window, std::uint32_t a_sequence) noexcept
	{
		if (Check(a_window, a_sequence) != SequenceVerdict::Accept) {
			return;
		}
		if (GnsSnapshotEnvelope::IsNewer(a_sequence, a_window.highWater)) {
			const auto advance = TickDistance(a_window.highWater, a_sequence);
			a_window.seen = advance >= kVoiceSequenceWindowTicks ? 0 : a_window.seen << advance;
			a_window.seen |= 1ULL;
			a_window.highWater = a_sequence;
			return;
		}
		a_window.seen |= 1ULL << TickDistance(a_sequence, a_window.highWater);
	}
}
