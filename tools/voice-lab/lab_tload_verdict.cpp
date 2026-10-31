#include "lab_tload_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace VoiceLab::Tload
{
	namespace
	{
		constexpr double kLatencyIncreaseLimitMs = 10.0;
		constexpr double kLatencyIncreaseLimitPct = 25.0;
		constexpr double kDeliveryDropLimitPoints = 0.5;
		constexpr double kReliableIncreaseLimitMs = 50.0;
		constexpr double kServerCpuIncreaseLimitPct = 15.0;
		constexpr std::string_view kDeliveryPrefix = "gameplay_delivery_pct ";

		std::optional<std::string> ReadFile(const std::string& a_path)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return std::nullopt;
			}
			std::ostringstream text;
			text << file.rdbuf();
			return text.str();
		}

		std::optional<std::string_view> LineStarting(std::string_view a_text, std::string_view a_prefix)
		{
			std::size_t begin = 0;
			while (begin < a_text.size()) {
				std::size_t end = a_text.find('\n', begin);
				if (end == std::string_view::npos) {
					end = a_text.size();
				}
				std::string_view line = a_text.substr(begin, end - begin);
				if (!line.empty() && line.back() == '\r') {
					line.remove_suffix(1);
				}
				if (line.substr(0, a_prefix.size()) == a_prefix) {
					return line;
				}
				begin = end + 1;
			}
			return std::nullopt;
		}

		std::optional<std::string_view> Token(std::string_view a_line, std::string_view a_key)
		{
			std::size_t begin = 0;
			while (begin < a_line.size()) {
				std::size_t end = a_line.find(' ', begin);
				if (end == std::string_view::npos) {
					end = a_line.size();
				}
				const std::string_view token = a_line.substr(begin, end - begin);
				if (token.size() > a_key.size() && token.substr(0, a_key.size()) == a_key && token[a_key.size()] == '=') {
					return token.substr(a_key.size() + 1);
				}
				begin = end + 1;
			}
			return std::nullopt;
		}

		std::optional<double> ParseDouble(std::string_view a_text)
		{
			const std::string text{ a_text };
			char* stop = nullptr;
			const double value = std::strtod(text.c_str(), &stop);
			if (text.empty() || stop == text.c_str()) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<double> Number(std::string_view a_line, std::string_view a_key)
		{
			const auto token = Token(a_line, a_key);
			return token ? ParseDouble(*token) : std::nullopt;
		}

		std::optional<Figures> ParseBaseline(std::string_view a_text)
		{
			const auto summary = LineStarting(a_text, "tload summary:");
			const auto latency = LineStarting(a_text, "gameplay_latency_ms ");
			const auto delivery = LineStarting(a_text, kDeliveryPrefix);
			const auto reliable = LineStarting(a_text, "reliable_completion_ms ");
			const auto disconnects = LineStarting(a_text, "disconnects=");
			const auto cpu = LineStarting(a_text, "server_cpu ");
			const auto busy = LineStarting(a_text, "system_cpu ");
			const auto loop = LineStarting(a_text, "harness_loop_period_ms ");
			const auto egress = LineStarting(a_text, "voice_egress ");
			const auto run = LineStarting(a_text, "run_status=");
			if (!summary || !latency || !delivery || !reliable || !disconnects || !cpu || !busy || !loop || !egress || !run) {
				return std::nullopt;
			}
			const auto mode = Token(*summary, "mode");
			const auto cap = Token(*summary, "cap_bps");
			const auto voice = Token(*summary, "voice");
			const auto latencyP99 = Number(*latency, "p99");
			const auto deliveryPct = ParseDouble(delivery->substr(kDeliveryPrefix.size()));
			const auto reliableP99 = Number(*reliable, "p99");
			const auto disconnectCount = Number(*disconnects, "disconnects");
			const auto cpuPct = Number(*cpu, "one_core_pct");
			const auto busyPct = Number(*busy, "busy_pct_over_window");
			const auto loopP99 = Number(*loop, "p99");
			const auto maxBin = Number(*egress, "max_1s_bytes");
			const auto runStatus = Token(*run, "run_status");
			if (!mode || !cap || !voice || !latencyP99 || !deliveryPct || !reliableP99 || !disconnectCount || !cpuPct || !busyPct || !loopP99 || !maxBin || !runStatus) {
				return std::nullopt;
			}
			Figures figures;
			figures.mode = std::string{ *mode };
			figures.cap = std::string{ *cap };
			figures.voice = std::string{ *voice };
			figures.latencyP99Ms = *latencyP99;
			figures.deliveryPct = *deliveryPct;
			figures.reliableP99Ms = *reliableP99;
			figures.disconnects = static_cast<std::uint64_t>(*disconnectCount);
			figures.serverCpuPct = *cpuPct;
			figures.systemBusyPct = *busyPct;
			figures.loopP99Ms = *loopP99;
			figures.maxBinBytes = static_cast<std::uint64_t>(*maxBin);
			figures.runStatus = std::string{ *runStatus };
			return figures;
		}

		void PrintCriterion(int a_index, const char* a_name, double a_on, double a_off, double a_change, const char* a_limit, const char* a_verdict)
		{
			std::printf("criterion %d %s on=%.3f off=%.3f change=%.3f limit=%s verdict=%s\n", a_index, a_name, a_on, a_off, a_change, a_limit, a_verdict);
		}
	}

	void PrintVerdicts(const std::string& a_baselinePath, const Figures& a_on)
	{
		const auto text = ReadFile(a_baselinePath);
		const auto off = text ? ParseBaseline(*text) : std::nullopt;
		if (!off) {
			std::printf("comparison_status=INVALID reason=baseline_unreadable\n");
			return;
		}
		if (off->mode != a_on.mode || off->cap != a_on.cap || off->voice != "off" || a_on.voice != "on") {
			std::printf("comparison_status=INVALID reason=baseline_mismatch\n");
			return;
		}
		const bool loopOk = a_on.loopP99Ms <= kLoopPeriodLimitMs && off->loopP99Ms <= kLoopPeriodLimitMs;
		const bool latencyKnown = a_on.latencyP99Ms > 0.0 && off->latencyP99Ms > 0.0;
		const double latencyChange = a_on.latencyP99Ms - off->latencyP99Ms;
		const double latencyPct = off->latencyP99Ms > 0.0 ? 100.0 * latencyChange / off->latencyP99Ms : 0.0;
		const double deliveryDrop = off->deliveryPct - a_on.deliveryPct;
		const double reliableChange = a_on.reliableP99Ms - off->reliableP99Ms;
		const double cpuChange = a_on.serverCpuPct - off->serverCpuPct;
		const double egressMax = static_cast<double>(a_on.maxBinBytes);
		const char* latencyVerdict = !loopOk || !latencyKnown ? "INVALID" : latencyChange <= kLatencyIncreaseLimitMs && latencyPct <= kLatencyIncreaseLimitPct ? "PASS" : "FAIL";
		const char* deliveryVerdict = deliveryDrop <= kDeliveryDropLimitPoints ? "PASS" : "FAIL";
		const char* reliableVerdict = !std::isfinite(off->reliableP99Ms) ? "INVALID" : !std::isfinite(a_on.reliableP99Ms) || reliableChange > kReliableIncreaseLimitMs ? "FAIL" : "PASS";
		const char* disconnectVerdict = a_on.disconnects == 0 && off->disconnects == 0 ? "PASS" : "FAIL";
		const char* cpuVerdict = cpuChange <= kServerCpuIncreaseLimitPct ? "PASS" : "FAIL";
		const char* egressVerdict = egressMax <= kVoiceEgressLimitBytes ? "PASS" : "FAIL";
		PrintCriterion(1, "gameplay_latency_p99_ms", a_on.latencyP99Ms, off->latencyP99Ms, latencyChange, "10ms_and_25pct", latencyVerdict);
		std::printf("latency_p99_change_pct %.2f loop_p99_on_ms=%.3f loop_p99_off_ms=%.3f\n", latencyPct, a_on.loopP99Ms, off->loopP99Ms);
		PrintCriterion(2, "delivery_drop_points", a_on.deliveryPct, off->deliveryPct, deliveryDrop, "0.5", deliveryVerdict);
		PrintCriterion(3, "reliable_completion_p99_ms", a_on.reliableP99Ms, off->reliableP99Ms, reliableChange, "50", reliableVerdict);
		PrintCriterion(4, "disconnects", static_cast<double>(a_on.disconnects), static_cast<double>(off->disconnects), 0.0, "0", disconnectVerdict);
		PrintCriterion(5, "server_cpu_one_core_pct", a_on.serverCpuPct, off->serverCpuPct, cpuChange, "15", cpuVerdict);
		PrintCriterion(6, "voice_egress_max_1s_bytes", egressMax, 0.0, egressMax, std::to_string(static_cast<std::uint64_t>(kVoiceEgressLimitBytes)).c_str(), egressVerdict);
		std::printf("comparison_host system_busy_on=%.3f system_busy_off=%.3f\n", a_on.systemBusyPct, off->systemBusyPct);
		const char* runReason = off->runStatus != "VALID" ? "baseline_run_invalid" : a_on.runStatus != "VALID" ? "on_run_invalid" : nullptr;
		if (runReason != nullptr) {
			std::printf("comparison_status=INVALID reason=%s\n", runReason);
			return;
		}
		const std::array<std::string_view, 6> verdicts{ latencyVerdict, deliveryVerdict, reliableVerdict, disconnectVerdict, cpuVerdict, egressVerdict };
		const bool anyFail = std::count(verdicts.begin(), verdicts.end(), std::string_view{ "FAIL" }) > 0;
		const bool anyInvalid = std::count(verdicts.begin(), verdicts.end(), std::string_view{ "INVALID" }) > 0;
		std::printf("comparison_status=%s\n", anyFail ? "FAIL" : anyInvalid ? "INVALID" : "PASS");
	}
}
