#pragma once

#include "ServerProtocol.h"
#include "ServerVoice.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ServerVoiceTestSupport
{
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::VoicePeerEntry;
	using CommonwealthOnline::Server::VoiceService;
	using CommonwealthOnline::Server::VoiceSessionState;
	using F4T::Voice::Tier;

	constexpr std::size_t kPayloadBytes = 160;

	struct Harness
	{
		ProtocolStats stats{};
		VoiceService service{ stats };
		std::deque<VoiceSessionState> states;
		std::vector<VoicePeerEntry> peers;
	};

	inline VoiceSessionState& AddPeerAt(
		Harness& a_harness,
		std::uint32_t a_connectionId,
		std::uint16_t a_playerId,
		F4T::InterestScope::StateScope a_scope,
		double a_z = 0.0)
	{
		auto& state = a_harness.states.emplace_back();
		state.stateApplied = true;
		state.canSend = true;
		state.canPlay = true;
		F4T::Voice::VoicePeer view{
			.playerId = a_playerId,
			.active = true,
			.voiceV1 = true,
			.canPlay = true,
			.scope = std::move(a_scope),
			.z = a_z
		};
		a_harness.peers.push_back({ a_connectionId, view, &state });
		return state;
	}

	inline VoiceSessionState& AddPeer(Harness& a_harness, std::uint32_t a_connectionId, std::uint16_t a_playerId, double a_x)
	{
		return AddPeerAt(a_harness, a_connectionId, a_playerId, F4T::InterestScope::StateScope{ "Cell", "WS", a_x, 0.0 });
	}

	inline std::vector<std::uint8_t> MakeDatagram(std::uint32_t a_sequence, bool a_burstStart, Tier a_tier = Tier::Whisper, bool a_burstEnd = false)
	{
		F4T::Voice::VoiceHeader header;
		header.sequence = a_sequence;
		header.burstStart = a_burstStart;
		header.burstEnd = a_burstEnd;
		header.tier = a_tier;
		const std::vector<std::byte> payload(kPayloadBytes, std::byte{ 0x2A });
		std::vector<std::byte> bytes(F4T::Voice::kVoiceHeaderBytes + kPayloadBytes);
		const auto size = F4T::Voice::EncodeVoicePacket(header, payload, bytes);
		assert(size == bytes.size());
		const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
		return { data, data + size };
	}

	inline std::vector<OutboundFrame> SendVoice(Harness& a_harness, std::uint32_t a_connectionId, std::uint32_t a_sequence, double a_now, Tier a_tier = Tier::Whisper)
	{
		return a_harness.service.HandleFrame(a_connectionId, MakeDatagram(a_sequence, false, a_tier), a_harness.peers, a_now);
	}

	inline nlohmann::json ParseFrame(const OutboundFrame& a_frame)
	{
		return nlohmann::json::parse(std::string(a_frame.payload.begin(), a_frame.payload.end()));
	}

	inline std::size_t RelayCount(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::UnreliableSequenced) {
				count++;
			}
		}
		return count;
	}

	inline std::uint16_t SpeakerOf(const OutboundFrame& a_frame)
	{
		F4T::Voice::VoicePacket packet;
		assert(F4T::Voice::DecodeVoicePacket(std::as_bytes(std::span{ a_frame.payload }), F4T::Voice::VoiceDirection::ServerToClient, packet) == F4T::Voice::VoiceDecodeResult::Ok);
		return packet.header.speakerId;
	}

	inline std::size_t NoticeCount(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_code)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::ReliableOrdered && ParseFrame(frame).value("code", std::string{}) == std::string{ a_code }) {
				count++;
			}
		}
		return count;
	}

	inline std::size_t TalkCount(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::ReliableOrdered && ParseFrame(frame).value("type", std::string{}) == "voiceTalk") {
				count++;
			}
		}
		return count;
	}

	inline std::uint64_t DroppedInbound(const ProtocolStats& a_stats)
	{
		return a_stats.voiceDroppedInactive + a_stats.voiceDroppedUnnegotiated + a_stats.voiceDroppedNotSendable + a_stats.voiceDroppedNoState +
			a_stats.voiceDroppedMalformed + a_stats.voiceDroppedNoMovement + a_stats.voiceDroppedMuted +
			a_stats.voiceDroppedDuplicate + a_stats.voiceDroppedStale + a_stats.voiceDroppedSoftLimit;
	}
}
