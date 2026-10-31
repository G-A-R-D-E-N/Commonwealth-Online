#include "ServerVoice.h"
#include "ServerPacketDecode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using F4T::Voice::Tier;

		constexpr double kVoiceNoticeIntervalSeconds = 10.0;
		constexpr double kVoiceTalkSilenceSeconds = 0.4;
		constexpr std::uint64_t kMaximumRevision = (std::uint64_t{ 1 } << 53) - 1;
		constexpr std::uint64_t kVoiceRate = 48000;
		constexpr std::uint64_t kVoiceChannels = 1;
		constexpr std::uint64_t kVoiceFrameMs = 20;
		constexpr std::array<const char*, static_cast<std::size_t>(VoiceNoticeCode::Count)> kVoiceNoticeNames{
			"rateLimited",
			"profileMismatch",
			"serverMuted",
			"serverUnmuted"
		};

		struct VoiceSnapshot
		{
			std::uint64_t revision{};
			std::uint64_t profile{};
			std::uint64_t rate{};
			std::uint64_t channels{};
			std::uint64_t frameMs{};
			bool canSend{};
			bool canPlay{};
			bool deafened{};
			std::array<std::uint16_t, F4T::Voice::MaxVoiceBlockedPlayers> blockedPlayerIds{};
			std::uint8_t blockedCount{};
		};

		OutboundFrame ReliableFrame(std::uint32_t a_connectionId, const Json& a_packet)
		{
			return { a_connectionId, Delivery::ReliableOrdered, PacketDecode::JsonBytes(a_packet) };
		}

		const char* TierName(Tier a_tier)
		{
			return a_tier == Tier::Whisper ? "whisper" : a_tier == Tier::Shout ? "shout" : "normal";
		}

		Json TalkPacket(std::uint16_t a_playerId, bool a_talking, Tier a_tier)
		{
			return Json{
				{ "type", "voiceTalk" },
				{ "playerId", a_playerId },
				{ "talking", a_talking },
				{ "tier", TierName(a_tier) }
			};
		}

		std::vector<OutboundFrame> StartTalk(
			const VoicePeerEntry& a_sender,
			std::span<const VoicePeerEntry> a_peers,
			Tier a_tier)
		{
			auto& state = *a_sender.state;
			state.burstActive = true;
			state.burstTier = a_tier;
			state.talkListeners.clear();
			std::vector<OutboundFrame> frames;
			const auto packet = TalkPacket(a_sender.peer.playerId, true, a_tier);
			for (const auto& listener : a_peers) {
				if (listener.connectionId == a_sender.connectionId || !listener.peer.voiceV1 || listener.peer.canPlay) {
					continue;
				}
				if (!F4T::Voice::VoiceBlocked(a_sender.peer, listener.peer) && F4T::Voice::VoiceInRange(a_sender.peer, listener.peer, a_tier)) {
					state.talkListeners.push_back(listener.connectionId);
					frames.push_back(ReliableFrame(listener.connectionId, packet));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> EndTalk(VoiceSessionState& a_state, std::uint16_t a_playerId)
		{
			const auto packet = TalkPacket(a_playerId, false, a_state.burstTier);
			std::vector<OutboundFrame> frames;
			for (const auto connectionId : a_state.talkListeners) {
				frames.push_back(ReliableFrame(connectionId, packet));
			}
			a_state.talkListeners.clear();
			a_state.burstActive = false;
			return frames;
		}

		OutboundFrame NoticeFrame(std::uint32_t a_connectionId, VoiceNoticeCode a_code, std::optional<double> a_seconds)
		{
			Json packet{ { "type", "voiceNotice" }, { "code", kVoiceNoticeNames[static_cast<std::size_t>(a_code)] } };
			if (a_seconds) {
				packet["seconds"] = *a_seconds;
			}
			return ReliableFrame(a_connectionId, packet);
		}

		std::vector<OutboundFrame> Notice(
			std::uint32_t a_connectionId,
			VoiceSessionState& a_state,
			VoiceNoticeCode a_code,
			double a_monotonicSeconds)
		{
			auto& last = a_state.lastNoticeMonotonic[static_cast<std::size_t>(a_code)];
			if (last && a_monotonicSeconds - *last < kVoiceNoticeIntervalSeconds) {
				return {};
			}
			last = a_monotonicSeconds;
			return { NoticeFrame(a_connectionId, a_code, std::nullopt) };
		}

		std::optional<std::uint64_t> ToInteger(const Json& a_value)
		{
			if (a_value.is_number_unsigned()) {
				return a_value.get<std::uint64_t>();
			}
			if (a_value.is_number_integer() && a_value.get<std::int64_t>() >= 0) {
				return static_cast<std::uint64_t>(a_value.get<std::int64_t>());
			}
			return std::nullopt;
		}

		std::optional<std::uint64_t> ReadInteger(const Json& a_packet, const char* a_key)
		{
			const auto found = a_packet.find(a_key);
			return found == a_packet.end() ? std::nullopt : ToInteger(*found);
		}

		std::optional<bool> ReadBool(const Json& a_packet, const char* a_key)
		{
			const auto found = a_packet.find(a_key);
			if (found == a_packet.end() || !found->is_boolean()) {
				return std::nullopt;
			}
			return found->get<bool>();
		}

		std::optional<VoiceSnapshot> ParseSnapshot(const Json& a_packet)
		{
			const auto revision = ReadInteger(a_packet, "revision");
			const auto profile = ReadInteger(a_packet, "profile");
			const auto rate = ReadInteger(a_packet, "rate");
			const auto channels = ReadInteger(a_packet, "channels");
			const auto frameMs = ReadInteger(a_packet, "frameMs");
			const auto canSend = ReadBool(a_packet, "canSend");
			const auto canPlay = ReadBool(a_packet, "canPlay");
			const auto deafened = ReadBool(a_packet, "deafened");
			const auto blocked = a_packet.find("blockedPlayerIds");
			if (!revision || *revision < 1 || *revision > kMaximumRevision || !profile || !rate || !channels || !frameMs ||
				!canSend || !canPlay || !deafened || blocked == a_packet.end() || !blocked->is_array() ||
				blocked->size() > F4T::Voice::MaxVoiceBlockedPlayers) {
				return std::nullopt;
			}
			VoiceSnapshot snapshot{
				.revision = *revision,
				.profile = *profile,
				.rate = *rate,
				.channels = *channels,
				.frameMs = *frameMs,
				.canSend = *canSend,
				.canPlay = *canPlay,
				.deafened = *deafened
			};
			std::uint64_t previous = 0;
			for (const auto& item : *blocked) {
				const auto playerId = ToInteger(item);
				if (!playerId || *playerId <= previous || *playerId > (std::numeric_limits<std::uint16_t>::max)()) {
					return std::nullopt;
				}
				previous = *playerId;
				snapshot.blockedPlayerIds[snapshot.blockedCount] = static_cast<std::uint16_t>(*playerId);
				snapshot.blockedCount++;
			}
			return snapshot;
		}

		bool IsVoiceV1Profile(const VoiceSnapshot& a_snapshot)
		{
			return a_snapshot.profile == F4T::Voice::kVoiceProfileOpus48kMono20ms && a_snapshot.rate == kVoiceRate &&
				a_snapshot.channels == kVoiceChannels && a_snapshot.frameMs == kVoiceFrameMs;
		}

		bool SameContents(const VoiceSessionState& a_state, const VoiceSnapshot& a_snapshot)
		{
			return a_state.profile == a_snapshot.profile && a_state.rate == a_snapshot.rate &&
				a_state.channels == a_snapshot.channels && a_state.frameMs == a_snapshot.frameMs &&
				a_state.canSend == a_snapshot.canSend && a_state.canPlay == a_snapshot.canPlay &&
				a_state.deafened == a_snapshot.deafened && a_state.blockedCount == a_snapshot.blockedCount &&
				std::equal(a_state.blockedPlayerIds.begin(), a_state.blockedPlayerIds.begin() + a_state.blockedCount, a_snapshot.blockedPlayerIds.begin());
		}

		void ApplySnapshot(VoiceSessionState& a_state, const VoiceSnapshot& a_snapshot)
		{
			a_state.stateApplied = true;
			a_state.revision = a_snapshot.revision;
			a_state.profile = static_cast<std::uint8_t>(a_snapshot.profile);
			a_state.rate = static_cast<std::uint32_t>(a_snapshot.rate);
			a_state.channels = static_cast<std::uint8_t>(a_snapshot.channels);
			a_state.frameMs = static_cast<std::uint32_t>(a_snapshot.frameMs);
			a_state.canSend = a_snapshot.canSend;
			a_state.canPlay = a_snapshot.canPlay;
			a_state.deafened = a_snapshot.deafened;
			a_state.blockedPlayerIds = a_snapshot.blockedPlayerIds;
			a_state.blockedCount = a_snapshot.blockedCount;
		}

		void DisableVoice(VoiceSessionState& a_state, std::uint64_t a_revision)
		{
			a_state.stateApplied = false;
			a_state.revision = a_revision;
			a_state.canSend = false;
			a_state.canPlay = false;
		}

		OutboundFrame AckFrame(std::uint32_t a_connectionId, std::uint64_t a_revision)
		{
			return ReliableFrame(a_connectionId, Json{ { "type", "voiceStateAck" }, { "revision", a_revision } });
		}
	}

	std::vector<OutboundFrame> VoiceService::HandleFrame(
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_datagram,
		std::vector<VoicePeerEntry> a_peers,
		double a_monotonicSeconds)
	{
		stats.voiceReceived++;
		const auto sender = std::find_if(a_peers.begin(), a_peers.end(), [a_connectionId](const VoicePeerEntry& a_entry) {
			return a_entry.connectionId == a_connectionId;
		});
		if (sender == a_peers.end()) {
			stats.voiceDroppedInactive++;
			return {};
		}
		auto& state = *sender->state;
		if (!sender->peer.voiceV1) {
			stats.voiceDroppedUnnegotiated++;
			return {};
		}
		if (!state.stateApplied) {
			stats.voiceDroppedNoState++;
			return {};
		}
		if (!state.canSend) {
			stats.voiceDroppedNotSendable++;
			return {};
		}
		if (!state.softMonitor) {
			state.softMonitor = F4T::Voice::MakeSoftDropMonitor(a_monotonicSeconds);
		}

		F4T::Voice::VoicePacket packet;
		if (F4T::Voice::DecodeVoicePacket(std::as_bytes(a_datagram), F4T::Voice::VoiceDirection::ClientToServer, packet) != F4T::Voice::VoiceDecodeResult::Ok) {
			stats.voiceDroppedMalformed++;
			return {};
		}
		if (!sender->peer.scope) {
			stats.voiceDroppedNoMovement++;
			return {};
		}
		if (IsServerMuted(sender->steamId, sender->profileId, a_monotonicSeconds) || F4T::Voice::IsMuted(*state.softMonitor, a_monotonicSeconds)) {
			stats.voiceDroppedMuted++;
			return {};
		}
		const auto verdict = F4T::Voice::Check(state.sequences, packet.header.sequence);
		if (verdict == F4T::Voice::SequenceVerdict::Duplicate) {
			stats.voiceDroppedDuplicate++;
			return {};
		}
		if (verdict != F4T::Voice::SequenceVerdict::Accept) {
			stats.voiceDroppedStale++;
			return {};
		}

		const bool softDropped = !F4T::Voice::TakeVoiceSoftToken(state.softBucket, a_monotonicSeconds);
		const bool autoMuted = F4T::Voice::Record(*state.softMonitor, a_monotonicSeconds, softDropped);
		if (softDropped) {
			stats.voiceDroppedSoftLimit++;
		} else if (autoMuted) {
			stats.voiceDroppedMuted++;
		}
		if (autoMuted) {
			stats.voiceAutoMutes++;
			return Notice(sender->connectionId, state, VoiceNoticeCode::RateLimited, a_monotonicSeconds);
		}
		if (softDropped) {
			return {};
		}
		F4T::Voice::Commit(state.sequences, packet.header.sequence);
		stats.voiceAccepted++;

		std::vector<OutboundFrame> frames;
		if (packet.header.burstStart && !state.burstActive) {
			frames = StartTalk(*sender, a_peers, packet.header.tier);
		} else if (packet.header.burstEnd && !packet.header.burstStart && state.burstActive) {
			frames = EndTalk(state, sender->peer.playerId);
		}
		state.lastFrameMonotonic = a_monotonicSeconds;

		std::vector<F4T::Voice::VoicePeer> listeners;
		std::vector<F4T::Voice::TalkerSlots> slots;
		listeners.reserve(a_peers.size());
		slots.reserve(a_peers.size());
		for (const auto& entry : a_peers) {
			listeners.push_back(entry.peer);
			slots.push_back(entry.state->talkers);
		}
		const auto recipients = F4T::Voice::SelectVoiceRecipients(sender->peer, packet.header.tier, a_monotonicSeconds, listeners, slots);
		for (std::size_t index = 0; index < a_peers.size(); ++index) {
			a_peers[index].state->talkers = slots[index];
		}

		std::vector<std::uint8_t> relayed(a_datagram.begin(), a_datagram.end());
		(void)F4T::Voice::WriteSpeakerId(std::as_writable_bytes(std::span{ relayed }), sender->peer.playerId);
		for (std::size_t index = 0; index < recipients.count; ++index) {
			const auto playerId = recipients.playerIds[index];
			const auto target = std::find_if(a_peers.begin(), a_peers.end(), [playerId](const VoicePeerEntry& a_entry) {
				return a_entry.peer.voiceV1 && a_entry.peer.playerId == playerId;
			});
			if (target == a_peers.end()) {
				continue;
			}
			if (!F4T::Voice::TakeVoiceEgressBytes(egress, a_monotonicSeconds, relayed.size())) {
				stats.voiceDroppedEgress++;
				continue;
			}
			frames.push_back({ target->connectionId, Delivery::UnreliableSequenced, relayed });
			stats.voiceRelayed++;
		}
		return frames;
	}

	std::vector<OutboundFrame> VoiceService::HandleState(
		std::uint32_t a_connectionId,
		VoiceSessionState& a_state,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		if (!F4T::Voice::TakeVoiceControlToken(a_state.controlBucket, a_monotonicSeconds)) {
			stats.voiceStatesLimited++;
			return {};
		}
		const auto snapshot = ParseSnapshot(a_packet);
		if (!snapshot) {
			stats.voiceStatesRejected++;
			return {};
		}
		const bool sameRevision = snapshot->revision == a_state.revision;
		if (snapshot->revision < a_state.revision || (sameRevision && !(a_state.stateApplied && SameContents(a_state, *snapshot)))) {
			stats.voiceStatesRejected++;
			return {};
		}
		if (!IsVoiceV1Profile(*snapshot)) {
			DisableVoice(a_state, snapshot->revision);
			return Notice(a_connectionId, a_state, VoiceNoticeCode::ProfileMismatch, a_monotonicSeconds);
		}
		if (!sameRevision) {
			ApplySnapshot(a_state, *snapshot);
			stats.voiceStatesApplied++;
		}
		return { AckFrame(a_connectionId, snapshot->revision) };
	}

	std::vector<OutboundFrame> VoiceService::SetServerMute(
		std::vector<VoicePeerEntry> a_peers,
		std::string_view a_muteKey,
		std::string_view a_profileId,
		bool a_muted,
		std::optional<double> a_seconds,
		double a_monotonicSeconds)
	{
		if (a_muteKey.empty()) {
			return {};
		}
		if (a_muted) {
			serverMutes.insert_or_assign(
				std::string{ a_muteKey },
				a_seconds ? a_monotonicSeconds + *a_seconds : std::numeric_limits<double>::infinity());
		} else {
			for (const auto key : { a_muteKey, a_profileId }) {
				if (const auto found = serverMutes.find(key); found != serverMutes.end()) {
					serverMutes.erase(found);
				}
			}
		}
		std::vector<OutboundFrame> frames;
		const auto code = a_muted ? VoiceNoticeCode::ServerMuted : VoiceNoticeCode::ServerUnmuted;
		for (auto& entry : a_peers) {
			if ((entry.steamId != a_muteKey && entry.profileId != a_muteKey) || !entry.peer.voiceV1) {
				continue;
			}
			frames.push_back(NoticeFrame(entry.connectionId, code, a_muted ? a_seconds : std::nullopt));
		}
		return frames;
	}

	bool VoiceService::IsServerMuted(std::string_view a_steamId, std::string_view a_profileId, double a_monotonicSeconds)
	{
		return IsMuteActive(a_steamId, a_monotonicSeconds) || IsMuteActive(a_profileId, a_monotonicSeconds);
	}

	bool VoiceService::IsMuteActive(std::string_view a_muteKey, double a_monotonicSeconds)
	{
		const auto found = serverMutes.find(a_muteKey);
		if (found == serverMutes.end()) {
			return false;
		}
		if (found->second > a_monotonicSeconds) {
			return true;
		}
		serverMutes.erase(found);
		return false;
	}

	std::vector<OutboundFrame> VoiceService::Poll(std::vector<VoicePeerEntry> a_peers, double a_monotonicSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& entry : a_peers) {
			auto& state = *entry.state;
			if (!state.burstActive || !state.lastFrameMonotonic || a_monotonicSeconds - *state.lastFrameMonotonic < kVoiceTalkSilenceSeconds) {
				continue;
			}
			auto ended = EndTalk(state, entry.peer.playerId);
			frames.insert(frames.end(), std::make_move_iterator(ended.begin()), std::make_move_iterator(ended.end()));
		}
		return frames;
	}

	std::vector<OutboundFrame> VoiceService::Depart(std::vector<VoicePeerEntry> a_peers, std::uint32_t a_connectionId)
	{
		const auto sender = std::find_if(a_peers.begin(), a_peers.end(), [a_connectionId](const VoicePeerEntry& a_entry) {
			return a_entry.connectionId == a_connectionId;
		});
		if (sender == a_peers.end() || !sender->state->burstActive) {
			return {};
		}
		return EndTalk(*sender->state, sender->peer.playerId);
	}

	void VoiceService::RecordSend(bool a_sent, bool a_backpressure, bool a_dropped) noexcept
	{
		if (a_sent) {
			stats.voiceSent++;
		} else if (a_backpressure) {
			stats.voiceSendBackpressure++;
		} else if (a_dropped) {
			stats.voiceSendDropped++;
		} else {
			stats.voiceSendFailed++;
		}
	}
}
