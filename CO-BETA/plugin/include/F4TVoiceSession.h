#pragma once

#include "F4TVoiceControl.h"
#include "F4TVoiceSettings.h"
#include "F4TVoiceStatus.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace F4T::Voice
{
	struct VoicePlayerRow
	{
		std::uint32_t playerId{};
		std::string name;
		bool muted{};
		int volume{ 100 };

		bool operator==(const VoicePlayerRow&) const = default;
	};

	struct SessionPlayer
	{
		std::uint32_t playerId{};
		std::string name;
	};

	struct NoticeFlags
	{
		bool serverMuted{};
		bool rateLimited{};
		bool profileMismatch{};

		bool operator==(const NoticeFlags&) const = default;
	};

	[[nodiscard]] std::optional<std::uint32_t> ParsePlayerId(const nlohmann::json& a_packet);

	class VoiceSession
	{
	public:
		void Reset();
		bool SyncPlayers(std::span<const SessionPlayer> a_present, const VoiceSettings& a_settings);
		bool SetMuted(std::uint32_t a_playerId, bool a_muted, VoiceSettings& a_settings);
		bool SetVolume(std::uint32_t a_playerId, int a_volume, VoiceSettings& a_settings);
		[[nodiscard]] const std::vector<VoicePlayerRow>& Players() const noexcept;
		[[nodiscard]] std::vector<std::uint32_t> BlockedPlayerIds() const;
		bool ApplyTalk(const nlohmann::json& a_packet);
		[[nodiscard]] const std::vector<std::uint32_t>& TalkSpeakers() const noexcept;
		bool ApplyNotice(const nlohmann::json& a_packet, double a_now);
		[[nodiscard]] NoticeFlags Notices(double a_now) const noexcept;

	private:
		std::vector<VoicePlayerRow> m_players;
		std::vector<std::uint32_t> m_talkers;
		double m_serverMutedUntil{ -std::numeric_limits<double>::infinity() };
		double m_rateLimitedUntil{ -std::numeric_limits<double>::infinity() };
		bool m_profileMismatch{};
	};

	[[nodiscard]] ControlState DesiredControl(
		const VoiceSettings& a_settings,
		std::vector<std::uint32_t> a_blockedPlayerIds,
		bool a_codecFailed,
		bool a_micFailed,
		bool a_outputFailed);

	[[nodiscard]] VoiceStatusInputs AggregateStatus(
		const VoiceStatusInputs& a_transmit,
		bool a_outputFailed,
		bool a_serverNoVoice,
		const NoticeFlags& a_notices,
		const VoiceSettings& a_settings);

	[[nodiscard]] bool MicHot(bool a_active, const VoiceSettings& a_settings, bool a_pttHeld, bool a_transmitting);
}
