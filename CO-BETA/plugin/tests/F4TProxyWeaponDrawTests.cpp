#include "F4TProxyWeaponDraw.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <unordered_map>

namespace
{
	using F4T::ProxyWeaponDraw::BindToProxy;
	using F4T::ProxyWeaponDraw::ClearPendingKeepInterval;
	using F4T::ProxyWeaponDraw::Decide;
	using F4T::ProxyWeaponDraw::Decision;
	using F4T::ProxyWeaponDraw::kProxyGenerationGapSeconds;
	using F4T::ProxyWeaponDraw::Parameters;
	using F4T::ProxyWeaponDraw::PruneStale;
	using F4T::ProxyWeaponDraw::ResolveStableDesired;
	using F4T::ProxyWeaponDraw::State;

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

	void test_desired_is_unstable_for_first_150ms()
	{
		State state{};
		CHECK(!ResolveStableDesired(state, 10.000, true).has_value());
		CHECK(!ResolveStableDesired(state, 10.100, true).has_value());
		CHECK(!ResolveStableDesired(state, 10.149, true).has_value());

		const auto atBoundary = ResolveStableDesired(state, 10.150, true);
		CHECK(atBoundary.has_value());
		CHECK(*atBoundary == true);
	}

	void test_flapping_desired_restarts_the_stability_window()
	{
		State state{};
		CHECK(!ResolveStableDesired(state, 0.00, true).has_value());
		CHECK(!ResolveStableDesired(state, 0.10, false).has_value());
		CHECK(!ResolveStableDesired(state, 0.20, true).has_value());
		CHECK(!ResolveStableDesired(state, 0.30, false).has_value());

		CHECK(!ResolveStableDesired(state, 0.40, false).has_value());
		const auto stable = ResolveStableDesired(state, 0.55, false);
		CHECK(stable.has_value());
		CHECK(*stable == false);
	}

	void test_no_decision_when_remote_and_actual_agree()
	{
		CHECK(Decide(true, true, true, false, 0.0, 100.0, 0) == Decision::kNone);
		CHECK(Decide(false, false, true, false, 0.0, 100.0, 0) == Decision::kNone);
	}

	void test_draw_when_remote_drawn_but_proxy_holstered()
	{
		CHECK(Decide(true, false, true, false, 0.0, 100.0, 0) == Decision::kDraw);
	}

	void test_sheathe_when_remote_holstered_but_proxy_drawn()
	{
		CHECK(Decide(false, true, true, false, 0.0, 100.0, 0) == Decision::kSheathe);
	}

	void test_no_decision_without_a_weapon()
	{
		CHECK(Decide(true, false, false, false, 0.0, 100.0, 0) == Decision::kNone);
		CHECK(Decide(false, true, false, false, 0.0, 100.0, 0) == Decision::kNone);
	}

	void test_no_decision_while_the_weapon_graph_is_settling()
	{
		CHECK(Decide(true, false, true, true, 0.0, 100.0, 0) == Decision::kNone);
		CHECK(Decide(false, true, true, true, 0.0, 100.0, 0) == Decision::kNone);
	}

	void test_rate_limit_allows_one_change_per_500ms()
	{
		CHECK(Decide(true, false, true, false, 100.000, 100.100, 0) == Decision::kNone);
		CHECK(Decide(true, false, true, false, 100.000, 100.499, 0) == Decision::kNone);
		CHECK(Decide(true, false, true, false, 100.000, 100.500, 0) == Decision::kDraw);

		CHECK(Decide(true, false, true, false, 0.0, 100.500, 0) == Decision::kDraw);
	}

	void test_gives_up_after_max_attempts()
	{
		const Parameters parameters{};
		CHECK(Decide(true, false, true, false, 0.0, 100.0, parameters.maxAttempts - 1) == Decision::kDraw);
		CHECK(Decide(true, false, true, false, 0.0, 100.0, parameters.maxAttempts) == Decision::kNone);
		CHECK(Decide(false, true, true, false, 0.0, 100.0, parameters.maxAttempts) == Decision::kNone);
	}

	void test_attempts_reset_when_the_desired_value_changes()
	{
		State state{};
		CHECK(!ResolveStableDesired(state, 0.0, true).has_value());
		state.attempts = 4;
		CHECK(!ResolveStableDesired(state, 0.2, false).has_value());
		CHECK(state.attempts == 0);
	}

