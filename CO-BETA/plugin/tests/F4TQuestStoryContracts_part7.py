#!/usr/bin/env python3
"""F4TQuestStoryContracts_part7: consolidated contract sections.

Holds the multiplayer sleep-block checks split out of F4TQuestStoryContracts_part5
so each part file stays under 750 lines. Every assertion below is the original
assertion, moved verbatim into the labelled section it came from, so a failure
still names the contract that broke.

Run it directly, exactly like the other parts:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts_part7.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[3]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok

def _section_016_f4tsleepbedblockcontract():
    """Multiplayer sleep-block checks, moved from F4TSleepContract (see F4TQuestStoryContracts_part5)."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSleepContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        if not path.exists():
            failures.append(f"missing {path.relative_to(ROOT).as_posix()}")
            return ""
        return path.read_text(encoding="utf-8")


    hook = read(PLUGIN / "src/F4TSleepBedHook.cpp")
    thread_table = read(PLUGIN / "include/F4THookThreadTable.h")
    controller = read(PLUGIN / "src/F4TSleepController.cpp")
    check("canPassTime(player, nullptr, true, false)" in hook, "bed gate must pass true as CanPassTime's third argument so vanilla shows its own refusal")
    check(0 <= hook.find("if (!VanillaGatesPass())") < hook.find("F4T::SleepController::TryBegin(bedRef.get())"), "bed gate must run before TryBegin and return without the generic notice when it refuses")
    check("sleep not started: vanilla gates refused the bed" in hook, "a refused bed gate must log its reason")
    check("InMultiplayer()" in hook and "F4T::Reconnect::IsActive()" in hook, "bed hook must gate on the multiplayer session including reconnect")
    check(hook.count("s_sleepHook.hook->original<SleepBedMenuFn>()") == 1, "vanilla bed/wait call must appear exactly once")
    check(0 <= hook.find("if (!InMultiplayer())") < hook.find("s_sleepHook.hook->original<SleepBedMenuFn>()"), "vanilla bed/wait call must sit behind the multiplayer check")
    check("ShowNotice(a_mode == 1" in hook, "blocked bed/wait must show a player-visible message through ShowNotice")
    flat_hook = " ".join(hook.split())
    check(flat_hook.count("RE::SendHUDMessage::ShowHUDMessage(") == 1, "only ShowNotice may call ShowHUDMessage")
    notice_body = flat_hook[flat_hook.find("void ShowNotice("):][:200]
    check(0 <= notice_body.find("if (OnGameThread()) {") < notice_body.find("RE::SendHUDMessage::ShowHUDMessage("), "ShowNotice must show only on the game thread")
    check("bool OnGameThread()" in hook and "g_gameThread.compare_exchange_strong" in hook, "sleep hook must record the game thread from Tick")
    check("SleepBedHook::Tick();" in read(PLUGIN / "src/main.cpp"), "main permanent task must tick the sleep hook")
    check("void Tick();" in read(PLUGIN / "include/F4TSleepBedHook.h"), "bed hook header must declare Tick")
    bed_start = flat_hook.find("bool BlockedInMultiplayer(")
    check(
        flat_hook[bed_start:].startswith("bool BlockedInMultiplayer(std::string_view a_name, std::string_view a_notice, std::int32_t a_hours) { if (!InMultiplayer()) { return false; }"),
        "BlockedInMultiplayer must begin with the multiplayer check",
    )
    check(
        "if (!a_notice.empty())" in hook and 'BlockedInMultiplayer("pass time", "", a_hours)' in hook,
        "pass time detour must show no notice, since OG jail release runs through it",
    )
    check("const auto identity = IdentityOf(*a_bed);" in controller, "bed identity must still be resolved before the picker opens")
    check("bed has no server identity" in controller, "a bed without a server identity must log and still start the MP sleep")
    check(0 <= controller.find("const auto identity = IdentityOf(*a_bed);") < controller.find("SleepView::Open(token"), "bed identity must resolve before SleepView::Open")
    check("IsReady()" not in controller, "TryBegin must not gate on SleepView::IsReady, which is false at idle")
    for message in (
        "a sleep session is already active",
        "bed or player unavailable",
        "not connected to the server",
        "player is downed or dead",
        "sleep view did not open",
    ):
        check(f'"sleep not started: {message}' in controller, f"TryBegin early return must log: {message}")
    check("F4T::SleepBedHook::CallNativeSleep(player" in controller, "controller must start the native sleep through CallNativeSleep")
    check("NativeSleep(player" not in controller.replace("CallNativeSleep(player", ""), "controller must not call the native sleep directly")
    check("kSleep{" not in controller, "controller must not keep its own native sleep id")
    check('#include "F4TSleepBedHook.h"' in controller, "controller must include the bed hook header")
    check("void CallNativeSleep(RE::PlayerCharacter* a_player, std::int32_t a_hours);" in read(PLUGIN / "include/F4TSleepBedHook.h"), "bed hook header must declare CallNativeSleep")
    check("SleepNative = 25" in thread_table, "hook thread table must name SleepNative")
    check("kNativeSleep{ 781390, 2232887, 2232887 }" in hook and "0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20" in hook, "native sleep hook must carry the 781390/2232887 ids and the 15-byte prologue")
    check("Id::SleepNative" in hook and "NativeSleepDetour" in hook, "native sleep detour must note its hook thread")
    check(hook.count("BlockedInMultiplayer(") == 4, "native sleep, wait and pass time detours must block through BlockedInMultiplayer")
    def segment(start_marker, end_marker):
        start = hook.find(start_marker)
        end = hook.find(end_marker, start + 1)
        return hook[start:end] if 0 <= start < end else ""


    bed_detour = segment("void SleepBedMenuDetour(", "bool BlockedInMultiplayer(")
    blocked = segment("bool BlockedInMultiplayer(", "void NativeSleepDetour(")
    native_detours = segment("void NativeSleepDetour(", "void ServePrisonHourDetour(")
    serve_detour = segment("void ServePrisonHourDetour(", "class SleepWaitMenuSink")
    sink_body = segment("class SleepWaitMenuSink", "SleepWaitMenuSink g_sleepWaitMenuSink")
    check(bed_detour.count("try {") == 2 and bed_detour.count("catch (...)") == 2, "bed detour must wrap its block and notice paths in try/catch")
    check(blocked.count("try {") == 1 and blocked.count("catch (...)") == 1, "BlockedInMultiplayer must wrap its log and notice in try/catch")
    check("catch (...)" not in native_detours, "native sleep, wait and pass time detours must rely on BlockedInMultiplayer's try/catch")
    check("catch (...)" not in serve_detour, "serve prison hour detour must not throw: it only writes the context")
    check(sink_body.count("try {") == 1 and sink_body.count("catch (...)") == 1, "menu guard sink must wrap its log, hide and notice in try/catch")
    check("std::string_view{ menuName } != kMenuSleepWait || !InMultiplayer()" in hook, "menu guard sink must only act in a multiplayer session")
    check(
        "REX::FModule::GetRuntimeIndex() == REX::FModule::Runtime::kNG ? std::span<const std::uint8_t>{ kServePrisonHourPrologueNG } : std::span<const std::uint8_t>{ kServePrisonHourPrologueAE }" in flat_hook,
        "serve prison hour window must select the NG or AE bytes by runtime index",
    )
    check("kNativePassTime{ 1005712, 2232891, 2232891 }" in hook and "native-passtime-ids.md" in hook, "pass time hook must carry the 1005712/2232891 ids and its record")
    check("0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20" in hook, "pass time hook must carry the 20-byte prologue")
    check("Id::PassTimeNative" in hook and "NativePassTimeDetour" in hook and "PassTimeNative = 27" in thread_table, "pass time detour must note its own hook thread")
    check("F4T::RuntimeSelect::IsOG() ? std::span<const std::uint8_t>{ kNativeWaitPrologueOG }" in hook, "wait hook must pick its OG prologue through IsOG")
    check("REL::Relocation<void(RE::PlayerCharacter*, std::int32_t)> func{ kNativeSleep };" in hook, "CallNativeSleep must fall back to the native sleep when its hook is absent")
    check('"0x00E99870"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json") and '"0x00CCEEC0"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json"), "lock must carry the pass time rows")
    check("kNativeWait{ 328629, 2232888, 2232888 }" in hook, "native wait hook must carry the 328629/2232888 ids")
    check("0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x0D, 0x5C, 0x74, 0xA3, 0x04" in hook, "native wait OG hook must carry the 20-byte prologue")
    check("0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x8B, 0xDA" in hook, "native wait NG and AE hook must carry the 15-byte prologue")
    check("Id::WaitNative" in hook and "NativeWaitDetour" in hook, "native wait detour must note its hook thread")
    for block, original in (
        ('if (!BlockedInMultiplayer("native sleep"', "s_nativeSleepHook.hook->original<NativeTimeFn>()(a_player, a_hours);"),
        ('if (!BlockedInMultiplayer("native wait"', "s_nativeWaitHook.hook->original<NativeTimeFn>()(a_player, a_hours);"),
        ('if (!BlockedInMultiplayer("pass time"', "s_passTimeHook.hook->original<PassTimeFn>()(a_player, a_hours, a_flag);"),
    ):
        check(0 <= hook.find(block) < hook.find(original), f"{block} must sit ahead of its vanilla original call")
    check(
        0 <= hook.find("request.target = kNativePassTime.address();")
        < hook.find("request.destination = REX::UNRESTRICTED_CAST<void*>(&NativePassTimeDetour);")
        < hook.find("F4T::InlineHook::Install(request, LogLine, s_passTimeHook);"),
        "pass time install must set target and destination before Install(..., s_passTimeHook)",
    )
    check("kServePrisonTime{ 1117393, 2233140, 2233140 }" in hook and "native-serveprison-ids.md" in hook, "serve prison hook must carry the 1117393/2233140 ids and its record")
    check("0x66, 0x0F, 0x6E, 0xCB, 0x0F, 0x5B, 0xC9, 0xF3, 0x0F, 0x59, 0x0D, 0x76, 0x4C, 0x5A, 0x01" in hook, "serve prison NG hour window must carry the 15-byte prologue")
    check("0x66, 0x0F, 0x6E, 0xCB, 0x0F, 0x5B, 0xC9, 0xF3, 0x0F, 0x59, 0x0D, 0x6E, 0x26, 0x71, 0x01" in hook, "serve prison AE hour window must carry the 15-byte prologue")
    check(
        0 <= hook.find("if (!F4T::RuntimeSelect::IsOG()) {")
        < hook.find("request.target = kServePrisonTime.address() + kServePrisonHourOffset;")
        < hook.find("F4T::InlineHook::InstallMid(request, &ServePrisonHourDetour, LogLine, s_servePrisonHourHook);"),
        "serve prison hour mid-hook must install on NG and AE only, through InstallMid",
    )
    check("kServePrisonHourOffset = 0xF7" in hook, "serve prison hour site must be the 0xF7 offset from the function")
    check("a_context.rbx = 0;" in hook and "Id::ServePrisonNative" in hook, "serve prison mid-hook must zero rbx in multiplayer and note its hook thread")
    check("ServePrisonNative = 28" in thread_table, "hook thread table must name ServePrisonNative")
    check("ServePrisonHourDetour(safetyhook::Context& a_context)" in hook and "a_context.rbx = 0" in hook, "serve prison detour must take the mid-hook context")
    check('"kServePrisonTime"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json") and '"0x00CEC6B0"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json") and '"0x00D726F0"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json"), "lock must carry the serve prison rows")
    check(
        0 <= hook.find("F4T::InlineHook::InstallMid(request, &ServePrisonHourDetour, LogLine, s_servePrisonHourHook);")
        < hook.find("g_guardsInstalled.store(true, std::memory_order_release);")
        < hook.find("g_blockIncomplete.store(true, std::memory_order_release);", hook.find("g_guardsInstalled.store(true")),
        "guards must install after every hook attempt, and an incomplete install must set g_blockIncomplete",
    )
    check(hook.count("++missing;") == 5 and "sleep guards not installed" not in hook, "every failed vanilla time hook must count toward one incomplete-install error and a failed bed hook must not return early")
    check('LogLine(std::format("error: {} vanilla sleep/wait/pass-time hook(s) not installed' in hook, "an incomplete install must log one summary error line")
    check('#include "F4TFailClosedNotice.h"' in hook and "F4T::FailClosedNotice::Show(kBlockIncompleteNotice);" in hook, "an incomplete block must show the existing fail-closed notice")
    check("if (g_blockIncomplete.load(std::memory_order_acquire) && !g_incompleteNoticeShown && InMultiplayer()) {" in hook, "the incomplete-block notice must show once, only in a multiplayer session, from Tick")
    check("kServePrisonHourPrologueOG" not in hook and "if (!F4T::RuntimeSelect::IsOG()) {" in hook, "OG jail release is covered by the pass time hook, so no OG mid-hook is installed")
    check(
        0 <= hook.find("void EnsureSinkRegistered()")
        < hook.find("!g_guardsInstalled.load(")
        < hook.find("RegisterSink(&g_sleepWaitMenuSink)"),
        "EnsureSinkRegistered must return before RegisterSink unless the guards installed",
    )
    check("class SleepWaitMenuSink" in hook and "RegisterSink(&g_sleepWaitMenuSink)" in hook, "vanilla sleep wait menu must be guarded by a menu open sink")
    check('"SleepWaitMenu"' in hook and "UI_MESSAGE_TYPE::kHide" in hook and "a_event.opening" in hook, "menu guard must hide a SleepWaitMenu opened in multiplayer")
    check("SleepNative = 25" in thread_table and "WaitNative = 26" in thread_table, "hook thread table must name SleepNative and WaitNative")
    check('"kNativeWait"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json") and '"0x00E99650"' in read(ROOT / "CO-BETA/tools/engine-ids.lock.json"), "lock must carry the native wait rows")
    lock = read(ROOT / "CO-BETA/tools/engine-ids.lock.json")
    check('"kBedEligibility"' not in lock and '"kCanPassTime"' in lock, "lock must name the bed eligibility rows kCanPassTime")
    check('"kSleep"' not in lock and '"kNativeSleep"' in lock, "lock must name the native sleep rows kNativeSleep")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TSleepBedBlockContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TSleepBedBlockContract.py", _section_016_f4tsleepbedblockcontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts_part7: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())