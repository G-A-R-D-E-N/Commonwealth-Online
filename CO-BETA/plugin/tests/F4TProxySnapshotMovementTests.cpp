#include "F4TProxySnapshotMovement.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	using F4T::ProxySnapshotMovement::RenderResult;
	using F4T::ProxySnapshotMovement::State;
	using F4T::Replication::MovementSnapshot;
	using F4T::Replication::Vec3;
	using F4T::ProxySnapshotMovement::kBlendSeconds;

	constexpr float kEpsilon = 1.0e-3F;

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

	bool NearlyEqual(float a_lhs, float a_rhs, float a_epsilon = kEpsilon)
	{
		return std::fabs(a_lhs - a_rhs) <= a_epsilon;
	}

	MovementSnapshot Sample(float a_x, float a_y = 0.0F, float a_z = 0.0F, float a_heading = 0.0F, bool a_moving = true)
	{
		return MovementSnapshot{ Vec3{ a_x, a_y, a_z }, a_heading, a_moving };
	}

	void test_empty_state_has_nothing_to_render()
	{
		State state{};
		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.0);
		CHECK(!rendered.hasValue);
	}

	void test_first_sample_renders_at_its_own_position()
	{
		State state{};
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(100.0F, 200.0F, 300.0F, 1.5F), false, 1.0, 0.0));
		CHECK(state.buffer.Size() == 1);

		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.0);
		CHECK(rendered.hasValue);
		CHECK(NearlyEqual(rendered.snapshot.position.x, 100.0F));
		CHECK(NearlyEqual(rendered.snapshot.position.y, 200.0F));
		CHECK(NearlyEqual(rendered.snapshot.position.z, 300.0F));
		CHECK(NearlyEqual(rendered.snapshot.heading, 1.5F));
	}

	void test_repeated_snapshot_is_not_a_new_sample()
	{
		State state{};
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(10.0F), false, 1.0, 0.0));
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(10.0F), false, 1.1, 0.0));
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(10.0F), false, 1.2, 0.0));
		CHECK(state.buffer.Size() == 1);

		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(11.0F), false, 1.3, 0.0));
		CHECK(state.buffer.Size() == 2);
	}

	void test_non_finite_input_is_rejected()
	{
		State state{};
		const float nan = std::nanf("");
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(nan), false, 1.0, 0.0));
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(1.0F, nan), false, 1.0, 0.0));
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(1.0F, 0.0F, nan), false, 1.0, 0.0));
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(1.0F, 0.0F, 0.0F, nan), false, 1.0, 0.0));
		CHECK(!F4T::ProxySnapshotMovement::Record(state, 7, Sample(1.0F), false, std::nan(""), 0.0));
		CHECK(state.buffer.Empty());
		CHECK(!state.hasLastSnapshot);
	}

	void test_reassigned_slot_drops_the_previous_players_history()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(100.0F), false, 1.1, 0.0);
		CHECK(state.buffer.Size() == 2);

		CHECK(F4T::ProxySnapshotMovement::Record(state, 9, Sample(300.0F), false, 1.2, 0.0));
		CHECK(state.buffer.Size() == 1);

		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.2);
		CHECK(rendered.hasValue);
		CHECK(NearlyEqual(rendered.snapshot.position.x, 300.0F));
	}

	void test_reset_clears_the_diagnostics_window()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxyMovementDiagnostics::NoteSend(state.diagnostics);
		CHECK(state.diagnostics.sent == 1);
		F4T::ProxySnapshotMovement::Reset(state);
		CHECK(state.diagnostics.sent == 0);
		CHECK(!state.diagnostics.started);
	}

	void test_forced_snap_and_large_gap_drop_the_buffer()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(50.0F), false, 1.1, 0.0);
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(100.0F), true, 1.2, 0.0));
		CHECK(state.buffer.Size() == 1);

		F4T::ProxySnapshotMovement::Record(state, 7, Sample(150.0F), false, 1.3, 0.0);
		CHECK(state.buffer.Size() == 2);
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(1700.0F), false, 1.4, 0.0));
		CHECK(state.buffer.Size() == 1);
	}

	void test_playback_is_monotonic_and_lags_the_newest_sample()
	{
		State state{};
		for (int i = 0; i <= 30; ++i) {
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(i) * 30.0F), false, i * 0.1, 0.0);
		}

		float previousX = -1.0e9F;
		int rendered = 0;
		for (int frame = 0; frame <= 100; ++frame) {
			const double now = 1.2 + (frame * 0.016);
			const auto result = F4T::ProxySnapshotMovement::Render(state, now);
			CHECK(result.hasValue);
			if (!result.hasValue) {
				return;
			}
			CHECK(result.snapshot.position.x >= previousX - 1.0e-3F);
			CHECK(result.snapshot.position.x <= 30.0F * 30.0F + kEpsilon);
			previousX = result.snapshot.position.x;
			++rendered;
		}
		CHECK(rendered > 0);
		CHECK(state.buffer.RenderDelay() > F4T::Replication::AdaptiveRenderDelay::kDefaultDelaySeconds);
	}

	void test_playback_never_jumps_more_than_a_sample_step()
	{
		State state{};
		for (int i = 0; i <= 30; ++i) {
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(i) * 30.0F), false, i * 0.1, 0.0);
		}

		float previousX = -1.0e9F;
		float largestStep = 0.0F;
		for (int frame = 0; frame <= 100; ++frame) {
			const auto result = F4T::ProxySnapshotMovement::Render(state, 1.2 + (frame * 0.016));
			if (result.hasValue && previousX > -1.0e8F) {
				largestStep = std::max(largestStep, result.snapshot.position.x - previousX);
			}
			if (result.hasValue) {
				previousX = result.snapshot.position.x;
			}
		}
		CHECK(largestStep > 0.0F);
		CHECK(largestStep < 30.0F);
	}

	void test_playback_holds_the_capped_extrapolation_when_the_sender_stalls()
	{
		State state{};
		for (int i = 0; i <= 10; ++i) {
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(i) * 30.0F), false, i * 0.1, 0.0);
		}

		const auto stalled = F4T::ProxySnapshotMovement::Render(state, 20.0);
		CHECK(stalled.hasValue);
		CHECK(NearlyEqual(stalled.snapshot.position.x, 337.5F));

		const auto held = F4T::ProxySnapshotMovement::Render(state, 60.0);
		CHECK(held.hasValue);
		CHECK(NearlyEqual(held.snapshot.position.x, 337.5F));
	}

	void test_render_before_the_oldest_sample_clamps_to_it()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(400.0F, 0.0F, 0.0F, 0.5F), false, 5.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(500.0F, 0.0F, 0.0F, 0.6F), false, 5.1, 0.0);

		const auto clamped = F4T::ProxySnapshotMovement::Render(state, 5.0);
		CHECK(clamped.hasValue);
		CHECK(NearlyEqual(clamped.snapshot.position.x, 400.0F));
		CHECK(NearlyEqual(clamped.snapshot.heading, 0.5F));
	}

	void test_heading_is_interpolated_along_the_shortest_arc()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F, 0.0F, 0.0F, 3.10F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F, 0.0F, 0.0F, -3.10F), false, 1.1, 0.0);

		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.15);
		CHECK(rendered.hasValue);
		const float heading = std::fabs(rendered.snapshot.heading);
		CHECK(heading > 3.13F && heading <= 3.15F);
	}

	void test_extrapolation_stops_at_the_cap()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(30.0F), false, 1.1, 0.0);

		const auto ahead = F4T::ProxySnapshotMovement::Render(state, 1.25);
		CHECK(ahead.hasValue);
		CHECK(NearlyEqual(ahead.snapshot.position.x, 43.5F));

		const auto past = F4T::ProxySnapshotMovement::Render(state, 1.5);
		CHECK(past.hasValue);
		CHECK(NearlyEqual(past.snapshot.position.x, 67.5F));
	}

	State RecordedPastTheNewestSample(bool a_renderAhead)
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(30.0F), false, 1.1, 0.0);
		if (a_renderAhead) {
			const auto ahead = F4T::ProxySnapshotMovement::Render(state, 1.25);
			CHECK(NearlyEqual(ahead.snapshot.position.x, 43.5F));
		}
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(40.0F), false, 1.3, 0.0);
		return state;
	}

	void test_new_sample_after_extrapolation_blends_instead_of_snapping()
	{
		State blended = RecordedPastTheNewestSample(true);
		State control = RecordedPastTheNewestSample(false);

		const auto start = F4T::ProxySnapshotMovement::Render(blended, 1.3);
		CHECK(NearlyEqual(start.snapshot.position.x, 43.25F));

		const auto half = F4T::ProxySnapshotMovement::Render(blended, 1.33);
		CHECK(NearlyEqual(half.snapshot.position.x, 37.53008F));

		const auto done = F4T::ProxySnapshotMovement::Render(blended, 1.45);
		const auto doneControl = F4T::ProxySnapshotMovement::Render(control, 1.45);
		CHECK(NearlyEqual(done.snapshot.position.x, doneControl.snapshot.position.x));
		CHECK(!blended.blending);
	}

	void test_snap_cancels_the_blend()
	{
		State state = RecordedPastTheNewestSample(true);
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(900.0F), true, 1.31, 0.0));

		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.31);
		CHECK(NearlyEqual(rendered.snapshot.position.x, 900.0F));
	}

	void test_buffer_capacity_is_bounded()
	{
		State state{};
		for (int i = 0; i <= 500; ++i) {
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(i)), false, i * 0.033, 0.0);
		}
		CHECK(state.buffer.Size() <= F4T::ProxySnapshotMovement::kSampleCapacity);
	}

	void test_restore_reset_drops_the_stale_history()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(100.0F), false, 1.1, 0.0);
		F4T::ProxySnapshotMovement::Reset(state);
		CHECK(state.buffer.Empty());
		CHECK(!F4T::ProxySnapshotMovement::Render(state, 1.2).hasValue);

		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(500.0F), false, 1.3, 0.0));
		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.3);
		CHECK(rendered.hasValue);
		CHECK(NearlyEqual(rendered.snapshot.position.x, 500.0F));
	}

	void test_forced_idle_snap_replaces_the_delayed_history()
	{
		State state{};
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(0.0F), false, 1.0, 0.0);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(100.0F), false, 1.1, 0.0);
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(105.0F), true, 1.2, 0.0));

		const auto rendered = F4T::ProxySnapshotMovement::Render(state, 1.2);
		CHECK(rendered.hasValue);
		CHECK(NearlyEqual(rendered.snapshot.position.x, 105.0F));
	}

	void RunTest(const char* a_name, void (*a_test)())
	{
		g_currentTest = a_name;
		const auto failuresBefore = g_failures;
		a_test();
		std::printf("%s %s\n", g_failures == failuresBefore ? "PASS" : "FAIL", a_name);
	}

	constexpr double kSenderEpoch = 1.79e9;
	constexpr double kSendInterval = 0.05;
	constexpr double kFrameSeconds = 1.0 / 60.0;
	constexpr double kArrivalJitter[8] = { 0.005, 0.040, 0.010, 0.035, 0.000, 0.045, 0.020, 0.030 };
	constexpr int kRestartPacket = 40;
	constexpr double kRestartShiftSeconds = 100.0;

	double ArrivalSeconds(int a_packet)
	{
		return a_packet * kSendInterval + kArrivalJitter[a_packet % 8];
	}

	double SenderSeconds(int a_packet, bool a_restart)
	{
		const double shift = (a_restart && a_packet >= kRestartPacket) ? kRestartShiftSeconds : 0.0;
		return kSenderEpoch + a_packet * kSendInterval - shift;
	}

	double WorstSpeedError(bool a_useSender, bool a_restart, double a_measureFrom, double a_measureTo)
	{
		State state{};
		int nextPacket = 0;
		int lastRecorded = -1;
		bool hasPrevious = false;
		double previousSeconds = 0.0;
		double previousX = 0.0;
		double worst = 0.0;
		for (int frame = 1; frame <= 600; ++frame) {
			const double nowSeconds = frame * kFrameSeconds;
			int newest = lastRecorded;
			while (nextPacket < 200 && ArrivalSeconds(nextPacket) <= nowSeconds) {
				newest = nextPacket++;
			}
			if (newest > lastRecorded) {
				F4T::ProxySnapshotMovement::Record(
					state, 7, Sample(static_cast<float>(15.0 * newest)), false, nowSeconds,
					a_useSender ? SenderSeconds(newest, a_restart) : 0.0);
				lastRecorded = newest;
			}
			const auto rendered = F4T::ProxySnapshotMovement::Render(state, nowSeconds);
			if (!rendered.hasValue) {
				continue;
			}
			const double x = rendered.snapshot.position.x;
			if (hasPrevious && nowSeconds >= a_measureFrom && nowSeconds <= a_measureTo) {
				worst = std::max(worst, std::fabs((x - previousX) / (nowSeconds - previousSeconds) - 300.0));
			}
			hasPrevious = true;
			previousSeconds = nowSeconds;
			previousX = x;
		}
		return worst;
	}

	void test_sender_timeline_keeps_speed_under_arrival_jitter()
	{
		const double senderWorst = WorstSpeedError(true, false, 1.0, 3.0);
		const double receiveWorst = WorstSpeedError(false, false, 1.0, 3.0);
		CHECK(senderWorst <= 75.0);
		CHECK(senderWorst <= 0.25 * receiveWorst);
	}

	void test_sender_restart_resynchronises_instead_of_freezing()
	{
		const double restartWorst = WorstSpeedError(true, true, 3.0, 5.0);
		CHECK(restartWorst <= 75.0);
		CHECK(restartWorst <= 0.25 * WorstSpeedError(false, false, 3.0, 5.0));
	}

	void test_missing_sender_time_falls_back_to_receive_time()
	{
		State state{};
		for (int packet = 0; packet < 20; ++packet) {
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(15.0 * packet)), false, ArrivalSeconds(packet), 0.0);
		}
		CHECK(!state.senderTimeline);
		CHECK(!state.senderClock.Established());
		CHECK(F4T::ProxySnapshotMovement::Render(state, ArrivalSeconds(19)).hasValue);
	}

	void test_switching_clock_domain_clears_the_buffer_instead_of_mixing()
	{
		State state{};
		for (int packet = 0; packet < 10; ++packet) {
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(15.0 * packet)), false, ArrivalSeconds(packet), SenderSeconds(packet, false));
		}
		CHECK(state.senderTimeline);
		CHECK(state.buffer.Size() == 10);
		F4T::ProxySnapshotMovement::Record(state, 7, Sample(150.0F), false, 0.6, 0.0);
		CHECK(!state.senderTimeline);
		CHECK(state.buffer.Size() == 1);
	}

	constexpr double kCruiseSpeed = 300.0;
	constexpr double kGapStartSeconds = 1.0;
	constexpr double kTraceStartSeconds = 0.5;
	constexpr double kTraceEndSeconds = 2.0;
	constexpr int kTraceCapacity = 1600;
	constexpr int kStopPacketCount = 22;
	constexpr int kPacketCount = 64;
	constexpr float kFrameStepBound = 7.5F;
	constexpr float kCatchUpStepBound = 25.0F;

	struct Trace
	{
		float x[kTraceCapacity]{};
		float heading[kTraceCapacity]{};
		int count{ 0 };
	};

	int IndexAt(double a_seconds, double a_step)
	{
		return static_cast<int>((a_seconds - kTraceStartSeconds) / a_step + 0.5);
	}

	MovementSnapshot PacketAt(int a_index, double a_gapSeconds, bool a_stop, float a_heading, double& a_arrival)
	{
		if (a_index <= 20) {
			a_arrival = a_index * kSendInterval;
			return Sample(static_cast<float>(kCruiseSpeed * a_arrival));
		}
		a_arrival = kGapStartSeconds + a_gapSeconds + (a_index - 21) * kSendInterval;
		if (a_stop) {
			return Sample(static_cast<float>(kCruiseSpeed * kGapStartSeconds), 0.0F, 0.0F, 0.0F, false);
		}
		return Sample(static_cast<float>(kCruiseSpeed * a_arrival), 0.0F, 0.0F, a_heading);
	}

	Trace DriveAcrossGap(double a_gapSeconds, bool a_stop, float a_heading, double a_step)
	{
		Trace trace{};
		State state{};
		const int packetCount = a_stop ? kStopPacketCount : kPacketCount;
		int nextPacket = 0;
		for (int frame = 0; frame < kTraceCapacity; ++frame) {
			const double now = kTraceStartSeconds + frame * a_step;
			if (now > kTraceEndSeconds) {
				break;
			}
			while (nextPacket < packetCount) {
				double arrival = 0.0;
				const MovementSnapshot packet = PacketAt(nextPacket, a_gapSeconds, a_stop, a_heading, arrival);
				if (arrival > now) {
					break;
				}
				F4T::ProxySnapshotMovement::Record(state, 7, packet, false, arrival, 0.0);
				++nextPacket;
			}
			const auto rendered = F4T::ProxySnapshotMovement::Render(state, now);
			trace.x[frame] = rendered.snapshot.position.x;
			trace.heading[frame] = rendered.snapshot.heading;
			trace.count = frame + 1;
		}
		return trace;
	}

	void test_steady_run_across_a_late_packet_stays_continuous()
	{
		const double gaps[3] = { 0.1, 0.2, 0.4 };
		for (const double gap : gaps) {
			const Trace trace = DriveAcrossGap(gap, false, 0.0F, kFrameSeconds);
			const int gapStart = IndexAt(kGapStartSeconds, kFrameSeconds);
			const int arrival = IndexAt(kGapStartSeconds + gap, kFrameSeconds);
			const float bound = gap > 0.3 ? kCatchUpStepBound : kFrameStepBound;
			bool steady = true;
			for (int i = 1; i < trace.count; ++i) {
				const float dx = trace.x[i] - trace.x[i - 1];
				steady = steady && std::fabs(dx) <= bound;
				steady = steady && (i <= gapStart || dx >= -kEpsilon);
			}
			CHECK(steady);

			double previousSpeed = kCruiseSpeed;
			bool smooth = true;
			for (int i = gapStart + 1; i < arrival; ++i) {
				const double speed = (trace.x[i] - trace.x[i - 1]) / kFrameSeconds;
				smooth = smooth && speed <= previousSpeed + 0.01;
				smooth = smooth && previousSpeed - speed <= kCruiseSpeed * kFrameSeconds / F4T::Replication::kExtrapolationEaseSeconds + 2.0;
				previousSpeed = speed;
			}
			CHECK(smooth);
			if (gap > 0.3) {
				CHECK(previousSpeed < 200.0);
			}
		}
	}

	void test_sender_stop_in_a_gap_overshoots_a_little_and_settles()
	{
		const Trace trace = DriveAcrossGap(0.4, true, 0.0F, kFrameSeconds);
		float overshoot = 0.0F;
		for (int i = 0; i < trace.count; ++i) {
			overshoot = std::max(overshoot, trace.x[i] - 300.0F);
		}
		CHECK(overshoot <= kCruiseSpeed * 0.125 + 1.0);

		bool settled = true;
		for (int i = IndexAt(kGapStartSeconds + 0.4 + kBlendSeconds, kFrameSeconds); i < trace.count; ++i) {
			settled = settled && std::fabs(trace.x[i] - 300.0F) <= 0.01F;
		}
		CHECK(settled);
	}

	void test_turn_in_a_gap_converges_without_nan()
	{
		constexpr float kQuarterTurn = 1.5707963F;
		const Trace trace = DriveAcrossGap(0.2, false, kQuarterTurn, kFrameSeconds);
		bool allFinite = true;
		for (int i = 0; i < trace.count; ++i) {
			allFinite = allFinite && std::isfinite(trace.x[i]) && std::isfinite(trace.heading[i]);
		}
		CHECK(allFinite);
		CHECK(NearlyEqual(trace.heading[trace.count - 1], kQuarterTurn, 1.0e-2F));
	}

	double WorstVelocityJump(const float* a_x, int a_first, int a_last, double a_step)
	{
		double worst = 0.0;
		for (int i = a_first + 2; i <= a_last; ++i) {
			const double previous = (a_x[i - 1] - a_x[i - 2]) / a_step;
			const double current = (a_x[i] - a_x[i - 1]) / a_step;
			worst = std::max(worst, std::fabs(current - previous));
		}
		return worst;
	}

	void test_blend_back_keeps_velocity_continuous()
	{
		constexpr double kStep = 0.001;
		const double arrival = kGapStartSeconds + 0.2;
		const Trace trace = DriveAcrossGap(0.2, false, 0.0F, kStep);
		const int first = IndexAt(arrival - 0.1, kStep);
		const int last = IndexAt(arrival + 0.2, kStep);
		CHECK(WorstVelocityJump(trace.x, first, last, kStep) < 0.2 * kCruiseSpeed);

		const int blendStart = IndexAt(arrival, kStep);
		const int blendEnd = IndexAt(arrival + kBlendSeconds, kStep);
		float positionOnly[kTraceCapacity]{};
		for (int i = 0; i < trace.count; ++i) {
			positionOnly[i] = trace.x[i];
		}
		const float from = trace.x[blendStart - 1];
		const float target = trace.x[blendEnd];
		for (int i = blendStart; i < blendEnd; ++i) {
			positionOnly[i] = from + (target - from) * static_cast<float>((i - blendStart) * kStep / kBlendSeconds);
		}
		CHECK(WorstVelocityJump(positionOnly, first, last, kStep) > 0.2 * kCruiseSpeed);
	}

	void test_snap_then_blend_does_not_fling()
	{
		constexpr double kSnapOffset = 5000.0;
		constexpr double kTrackBound = 100.0;
		constexpr int kLatePackets = 8;

		State state{};
		for (int i = 0; i <= 20; ++i) {
			const double arrival = i * kSendInterval;
			F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(kCruiseSpeed * arrival)), false, arrival, 0.0);
			CHECK(F4T::ProxySnapshotMovement::Render(state, arrival).hasValue);
		}
		CHECK(F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(kCruiseSpeed * 1.05 + kSnapOffset)), false, 1.05, 0.0));

		const auto ahead = F4T::ProxySnapshotMovement::Render(state, 1.25);
		CHECK(ahead.hasValue);
		double worst = std::fabs(ahead.snapshot.position.x - (kSnapOffset + kCruiseSpeed * 1.25));

		int nextPacket = 0;
		for (int frame = 1; frame <= 24; ++frame) {
			const double now = 1.25 + frame * kFrameSeconds;
			while (nextPacket < kLatePackets) {
				const double arrival = 1.26 + nextPacket * kSendInterval;
				if (arrival > now) {
					break;
				}
				F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(kCruiseSpeed * arrival + kSnapOffset)), false, arrival, 0.0);
				++nextPacket;
			}
			if (frame == 1) {
				CHECK(state.blending);
			}
			const auto rendered = F4T::ProxySnapshotMovement::Render(state, now);
			CHECK(rendered.hasValue);
			worst = std::max(worst, std::fabs(rendered.snapshot.position.x - (kSnapOffset + kCruiseSpeed * now)));
		}
		CHECK(worst <= kTrackBound);
	}

	void test_blend_after_long_stall_does_not_run_backwards()
	{
		constexpr double kStallSeconds = 2.0;
		constexpr double kBurstSpacing = 0.002;
		constexpr double kRunnerSpeed = 400.0;
		constexpr double kOvershootBound = 5.0;
		constexpr int kPacketCount = 120;
		constexpr int kFrameCount = 360;
		const double stallEnd = kGapStartSeconds + kStallSeconds;

		double arrivals[kPacketCount]{};
		for (int packet = 0; packet < kPacketCount; ++packet) {
			const double sent = packet * kSendInterval;
			double arrival = sent;
			if (sent >= kGapStartSeconds && sent < stallEnd) {
				arrival = std::max(arrival, stallEnd);
			}
			if (packet > 0) {
				arrival = std::max(arrival, arrivals[packet - 1] + kBurstSpacing);
			}
			arrivals[packet] = arrival;
		}

		State state{};
		int nextPacket = 0;
		bool hasPrevious = false;
		float previousX = 0.0F;
		double worstOvershoot = -1.0e9;
		float worstBackwards = 0.0F;
		for (int frame = 1; frame <= kFrameCount; ++frame) {
			const double now = frame * kFrameSeconds;
			while (nextPacket < kPacketCount && arrivals[nextPacket] <= now) {
				F4T::ProxySnapshotMovement::Record(state, 7, Sample(static_cast<float>(kRunnerSpeed * nextPacket * kSendInterval)), false, arrivals[nextPacket], 0.0);
				++nextPacket;
			}
			const auto rendered = F4T::ProxySnapshotMovement::Render(state, now);
			if (!rendered.hasValue) {
				continue;
			}
			const float x = rendered.snapshot.position.x;
			worstOvershoot = std::max(worstOvershoot, x - kRunnerSpeed * now);
			if (hasPrevious && now >= stallEnd + kBlendSeconds) {
				worstBackwards = std::min(worstBackwards, x - previousX);
			}
			hasPrevious = true;
			previousX = x;
		}
		CHECK(worstOvershoot <= kOvershootBound);
		CHECK(worstBackwards >= -kEpsilon);
	}
}

