#pragma once

#include "F4TInterestScope.h"
#include "F4TVoiceTiers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace F4T::Voice
{
	inline constexpr std::size_t MaxVoiceBlockedPlayers = 15;
	inline constexpr std::size_t MaxVoiceRecipients = 16;
	inline constexpr std::size_t MaxVoiceTalkers = 4;
	inline constexpr double VoiceTalkerActiveSeconds = 0.3;
	inline constexpr double VoiceTalkerReplaceRatio = 0.85;

	struct VoicePeer
	{
		std::uint16_t playerId{};
		bool active{};
		bool voiceV1{};
		bool canPlay{};
		bool deafened{};
		std::uint8_t profile{};
		std::array<std::uint16_t, MaxVoiceBlockedPlayers> blocked{};
		std::uint8_t blockedCount{};
		std::optional<F4T::InterestScope::StateScope> scope{};
		double z{};
	};

	struct TalkerEntry
	{
		std::uint16_t speakerId{};
		double lastFrameSeconds{};
		double distance{};
		bool used{};
	};

	using TalkerSlots = std::array<TalkerEntry, MaxVoiceTalkers>;

	struct VoiceRecipients
	{
		std::array<std::uint16_t, MaxVoiceRecipients> playerIds{};
		std::uint8_t count{};
	};

	[[nodiscard]] bool AdmitTalker(
		TalkerSlots& a_slots,
		std::uint16_t a_speakerId,
		double a_distance,
		double a_nowSeconds) noexcept;

	[[nodiscard]] bool VoiceInRange(
		const VoicePeer& a_from,
		const VoicePeer& a_to,
		Tier a_tier) noexcept;

	[[nodiscard]] bool VoiceBlocked(const VoicePeer& a_sender, const VoicePeer& a_listener) noexcept;

	[[nodiscard]] VoiceRecipients SelectVoiceRecipients(
		const VoicePeer& a_sender,
		Tier a_tier,
		double a_nowSeconds,
		std::span<const VoicePeer> a_listeners,
		std::span<TalkerSlots> a_talkerSlots) noexcept;
}
