#pragma once

#include "ServerProtocol.h"

#include "F4TVoiceBudget.h"
#include "F4TVoiceRelay.h"
#include "F4TVoiceWire.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	enum class VoiceNoticeCode : std::uint8_t
	{
		RateLimited,
		ProfileMismatch,
		ServerMuted,
		ServerUnmuted,
		Count
	};

	struct VoiceSessionState
	{
		bool stateApplied{};
		std::uint64_t revision{};
		std::uint8_t profile{};
		std::uint32_t rate{};
		std::uint8_t channels{};
		std::uint32_t frameMs{};
		bool canSend{};
		bool canPlay{};
		bool deafened{};
		std::array<std::uint16_t, F4T::Voice::MaxVoiceBlockedPlayers> blockedPlayerIds{};
		std::uint8_t blockedCount{};
		F4T::Voice::SequenceWindow sequences;
		F4T::PacketRateLimit::TokenBucket softBucket{ F4T::Voice::MakeVoiceSoftBucket(0.0) };
		F4T::PacketRateLimit::TokenBucket controlBucket{ F4T::Voice::MakeVoiceControlBucket(0.0) };
		std::optional<F4T::Voice::SoftDropMonitor> softMonitor;
		std::optional<double> lastFrameMonotonic;
		std::array<std::optional<double>, static_cast<std::size_t>(VoiceNoticeCode::Count)> lastNoticeMonotonic{};
		F4T::Voice::Tier burstTier{};
		bool burstActive{};
		std::vector<std::uint32_t> talkListeners;
		F4T::Voice::TalkerSlots talkers{};
	};

	struct VoicePeerEntry
	{
		std::uint32_t connectionId{};
		F4T::Voice::VoicePeer peer{};
		VoiceSessionState* state{};
		std::string_view profileId{};
		std::string_view steamId{};
	};

	class VoiceService
	{
	public:
		explicit VoiceService(ProtocolStats& a_stats) :
			stats(a_stats)
		{}

		std::vector<OutboundFrame> HandleFrame(
			std::uint32_t a_connectionId,
			std::span<const std::uint8_t> a_datagram,
			std::vector<VoicePeerEntry> a_peers,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> HandleState(
			std::uint32_t a_connectionId,
			VoiceSessionState& a_state,
			const nlohmann::json& a_packet,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> SetServerMute(
			std::vector<VoicePeerEntry> a_peers,
			std::string_view a_muteKey,
			std::string_view a_profileId,
			bool a_muted,
			std::optional<double> a_seconds,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> Poll(std::vector<VoicePeerEntry> a_peers, double a_monotonicSeconds);
		std::vector<OutboundFrame> Depart(std::vector<VoicePeerEntry> a_peers, std::uint32_t a_connectionId);
		void RecordSend(bool a_sent, bool a_backpressure, bool a_dropped) noexcept;

	private:
		bool IsServerMuted(std::string_view a_steamId, std::string_view a_profileId, double a_monotonicSeconds);
		bool IsMuteActive(std::string_view a_muteKey, double a_monotonicSeconds);

		ProtocolStats& stats;
		F4T::Voice::ByteBucket egress{ F4T::Voice::MakeVoiceEgressBucket(0.0) };
		std::map<std::string, double, std::less<>> serverMutes;
	};
}