int main()
{
	RunTest("test_empty_state_has_nothing_to_render", test_empty_state_has_nothing_to_render);
	RunTest("test_first_sample_renders_at_its_own_position", test_first_sample_renders_at_its_own_position);
	RunTest("test_repeated_snapshot_is_not_a_new_sample", test_repeated_snapshot_is_not_a_new_sample);
	RunTest("test_non_finite_input_is_rejected", test_non_finite_input_is_rejected);
	RunTest("test_reassigned_slot_drops_the_previous_players_history", test_reassigned_slot_drops_the_previous_players_history);
	RunTest("test_forced_snap_and_large_gap_drop_the_buffer", test_forced_snap_and_large_gap_drop_the_buffer);
	RunTest("test_playback_is_monotonic_and_lags_the_newest_sample", test_playback_is_monotonic_and_lags_the_newest_sample);
	RunTest("test_playback_never_jumps_more_than_a_sample_step", test_playback_never_jumps_more_than_a_sample_step);
	RunTest("test_playback_holds_the_capped_extrapolation_when_the_sender_stalls", test_playback_holds_the_capped_extrapolation_when_the_sender_stalls);
	RunTest("test_render_before_the_oldest_sample_clamps_to_it", test_render_before_the_oldest_sample_clamps_to_it);
	RunTest("test_heading_is_interpolated_along_the_shortest_arc", test_heading_is_interpolated_along_the_shortest_arc);
	RunTest("test_extrapolation_stops_at_the_cap", test_extrapolation_stops_at_the_cap);
	RunTest("test_new_sample_after_extrapolation_blends_instead_of_snapping", test_new_sample_after_extrapolation_blends_instead_of_snapping);
	RunTest("test_snap_cancels_the_blend", test_snap_cancels_the_blend);
	RunTest("test_buffer_capacity_is_bounded", test_buffer_capacity_is_bounded);
	RunTest("test_restore_reset_drops_the_stale_history", test_restore_reset_drops_the_stale_history);
	RunTest("test_forced_idle_snap_replaces_the_delayed_history", test_forced_idle_snap_replaces_the_delayed_history);
	RunTest("test_sender_timeline_keeps_speed_under_arrival_jitter", test_sender_timeline_keeps_speed_under_arrival_jitter);
	RunTest("test_sender_restart_resynchronises_instead_of_freezing", test_sender_restart_resynchronises_instead_of_freezing);
	RunTest("test_missing_sender_time_falls_back_to_receive_time", test_missing_sender_time_falls_back_to_receive_time);
	RunTest("test_switching_clock_domain_clears_the_buffer_instead_of_mixing", test_switching_clock_domain_clears_the_buffer_instead_of_mixing);
	RunTest("test_steady_run_across_a_late_packet_stays_continuous", test_steady_run_across_a_late_packet_stays_continuous);
	RunTest("test_sender_stop_in_a_gap_overshoots_a_little_and_settles", test_sender_stop_in_a_gap_overshoots_a_little_and_settles);
	RunTest("test_turn_in_a_gap_converges_without_nan", test_turn_in_a_gap_converges_without_nan);
	RunTest("test_blend_back_keeps_velocity_continuous", test_blend_back_keeps_velocity_continuous);
	RunTest("test_snap_then_blend_does_not_fling", test_snap_then_blend_does_not_fling);
	RunTest("test_blend_after_long_stall_does_not_run_backwards", test_blend_after_long_stall_does_not_run_backwards);
	RunTest("test_reset_clears_the_diagnostics_window", test_reset_clears_the_diagnostics_window);

	std::printf("%s\n", g_failures == 0 ? "all checks passed" : "FAILURES PRESENT");
	return g_failures == 0 ? 0 : 1;
}
