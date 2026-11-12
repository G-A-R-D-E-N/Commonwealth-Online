# address source:commonlibf4-nomad
"""Contract: F4T frame limiter (FathersFallout FrameLimiter port, #568). Prologue bytes are the 15-byte windows from source:E:/GardenEngine/re-answers/2026-10-27-fs-p2-onidle-prologue.md; ids and runtime behaviour are unverified until CI and in-game checks."""
from pathlib import Path
import re


plugin = Path(__file__).resolve().parents[1]
src = (plugin / "src/F4TFrameLimiter.cpp").read_text(encoding="utf-8")
header = (plugin / "include/F4TFrameLimiter.h").read_text(encoding="utf-8")
pacing = (plugin / "include/F4TFrameLimiterPacing.h").read_text(encoding="utf-8")
tests = (plugin / "tests/F4TFrameLimiterTests.cpp").read_text(encoding="utf-8")
main = (plugin / "src/main.cpp").read_text(encoding="utf-8")
xmake = (plugin / "xmake.lua").read_text(encoding="utf-8")

OG_PROLOGUE = [0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40]
NG_PROLOGUE = [0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18]


def hex_bytes(text):
    return [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", text)]


assert "REL::ID kOnIdle{ 633524, 2228917, 2228917 }" in src, "OG uses Address Library id 633524; NG and AE use 2228917"
assert "F4T_TARGET_" not in src
for name, label, expected in (("OG", "kExpectedPrologueOg", OG_PROLOGUE), ("NG/AE", "kExpectedPrologueNgAe", NG_PROLOGUE)):
    array = re.search(r"std::array<std::uint8_t, 15> " + label + r"\{(.*?)\}", src, re.S)
    assert array is not None, f"{name}: expected prologue must be a 15-byte array"
    assert hex_bytes(array.group(1)) == expected, f"{name}: prologue bytes differ from the RE answer"
assert "Must be 15 bytes" in src and "offset 0x0B" in src, "the 15-byte window must carry its single-line constraint"

install = re.search(r"void Install\(\)\s*\{(.*?)\n\t\}", src, re.S)
assert install is not None, "Install() not found"
install = install.group(1)
assert re.search(r"if \(!s_timer\) \{\s*timeBeginPeriod\(1\);\s*\}", install), "timeBeginPeriod(1) must run once at Install when no hi-res timer exists"
assert "#include <timeapi.h>" in src, "timeapi.h must be included for timeBeginPeriod"
assert xmake.count('add_syslinks("ws2_32", "iphlpapi", "shell32", "crypt32", "ole32", "avrt", "winmm")') == 1, "the plugin target must link winmm"

detour = re.search(r"void OnIdleDetour\(void\* a_main\) noexcept\s*\{(.*?)\n\t\t\}", src, re.S)
assert detour is not None, "OnIdleDetour body not found"
body = detour.group(1)
assert "if (!s_hook.hook)" in body, "detour must not call through a missing hook"
assert body.index("Pace();") < body.index("original<OnIdleFn>()(a_main);"), "detour must pace before calling the original"
assert "now - s_lastIdleNs < kDoubleIdleNs" in body and "s_doubleIdleLogged = true" in body, "the double-OnIdle diagnostic must log once"
assert "TODO-verify(#568)" in body and "kDoubleIdleNs = 4'000'000;" in src, "the double-OnIdle window must be 4 ms and marked TODO-verify(#568)"

capped = re.search(r"bool Capped\(\)\s*\{(.*?)\n\t\t\}", src, re.S)
assert capped is not None, "Capped() not found"
for needle in ('GetMenuOpen("LoadingMenu")', 'GetMenuOpen("MainMenu")', 'GetMenuOpen("PauseMenu")', "RE::PlayerCharacter::GetSingleton() != nullptr", "ShouldCap("):
    assert needle in capped.group(1), f"Capped() missing {needle}"

assert "return a_playerPresent && !a_loadingMenuOpen && !a_mainMenuOpen && !a_pauseMenuOpen;" in pacing, "cap needs a player and no LoadingMenu, MainMenu or PauseMenu"
assert "return a_nowNs - deadlineNs >= a_periodNs ? a_nowNs : deadlineNs;" in pacing, "a full period behind must reset the anchor, not catch up"
assert "kTargetFps = 60;" in src, "target must be 60 FPS"
assert "CREATE_WAITABLE_TIMER_HIGH_RESOLUTION" in src and "Sleep(1);" in src, "wait must use a hi-res timer with a bounded fallback"
assert "F4T::InlineHook::Install(request, LogLine, s_hook);" in src, "the hook must go through the verified inline-hook path"
assert "F4T::FrameLimiter::Install();" in main, "main.cpp must install the limiter"
assert main.index("F4T::RangedCombatCapture::Initialize();") < main.index("F4T::FrameLimiter::Install();"), "install sits beside the other hook installs"
assert "namespace F4T::FrameLimiter" in header and "void Install();" in header, "public header must expose Install only"

for label, text in (("F4TFrameLimiter.cpp", src), ("F4TFrameLimiterPacing.h", pacing), ("F4TFrameLimiter.h", header), ("F4TFrameLimiterTests.cpp", tests)):
    assert len(text.splitlines()) < 1000, f"{label} is over 1000 lines"
    assert ".inc" not in text, f"{label} must not pull in an .inc file"
    for line in text.splitlines():
        if "//" not in line:
            continue
        comment = line[line.index("//"):]
        if re.match(r"//\s*(TODO|FIXME|License)", comment):
            if comment.startswith("// TODO-verify(#568)"):
                assert "source:E:/GardenEngine/re-answers/" in comment, f"{label}: TODO-verify must cite its RE answer"
            continue
        assert re.search(r"\b(must|requires|offset|address|rva|lock)\b", comment, re.I), f"{label}: comment is not a permitted constraint: {comment}"

print("frame limiter contract OK")
