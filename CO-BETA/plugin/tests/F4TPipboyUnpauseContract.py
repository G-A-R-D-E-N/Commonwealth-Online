from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    return (PLUGIN / relative).read_text(encoding="utf-8")


def function_body(code, signature):
    start = code.index(signature)
    brace = code.index("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


pipboy = read("src/F4TPipboyUnpause.cpp")
pipboy_header = read("include/F4TPipboyUnpause.h")
main = read("src/main.cpp")
world_sync = read("src/F4TWorldStateSync.cpp")
runtime = read("src/F4TReplicationRuntime.cpp")
downed_local = read("src/F4TDownedGlueLocal.cpp")
latch_tests = read("../../co-core/xmake-tests.lua")

hook = function_body(pipboy, "RE::UI_MESSAGE_RESULTS ProcessMessageHook(")
tick = function_body(pipboy, "void Tick()")
restore = function_body(pipboy, "void RestorePauseFlag(")
freeze = function_body(world_sync, "bool IsGameTimeFrozen()")
lambda_body = function_body(main, "F4T::F4SECapabilities::AddPermanentTask([]() {")
runtime_tick = function_body(runtime, "void Tick()")
enter_downed = function_body(downed_local, "void EnterDownedPhase(")
close_downed = function_body(pipboy, "void CloseForDownedPlayer()")

check("namespace F4T::PipboyUnpause" in pipboy_header, "the header must declare F4T::PipboyUnpause")
check("!F4T::SessionLaunch::IsFirstJoinOnboarding()" in function_body(pipboy, "bool InMultiplayerSession()"), "the Pip-Boy must stay paused during first-join onboarding: a blank unpaused handout Pip-Boy regressed on 2026-11-08")
check("CloseIfOnboardingStuck(isOpen);" in function_body(pipboy, "void Tick()") and "IsFirstJoinOnboarding()" in function_body(pipboy, "void CloseIfOnboardingStuck(bool a_open)") and "CloseForDownedPlayer();" in function_body(pipboy, "void CloseIfOnboardingStuck(bool a_open)"), "a Pip-Boy left open and idle for 90 s during first-join onboarding must be force-closed, but one the player is using must not be")
check("PlayerInputSeen()" in function_body(pipboy, "void Tick()"), "the stuck-Pip-Boy timer must restart on any player input so a Pip-Boy being used is not force-closed")
check("kOnboardingHardCap" in function_body(pipboy, "static void CloseIfOnboardingStuck(bool a_open)"), "the stuck-Pip-Boy close must also fire after a hard total-open cap so input noise cannot keep it open forever")
check("void Install();" in pipboy_header and "void Tick();" in pipboy_header and "void CloseForDownedPlayer();" in pipboy_header, "the header must declare Install, Tick and CloseForDownedPlayer")
check('#include "F4TPipboyPauseLatch.h"' in pipboy and "PipboyPauseLatch::OnShow(g_latch, a_this->OnStack(), InMultiplayerSession())" in hook, "the show decision must come from the co-core latch")
check("PipboyPauseLatch::OnTick(g_latch, menu != nullptr, menu && menu->OnStack())" in restore, "the restore decision must come from the co-core latch with the live menu state")
check("F4TPipboyPauseLatchTests.cpp" in latch_tests, "the co-core latch must have its unit test registered")
check("Shutdown" not in pipboy_header and "Shutdown" not in pipboy, "the module must not declare a shutdown path")
check("TODO" not in pipboy and "FIXME" not in pipboy, "the module must ship without open markers")
check("E:/RE-work/co-pipboy/pipboy-unpause-static-facts.md" in pipboy, "the fixed table slot must cite the static facts record")
check("kProcessMessageSlot{ 3 }" in pipboy, "the hook must replace the third slot of the PipboyMenu table")
check("g_installAttempted" in pipboy, "the hook must install once")
check("kShow" in hook and "InMultiplayerSession()" in hook, "the pause flag must change only on a show message during a multiplayer session")
check(pipboy.count("menuFlags.reset(") == 1 and "menuFlags.reset(RE::UI_MENU_FLAGS::kPausesGame)" in hook, "the hook must be the only place that clears the pause flag")
check(pipboy.count("menuFlags.set(") == 1 and "menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame)" in restore, "the restore helper must be the only place that sets the pause flag back")
check("kHide" not in hook, "the hook must not restore on hide: the engine reads the flag after slot 3 returns on hide")
check(hook.index("OnShow(") < hook.index("menuFlags.reset("), "the latch decision must run before the pause flag is cleared")
check(restore.index("OnStack()") < restore.index("menuFlags.set("), "the restore must wait until the menu left the stack")
check(restore.index("GetMenu<RE::PipboyMenu>()") < restore.index("OnTick(") < restore.index("menuFlags.set("), "the restore must be a no-op unless the latch says the hook cleared the flag")
check(tick.index("RestorePauseFlag(") < tick.index("LockInput()"), "the restore must run on every tick, before the multiplayer-only input lock")
check("InMultiplayerSession" not in restore, "the restore must run after the session ended too")
check("kSlotNotExecutable" not in pipboy, "a chained slot must be refused")
check("verdict != VtableGuard::SlotVerdict::kOk" in pipboy and "SlotMatchesExpectedRva(" in pipboy, "the slot must be exactly the engine function for this runtime")
check("kOgRva{ 0xB935C0 }" in pipboy and "kNgRva{ 0xA728F0 }" in pipboy and "kAeRva{ 0xAE4090 }" in pipboy, "the verified ProcessMessage RVAs must be pinned for OG, NG and AE")
check(tick.index("RestorePauseFlag(") < tick.index("g_inputLayer.reset()"), "the restore must stay outside the lock branch")
check(hook.index("g_originalProcessMessage(") < hook.index("menuFlags.reset("), "the original hook must run before the pause flag is cleared")
check("a_this->OnStack()" in hook, "the pause flag must stay set when the Pip-Boy is already on the menu stack")
check(hook.index("OnStack()") < hook.index("menuFlags.reset("), "the menu stack check must run before the pause flag is cleared")
check("return result;" in hook, "the hook must return the original result unchanged")
check("USER_EVENT_FLAG::kMovement" in pipboy and "USER_EVENT_FLAG::kFighting" in pipboy, "movement and fighting must be locked while the Pip-Boy is open")
check("kLooking" not in pipboy, "looking must stay free so Pip-Boy cursor input is not locked")
check("false, RE::UserEvents::SENDER_ID::kScript" in pipboy, "the input lock must disable events without consuming keys")
check("GetMenuOpen<RE::PipboyMenu>()" in tick and "InMultiplayerSession()" in tick, "the input lock must apply only while the Pip-Boy is open in a multiplayer session")
check("g_inputLayer.reset();" in tick, "the input lock must be released when the condition ends")
check("PipboyMenu show: onStack" in hook and hook.index("PipboyMenu show: onStack") < hook.index("menuFlags.reset("), "the show decision must be logged inside the hook before the pause flag is cleared")
check("g_pipboyOpen" in tick and tick.index("RestorePauseFlag(") < tick.index("PipboyMenu {}") < tick.index("LockInput()"), "the open and close edges must be logged in the tick between the pause restore and the input lock")
check("constexpr bool kVanillaPause{ true };" in pipboy and "!kVanillaPause &&" in function_body(pipboy, "bool InMultiplayerSession()"), "the Pip-Boy must keep vanilla pausing while the unpause is withdrawn: the multiplayer Pip-Boy blank screen regressed on 2026-11-08")
check('#include "F4TPipboyUnpause.h"' in main,"main must include the Pip-Boy unpause module")
check(main.count("F4T::PipboyUnpause::Install();") == 1 and main.count("F4T::PipboyUnpause::Tick();") == 0, "main must install the Pip-Boy unpause module once and must not tick it")
check("F4T::PipboyUnpause::Install();" in lambda_body and "F4T::PipboyUnpause::Tick();" not in lambda_body, "the permanent task must install the Pip-Boy unpause module without ticking it")
check('#include "F4TPipboyUnpause.h"' in runtime, "the replication runtime must include the Pip-Boy unpause module")
check(runtime_tick.count("F4T::PipboyUnpause::Tick();") == 1 and runtime_tick.index("F4T::PipboyUnpause::Tick();") < runtime_tick.index("F4T::Downed::Tick();"), "the replication runtime must tick the Pip-Boy unpause module once, before Downed::Tick")
check(enter_downed.index("F4T::Downed::CloseChatForDownedPlayer();") < enter_downed.index("F4T::PipboyUnpause::CloseForDownedPlayer();") < enter_downed.index("ReapplyDownedPose("), "entering the downed phase must close the Pip-Boy between the chat close and the pose")
check('#include "F4TPipboyUnpause.h"' in downed_local, "the downed glue must include the Pip-Boy unpause module")
check("kHide" in close_downed and "PipboyMenu::MENU_NAME" in close_downed and "GetMenuOpen<RE::PipboyMenu>()" in close_downed, "the downed close must send kHide to the Pip-Boy only while it is open")
check("PipboyMenu" not in world_sync, "the Pip-Boy must not freeze multiplayer world time")
check("ui->GetMenuOpen<RE::SitWaitMenu>()) {" in freeze, "the other menu freeze terms must stay")
check("kPausesGame" not in world_sync and "kPausesGame" not in main, "no other source may clear the pause flag")
check("UpdateMenuMode" not in pipboy and "menuMode" not in pipboy and "GameIsPaused" not in pipboy, "the module must not write or read the menu mode counter")
check("#if" not in pipboy and "#if" not in pipboy_header, "the module must ship without build or config gates")


class Pump:
    def __init__(self):
        self.counter = 0
        self.menu = None

    def show(self, multiplayer):
        if self.menu is None:
            self.menu = {"pauses": True, "on_stack": False, "cleared": False}
        menu = self.menu
        if menu["on_stack"]:
            return
        if multiplayer:
            menu["pauses"] = False
            menu["cleared"] = True
        menu["on_stack"] = True
        if menu["pauses"]:
            self.counter += 1

    def hide(self):
        menu = self.menu
        if menu is None or not menu["on_stack"]:
            return
        menu["on_stack"] = False
        if menu["pauses"]:
            self.counter -= 1

    def tick(self):
        menu = self.menu
        if menu is not None and menu["cleared"] and not menu["on_stack"]:
            menu["pauses"] = True
            menu["cleared"] = False

    def destroy(self):
        self.menu = None


def run(steps):
    pump = Pump()
    for step in steps:
        if step == "show_mp":
            pump.show(True)
        elif step == "show_solo":
            pump.show(False)
        elif step == "hide":
            pump.hide()
        elif step == "tick":
            pump.tick()
        elif step == "destroy":
            pump.destroy()
        assert pump.counter >= 0, steps
    return pump.counter


check(run(["show_mp", "tick", "hide", "tick"]) == 0, "model: show then hide must leave the counter at its start")
check(run(["show_mp", "tick", "hide", "tick", "show_mp", "tick", "hide", "tick"]) == 0, "model: show, hide, show, hide must leave the counter at its start")
check(run(["show_mp", "show_mp", "tick", "hide", "tick"]) == 0, "model: a repeat show must not move the counter")
check(run(["show_mp", "tick", "show_solo", "hide", "tick"]) == 0, "model: disconnect while open then hide must leave the counter at its start")
check(run(["show_mp", "hide", "destroy", "tick", "show_mp", "hide", "tick"]) == 0, "model: a destroyed menu must leave the counter at its start")
check(run(["show_mp", "hide", "tick", "show_solo", "hide", "tick"]) == 0, "model: a solo open after a multiplayer open must balance")
check(run(["show_solo"]) == 1, "model: a solo open must still pause")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TPipboyUnpauseContract: all checks passed")
