#pragma once

#include "F4TVoiceTiers.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::Voice
{
	inline constexpr std::size_t kVoiceHeaderBytes = 12;
	inline constexpr std::size_t kVoiceMaxPayloadBytes = 160;
	inline constexpr std::size_t kVoiceMaxDatagramBytes = kVoiceHeaderBytes + kVoiceMaxPayloadBytes;
	inline constexpr std::uint8_t kVoiceProfileOpus48kMono20ms = 0;
	inline constexpr std::uint32_t kVoiceSequenceWindowTicks = 64;

	enum class VoiceDirection : std::uint8_t
	{
		ClientToServer,
		ServerToClient
	};

	enum class VoiceDecodeResult : std::uint8_t
	{
		Ok,
		NotVoice,
		BadSize,
		BadProfile,
		BadFlags,
		BadTier,
		BadSpeaker,
		BadSequence
	};

	enum class SequenceVerdict : std::uint8_t
	{
		Accept,
		Duplicate,
		TooOld,
		Zero
	};

	struct VoiceHeader
	{
		std::uint8_t profile{ kVoiceProfileOpus48kMono20ms };
		Tier tier{ Tier::Whisper };
		bool burstStart{};
		bool burstEnd{};
		std::uint16_t speakerId{};
		std::uint32_t sequence{};
	};

	struct VoicePacket
	{
		VoiceHeader header{};
		std::span<const std::byte> payload{};
	};

	struct SequenceWindow
	{
		std::uint32_t highWater{};
		std::uint64_t seen{};
	};

	[[nodiscard]] bool HasVoicePrefix(std::span<const std::byte> a_datagram) noexcept;

	[[nodiscard]] std::size_t EncodeVoicePacket(
		const VoiceHeader& a_header,
		std::span<const std::byte> a_payload,
		std::span<std::byte> a_output) noexcept;

	[[nodiscard]] VoiceDecodeResult DecodeVoicePacket(
		std::span<const std::byte> a_datagram,
		VoiceDirection a_direction,
		VoicePacket& a_out) noexcept;

	[[nodiscard]] bool WriteSpeakerId(std::span<std::byte> a_datagram, std::uint16_t a_speakerId) noexcept;

	[[nodiscard]] std::uint32_t NextTick(std::uint32_t a_sequence) noexcept;

	[[nodiscard]] std::uint32_t TickDistance(std::uint32_t a_from, std::uint32_t a_to) noexcept;

	[[nodiscard]] SequenceVerdict Check(const SequenceWindow& a_window, std::uint32_t a_sequence) noexcept;

	void Commit(SequenceWindow& a_window, std::uint32_t a_sequence) noexcept;
}