	void test_changed_proxy_resets_attempts()
	{
		State state{};
		BindToProxy(state, 0x1000, 100.0);
		state.attempts = 5;
		state.lastChangeSeconds = 100.0;

		BindToProxy(state, 0x1000, 100.100);
		CHECK(state.attempts == 5);
		CHECK(Decide(true, false, true, false, state.lastChangeSeconds, 100.100, state.attempts) == Decision::kNone);

		BindToProxy(state, 0x2000, 100.200);
		CHECK(state.attempts == 0);
		CHECK(state.lastChangeSeconds == 0.0);
		CHECK(Decide(true, false, true, false, state.lastChangeSeconds, 100.200, state.attempts) == Decision::kDraw);
	}

	void test_same_form_id_after_a_visit_gap_starts_a_new_generation()
	{
		State state{};
		BindToProxy(state, 0x1000, 10.0);
		state.attempts = 5;
		state.lastChangeSeconds = 9.5;
		state.hasPendingDrawn = true;
		state.pendingDrawn = true;
		state.pendingSinceSeconds = 9.6;

		BindToProxy(state, 0x1000, 10.0 + kProxyGenerationGapSeconds + 0.5);
		CHECK(state.proxyFormId == 0x1000);
		CHECK(state.attempts == 0);
		CHECK(state.lastChangeSeconds == 0.0);
		CHECK(!state.hasPendingDrawn);
	}

	void test_same_form_id_within_the_generation_gap_keeps_attempts()
	{
		State state{};
		BindToProxy(state, 0x1000, 10.0);
		state.attempts = 5;
		state.lastChangeSeconds = 9.5;

		BindToProxy(state, 0x1000, 10.0 + kProxyGenerationGapSeconds);
		CHECK(state.proxyFormId == 0x1000);
		CHECK(state.attempts == 5);
		CHECK(state.lastChangeSeconds == 9.5);
	}

	void test_prune_drops_entries_of_gone_proxies()
	{
		std::unordered_map<std::uint32_t, State> states;
		BindToProxy(states[0x1000], 0x1000, 0.0);
		BindToProxy(states[0x2000], 0x2000, 40.0);

		const auto erased = PruneStale(states, 60.0);
		CHECK(erased == 1);
		CHECK(states.count(0x1000) == 0);
		CHECK(states.count(0x2000) == 1);
	}

	void test_pruned_entry_starts_clean()
	{
		std::unordered_map<std::uint32_t, State> states;
		BindToProxy(states[0x1000], 0x1000, 0.0);
		states[0x1000].attempts = 5;
		states[0x1000].lastChangeSeconds = 1.0;

		CHECK(PruneStale(states, 60.0) == 1);
		BindToProxy(states[0x1000], 0x1000, 60.0);
		CHECK(states[0x1000].attempts == 0);
		CHECK(states[0x1000].lastChangeSeconds == 0.0);
	}

	void test_late_joiner_draws_once_the_graph_settles()
	{
		State state{};
		double now = 500.0;

		CHECK(Decide(true, false, true, true, 0.0, now, 0) == Decision::kNone);

		for (int frame = 0; frame < 20; ++frame) {
			now += 1.0 / 60.0;
			const auto stable = ResolveStableDesired(state, now, true);
			const auto decision = stable
				? Decide(*stable, false, true, false, state.lastChangeSeconds, now, state.attempts)
				: Decision::kNone;
			if (decision != Decision::kNone) {
				CHECK(decision == Decision::kDraw);
				state.lastChangeSeconds = now;
				++state.attempts;
				break;
			}
		}

		CHECK(state.attempts == 1);
		CHECK(now >= 500.150 && now <= 500.300);

		const auto converged = ResolveStableDesired(state, now, true);
		CHECK(converged.has_value());
		CHECK(Decide(*converged, true, true, false, state.lastChangeSeconds, now, state.attempts) == Decision::kNone);
	}

	void test_convergence_keeps_the_change_interval()
	{
		State state{};
		CHECK(!ResolveStableDesired(state, 99.500, true).has_value());
		const auto drawn = ResolveStableDesired(state, 100.000, true);
		CHECK(drawn.has_value() && *drawn);
		CHECK(Decide(*drawn, false, true, false, state.lastChangeSeconds, 100.000, state.attempts) == Decision::kDraw);
		state.lastChangeSeconds = 100.000;
		++state.attempts;

		const auto converged = ResolveStableDesired(state, 100.100, true);
		CHECK(converged.has_value() && *converged);
		CHECK(Decide(*converged, true, true, false, state.lastChangeSeconds, 100.100, state.attempts) == Decision::kNone);
		ClearPendingKeepInterval(state);
		CHECK(state.lastChangeSeconds == 100.000);
		CHECK(state.attempts == 0);

		CHECK(!ResolveStableDesired(state, 100.200, false).has_value());
		const auto sheathe = ResolveStableDesired(state, 100.400, false);
		CHECK(sheathe.has_value() && !*sheathe);
		CHECK(Decide(false, true, true, false, state.lastChangeSeconds, 100.400, state.attempts) == Decision::kNone);
		CHECK(Decide(false, true, true, false, state.lastChangeSeconds, 100.499, state.attempts) == Decision::kNone);
		CHECK(Decide(false, true, true, false, state.lastChangeSeconds, 100.500, state.attempts) == Decision::kSheathe);
	}

