#pragma once

#include "AdminCommandDispatcher.h"
#include "ServerProtocol.h"
#include "ServerSessionTypes.h"
#include "ServerVoice.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	using VoiceFrameSender = std::function<void(std::vector<OutboundFrame>&)>;
	using VoiceSessionMap = std::unordered_map<std::uint32_t, SessionTypes::Session>;

	std::vector<VoicePeerEntry> VoicePeers(VoiceSessionMap& a_sessions);
	std::vector<OutboundFrame> VoicePoll(VoiceService& a_voice, VoiceSessionMap& a_sessions, double a_monotonicSeconds);
	std::vector<OutboundFrame> VoiceDisconnect(
		VoiceService& a_voice,
		VoiceSessionMap& a_sessions,
		std::uint32_t a_connectionId,
		const std::function<std::vector<OutboundFrame>()>& a_disconnect);
	std::optional<VoiceAdminOutcome> VoiceMuteByAdmin(
		VoiceService& a_voice,
		VoiceSessionMap& a_sessions,
		std::optional<std::uint32_t> a_playerId,
		std::string_view a_profileId,
		bool a_muted,
		std::optional<double> a_seconds,
		double a_monotonicSeconds);
	AdminActionResult VoiceAdminAction(
		ProtocolHost& a_protocol,
		bool a_muted,
		std::optional<std::uint32_t> a_playerId,
		std::string_view a_profileId,
		std::optional<double> a_seconds,
		double a_monotonicSeconds,
		const VoiceFrameSender& a_send);
}
