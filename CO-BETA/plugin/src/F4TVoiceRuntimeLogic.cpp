#include "F4TVoiceRuntimeLogic.h"

#include <algorithm>

namespace F4T::Voice
{
	namespace
	{
		bool HasRow(std::span<const VoicePlayerRow> a_rows, std::uint32_t a_playerId)
		{
			return std::any_of(a_rows.begin(), a_rows.end(), [a_playerId](const VoicePlayerRow& a_row) {
				return a_row.playerId == a_playerId;
			});
		}
	}

	std::vector<SpeakerPosition> SpeakerPositionsOf(std::span<const RemoteVoicePlayer> a_players)
	{
		std::vector<SpeakerPosition> positions;
		positions.reserve(a_players.size());
		for (const auto& player : a_players) {
			if (player.playerId > kMaxSpeakerId) {
				continue;
			}
			positions.push_back(SpeakerPosition{ static_cast<std::uint16_t>(player.playerId), player.x, player.y, player.z });
		}
		return positions;
	}

	std::vector<std::uint32_t> MergeSpeakers(
		std::span<const std::uint32_t> a_talk,
		std::span<const std::uint16_t> a_heard,
		std::span<const VoicePlayerRow> a_rows)
	{
		std::vector<std::uint32_t> ids(a_talk.begin(), a_talk.end());
		ids.insert(ids.end(), a_heard.begin(), a_heard.end());
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		std::erase_if(ids, [a_rows](std::uint32_t a_id) {
			return !HasRow(a_rows, a_id);
		});
		return ids;
	}

	std::vector<std::uint16_t> DepartedSpeakers(
		std::span<const VoicePlayerRow> a_before,
		std::span<const VoicePlayerRow> a_after)
	{
		std::vector<std::uint16_t> departed;
		for (const auto& row : a_before) {
			if (row.playerId <= kMaxSpeakerId && !HasRow(a_after, row.playerId)) {
				departed.push_back(static_cast<std::uint16_t>(row.playerId));
			}
		}
		return departed;
	}

	TransmitConfig TransmitConfigOf(const VoiceSettings& a_settings)
	{
		return TransmitConfig{
			.voiceChat = a_settings.voiceChat,
			.pushToTalk = a_settings.pushToTalk,
			.openMicThresholdDbfs = static_cast<float>(a_settings.openMicSensitivity),
			.pttVk = a_settings.pushToTalkVirtualKey,
			.whisperVk = a_settings.whisperVirtualKey,
			.shoutVk = a_settings.shoutVirtualKey,
			.micMuted = a_settings.micMuted
		};
	}

	bool DevicesWanted(bool a_active, const VoiceSettings& a_settings, bool a_serverNoVoice)
	{
		return a_active && a_settings.voiceChat && !a_serverNoVoice;
	}

	void SaveDebounce::MarkDirty(double a_now) noexcept
	{
		m_dirty = true;
		m_markedAt = a_now;
	}

	bool SaveDebounce::TakeDue(double a_now) noexcept
	{
		if (!m_dirty || a_now - m_markedAt < kVoiceSaveDelaySeconds) {
			return false;
		}
		m_dirty = false;
		return true;
	}
}
