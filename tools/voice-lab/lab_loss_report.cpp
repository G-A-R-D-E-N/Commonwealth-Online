#include "lab_loss.h"
#include "lab_stats.h"

#include <cstdio>

namespace VoiceLab::Loss
{
	namespace
	{
		constexpr double kPercentScale = 100.0;
		constexpr double kConcealLimitPercent = 6.0;
		constexpr double kConcealOrRecoverLimitPercent = 16.0;
		constexpr double kUnderrunsPerMinuteLimit = 2.0;
		constexpr double kLagLimitMs = 200.0;
		constexpr double kLatencyLimitMs = 350.0;
		constexpr double kBytesPerMiB = 1048576.0;
		constexpr double kPrivateGrowthLimitBytes = kBytesPerMiB;
		constexpr double kSecondsPerMinute = 60.0;

		void PrintDirection(const char* a_label, const VoiceLab::DirectionCounters& a_counters)
		{
			const std::uint64_t accounted = a_counters.droppedLoss + a_counters.droppedQueue + a_counters.forwarded + a_counters.failed + a_counters.abandoned;
			std::printf("%s received %llu (%llu bytes) dropped-loss %llu dropped-queue %llu forwarded %llu failed %llu abandoned-at-stop %llu receive-errors %llu identity %s\n",
				a_label, a_counters.datagrams, a_counters.bytes, a_counters.droppedLoss, a_counters.droppedQueue, a_counters.forwarded, a_counters.failed,
				a_counters.abandoned, a_counters.receiveErrors, accounted == a_counters.datagrams ? "closes" : "OPEN");
		}
	}