	void test_null_weapon_keeps_the_change_interval()
	{
		State state{};
		CHECK(!ResolveStableDesired(state, 99.500, true).has_value());
		const auto drawn = ResolveStableDesired(state, 100.000, true);
		CHECK(drawn.has_value() && *drawn);
		CHECK(Decide(*drawn, false, true, false, state.lastChangeSeconds, 100.000, state.attempts) == Decision::kDraw);
		state.lastChangeSeconds = 100.000;
		++state.attempts;

		const auto unarmed = ResolveStableDesired(state, 100.100, true);
		CHECK(unarmed.has_value() && *unarmed);
		CHECK(Decide(*unarmed, false, false, false, state.lastChangeSeconds, 100.100, state.attempts) == Decision::kNone);
		ClearPendingKeepInterval(state);
		CHECK(state.lastChangeSeconds == 100.000);
		CHECK(state.attempts == 0);
		CHECK(!state.hasPendingDrawn);

		CHECK(!ResolveStableDesired(state, 100.200, true).has_value());
		const auto redrawn = ResolveStableDesired(state, 100.400, true);
		CHECK(redrawn.has_value() && *redrawn);
		CHECK(Decide(*redrawn, false, true, false, state.lastChangeSeconds, 100.400, state.attempts) == Decision::kNone);
		CHECK(Decide(*redrawn, false, true, false, state.lastChangeSeconds, 100.500, state.attempts) == Decision::kDraw);
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
	RunTest("test_desired_is_unstable_for_first_150ms", test_desired_is_unstable_for_first_150ms);
	RunTest("test_flapping_desired_restarts_the_stability_window", test_flapping_desired_restarts_the_stability_window);
	RunTest("test_no_decision_when_remote_and_actual_agree", test_no_decision_when_remote_and_actual_agree);
	RunTest("test_draw_when_remote_drawn_but_proxy_holstered", test_draw_when_remote_drawn_but_proxy_holstered);
	RunTest("test_sheathe_when_remote_holstered_but_proxy_drawn", test_sheathe_when_remote_holstered_but_proxy_drawn);
	RunTest("test_no_decision_without_a_weapon", test_no_decision_without_a_weapon);
	RunTest("test_no_decision_while_the_weapon_graph_is_settling", test_no_decision_while_the_weapon_graph_is_settling);
	RunTest("test_rate_limit_allows_one_change_per_500ms", test_rate_limit_allows_one_change_per_500ms);
	RunTest("test_gives_up_after_max_attempts", test_gives_up_after_max_attempts);
	RunTest("test_attempts_reset_when_the_desired_value_changes", test_attempts_reset_when_the_desired_value_changes);
	RunTest("test_changed_proxy_resets_attempts", test_changed_proxy_resets_attempts);
	RunTest("test_same_form_id_after_a_visit_gap_starts_a_new_generation", test_same_form_id_after_a_visit_gap_starts_a_new_generation);
	RunTest("test_same_form_id_within_the_generation_gap_keeps_attempts", test_same_form_id_within_the_generation_gap_keeps_attempts);
	RunTest("test_prune_drops_entries_of_gone_proxies", test_prune_drops_entries_of_gone_proxies);
	RunTest("test_pruned_entry_starts_clean", test_pruned_entry_starts_clean);
	RunTest("test_late_joiner_draws_once_the_graph_settles", test_late_joiner_draws_once_the_graph_settles);
	RunTest("test_convergence_keeps_the_change_interval", test_convergence_keeps_the_change_interval);
	RunTest("test_null_weapon_keeps_the_change_interval", test_null_weapon_keeps_the_change_interval);

	std::printf("%s\n", g_failures == 0 ? "all checks passed" : "FAILURES PRESENT");
	return g_failures == 0 ? 0 : 1;
}
