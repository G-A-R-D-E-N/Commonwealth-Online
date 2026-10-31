#include "F4TProxyMovementDiagnostics.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
	using F4T::ProxyMovementDiagnostics::Clock;
	using F4T::ProxyMovementDiagnostics::RenderStatus;
	using F4T::ProxyMovementDiagnostics::RunningStat;
	using F4T::ProxyMovementDiagnostics::SnapReason;
	using F4T::ProxyMovementDiagnostics::Window;

	constexpr double kEpsilon = 1.0e-6;

	const char* g_currentTest = "";
	int g_failures = 0;

	void Check(bool a_condition, const char* a_expression, int a_line)
	{
		if (!a_condition) {
			std::printf("  FAIL %s: %s (line %d)\n", g_currentTest, a_expression, a_line);
			++g_failures;
		}
	}

#define CHECK(expr) Check((expr), #expr, __LINE__)

	bool NearlyEqual(double a_lhs, double a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) <= kEpsilon;
	}

	Clock::time_point AtMilliseconds(std::int64_t a_milliseconds)
	{
		return Clock::time_point{ std::chrono::milliseconds{ a_milliseconds } };
	}

	void test_running_stat_mean_max_and_population_stddev()
	{
		RunningStat stat{};
		CHECK(stat.Mean() == 0.0);
		CHECK(stat.StdDev() == 0.0);
		for (const double value : { 1.0, 2.0, 3.0, 4.0 }) {
			stat.Add(value);
		}
		CHECK(stat.count == 4);
		CHECK(NearlyEqual(stat.Mean(), 2.5));
		CHECK(NearlyEqual(stat.maximum, 4.0));
		CHECK(NearlyEqual(stat.StdDev(), std::sqrt(1.25)));
	}

	void test_constant_samples_have_zero_stddev()
	{
		RunningStat stat{};
		for (int i = 0; i < 3; ++i) {
			stat.Add(0.05);
		}
		CHECK(NearlyEqual(stat.Mean(), 0.05));
		CHECK(NearlyEqual(stat.StdDev(), 0.0));
	}

	void test_packets_skip_repeated_stamps_and_empty_stamps()
	{
		Window window{};
		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(10000), 100.0);
		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(10000), 100.0);
		F4T::ProxyMovementDiagnostics::NotePacket(window, Clock::time_point{}, 0.0);
		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(10050), 120.0);
		CHECK(window.packets == 2);
		CHECK(window.arrivalIntervalSeconds.count == 1);
		CHECK(NearlyEqual(window.arrivalIntervalSeconds.Mean(), 0.05));
		CHECK(NearlyEqual(window.receivedSpeed.Mean(), 110.0));
	}

	void test_render_status_fractions_and_depth()
	{
		Window window{};
		CHECK(F4T::ProxyMovementDiagnostics::Fraction(window, RenderStatus::Interpolating) == 0.0);
		for (int i = 0; i < 3; ++i) {
			F4T::ProxyMovementDiagnostics::NoteRender(window, RenderStatus::Interpolating, 200.0, 4, 0.1);
		}
		F4T::ProxyMovementDiagnostics::NoteRender(window, RenderStatus::Extrapolating, 200.0, 2, 0.1);
		CHECK(window.renders == 4);
		CHECK(NearlyEqual(F4T::ProxyMovementDiagnostics::Fraction(window, RenderStatus::Interpolating), 0.75));
		CHECK(NearlyEqual(F4T::ProxyMovementDiagnostics::Fraction(window, RenderStatus::Extrapolating), 0.25));
		CHECK(NearlyEqual(F4T::ProxyMovementDiagnostics::Fraction(window, RenderStatus::HoldingEnd), 0.0));
		CHECK(NearlyEqual(window.bufferDepth.Mean(), 3.5));
		CHECK(NearlyEqual(window.renderDelaySeconds.Mean(), 0.1));
	}

	void test_snap_reasons_are_counted_separately()
	{
		Window window{};
		F4T::ProxyMovementDiagnostics::NoteSnap(window, SnapReason::kMovementType);
		F4T::ProxyMovementDiagnostics::NoteSnap(window, SnapReason::kIdle);
		F4T::ProxyMovementDiagnostics::NoteSnap(window, SnapReason::kIdle);
		F4T::ProxyMovementDiagnostics::NoteSnap(window, SnapReason::kDistance);
		CHECK(window.snaps[static_cast<std::size_t>(SnapReason::kMovementType)] == 1);
		CHECK(window.snaps[static_cast<std::size_t>(SnapReason::kIdle)] == 2);
		CHECK(window.snaps[static_cast<std::size_t>(SnapReason::kFirstSample)] == 0);
		CHECK(window.snaps[static_cast<std::size_t>(SnapReason::kDistance)] == 1);
	}

	void test_take_if_due_rolls_over_each_second_and_keeps_the_last_arrival()
	{
		Window window{};
		CHECK(!F4T::ProxyMovementDiagnostics::TakeIfDue(window, AtMilliseconds(0)).has_value());
		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(100), 50.0);
		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(150), 50.0);
		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(950), 50.0);
		CHECK(!F4T::ProxyMovementDiagnostics::TakeIfDue(window, AtMilliseconds(999)).has_value());

		const auto closed = F4T::ProxyMovementDiagnostics::TakeIfDue(window, AtMilliseconds(1000));
		CHECK(closed.has_value());
		if (closed) {
			CHECK(closed->packets == 3);
			CHECK(closed->arrivalIntervalSeconds.count == 2);
			CHECK(NearlyEqual(closed->durationSeconds, 1.0));
		}
		CHECK(window.packets == 0);
		CHECK(window.arrivalIntervalSeconds.count == 0);

		F4T::ProxyMovementDiagnostics::NotePacket(window, AtMilliseconds(1050), 50.0);
		CHECK(window.arrivalIntervalSeconds.count == 1);
		CHECK(NearlyEqual(window.arrivalIntervalSeconds.Mean(), 0.1));
	}

	void test_format_names_the_player_and_the_snap_reasons()
	{
		Window window{};
		F4T::ProxyMovementDiagnostics::NoteSnap(window, SnapReason::kIdle);
		window.durationSeconds = 1.0;
		const auto line = F4T::ProxyMovementDiagnostics::FormatProxyWindow(7, window);
		CHECK(line.find("playerId=7") != std::string::npos);
		CHECK(line.find("idle=1") != std::string::npos);
	}

	void test_send_count_formats_a_rate()
	{
		Window window{};
		for (int i = 0; i < 20; ++i) {
			F4T::ProxyMovementDiagnostics::NoteSend(window);
		}
		window.durationSeconds = 1.0;
		const auto line = F4T::ProxyMovementDiagnostics::FormatSendWindow(window);
		CHECK(line.find("transformsSentPerSecond=20.0") != std::string::npos);
	}

	void RunTest(const char* a_name, void (*a_test)())
	{
		g_currentTest = a_name;
		const auto failuresBefore = g_failures;
		a_test();
		std::printf("%s %s\n", g_failures == failuresBefore ? "PASS" : "FAIL", a_name);
	}
}

int main()
{
	RunTest("test_running_stat_mean_max_and_population_stddev", test_running_stat_mean_max_and_population_stddev);
	RunTest("test_constant_samples_have_zero_stddev", test_constant_samples_have_zero_stddev);
	RunTest("test_packets_skip_repeated_stamps_and_empty_stamps", test_packets_skip_repeated_stamps_and_empty_stamps);
	RunTest("test_render_status_fractions_and_depth", test_render_status_fractions_and_depth);
	RunTest("test_snap_reasons_are_counted_separately", test_snap_reasons_are_counted_separately);
	RunTest("test_take_if_due_rolls_over_each_second_and_keeps_the_last_arrival", test_take_if_due_rolls_over_each_second_and_keeps_the_last_arrival);
	RunTest("test_format_names_the_player_and_the_snap_reasons", test_format_names_the_player_and_the_snap_reasons);
	RunTest("test_send_count_formats_a_rate", test_send_count_formats_a_rate);

	std::printf("%s\n", g_failures == 0 ? "all checks passed" : "FAILURES PRESENT");
	return g_failures == 0 ? 0 : 1;
}