	int Summarize(const LossOptions& a_options, const Profile& a_profile, const Session& a_session, const SenderTotals& a_sender,
		const ReceiverTotals& a_receiver, const VoiceLab::RelayCounters& a_relay, const MemorySample& a_memoryEnd)
	{
		const double ticks = static_cast<double>(a_session.ticks);
		const std::uint64_t concealed = a_receiver.conceal + a_receiver.silence;
		const double concealedPercent = kPercentScale * static_cast<double>(concealed) / ticks;
		const double recoveredPercent = kPercentScale * static_cast<double>(a_receiver.fec) / ticks;
		const double concealedOrRecoveredPercent = concealedPercent + recoveredPercent;
		const double underruns = static_cast<double>(a_receiver.underrunsFinal - a_receiver.underrunsAtWarmup);
		const double minutes = static_cast<double>(a_options.seconds - a_options.warmupSeconds) / kSecondsPerMinute;
		const double underrunsPerMinute = underruns / minutes;
		const double p50 = VoiceLab::Percentile(a_receiver.latencyMs, 0.50);
		const double p95 = VoiceLab::Percentile(a_receiver.latencyMs, 0.95);
		const double latencyMax = VoiceLab::Max(a_receiver.latencyMs);
		const double lagP95 = VoiceLab::Percentile(a_receiver.lagMs, 0.95);
		const double lagMax = VoiceLab::Max(a_receiver.lagMs);
		const double growth = static_cast<double>(a_memoryEnd.privateBytes) - static_cast<double>(a_receiver.memoryWarmup.privateBytes);
		const VoiceLab::Impairment& impairment = a_profile.impairment;

		std::printf("profile %c: loss %.2f%% fixed %d ms jitter 0-%d ms cap %.0f B/s seed %u seconds %d warmup %d\n", a_profile.letter,
			impairment.lossPercent, impairment.fixedDelayMs, impairment.jitterMaxMs, impairment.capBytesPerSecond, a_options.seed, a_options.seconds,
			a_options.warmupSeconds);
		std::printf("frames sent %llu send-failed %llu codec-dropped %llu\n", a_sender.sent, a_sender.sendFailed, a_sender.codecDropped);
		std::printf("playout ticks after start %d (noneYet %llu, decode errors %llu)\n", a_session.ticks, a_receiver.noneYet, a_receiver.decodeErrors);
		std::printf("played %llu fec %llu conceal %llu silence %llu\n", a_receiver.played, a_receiver.fec, a_receiver.conceal, a_receiver.silence);
		std::printf("concealed ratio %.3f%% ((conceal+silence)/ticks), recovered ratio %.3f%% (fec/ticks)\n", concealedPercent, recoveredPercent);
		std::printf("underruns total %llu, after warmup %.0f, per minute after warmup %.3f\n", a_receiver.underrunsFinal, underruns, underrunsPerMinute);
		std::printf("depth target after warmup min %d ms max %d ms final %d ms (information only)\n", a_receiver.depthMinMs, a_receiver.depthMaxMs, a_receiver.depthFinalMs);
		std::printf("buffered lag after warmup p95 %.1f ms max %.1f ms over %zu playing ticks, drains %llu\n", lagP95, lagMax, a_receiver.lagMs.size(), a_receiver.drainEvents);
		std::printf("latency send->playout p50 %.1f ms p95 %.1f ms max %.1f ms (%zu samples, whole run)\n", p50, p95, latencyMax, a_receiver.latencyMs.size());
		std::printf("receiver datagrams %llu accepted %llu overflow-discards %llu malformed %llu window-rejected %llu jitter-rejected %llu\n", a_receiver.datagrams,
			a_receiver.accepted, a_receiver.overflowDiscards, a_receiver.malformed, a_receiver.windowRejected, a_receiver.jitterRejected);
		std::printf("frames accounted: accepted %llu = played %llu + drained %llu + overflow-discards %llu + expired %llu + buffered at end %llu, identity %s\n",
			a_receiver.accepted, a_receiver.played, a_receiver.drainedFrames, a_receiver.overflowDiscards, a_receiver.expiredFrames, a_receiver.bufferedFinal,
			a_receiver.played + a_receiver.drainedFrames + a_receiver.overflowDiscards + a_receiver.expiredFrames + a_receiver.bufferedFinal == a_receiver.accepted ? "closes" : "OPEN");
		std::printf("receiver drained after relay stop %llu, relay forwarded %llu, difference %lld\n", a_receiver.lateDatagrams,
			a_relay.clientToTarget.forwarded,
			static_cast<long long>(a_relay.clientToTarget.forwarded) - static_cast<long long>(a_receiver.datagrams + a_receiver.lateDatagrams));
		PrintDirection("relay client->target", a_relay.clientToTarget);
		PrintDirection("relay target->client", a_relay.targetToClient);
		std::printf("private bytes at warmup %.2f MB, at end %.2f MB, growth %.0f bytes\n", static_cast<double>(a_receiver.memoryWarmup.privateBytes) / kBytesPerMiB,
			static_cast<double>(a_memoryEnd.privateBytes) / kBytesPerMiB, growth);
		std::printf("working set at warmup %.2f MB, at end %.2f MB (information only, no criterion)\n",
			static_cast<double>(a_receiver.memoryWarmup.workingSetBytes) / kBytesPerMiB, static_cast<double>(a_memoryEnd.workingSetBytes) / kBytesPerMiB);

		bool pass = true;
		if (a_profile.checkConcealed)
		{
			pass = PrintCriterion("(a) concealed ratio <= 6%", concealedPercent <= kConcealLimitPercent) && pass;
		}
		if (a_profile.checkRecovered)
		{
			pass = PrintCriterion("(b) concealed + recovered <= 16%", concealedOrRecoveredPercent <= kConcealOrRecoverLimitPercent) && pass;
		}
		if (a_profile.checkJitter)
		{
			pass = PrintCriterion("(c) underruns <= 2 per minute and buffered lag p95 <= 200 ms", underrunsPerMinute <= kUnderrunsPerMinuteLimit && !a_receiver.lagMs.empty() && lagP95 <= kLagLimitMs) && pass;
		}
		if (a_profile.checkLatency)
		{
			pass = PrintCriterion("(d) latency p95 <= 350 ms", !a_receiver.latencyMs.empty() && p95 <= kLatencyLimitMs) && pass;
		}
		pass = PrintCriterion("private bytes growth warmup to end <= 1 MB", growth <= kPrivateGrowthLimitBytes) && pass;
		return pass ? 0 : 1;
	}
}
