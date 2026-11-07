#pragma once

#include "lab_gns.h"

#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace VoiceLab
{
	class Relay;
}

namespace VoiceLab::Tload
{
	using Clock = std::chrono::steady_clock;
	using Seconds = std::chrono::duration<double>;

	inline constexpr int kMaxClients = 16;
	inline constexpr double kWarmupSeconds = 30.0;
	inline constexpr double kDrainSeconds = 10.0;
	inline constexpr double kTransformIntervalSeconds = 0.1;
	inline constexpr double kVoiceIntervalSeconds = 0.02;
	inline constexpr double kChatIntervalSeconds = 2.2;
	inline constexpr std::size_t kPollBufferBytes = 64 * 1024;
	inline constexpr double kLoopPeriodLimitMs = 10.0;
	inline constexpr double kVoiceEgressLimitBytes = 192000.0 + 16384.0;

	struct Options
	{
		std::string bridgePath;
		std::uint16_t port{};
		std::uint32_t serverPid{};
		std::string voice;
		std::optional<std::uint64_t> relayCapBps;
		std::uint16_t adminPort{};
		std::string adminTokenFile;
		std::string baselinePath;
		int clients{ kMaxClients };
		int seconds{ 600 };
		std::uint32_t seed{ 20261024U };
	};

	struct VoiceCounters
	{
		std::uint64_t sent{};
		std::uint64_t relayed{};
		std::uint64_t droppedEgress{};
		std::uint64_t droppedSoftLimit{};
	};

	struct ChatMessage
	{
		double sent{};
		std::uint32_t arrivals{};
	};

	struct Tally
	{
		std::uint64_t transformsSent{};
		std::uint64_t transformRefused{};
		std::uint64_t transformDelivered{};
		std::uint64_t transformBytes{};
		std::vector<double> latencies;
		std::uint64_t chatSent{};
		std::uint64_t chatRefused{};
		std::uint64_t chatDelivered{};
		std::vector<double> chatLatencies;
		std::uint64_t voiceSent{};
		std::uint64_t voiceRefused{};
		std::uint64_t voiceDatagrams{};
		std::uint64_t voiceBytes{};
		std::vector<std::uint64_t> voiceBins;
		std::uint64_t chatRejected{};
		std::uint64_t chatRejectedRateLimited{};
		std::uint64_t positionCorrections{};
		std::uint64_t sessionEnded{};
		std::uint64_t disconnects{};
		std::uint64_t malformed{};
		std::uint64_t pollErrors{};
		std::uint64_t encodeFailures{};
		std::vector<double> loopPeriods;
	};

	struct Figures
	{
		std::string mode;
		std::string cap;
		std::string voice;
		double latencyP99Ms{};
		double deliveryPct{};
		double reliableP99Ms{};
		std::uint64_t disconnects{};
		double serverCpuPct{};
		double systemBusyPct{};
		double loopP99Ms{};
		std::uint64_t maxBinBytes{};
		std::string runStatus;
	};

	struct Player
	{
		std::unique_ptr<void, DestroyFn> client{ nullptr, nullptr };
		std::string profileId;
		std::size_t index{};
		std::uint32_t playerId{};
		bool hasPlayerId{};
		std::string resumeToken;
		bool sessionReady{};
		bool failed{};
		std::uint32_t transformSequence{};
		std::uint32_t voiceSequence{};
		bool voiceStarted{};
		double x{};
		double y{};
		Clock::time_point nextTransform{};
		Clock::time_point nextVoice{};
		Clock::time_point nextChat{};
		std::vector<ChatMessage> chatMessages;
		std::vector<double> lastSeen;
	};

	struct Harness
	{
		Harness(const Bridge& a_bridge, const Options& a_options) :
			bridge(a_bridge),
			options(a_options),
			buffer(kPollBufferBytes)
		{
		}

		double StampOf(Clock::time_point a_when) const
		{
			return Seconds(a_when - epoch).count();
		}

		bool InWindow(double a_stamp) const
		{
			return a_stamp >= windowStart && a_stamp < windowEnd;
		}

		std::optional<std::size_t> IndexOfPlayer(std::uint32_t a_playerId) const
		{
			for (const Player& player : players) {
				if (player.hasPlayerId && player.playerId == a_playerId) {
					return player.index;
				}
			}
			return std::nullopt;
		}

		const Bridge& bridge;
		const Options& options;
		Clock::time_point epoch{ Clock::now() };
		double windowStart{};
		double windowEnd{};
		double unixWindowStart{};
		double unixWindowEnd{};
		std::vector<Player> players;
		std::vector<Clock::time_point> connectTimes;
		std::vector<std::uint8_t> buffer;
		Tally tally;
		std::optional<VoiceCounters> adminStart;
		std::optional<VoiceCounters> adminEnd;
	};

	struct CpuMark
	{
		Clock::time_point when{};
		double processSeconds{};
		std::uint64_t systemTotal{};
		std::uint64_t systemIdle{};
	};

	inline Clock::duration ToClock(double a_seconds)
	{
		return std::chrono::duration_cast<Clock::duration>(Seconds(a_seconds));
	}

	void PollAll(Harness& a_h);
	CpuMark TakeMark(HANDLE a_process);
	bool ReadVoiceCounters(std::uint16_t a_port, const std::string& a_tokenFile, VoiceCounters& a_out);
	void PrintReport(const Harness& a_h, const CpuMark& a_start, const CpuMark& a_end, const VoiceLab::Relay* a_relay);
	void PrintVerdicts(const std::string& a_baselinePath, const Figures& a_on);
}
