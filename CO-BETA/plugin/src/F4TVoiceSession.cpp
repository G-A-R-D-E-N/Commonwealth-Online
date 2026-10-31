#include "F4TVoiceSession.h"

#include "F4TVoiceBudget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace F4T::Voice
{
	namespace
	{
		inline constexpr std::string_view kStrangerDisplayName{ "Stranger" };

		[[nodiscard]] bool IsKnownName(const std::string& a_name)
		{
			return !a_name.empty() && a_name != kStrangerDisplayName;
		}

		std::size_t FindIndex(const std::vector<VoicePlayerRow>& a_rows, std::uint32_t a_playerId)
		{
			const auto found = std::find_if(a_rows.begin(), a_rows.end(), [a_playerId](const VoicePlayerRow& a_row) {
				return a_row.playerId == a_playerId;
			});
			return static_cast<std::size_t>(found - a_rows.begin());
		}

		const PlayerVoiceSettings* FindStored(const VoiceSettings& a_settings, const std::string& a_name)
		{
			// Constraint: stranger rows never consult persisted mutes.
			if (!IsKnownName(a_name)) {
				return nullptr;
			}
			const auto found = a_settings.players.find(a_name);
			return found == a_settings.players.end() ? nullptr : &found->second;
		}

		bool IsUtf8(const std::string& a_text)
		{
			try {
				return !nlohmann::json(a_text).dump().empty();
			} catch (const nlohmann::json::type_error&) {
				return false;
			}
		}

		void StoreRow(VoiceSettings& a_settings, const VoicePlayerRow& a_row)
		{
			// Constraint: stranger mutes stay in session memory, never in settings.
			if (!IsKnownName(a_row.name) || !IsUtf8(a_row.name)) {
				return;
			}
			a_settings.players[a_row.name] = PlayerVoiceSettings{ a_row.volume, a_row.muted };
		}

		bool IsTier(const std::string& a_tier)
		{
			return a_tier == "whisper" || a_tier == "normal" || a_tier == "shout";
		}

		std::optional<double> PositiveSeconds(const nlohmann::json& a_packet)
		{
			const auto found = a_packet.find("seconds");
			if (found == a_packet.end() || !found->is_number() || found->get<double>() <= 0.0) {
				return std::nullopt;
			}
			return found->get<double>();
		}
	}

	std::optional<std::uint32_t> ParsePlayerId(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("playerId");
		if (found == a_packet.end() || !found->is_number_integer()) {
			return std::nullopt;
		}
		const auto playerId = found->get<double>();
		if (playerId < 0.0 || playerId > static_cast<double>((std::numeric_limits<std::uint32_t>::max)())) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(playerId);
	}

	void VoiceSession::Reset()
	{
		*this = VoiceSession{};
	}

	bool VoiceSession::SyncPlayers(std::span<const SessionPlayer> a_present, VoiceSettings& a_settings)
	{
		std::vector<VoicePlayerRow> next;
		next.reserve(a_present.size());
		for (const auto& player : a_present) {
			const auto* stored = FindStored(a_settings, player.name);
			const auto index = FindIndex(m_players, player.playerId);
			const bool known = index < m_players.size();
			VoicePlayerRow row{ .playerId = player.playerId, .name = player.name, .muted = a_settings.mutePlayersByDefault };
			const bool explicitMute = std::find(m_explicit.begin(), m_explicit.end(), player.playerId) != m_explicit.end();
			if (known && m_players[index].name == player.name) {
				row = m_players[index];
			} else if (known && !IsKnownName(m_players[index].name) && IsKnownName(player.name) && explicitMute) {
				// Constraint: a muted stranger that resolves to a name keeps the session mute.
				row.muted = m_players[index].muted;
				row.volume = m_players[index].volume;
				StoreRow(a_settings, row);
			} else if (stored) {
				row.muted = stored->muted;
				row.volume = stored->volume;
			} else if (known) {
				row.muted = m_players[index].muted;
				row.volume = m_players[index].volume;
			}
			next.push_back(std::move(row));
		}
		std::erase_if(m_explicit, [&](std::uint32_t a_playerId) { return FindIndex(next, a_playerId) == next.size(); });
		std::sort(next.begin(), next.end(), [](const VoicePlayerRow& a_left, const VoicePlayerRow& a_right) {
			return a_left.playerId < a_right.playerId;
		});

		const bool changed = next != m_players;
		m_players = std::move(next);
		std::erase_if(m_talkers, [this](std::uint32_t a_playerId) {
			return FindIndex(m_players, a_playerId) == m_players.size();
		});
		return changed;
	}

	bool VoiceSession::SetMuted(std::uint32_t a_playerId, bool a_muted, VoiceSettings& a_settings)
	{
		const auto index = FindIndex(m_players, a_playerId);
		if (index == m_players.size()) {
			return false;
		}
		auto& row = m_players[index];
		row.muted = a_muted;
		if (std::find(m_explicit.begin(), m_explicit.end(), a_playerId) == m_explicit.end()) m_explicit.push_back(a_playerId);
		StoreRow(a_settings, row);
		return true;
	}

	bool VoiceSession::SetVolume(std::uint32_t a_playerId, int a_volume, VoiceSettings& a_settings)
	{
		const auto index = FindIndex(m_players, a_playerId);
		if (index == m_players.size() || a_volume < kVolumeMin || a_volume > kVolumeMax) {
			return false;
		}
		auto& row = m_players[index];
		row.volume = a_volume;
		if (std::find(m_explicit.begin(), m_explicit.end(), a_playerId) == m_explicit.end()) m_explicit.push_back(a_playerId);
		StoreRow(a_settings, row);
		return true;
	}

	const std::vector<VoicePlayerRow>& VoiceSession::Players() const noexcept
	{
		return m_players;
	}

	std::vector<std::uint32_t> VoiceSession::BlockedPlayerIds() const
	{
		std::vector<std::uint32_t> blocked;
		for (const auto& row : m_players) {
			if (row.muted) {
				blocked.push_back(row.playerId);
			}
		}
		return blocked;
	}

	bool VoiceSession::ApplyTalk(const nlohmann::json& a_packet)
	{
		const auto playerId = ParsePlayerId(a_packet);
		const auto talking = a_packet.find("talking");
		const auto tier = a_packet.find("tier");
		if (!playerId || talking == a_packet.end() || !talking->is_boolean() || tier == a_packet.end() || !tier->is_string() ||
			!IsTier(tier->get_ref<const std::string&>()) || FindIndex(m_players, *playerId) == m_players.size()) {
			return false;
		}
		const auto position = std::lower_bound(m_talkers.begin(), m_talkers.end(), *playerId);
		const bool present = position != m_talkers.end() && *position == *playerId;
		if (talking->get<bool>() == present) {
			return false;
		}
		if (present) {
			m_talkers.erase(position);
		} else {
			m_talkers.insert(position, *playerId);
		}
		return true;
	}

	const std::vector<std::uint32_t>& VoiceSession::TalkSpeakers() const noexcept
	{
		return m_talkers;
	}

	bool VoiceSession::ApplyNotice(const nlohmann::json& a_packet, double a_now)
	{
		const auto code = a_packet.find("code");
		if (code == a_packet.end() || !code->is_string()) {
			return false;
		}
		const auto& name = code->get_ref<const std::string&>();
		const auto seconds = PositiveSeconds(a_packet);
		if (name == "serverMuted") {
			m_serverMutedUntil = seconds ? a_now + *seconds : std::numeric_limits<double>::infinity();
			return true;
		}
		if (name == "serverUnmuted") {
			m_serverMutedUntil = -std::numeric_limits<double>::infinity();
			return true;
		}
		if (name == "rateLimited") {
			m_rateLimitedUntil = a_now + seconds.value_or(kVoiceAutoMuteSeconds);
			return true;
		}
		if (name == "profileMismatch") {
			m_profileMismatch = true;
			return true;
		}
		return false;
	}

	NoticeFlags VoiceSession::Notices(double a_now) const noexcept
	{
		return NoticeFlags{
			.serverMuted = a_now < m_serverMutedUntil,
			.rateLimited = a_now < m_rateLimitedUntil,
			.profileMismatch = m_profileMismatch
		};
	}

	ControlState DesiredControl(
		const VoiceSettings& a_settings,
		std::vector<std::uint32_t> a_blockedPlayerIds,
		bool a_codecFailed,
		bool a_micFailed,
		bool a_outputFailed)
	{
		ControlState state;
		state.canSend = a_settings.voiceChat && !a_settings.micMuted && !a_codecFailed && !a_micFailed;
		state.canPlay = a_settings.voiceChat && !a_outputFailed;
		state.deafened = a_settings.deafened;
		state.blockedPlayerIds = std::move(a_blockedPlayerIds);
		return state;
	}

	VoiceStatusInputs AggregateStatus(
		const VoiceStatusInputs& a_transmit,
		bool a_outputFailed,
		bool a_serverNoVoice,
		const NoticeFlags& a_notices,
		const VoiceSettings& a_settings)
	{
		VoiceStatusInputs inputs = a_transmit;
		inputs.noOutput = a_outputFailed;
		inputs.serverNoVoice = a_serverNoVoice;
		inputs.serverMuted = a_notices.serverMuted;
		inputs.rateLimited = a_notices.rateLimited;
		inputs.profileMismatch = a_notices.profileMismatch;
		inputs.deafened = a_settings.deafened;
		inputs.micMuted = inputs.micMuted || a_settings.micMuted;
		if (!a_settings.voiceChat) {
			inputs.micMuted = true;
			inputs.deafened = true;
			inputs.micFailed = false;
			inputs.micRecovering = false;
			inputs.micSilentHint = false;
			inputs.codecFailed = false;
			inputs.noOutput = false;
			inputs.transmitting = false;
		}
		return inputs;
	}

	bool MicHot(bool a_active, const VoiceSettings& a_settings, bool a_pttHeld, bool a_transmitting)
	{
		if (!a_active) {
			return false;
		}
		if (a_transmitting) {
			return true;
		}
		return a_settings.voiceChat && !a_settings.micMuted && (!a_settings.pushToTalk || a_pttHeld);
	}
}
