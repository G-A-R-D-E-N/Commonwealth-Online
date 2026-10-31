#include "ServerVoiceHost.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	std::vector<VoicePeerEntry> VoicePeers(VoiceSessionMap& a_sessions)
	{
		std::vector<VoicePeerEntry> peers;
		for (auto& [connectionId, session] : a_sessions) {
			if (!session.active) {
				continue;
			}
			F4T::Voice::VoicePeer view;
			view.playerId = static_cast<std::uint16_t>(session.playerId);
			view.active = true;
			view.voiceV1 = session.playerId <= std::numeric_limits<std::uint16_t>::max() && session.compatibilityCapabilities.Has("voice-v1");
			view.canPlay = session.voice.stateApplied && session.voice.canPlay;
			view.deafened = session.voice.deafened;
			view.profile = session.voice.profile;
			view.blocked = session.voice.blockedPlayerIds;
			view.blockedCount = session.voice.blockedCount;
			if (session.lastMovement) {
				const auto& movement = *session.lastMovement;
				view.scope = F4T::InterestScope::StateScope{ movement.cellId, movement.worldspaceId, movement.x, movement.y };
				view.z = movement.z;
			}
			peers.push_back({ connectionId, view, &session.voice, session.profileId, session.steamId });
		}
		return peers;
	}

	std::vector<OutboundFrame> VoicePoll(VoiceService& a_voice, VoiceSessionMap& a_sessions, double a_monotonicSeconds)
	{
		if (std::none_of(a_sessions.begin(), a_sessions.end(), [](const auto& entry) { return entry.second.voice.burstActive; })) {
			return {};
		}
		return a_voice.Poll(VoicePeers(a_sessions), a_monotonicSeconds);
	}

	std::vector<OutboundFrame> VoiceDisconnect(
		VoiceService& a_voice,
		VoiceSessionMap& a_sessions,
		std::uint32_t a_connectionId,
		const std::function<std::vector<OutboundFrame>()>& a_disconnect)
	{
		auto frames = a_voice.Depart(VoicePeers(a_sessions), a_connectionId);
		for (auto& frame : a_disconnect()) frames.push_back(std::move(frame));
		return frames;
	}

	std::optional<VoiceAdminOutcome> VoiceMuteByAdmin(
		VoiceService& a_voice,
		VoiceSessionMap& a_sessions,
		std::optional<std::uint32_t> a_playerId,
		std::string_view a_profileId,
		bool a_muted,
		std::optional<double> a_seconds,
		double a_monotonicSeconds)
	{
		std::string profileId{ a_profileId };
		std::string steamId;
		if (a_playerId) {
			const auto found = std::find_if(a_sessions.begin(), a_sessions.end(), [&](const auto& entry) {
				return entry.second.active && entry.second.playerId == *a_playerId;
			});
			if (found == a_sessions.end()) {
				return std::nullopt;
			}
			profileId = found->second.profileId;
			steamId = found->second.steamId;
		} else {
			const auto online = std::find_if(a_sessions.begin(), a_sessions.end(), [&](const auto& entry) {
				return entry.second.active && entry.second.profileId == profileId;
			});
			if (online != a_sessions.end()) {
				steamId = online->second.steamId;
			}
		}
		if (profileId.empty()) {
			return std::nullopt;
		}
		const std::string_view muteKey = steamId.empty() ? std::string_view{ profileId } : std::string_view{ steamId };
		auto frames = a_voice.SetServerMute(VoicePeers(a_sessions), muteKey, profileId, a_muted, a_seconds, a_monotonicSeconds);
		return VoiceAdminOutcome{ std::move(profileId), std::move(frames) };
	}

	AdminActionResult VoiceAdminAction(
		ProtocolHost& a_protocol,
		bool a_muted,
		std::optional<std::uint32_t> a_playerId,
		std::string_view a_profileId,
		std::optional<double> a_seconds,
		double a_monotonicSeconds,
		const VoiceFrameSender& a_send)
	{
		auto outcome = a_protocol.SetVoiceMutedByAdmin(a_playerId, a_profileId, a_muted, a_seconds, a_monotonicSeconds);
		if (!outcome) {
			return AdminActionResult{ false, "No connected player with id " + std::to_string(a_playerId.value_or(0)) + ".", nullptr };
		}
		a_send(outcome->frames);
		return AdminActionResult{
			true,
			(a_muted ? "Voice-muted profile " : "Voice-unmuted profile ") + outcome->profileId + ".",
			nlohmann::json{ { "profileId", outcome->profileId } }
		};
	}

	nlohmann::json VoiceStatsJson(const ProtocolStats& a_stats)
	{
		return nlohmann::json{
			{ "received", a_stats.voiceReceived }, { "accepted", a_stats.voiceAccepted },
			{ "relayed", a_stats.voiceRelayed }, { "dropped_inactive", a_stats.voiceDroppedInactive },
			{ "dropped_unnegotiated", a_stats.voiceDroppedUnnegotiated }, { "dropped_not_sendable", a_stats.voiceDroppedNotSendable },
			{ "dropped_no_state", a_stats.voiceDroppedNoState }, { "dropped_malformed", a_stats.voiceDroppedMalformed },
			{ "dropped_no_movement", a_stats.voiceDroppedNoMovement }, { "dropped_muted", a_stats.voiceDroppedMuted },
			{ "dropped_duplicate", a_stats.voiceDroppedDuplicate }, { "dropped_stale", a_stats.voiceDroppedStale },
			{ "dropped_soft_limit", a_stats.voiceDroppedSoftLimit }, { "dropped_egress", a_stats.voiceDroppedEgress },
			{ "auto_mutes", a_stats.voiceAutoMutes }, { "sent", a_stats.voiceSent },
			{ "send_backpressure", a_stats.voiceSendBackpressure }, { "send_dropped", a_stats.voiceSendDropped },
			{ "send_failed", a_stats.voiceSendFailed }, { "states_applied", a_stats.voiceStatesApplied },
			{ "states_rejected", a_stats.voiceStatesRejected }, { "states_limited", a_stats.voiceStatesLimited }
		};
	}
}
