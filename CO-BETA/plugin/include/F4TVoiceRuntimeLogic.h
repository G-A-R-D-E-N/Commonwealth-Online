#pragma once

#include "F4TVoiceRuntimeTypes.h"
#include "F4TVoiceSession.h"
#include "F4TVoiceSettings.h"

#include <cstdint>
#include <span>
#include <vector>

namespace F4T::Voice
{
	inline constexpr std::uint32_t kMaxSpeakerId = 0xFFFF;
	inline constexpr double kVoiceSaveDelaySeconds = 0.5;

	struct RemoteVoicePlayer
	{
		std::uint32_t playerId{};
		float x{};
		float y{};
		float z{};
	};

	[[nodiscard]] std::vector<SpeakerPosition> SpeakerPositionsOf(std::span<const RemoteVoicePlayer> a_players);
	[[nodiscard]] std::vector<std::uint32_t> MergeSpeakers(
		std::span<const std::uint32_t> a_talk,
		std::span<const std::uint16_t> a_heard,
		std::span<const VoicePlayerRow> a_rows);
	[[nodiscard]] std::vector<std::uint16_t> DepartedSpeakers(
		std::span<const VoicePlayerRow> a_before,
		std::span<const VoicePlayerRow> a_after);
	[[nodiscard]] TransmitConfig TransmitConfigOf(const VoiceSettings& a_settings);
	[[nodiscard]] bool DevicesWanted(bool a_active, const VoiceSettings& a_settings, bool a_serverNoVoice);

	class SaveDebounce
	{
	public:
		void MarkDirty(double a_now) noexcept;
		[[nodiscard]] bool TakeDue(double a_now) noexcept;

	private:
		double m_markedAt{};
		bool m_dirty{};
	};
}
