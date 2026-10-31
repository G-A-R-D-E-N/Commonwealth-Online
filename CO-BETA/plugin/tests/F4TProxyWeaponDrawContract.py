from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
header = (plugin / "include/F4TProxyWeaponDraw.h").read_text(encoding="utf-8")
impl = (plugin / "src/F4TProxyWeaponDraw.cpp").read_text(encoding="utf-8")
actions = (plugin / "src/F4TProxyActorControllerActions.cpp").read_text(encoding="utf-8")
animation_sync = (plugin / "src/F4TProxyAnimationSync.cpp").read_text(encoding="utf-8")
tests = (plugin / "tests/F4TProxyWeaponDrawTests.cpp").read_text(encoding="utf-8")


def brace_span(text, open_brace):
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return open_brace, index
    raise AssertionError("unbalanced braces")


apply_start = impl.index("void ApplyProxyWeaponDrawDecision(")
apply_open, apply_close = brace_span(impl, impl.index("{", apply_start))
draw_if_start = impl.index("if (drawn) {", apply_open, apply_close)
draw_if_open, draw_if_close = brace_span(impl, impl.index("{", draw_if_start))

assert impl.count("a_proxy.AttachWeapon(") == 1, "only the draw path may attach a weapon"
attach = impl.index("a_proxy.AttachWeapon(weaponInstance, equipIndex);")
assert apply_open < attach < apply_close, "the attach must live in the draw decision application"
assert draw_if_open < attach < draw_if_close, "only a draw may attach"
assert attach < impl.index("a_proxy.DoReparentWeapon("), "the attach must come first, as on the transition path"

reparent = impl.index("a_proxy.DoReparentWeapon(&a_weapon, equipIndex, drawn);")
magic_hands = impl.index("a_proxy.DrawWeaponMagicHands(drawn);")
assert apply_open < attach < reparent < magic_hands < apply_close, (
    "the draw decision must attach, reparent and send magic hands in order"
)
assert "BindToProxy(state, a_proxy.GetFormID(), now);" in impl, "the reconcile must bind its state to the proxy"
assert "lastVisitSeconds" in header, "the state must record the last visit"
assert re.search(
    r"a_nowSeconds - a_state\.lastVisitSeconds > a_generationGapSeconds",
    header,
), "a stale visit gap must start a new proxy generation"
assert "PruneStale(g_weaponDrawStates," in impl, "stale weapon draw state must be pruned"
assert impl.index("PruneStale(g_weaponDrawStates,") < impl.index("g_weaponDrawStates[a_remotePlayerId]"), (
    "pruning must happen before the entry is used"
)

assert re.search(
    r"\[\[nodiscard\]\] inline Decision Decide\(\s*bool a_remoteDrawn,\s*bool a_actualDrawn,\s*"
    r"bool a_hasWeapon,\s*bool a_graphSettling,\s*double a_lastChangeSeconds,\s*double a_nowSeconds,",
    header,
), "Decide must take remote drawn, actual drawn, has weapon, settling, last change and now"

assert "inline constexpr double kStabilitySeconds = 0.150;" in header
assert "inline constexpr double kMinChangeIntervalSeconds = 0.500;" in header
assert re.search(r"a_nowSeconds - a_state\.pendingSinceSeconds < a_parameters\.stabilitySeconds", header), (
    "the desired value must be stable before it is acted on"
)
assert re.search(
    r"a_nowSeconds - a_lastChangeSeconds < a_parameters\.minChangeIntervalSeconds",
    header,
), "changes must be rate limited"

assert "a_proxy.GetWeaponMagicDrawn()" in impl
assert "a_proxy.DoReparentWeapon(&a_weapon, equipIndex, drawn);" in impl
assert "a_proxy.DrawWeaponMagicHands(drawn);" in impl
assert "ProxyActorController::detail::ProxyHasLoaded3D(a_proxy)" in impl
assert "ProxyActorController::detail::LogThrottledInfo(" in impl
assert "kEnableProxyWeaponDrawReconcile" not in impl, "the reconcile must not sit behind an enable switch"
assert "ClearPendingKeepInterval(state);" in impl, "convergence and a missing weapon must keep the last change time for the rate limit"
assert "Reset(state);" not in impl, "a missing weapon must not reset the change interval"
for source_name, source_text in (
    ("src/F4TProxyWeaponDraw.cpp", impl),
    ("src/F4TProxyAnimationSync.cpp", animation_sync),
):
    assert not re.search(
        r"RE::BGSObjectInstanceT<RE::TESObjectWEAP>\s+weaponInstance\s*\{[^}]*\bnullptr\b",
        source_text,
    ), f"{source_name}: AttachWeapon must receive the equipped weapon's instance data, not nullptr"
assert re.search(
    r"GetEquippedRightHandWeapon\(a_proxy, equippedItem\)", impl
), "the draw path must hand the whole equipped item to the attach"
assert "ApplyProxyWeaponDrawDecision(a_proxy, *weapon, equippedItem.instanceData.get(), decision);" in impl, (
    "the draw decision must apply the equipped weapon's instance data"
)
assert animation_sync.count(
    "RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ weapon, equippedWeapon->instanceData.get() };"
) == 2, "both transition AttachWeapon calls must pass the equipped weapon's instance data"
xmake = (root / "CO-BETA/plugin/xmake.lua").read_text(encoding="utf-8")
assert 'target("CommonwealthOnlineProxyWeaponDrawTests")' in xmake, "the weapon draw test must be a build target"
assert re.search(
    r"F4T::ProxyWeaponDraw::ReconcileProxyWeaponDraw\(\s*a_remotePlayer\.playerId,\s*a_proxy,\s*"
    r"a_remotePlayer,\s*IsProxyWeaponGraphSettling\(a_slot\)\);",
    actions,
), "the proxy update path must reconcile the weapon draw with the slot settle flag"
assert actions.index("ReconcileProxyWeaponDraw(") < actions.index("if (IsProxyWeaponGraphSettling(a_slot)) {"), (
    "reconciliation must observe the settle window before the update path returns"
)

assert 'RunTest("test_desired_is_unstable_for_first_150ms"' in tests
assert 'RunTest("test_rate_limit_allows_one_change_per_500ms"' in tests
assert 'RunTest("test_no_decision_while_the_weapon_graph_is_settling"' in tests
assert 'RunTest("test_late_joiner_draws_once_the_graph_settles"' in tests
assert 'RunTest("test_changed_proxy_resets_attempts"' in tests
assert 'RunTest("test_same_form_id_after_a_visit_gap_starts_a_new_generation"' in tests
assert 'RunTest("test_same_form_id_within_the_generation_gap_keeps_attempts"' in tests
assert 'RunTest("test_prune_drops_entries_of_gone_proxies"' in tests
assert 'RunTest("test_pruned_entry_starts_clean"' in tests

for path in (plugin / "src").glob("F4TProxyWeaponDraw*.cpp"):
    assert len(path.read_text(encoding="utf-8").splitlines()) < 750, f"{path} exceeds 750 lines"

print("F4TProxyWeaponDrawContract: all checks passed.")
