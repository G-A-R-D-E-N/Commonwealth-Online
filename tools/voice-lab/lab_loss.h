#pragma once

#include "lab_relay.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace VoiceLab::Loss
{
	using Clock = std::chrono::steady_clock;

	struct Profile
	{
		char letter;
		VoiceLab::Impairment impairment;
		bool checkConcealed;
		bool checkRecovered;
		bool checkJitter;
		bool checkLatency;
	};

	struct LossOptions
	{
		std::string wavPath;
		char profile{};
		int seconds{ 180 };
		int warmupSeconds{ 30 };
		std::uint32_t seed{ 20261022U };
	};

	struct Session
	{
		Clock::time_point start{};
		int ticks{};
		int warmupTicks{};
		const std::vector<std::int16_t>* pcm{};
		std::size_t frames{};
		std::vector<std::atomic<Clock::rep>> sendNs{};
	};

	struct SenderTotals
	{
		bool encoderOk{};
		std::uint64_t sent{};
		std::uint64_t sendFailed{};
		std::uint64_t codecDropped{};
	};

	struct MemorySample
	{
		std::size_t privateBytes{};
		std::size_t workingSetBytes{};
	};

	struct ReceiverTotals
	{
		bool decoderOk{};
		std::uint64_t datagrams{};
		std::uint64_t lateDatagrams{};
		std::uint64_t malformed{};
		std::uint64_t windowRejected{};
		std::uint64_t jitterRejected{};
		std::uint64_t accepted{};
		std::uint64_t overflowDiscards{};
		std::uint64_t played{};
		std::uint64_t fec{};
		std::uint64_t conceal{};
		std::uint64_t silence{};
		std::uint64_t noneYet{};
		std::uint64_t decodeErrors{};
		std::uint64_t underrunsAtWarmup{};
		std::uint64_t underrunsFinal{};
		std::uint64_t drainEvents{};
		std::uint64_t drainedFrames{};
		std::uint64_t expiredFrames{};
		std::uint64_t bufferedFinal{};
		MemorySample memoryWarmup{};
		int depthMinMs{ (std::numeric_limits<int>::max)() };
		int depthMaxMs{};
		int depthFinalMs{};
		std::vector<double> latencyMs{};
		std::vector<double> lagMs{};
	};

	int Summarize(const LossOptions& a_options, const Profile& a_profile, const Session& a_session, const SenderTotals& a_sender,
		const ReceiverTotals& a_receiver, const VoiceLab::RelayCounters& a_relay, const MemorySample& a_memoryEnd);

	int RunLoss(int a_argc, char** a_argv);
}
