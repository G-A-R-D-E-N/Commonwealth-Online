#include "lab_tload_state.h"

#include "lab_relay.h"
#include "lab_stats.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace VoiceLab::Tload
{
	namespace
	{
		constexpr double kCpuTicksPerSecond = 1.0e7;
		constexpr double kBusyLimitPct = 50.0;
		constexpr double kDeliveryFloorPct = 99.0;
		constexpr double kNominalFloorPct = 98.0;

		struct ChatClass
		{
			std::uint64_t inWindow{};
			std::uint64_t zeroArrival{};
			std::uint64_t unclassified{};
		};

		std::uint64_t Ticks(const FILETIME& a_time)
		{
			ULARGE_INTEGER value{};
			value.LowPart = a_time.dwLowDateTime;
			value.HighPart = a_time.dwHighDateTime;
			return value.QuadPart;
		}

		double MillisecondsAt(const std::vector<double>& a_seconds, double a_fraction)
		{
			return a_seconds.empty() ? std::numeric_limits<double>::quiet_NaN() : 1000.0 * Percentile(a_seconds, a_fraction);
		}

		double CompletionMilliseconds(const std::vector<double>& a_arrivals, std::uint64_t a_undelivered, double a_fraction)
		{
			std::vector<double> samples = a_arrivals;
			samples.resize(samples.size() + a_undelivered, std::numeric_limits<double>::infinity());
			return MillisecondsAt(samples, a_fraction);
		}

		double Percent(std::uint64_t a_achieved, double a_nominal)
		{
			return a_nominal > 0.0 ? 100.0 * static_cast<double>(a_achieved) / a_nominal : 0.0;
		}

		void AddReason(std::string& a_reasons, const char* a_reason)
		{
			if (!a_reasons.empty()) {
				a_reasons += ',';
			}
			a_reasons += a_reason;
		}

		ChatClass ClassifyChat(const Harness& a_h)
		{
			ChatClass result;
			const auto others = static_cast<std::uint64_t>(a_h.players.size() - 1);
			for (const Player& player : a_h.players) {
				for (const ChatMessage& message : player.chatMessages) {
					if (!a_h.InWindow(message.sent)) {
						continue;
					}
					++result.inWindow;
					if (message.arrivals == 0) {
						++result.zeroArrival;
					} else if (message.arrivals < others) {
						result.unclassified += others - message.arrivals;
					}
				}
			}
			return result;
		}
	}

	CpuMark TakeMark(HANDLE a_process)
	{
		CpuMark mark{ Clock::now() };
		FILETIME creation{};
		FILETIME exited{};
		FILETIME kernel{};
		FILETIME user{};
		if (GetProcessTimes(a_process, &creation, &exited, &kernel, &user) != 0) {
			mark.processSeconds = static_cast<double>(Ticks(kernel) + Ticks(user)) / kCpuTicksPerSecond;
		}
		FILETIME idle{};
		if (GetSystemTimes(&idle, &kernel, &user) != 0) {
			mark.systemIdle = Ticks(idle);
			mark.systemTotal = Ticks(kernel) + Ticks(user);
		}
		return mark;
	}

	void PrintReport(const Harness& a_h, const CpuMark& a_start, const CpuMark& a_end, const VoiceLab::Relay* a_relay)
	{
		const Tally& t = a_h.tally;
		const auto others = static_cast<double>(a_h.players.size() - 1);
		const auto clients = static_cast<double>(a_h.players.size());
		const ChatClass chat = ClassifyChat(a_h);
		const std::uint64_t refusedMessages = (std::min)(chat.zeroArrival, t.chatRejectedRateLimited);
		const std::uint64_t expectedChatCount = (chat.inWindow - refusedMessages) * (a_h.players.size() - 1);
		const std::uint64_t undelivered = expectedChatCount > t.chatDelivered ? expectedChatCount - t.chatDelivered : 0;
		const double expectedTransforms = static_cast<double>(t.transformsSent) * others;
		const double deliveryPct = expectedTransforms > 0.0 ? 100.0 * static_cast<double>(t.transformDelivered) / expectedTransforms : 0.0;
		const double transformBytesAvg = t.transformDelivered > 0 ? static_cast<double>(t.transformBytes) / static_cast<double>(t.transformDelivered) : 0.0;
		const double offeredBytesPerSecond = transformBytesAvg * expectedTransforms / static_cast<double>(a_h.options.seconds);
		const double voiceCopiesPerSent = t.voiceSent > 0 ? static_cast<double>(t.voiceDatagrams) / static_cast<double>(t.voiceSent) : 0.0;
		const double wall = Seconds(a_end.when - a_start.when).count();
		const double serverCpuPct = wall > 0.0 ? 100.0 * (a_end.processSeconds - a_start.processSeconds) / wall : 0.0;
		const double systemTotal = static_cast<double>(a_end.systemTotal - a_start.systemTotal);
		const double systemBusyPct = systemTotal > 0.0 ? 100.0 * (systemTotal - static_cast<double>(a_end.systemIdle - a_start.systemIdle)) / systemTotal : 0.0;
		const std::uint64_t maxBin = t.voiceBins.empty() ? 0U : *std::max_element(t.voiceBins.begin(), t.voiceBins.end());
		const auto& options = a_h.options;
		const bool voiceOn = options.voice == "on";
		const double seconds = static_cast<double>(options.seconds);
		const double nominalTransforms = seconds / kTransformIntervalSeconds * clients;
		const double nominalChat = seconds / kChatIntervalSeconds * clients;
		const double nominalVoice = voiceOn ? seconds / kVoiceIntervalSeconds * clients : 0.0;
		const std::uint64_t transformAttempts = t.transformsSent + t.transformRefused;
		const std::uint64_t chatAttempts = t.chatSent + t.chatRefused;
		const std::uint64_t voiceAttempts = t.voiceSent + t.voiceRefused;
		const double transformPct = Percent(transformAttempts, nominalTransforms);
		const double chatPct = Percent(chatAttempts, nominalChat);
		const double voicePct = Percent(voiceAttempts, nominalVoice);
		const double loopP99Ms = MillisecondsAt(t.loopPeriods, 0.99);
		const std::string cap = options.relayCapBps ? std::to_string(*options.relayCapBps) : "none";
		std::printf("tload summary: mode=%s cap_bps=%s voice=%s clients=%zu warmup_s=%.0f window_s=%d drain_s=%.0f seed=%u\n",
			a_relay ? "relay" : "direct", cap.c_str(), options.voice.c_str(), a_h.players.size(), kWarmupSeconds, options.seconds, kDrainSeconds, options.seed);
		std::printf("window_unix_start=%.3f window_unix_end=%.3f\n", a_h.unixWindowStart, a_h.unixWindowEnd);
		std::printf("transforms sent_in_window=%llu refused=%llu expected_deliveries=%.0f delivered=%llu bytes_per_transform=%.1f\n",
			t.transformsSent, t.transformRefused, expectedTransforms, t.transformDelivered, transformBytesAvg);
		std::printf("gameplay_latency_ms p50=%.3f p99=%.3f samples=%zu\n", MillisecondsAt(t.latencies, 0.5), MillisecondsAt(t.latencies, 0.99), t.latencies.size());
		std::printf("gameplay_offered_bytes_per_s %.0f\n", offeredBytesPerSecond);
		std::printf("gameplay_delivery_pct %.4f\n", deliveryPct);
		std::printf("chat sent_in_window=%llu send_refused=%llu expected_deliveries=%llu arrivals=%llu undelivered=%llu zero_arrival_messages=%llu unclassified_missing=%llu\n",
			t.chatSent, t.chatRefused, expectedChatCount, t.chatDelivered, undelivered, chat.zeroArrival, chat.unclassified);
		std::printf("chat server_refused_messages=%llu chat_rejected_in_window=%llu rate_limited=%llu\n",
			refusedMessages, t.chatRejected, t.chatRejectedRateLimited);
		std::printf("reliable_completion_ms p50=%.3f p99=%.3f samples=%zu undelivered=%llu\n",
			CompletionMilliseconds(t.chatLatencies, undelivered, 0.5), CompletionMilliseconds(t.chatLatencies, undelivered, 0.99), t.chatLatencies.size(), undelivered);
		std::printf("nominal_transforms=%.0f achieved=%llu pct=%.2f\n", nominalTransforms, transformAttempts, transformPct);
		std::printf("nominal_chat=%.0f achieved=%llu pct=%.2f\n", nominalChat, chatAttempts, chatPct);
		if (voiceOn) {
			std::printf("nominal_voice=%.0f achieved=%llu pct=%.2f\n", nominalVoice, voiceAttempts, voicePct);
		} else {
			std::printf("nominal_voice=off\n");
		}
		std::printf("disconnects=%llu session_ended=%llu position_corrections=%llu malformed=%llu poll_errors=%llu encode_failures=%llu\n",
			t.disconnects, t.sessionEnded, t.positionCorrections, t.malformed, t.pollErrors, t.encodeFailures);
		std::printf("server_cpu process_s=%.3f wall_s=%.3f one_core_pct=%.3f\n", a_end.processSeconds - a_start.processSeconds, wall, serverCpuPct);
		std::printf("system_cpu busy_pct_over_window=%.3f\n", systemBusyPct);
		std::printf("harness_loop_period_ms p50=%.3f p99=%.3f max=%.3f samples=%zu\n",
			MillisecondsAt(t.loopPeriods, 0.5), loopP99Ms, 1000.0 * Max(t.loopPeriods), t.loopPeriods.size());
		std::printf("voice_egress window_bytes=%llu max_1s_bytes=%llu datagrams=%llu limit_1s_bytes=%.0f sent=%llu send_refused=%llu delivered_copies_per_sent=%.3f\n",
			t.voiceBytes, maxBin, t.voiceDatagrams, kVoiceEgressLimitBytes, t.voiceSent, t.voiceRefused, voiceCopiesPerSent);
		if (options.adminPort == 0) {
			std::printf("admin_voice_window not_requested\n");
		} else if (a_h.adminStart && a_h.adminEnd) {
			const VoiceCounters& start = *a_h.adminStart;
			const VoiceCounters& end = *a_h.adminEnd;
			std::printf("admin_voice_window sent=%llu relayed=%llu dropped_egress=%llu dropped_soft_limit=%llu\n",
				end.sent - start.sent, end.relayed - start.relayed, end.droppedEgress - start.droppedEgress, end.droppedSoftLimit - start.droppedSoftLimit);
		} else {
			std::printf("admin_voice_window unavailable\n");
		}
		if (a_relay != nullptr) {
			const auto counters = a_relay->Counters().targetToClient;
			std::printf("relay_target_to_client forwarded=%llu dropped_queue=%llu dropped_loss=%llu failed=%llu abandoned=%llu\n",
				counters.forwarded, counters.droppedQueue, counters.droppedLoss, counters.failed, counters.abandoned);
		}
		std::string reasons;
		if (deliveryPct < kDeliveryFloorPct) {
			AddReason(reasons, "delivery_below_99pct");
		}
		if (transformPct < kNominalFloorPct) {
			AddReason(reasons, "transforms_below_98pct_nominal");
		}
		if (chatPct < kNominalFloorPct) {
			AddReason(reasons, "chat_below_98pct_nominal");
		}
		if (voiceOn && voicePct < kNominalFloorPct) {
			AddReason(reasons, "voice_below_98pct_nominal");
		}
		if (undelivered > 0) {
			AddReason(reasons, "reliable_undelivered");
		}
		if (systemBusyPct > kBusyLimitPct) {
			AddReason(reasons, "system_busy_over_50pct");
		}
		if (loopP99Ms > kLoopPeriodLimitMs) {
			AddReason(reasons, "loop_period_p99_over_10ms");
		}
		if (options.adminPort != 0 && !(a_h.adminStart && a_h.adminEnd)) {
			AddReason(reasons, "admin_status_unavailable");
		}
		if (t.disconnects != 0 || t.sessionEnded != 0 || t.pollErrors != 0 || t.encodeFailures != 0) {
			AddReason(reasons, "disconnects_or_errors");
		}
		std::printf("run_status=%s reasons=%s\n", reasons.empty() ? "VALID" : "INVALID", reasons.empty() ? "none" : reasons.c_str());
		if (!options.baselinePath.empty()) {
			const Figures figures{
				.mode = a_relay ? "relay" : "direct",
				.cap = cap,
				.voice = options.voice,
				.latencyP99Ms = MillisecondsAt(t.latencies, 0.99),
				.deliveryPct = deliveryPct,
				.reliableP99Ms = CompletionMilliseconds(t.chatLatencies, undelivered, 0.99),
				.disconnects = t.disconnects,
				.serverCpuPct = serverCpuPct,
				.systemBusyPct = systemBusyPct,
				.loopP99Ms = loopP99Ms,
				.maxBinBytes = maxBin,
				.runStatus = reasons.empty() ? "VALID" : "INVALID",
			};
			PrintVerdicts(options.baselinePath, figures);
		}
	}
}
