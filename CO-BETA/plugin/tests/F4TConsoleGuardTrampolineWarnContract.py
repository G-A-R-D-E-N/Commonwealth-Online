from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
GUARD = ROOT / "CO-BETA/plugin/src/F4TConsoleGuard.cpp"
POLICY = ROOT / "CO-BETA/plugin/include/F4TConsoleGuardPolicy.h"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def body_after(source, anchor):
    start = source.find(anchor)
    if start < 0:
        return None
    brace = source.index("{", start + len(anchor))
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    return None


def missing_trampoline(detour):
    start = detour.find("if (!original)")
    if start < 0:
        return None
    end = detour.find("return;", start)
    if end < 0:
        return None
    return detour[start:end]


text = GUARD.read_text(encoding="utf-8") if GUARD.exists() else ""
policy = POLICY.read_text(encoding="utf-8") if POLICY.exists() else ""
check(bool(text), f"missing {GUARD}")
check("inline bool ClaimLogOnce(" in policy, "ClaimLogOnce must live in F4TConsoleGuardPolicy.h")
check("return !a_flag.exchange(true, std::memory_order_acq_rel);" in policy,
      "ClaimLogOnce must publish the flag and report only the first claim")

for name, flag in (
    ("void ExecuteCommandDetour(", "g_executeTrampolineWarned"),
    ("void CompileAndRunDetour(", "g_compileTrampolineWarned"),
):
    detour = body_after(text, name)
    check(detour is not None, f"{name} must be defined")
    if detour is None:
        continue
    gap = missing_trampoline(detour)
    check(gap is not None, f"{name} must return when the trampoline is missing")
    if gap is None:
        continue
    check("WarnOnce(" in gap and flag in gap, f"{name} must warn once through {flag}")
    check("REX::WARN" not in gap, f"{name} must not warn on every missing trampoline")

install = body_after(text, "void TryInstallHooks(")
check(install is not None, "TryInstallHooks must be defined")
if install is not None:
    start = install.find("if (!runtime)")
    end = install.find("} else {", start)
    gap = install[start:end] if start >= 0 and end > start else ""
    check("WarnOnce(" in gap and "g_unverifiedRuntimeWarned" in gap,
          "an unverified runtime must warn once")
    check("REX::WARN" not in gap, "an unverified runtime must not warn on every install retry")

reset = body_after(text, "void Reset(")
check(reset is not None, "Reset must be defined")
if reset is not None:
    check("g_executeTrampolineWarned.store(false" in reset, "Reset must clear the execute trampoline warning")
    check("g_compileTrampolineWarned.store(false" in reset, "Reset must clear the compile trampoline warning")
    check("g_unverifiedRuntimeWarned.store(false" in reset, "Reset must clear the unverified-runtime warning")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    print("F4TConsoleGuardTrampolineWarnContract: missing trampoline still warns every call.")
    sys.exit(1)

print("missing trampoline warns once: execute and compile")
print("unverified runtime warns once")
print("F4TConsoleGuardTrampolineWarnContract: all checks passed.")
