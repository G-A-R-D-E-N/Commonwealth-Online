#pragma once

#include "F4TVoiceWire.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::Voice
{
	inline constexpr std::size_t JitterMaxFrames = 10;
	inline constexpr std::size_t JitterFrameBytes = kVoiceMaxPayloadBytes;
	inline constexpr std::uint32_t JitterInitialDepthFrames = 3;
	inline constexpr std::uint32_t JitterMinDepthFrames = 2;
	inline constexpr std::uint32_t JitterMaxDepthFrames = 10;
	inline constexpr std::uint32_t JitterMaxConcealedFrames = 3;
	inline constexpr std::uint32_t JitterResetJumpTicks = 50;
	inline constexpr std::uint32_t JitterDrainWindowTicks = 50;
	inline constexpr std::int64_t JitterUnderrunWindowMs = 5000;
	inline constexpr std::int64_t JitterQuietMs = 30000;
	inline constexpr std::size_t VoiceSlotCount = 4;
	inline constexpr std::size_t VoiceSavedSpeakerCount = 16;
	inline constexpr std::int64_t VoiceSlotExpiryMs = 300;

	enum class DecisionKind : std::uint8_t
	{
		NoneYet,
		Play,
		Fec,
		Conceal,
		Silence
	};

	struct Decision
	{
		DecisionKind kind{ DecisionKind::NoneYet };
		std::uint32_t sequence{};
		std::span<const std::byte> payload{};
		bool resetDecoder{};
	};

	struct JitterFrame
	{
		std::uint32_t sequence{};
		bool burstStart{};
		bool burstEnd{};
		std::size_t size{};
		std::array<std::byte, JitterFrameBytes> data{};
	};

	struct JitterBuffer
	{
		std::array<JitterFrame, JitterMaxFrames> frames{};
		std::size_t count{};
		std::array<std::byte, JitterFrameBytes> playout{};
		std::size_t playoutSize{};
		std::uint32_t cursor{};
		std::uint32_t depthFrames{ JitterInitialDepthFrames };
		std::uint32_t underrunEvents{};
		std::uint32_t concealStreak{};
		std::uint32_t waitTicks{};
		std::uint32_t drainWindowTicks{};
		std::uint32_t drainWindowMinLag{ 0xFFFFFFFFU };
		std::uint32_t drainEvents{};
		double lastUnderrunSeconds{};
		double quietSinceSeconds{};
		bool playing{};
		bool hasUnderrun{};
		bool inDryRun{};
		bool resetPending{};
	};

	struct VoiceSlot
	{
		JitterBuffer jitter{};
		std::uint16_t speakerId{};
		double lastArrivalSeconds{};
		double admittedSeconds{};
		bool used{};
	};

	struct SavedSpeaker
	{
		std::uint16_t speakerId{};
		std::uint32_t lastPlayedSequence{};
		double savedSeconds{};
		bool used{};
	};

	struct VoiceSlots
	{
		std::array<VoiceSlot, VoiceSlotCount> slots{};
		std::array<SavedSpeaker, VoiceSavedSpeakerCount> saved{};
	};

	[[nodiscard]] bool PacketValid(std::uint32_t a_sequence, std::span<const std::byte> a_payload) noexcept;
	[[nodiscard]] bool Insert(
		JitterBuffer& a_buffer,
		std::uint32_t a_sequence,
		std::span<const std::byte> a_payload,
		bool a_burstStart,
		bool a_burstEnd) noexcept;
	[[nodiscard]] Decision Next(JitterBuffer& a_buffer, double a_nowSeconds) noexcept;
	void Reset(JitterBuffer& a_buffer) noexcept;

	VoiceSlot& Admit(VoiceSlots& a_slots, std::uint16_t a_speakerId, double a_nowSeconds) noexcept;
	[[nodiscard]] bool Accept(
		VoiceSlots& a_slots,
		std::uint16_t a_speakerId,
		std::uint32_t a_sequence,
		std::span<const std::byte> a_payload,
		bool a_burstStart,
		bool a_burstEnd,
		double a_nowSeconds) noexcept;
	void Expire(VoiceSlots& a_slots, double a_nowSeconds) noexcept;
	void Remove(VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept;
	void Forget(VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept;
	void ResetSession(VoiceSlots& a_slots) noexcept;
}
