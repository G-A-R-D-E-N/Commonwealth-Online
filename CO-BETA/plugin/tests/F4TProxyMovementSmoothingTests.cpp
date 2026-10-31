#include "F4TProxyMovementSmoothing.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
	using F4T::ProxyMovementSmoothing::Decision;
	using F4T::ProxyMovementSmoothing::Gait;
	using F4T::ProxyMovementSmoothing::Parameters;
	using F4T::ProxyMovementSmoothing::State;

	constexpr float kEpsilon = 1.0e-4F;
	constexpr double kTickSeconds = 1.0 / 60.0;

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

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) <= kEpsilon;
	}

	void Note(
		State& a_state,
		double a_now,
		float a_speed,
		bool a_sprinting = false,
		bool a_locomoting = true,
		float a_graphSpeed = -1.0F)
	{
		F4T::ProxyMovementSmoothing::NoteRemoteSample(
			a_state,
			a_now,
			a_speed,
			a_sprinting,
			a_locomoting,
			a_graphSpeed);
	}

	void test_hold_window_keeps_last_received_speed()
	{
		State state{};
		Note(state, 1.0, 300.0F, false, true);

		const auto duringHold = F4T::ProxyMovementSmoothing::Update(state, 1.05, 0.0F);
		CHECK(duringHold.engaging);
		CHECK(!duringHold.idle);
		CHECK(NearlyEqual(duringHold.coastSpeed, 300.0F));
		CHECK(NearlyEqual(duringHold.gaitSpeed, 300.0F));

		const auto atHoldBoundary = F4T::ProxyMovementSmoothing::Update(state, 1.10, 0.0F);
		CHECK(atHoldBoundary.engaging);
		CHECK(NearlyEqual(atHoldBoundary.coastSpeed, 300.0F));

		const auto midCoast = F4T::ProxyMovementSmoothing::Update(state, 1.15, 0.0F);
		CHECK(midCoast.engaging);
		CHECK(midCoast.coastSpeed < 300.0F);
		CHECK(midCoast.coastSpeed > 0.0F);
	}

	void test_idle_timeout_ends_locomotion()
	{
		State state{};
		Note(state, 2.0, 250.0F, false, true);

		const auto beforeTimeout = F4T::ProxyMovementSmoothing::Update(state, 2.249, 0.0F);
		CHECK(beforeTimeout.engaging);

		const auto atTimeout = F4T::ProxyMovementSmoothing::Update(state, 2.25, 0.0F);
		CHECK(!atTimeout.engaging);
		CHECK(atTimeout.idle);
		CHECK(atTimeout.gait == Gait::kIdle);
		CHECK(NearlyEqual(atTimeout.coastSpeed, 0.0F));
		CHECK(NearlyEqual(atTimeout.gaitSpeed, 0.0F));

		const auto afterTimeout = F4T::ProxyMovementSmoothing::Update(state, 3.0, 0.0F);
		CHECK(!afterTimeout.engaging);

		Note(state, 3.1, 250.0F, false, true);
		const auto refired = F4T::ProxyMovementSmoothing::Update(state, 3.15, 0.0F);
		CHECK(refired.engaging);
		CHECK(NearlyEqual(refired.gaitSpeed, 250.0F));
	}

	void test_gait_floor_at_95_percent_of_received_speed()
	{
		State state{};
		Note(state, 1.0, 200.0F, false, true);

		const auto withDisplacement = F4T::ProxyMovementSmoothing::Update(state, 1.2, 150.0F);
		CHECK(NearlyEqual(withDisplacement.coastSpeed, 200.0F / 3.0F));
		CHECK(NearlyEqual(withDisplacement.gaitSpeed, 0.95F * 200.0F));
		CHECK(withDisplacement.gaitSpeed > withDisplacement.coastSpeed);

		State stillState{};
		Note(stillState, 1.0, 200.0F, false, true);
		const auto withoutDisplacement = F4T::ProxyMovementSmoothing::Update(stillState, 1.2, 0.0F);
		CHECK(NearlyEqual(withoutDisplacement.gaitSpeed, withoutDisplacement.coastSpeed));

		State slowState{};
		Note(slowState, 1.0, 200.0F, false, true);
		const auto belowThreshold = F4T::ProxyMovementSmoothing::Update(slowState, 1.2, 4.0F);
		CHECK(NearlyEqual(belowThreshold.gaitSpeed, belowThreshold.coastSpeed));
	}

	void test_walk_run_hysteresis_does_not_flap()
	{
		State state{};
		Note(state, 0.0, 320.0F, false, true);
		CHECK(F4T::ProxyMovementSmoothing::Update(state, 0.02, 0.0F).gait == Gait::kRun);

		double now = 0.1;
		for (int block = 0; block < 4; ++block) {
			const float speed = (block % 2) == 1 ? 281.0F : 279.0F;
			for (int step = 0; step < 3; ++step) {
				Note(state, now, speed, false, true);
				const auto decision = F4T::ProxyMovementSmoothing::Update(state, now + 0.01, 0.0F);
				CHECK(decision.gait == Gait::kRun);
				CHECK(NearlyEqual(decision.gaitSpeed, speed));
				now += 0.1;
			}
		}

		for (int i = 0; i <= 3; ++i) {
			const double demoteNow = 2.0 + (0.1 * static_cast<double>(i));
			Note(state, demoteNow, 100.0F, false, true);
			F4T::ProxyMovementSmoothing::Update(state, demoteNow + 0.01, 0.0F);
		}
		Note(state, 2.4, 100.0F, false, true);
		const auto demoted = F4T::ProxyMovementSmoothing::Update(state, 2.41, 0.0F);
		CHECK(demoted.gait == Gait::kWalk);
		CHECK(NearlyEqual(demoted.gaitSpeed, 100.0F));

		Note(state, 2.5, 350.0F, false, true);
		const auto heldWalk = F4T::ProxyMovementSmoothing::Update(state, 2.51, 0.0F);
		CHECK(heldWalk.gait == Gait::kWalk);
		CHECK(NearlyEqual(heldWalk.gaitSpeed, 279.0F));

		for (int i = 0; i <= 3; ++i) {
			const double promoteNow = 2.6 + (0.1 * static_cast<double>(i));
			Note(state, promoteNow, 350.0F, false, true);
			F4T::ProxyMovementSmoothing::Update(state, promoteNow + 0.01, 0.0F);
		}
		const auto promoted = F4T::ProxyMovementSmoothing::Update(state, 2.91, 0.0F);
		CHECK(promoted.gait == Gait::kRun);
		CHECK(NearlyEqual(promoted.gaitSpeed, 350.0F));
	}

	void test_smoothing_alpha_is_frame_time_independent()
	{
		constexpr double kTotalSeconds = 0.5;
		const auto rate = F4T::ProxyMovementSmoothing::kFrameApproachRatePerSecond;
		const auto expected = std::exp(-rate * kTotalSeconds);

		const std::vector<double> stepSizes{ 1.0 / 120.0, 1.0 / 60.0, 1.0 / 30.0, 0.01, 0.05 };
		for (const auto step : stepSizes) {
			const auto steps = static_cast<int>(std::lround(kTotalSeconds / step));
			auto residual = 1.0;
			for (int i = 0; i < steps; ++i) {
				residual -= residual * static_cast<double>(
					F4T::ProxyMovementSmoothing::SmoothingAlpha(step, rate));
			}
			CHECK(std::fabs((residual - expected) / expected) < 1.0e-4);
		}

		CHECK(NearlyEqual(F4T::ProxyMovementSmoothing::SmoothingAlpha(0.0, rate), 0.0F));
		CHECK(F4T::ProxyMovementSmoothing::SmoothingAlpha(1.0 / 30.0, rate) >
			F4T::ProxyMovementSmoothing::SmoothingAlpha(1.0 / 120.0, rate));
		CHECK(F4T::ProxyMovementSmoothing::SmoothingAlpha(1.0, rate) < 1.0F);
	}

	void test_per_proxy_approach_clock_keeps_frame_delta()
	{
		constexpr double kFrameSeconds = 0.03;
		constexpr int kProxyCount = 3;
		const auto rate = F4T::ProxyMovementSmoothing::kPositionSmoothingRatePerSecond;
		const auto expectedAlpha = F4T::ProxyMovementSmoothing::SmoothingAlpha(kFrameSeconds, rate);

		std::vector<double> perProxyClocks(kProxyCount, 0.0);
		double sharedClock = 0.0;
		double now = 100.0;
		for (int frame = 0; frame < 4; ++frame) {
			now += kFrameSeconds;
			for (int proxy = 0; proxy < kProxyCount; ++proxy) {
				const auto perProxyDelta =
					F4T::ProxyMovementSmoothing::FrameDeltaSeconds(perProxyClocks[proxy], now);
				const auto sharedDelta =
					F4T::ProxyMovementSmoothing::FrameDeltaSeconds(sharedClock, now);
				if (frame == 0) {
					continue;
				}

				CHECK(NearlyEqual(perProxyDelta, static_cast<float>(kFrameSeconds)));
				CHECK(NearlyEqual(
					F4T::ProxyMovementSmoothing::SmoothingAlpha(perProxyDelta, rate),
					expectedAlpha));
				if (proxy == 0) {
					CHECK(NearlyEqual(sharedDelta, perProxyDelta));
				} else {
					CHECK(sharedDelta < perProxyDelta);
					CHECK(F4T::ProxyMovementSmoothing::SmoothingAlpha(sharedDelta, rate) <
						F4T::ProxyMovementSmoothing::SmoothingAlpha(perProxyDelta, rate));
				}
			}
		}

		double clampingClock = 0.0;
		CHECK(NearlyEqual(
			F4T::ProxyMovementSmoothing::FrameDeltaSeconds(clampingClock, 500.0),
			static_cast<float>(F4T::ProxyMovementSmoothing::kDefaultFrameDeltaSeconds)));
		CHECK(NearlyEqual(
			F4T::ProxyMovementSmoothing::FrameDeltaSeconds(clampingClock, 500.0001),
			static_cast<float>(F4T::ProxyMovementSmoothing::kMinFrameDeltaSeconds)));
		CHECK(NearlyEqual(
			F4T::ProxyMovementSmoothing::FrameDeltaSeconds(clampingClock, 510.0),
			static_cast<float>(F4T::ProxyMovementSmoothing::kMaxFrameDeltaSeconds)));
	}

	Decision Drive(
		State& a_state,
		double& a_now,
		double a_seconds,
		int a_sendEveryFrames,
		float a_speed,
		float a_graphSpeed,
		bool a_sprinting,
		float a_measuredSpeed)
	{
		Decision decision{};
		const int frames = static_cast<int>(std::lround(a_seconds / kTickSeconds));
		for (int frame = 0; frame < frames; ++frame) {
			a_now += kTickSeconds;
			if (a_sendEveryFrames > 0 && frame % a_sendEveryFrames == 0) {
				Note(a_state, a_now, a_speed, a_sprinting, true, a_graphSpeed);
				F4T::ProxyMovementSmoothing::Update(a_state, a_now, F4T::ProxyMovementSmoothing::kNoMeasuredSpeed);
			}
			decision = F4T::ProxyMovementSmoothing::Update(a_state, a_now, a_measuredSpeed);
		}
		return decision;
	}

	void test_run_tier_from_consistent_hint()
	{
		State state{};
		double now = 0.0;
		const auto decision = Drive(state, now, 1.0, 6, 300.0F, 300.0F, false, 300.0F);
		CHECK(decision.engaging);
		CHECK(decision.gait == Gait::kRun);
		CHECK(NearlyEqual(decision.gaitSpeed, 300.0F));
	}

	void test_walk_tier_from_consistent_hint()
	{
		State state{};
		double now = 0.0;
		const auto decision = Drive(state, now, 1.0, 6, 150.0F, 150.0F, false, 150.0F);
		CHECK(decision.gait == Gait::kWalk);
		CHECK(NearlyEqual(decision.gaitSpeed, 150.0F));
	}

	void test_sprint_flag_applies_only_with_consistent_hint()
	{
		State sprinting{};
		double sprintNow = 0.0;
		CHECK(Drive(sprinting, sprintNow, 0.5, 6, 260.0F, 260.0F, true, 260.0F).gait == Gait::kRun);

		State walking{};
		double walkNow = 0.0;
		CHECK(Drive(walking, walkNow, 0.5, 6, 260.0F, 260.0F, false, 260.0F).gait == Gait::kWalk);
	}

	void test_stop_returns_to_idle()
	{
		State state{};
		double now = 0.0;
		Drive(state, now, 1.0, 6, 300.0F, 300.0F, false, 300.0F);

		const auto stopping = Drive(state, now, 0.5, 0, 300.0F, 300.0F, false, 0.0F);
		CHECK(!stopping.engaging);
		CHECK(stopping.idle);
		CHECK(stopping.gait == Gait::kIdle);
		CHECK(NearlyEqual(stopping.gaitSpeed, 0.0F));
	}

	void test_turn_on_the_spot_stays_idle()
	{
		State state{};
		double now = 0.0;
		const auto decision = Drive(state, now, 0.5, 6, 0.0F, 0.0F, false, 0.0F);
		CHECK(!decision.engaging);
		CHECK(decision.gait == Gait::kIdle);
	}

	void test_missing_hint_falls_back_to_displacement()
	{
		State fast{};
		double fastNow = 0.0;
		CHECK(Drive(fast, fastNow, 0.5, 6, 300.0F, -1.0F, false, 300.0F).gait == Gait::kRun);

		State slow{};
		double slowNow = 0.0;
		CHECK(Drive(slow, slowNow, 0.5, 6, 150.0F, -1.0F, false, 150.0F).gait == Gait::kWalk);
	}

	void test_inconsistent_hint_falls_back_to_displacement()
	{
		State slowDisplacement{};
		double slowNow = 0.0;
		CHECK(Drive(slowDisplacement, slowNow, 0.5, 6, 300.0F, 300.0F, true, 150.0F).gait == Gait::kWalk);

		State fastDisplacement{};
		double fastNow = 0.0;
		CHECK(Drive(fastDisplacement, fastNow, 0.5, 6, 150.0F, 150.0F, false, 300.0F).gait == Gait::kRun);
	}

	void test_tier_hysteresis_holds_then_switches()
	{
		State state{};
		double now = 0.0;
		Drive(state, now, 1.0, 6, 300.0F, 300.0F, false, 300.0F);

		CHECK(Drive(state, now, 0.2, 6, 150.0F, 150.0F, false, 150.0F).gait == Gait::kRun);
		CHECK(Drive(state, now, 0.2, 6, 300.0F, 300.0F, false, 300.0F).gait == Gait::kRun);

		CHECK(Drive(state, now, 0.2, 6, 150.0F, 150.0F, false, 150.0F).gait == Gait::kRun);
		CHECK(Drive(state, now, 0.4, 6, 150.0F, 150.0F, false, 150.0F).gait == Gait::kWalk);
	}

	void test_sparse_sends_still_reach_run()
	{
		State state{};
		double now = 0.0;
		Drive(state, now, 0.5, 6, 150.0F, 150.0F, false, 150.0F);
		CHECK(Drive(state, now, 1.0, 9, 290.0F, 290.0F, false, 290.0F).gait == Gait::kRun);
	}

	void test_visible_tier_follows_gait_through_walk_blip()
	{
		State state{};
		double now = 0.0;
		Drive(state, now, 1.0, 6, 300.0F, 300.0F, false, 300.0F);

		const auto blip = Drive(state, now, 0.2, 6, 150.0F, 150.0F, false, 150.0F);
		CHECK(blip.gait == Gait::kRun);
		CHECK(F4T::ProxyMovementSmoothing::GaitGraphSpeed(150.0F, blip.gait) >= F4T::ProxyMovementSmoothing::kRunGaitSpeed);

		const auto sustained = Drive(state, now, 0.4, 6, 150.0F, 150.0F, false, 150.0F);
		CHECK(sustained.gait == Gait::kWalk);
		CHECK(F4T::ProxyMovementSmoothing::GaitGraphSpeed(150.0F, sustained.gait) < F4T::ProxyMovementSmoothing::kRunGaitSpeed);
		CHECK(NearlyEqual(F4T::ProxyMovementSmoothing::GaitGraphSpeed(300.0F, Gait::kWalk), 279.0F));
		CHECK(NearlyEqual(F4T::ProxyMovementSmoothing::GaitGraphSpeed(150.0F, Gait::kIdle), 150.0F));
	}

	void test_sprint_without_hint_votes_run()
	{
		State state{};
		double now = 0.0;
		CHECK(Drive(state, now, 0.5, 6, 260.0F, -1.0F, true, 260.0F).gait == Gait::kRun);
	}

	void test_first_receive_sample_sets_walk_gait_with_cap()
	{
		State state{};
		Note(state, 1.0, 279.5F);
		const auto decision = F4T::ProxyMovementSmoothing::Update(
			state,
			1.0,
			F4T::ProxyMovementSmoothing::kNoMeasuredSpeed);
		CHECK(decision.gait == Gait::kWalk);
		CHECK(NearlyEqual(decision.gaitSpeed, 279.0F));
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
	RunTest("test_hold_window_keeps_last_received_speed", test_hold_window_keeps_last_received_speed);
	RunTest("test_idle_timeout_ends_locomotion", test_idle_timeout_ends_locomotion);
	RunTest("test_gait_floor_at_95_percent_of_received_speed", test_gait_floor_at_95_percent_of_received_speed);
	RunTest("test_walk_run_hysteresis_does_not_flap", test_walk_run_hysteresis_does_not_flap);
	RunTest("test_smoothing_alpha_is_frame_time_independent", test_smoothing_alpha_is_frame_time_independent);
	RunTest("test_per_proxy_approach_clock_keeps_frame_delta", test_per_proxy_approach_clock_keeps_frame_delta);
	RunTest("test_run_tier_from_consistent_hint", test_run_tier_from_consistent_hint);
	RunTest("test_walk_tier_from_consistent_hint", test_walk_tier_from_consistent_hint);
	RunTest("test_sprint_flag_applies_only_with_consistent_hint", test_sprint_flag_applies_only_with_consistent_hint);
	RunTest("test_stop_returns_to_idle", test_stop_returns_to_idle);
	RunTest("test_turn_on_the_spot_stays_idle", test_turn_on_the_spot_stays_idle);
	RunTest("test_missing_hint_falls_back_to_displacement", test_missing_hint_falls_back_to_displacement);
	RunTest("test_inconsistent_hint_falls_back_to_displacement", test_inconsistent_hint_falls_back_to_displacement);
	RunTest("test_tier_hysteresis_holds_then_switches", test_tier_hysteresis_holds_then_switches);
	RunTest("test_sparse_sends_still_reach_run", test_sparse_sends_still_reach_run);
	RunTest("test_visible_tier_follows_gait_through_walk_blip", test_visible_tier_follows_gait_through_walk_blip);
	RunTest("test_sprint_without_hint_votes_run", test_sprint_without_hint_votes_run);
	RunTest("test_first_receive_sample_sets_walk_gait_with_cap", test_first_receive_sample_sets_walk_gait_with_cap);

	std::printf("%s\n", g_failures == 0 ? "all checks passed" : "FAILURES PRESENT");
	return g_failures == 0 ? 0 : 1;
}
