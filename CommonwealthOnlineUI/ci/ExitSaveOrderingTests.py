import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MENU = (ROOT / "src/F4TPrismaUI_Menu.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
LIFECYCLE = (ROOT / "src/F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")

begin = MENU.index("void BeginPauseMenuQuit(bool a_toMainMenu)")
body = MENU[begin:MENU.index("void RequestQuitToMainMenuFromPause()", begin)]

main_branch = body[:body.index("main->resetGame = true")]
assert "quitToMainMenuQueued =" not in body, "quit to main menu must use Main::resetGame only, never the PauseMenu flag"
assert main_branch.count("Session::Disconnect();") == 1, "quit to main menu must keep its immediate disconnect"

assert "Session::IsConnected()" in body, "desktop quit must detect an active multiplayer session"
assert "!inSession && saveMgr && saveMgr->savingAllowed" in body, \
    "exit save must only queue outside a multiplayer session"
queue = body.index("kSaveAndQuitToDesktop")
assert body.index("g_exitSaveGate.Arm(") < queue, "desktop quit must arm the exit save gate before queuing the save"
desktop_after_queue = body[queue:body.index("return;", queue)]
assert "Session::Disconnect" not in desktop_after_queue, "offline desktop quit must not disconnect before kPostSaveGame"
assert "the CO exit save is queued and the quit waits for it" in body, "in-session desktop quit must wait for the CO exit save"

assert re.search(r"case F4SE::MessagingInterface::kPostSaveGame:\s+F4T::PrismaUI::NotifyPostSaveGame\(\);", MAIN), \
    "main.cpp MessageHandler must forward kPostSaveGame to NotifyPostSaveGame"

assert LIFECYCLE.count("TickExitSaveDisconnect();") == 1, "exit save tick must be registered exactly once"
tick = LIFECYCLE.index("TickExitSaveDisconnect();")
watch = LIFECYCLE.index("WatchMainMenuState();", tick)
assert "});" not in LIFECYCLE[tick:watch], "exit save tick must run inside the menu watch task before the watchers"

desktop_wait = body.index("g_pendingSessionQuit = PendingSessionQuit::kDesktop")
desktop_quit = body.index("QueueDesktopQuit();")
assert desktop_wait < desktop_quit and "return;" in body[desktop_wait:desktop_quit], \
    "in-session desktop quit must wait for the session exit save before queueing the quit"
main_wait = body.index("g_pendingSessionQuit = PendingSessionQuit::kMainMenu")
assert main_wait < body.index("main->resetGame = true"), \
    "in-session main-menu quit must wait for the session exit save before queueing the quit"

tick_begin = MENU.index("void TickExitSaveDisconnect()")
tick_body = MENU[tick_begin:MENU.index("\n\t}\n", tick_begin)]
settled = tick_body.index("!COUI::Session::IsConnected()")
assert settled < tick_body.index("QueueDesktopQuit();"), "desktop quit resumes only after the session disconnected"
assert settled < tick_body.index("BeginPauseMenuQuit(true);"), "main-menu quit resumes only after the session disconnected"
assert "g_pendingSessionQuit = PendingSessionQuit::kNone;" in tick_body and \
    tick_body.index("g_pendingSessionQuit = PendingSessionQuit::kNone;") < tick_body.index("QueueDesktopQuit();"), \
    "the pending quit must be cleared before it resumes, so a settled quit runs exactly once"
wait = tick_body.index("g_pendingSessionQuit == PendingSessionQuit::kMainMenu && !pauseMenu")
wait_else = tick_body.index("} else {", wait)
assert "GetMenu<RE::PauseMenu>()" in tick_body and tick_body.index("GetMenu<RE::PauseMenu>()") < wait, \
    "the pause menu presence must be checked before a pending main-menu quit resumes"
assert "QueueDesktopQuit();" not in tick_body[wait:wait_else] and "BeginPauseMenuQuit(true);" not in tick_body[wait:wait_else], \
    "a pending main-menu quit with the pause menu closed must keep waiting, never become a desktop quit"
assert "g_pendingSessionQuit = PendingSessionQuit::kNone;" not in tick_body[wait:wait_else] and \
    tick_body.index("g_pendingSessionQuit = PendingSessionQuit::kNone;") > wait_else, \
    "the wait branch must leave the pending quit set so it resumes when the pause menu opens"

fail = MENU.index("void FailStuckPauseMenuQuit()")
fail_body = MENU[fail:tick_begin]
assert "g_pauseMenuQuitInProgress = false;" in fail_body and "g_pauseMenuQuitStartedAt = 0;" in fail_body, \
    "the quit watchdog must clear the in-progress quit"
assert "g_pendingSessionQuit = PendingSessionQuit::kNone;" in fail_body, \
    "the quit watchdog must clear the pending session quit"
assert "onPauseMenuQuitFailed" in fail_body, "the quit watchdog must call window.onPauseMenuQuitFailed"
assert "g_pauseMenuQuitStartedAt = GetTickCount64();" in body, \
    "BeginPauseMenuQuit must start the watchdog clock on the game thread"
assert "now - g_pauseMenuQuitStartedAt >= 75000" in tick_body and "GetTickCount64()" in tick_body, \
    "TickExitSaveDisconnect must measure 75s with GetTickCount64"
assert "FailStuckPauseMenuQuit();" in tick_body and tick_body.index("COUI::Session::IsConnected() || !quitHappened") < tick_body.index("FailStuckPauseMenuQuit();"), \
    "the watchdog fires only while still connected or before the main-menu or desktop quit"
assert "main->quitGame || main->resetGame" in tick_body, "a queued quit is the result that stops the watchdog"
assert "std::thread" not in fail_body and "std::thread" not in tick_body, "the watchdog must run on the game-thread tick"

script = (ROOT / "ui/views/CommonwealthOnline/pause-menu/script.js").read_text(encoding="utf-8")
assert "window.onPauseMenuQuitStarted = function" in script, "onPauseMenuQuitStarted must stay defined"
assert "window.onSessionExitSave = function" in script and 'state !== "saving"' in script and 'state !== "retrying"' in script \
    and 'state !== "saved"' in script and 'state !== "failed"' in script, \
    "onSessionExitSave must accept saving, retrying, saved and failed"
assert "window.onPauseMenuQuitFailed = function" in script
failed_handler = script[script.index("window.onPauseMenuQuitFailed"):script.index("window.handleControllerAxes")]
assert "resetToPauseRoot();" in failed_handler and "paintQuitSaveStatus(" in failed_handler, \
    "onPauseMenuQuitFailed must reset the pause menu and show the error"
notice = (ROOT.parent / "CO-BETA/plugin/src/F4TSessionExitSaveNotice.cpp").read_text(encoding="utf-8")
assert 'PushPause("saving"' in notice and 'PushPause("retrying"' in notice and 'PushPause("saved"' in notice and 'PushPause("failed"' in notice, \
    "the exit-save notice must push saving, retrying, saved and failed to the pause menu"
assert "IVCommonwealthOnlineUI10" in notice and "NotifySessionExitSave" in notice, \
    "the pause-menu save state must go through the versioned UI API"

print("exit save ordering contract passed: quit waits for the session exit save before disconnect or quit; kPostSaveGame forwarded; tick precedes menu watchers; quit watchdog and pause save state are pinned")

lifecycle = (Path(__file__).resolve().parents[1] / "src/F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
inject = (Path(__file__).resolve().parents[1] / "src/F4TMainMenuInject.cpp").read_text(encoding="utf-8")
assert "&& !g_vanillaMessageBoxOpen;" in lifecycle, "main-menu overlay and launcher must step aside while a vanilla MessageBoxMenu is open"
assert "return !g_vanillaMessageBoxOpen &&" in lifecycle, "CO must not capture menu input while a vanilla MessageBoxMenu is open"
assert "RE::MessageBoxMenu::MENU_NAME" in inject and "SetVanillaMessageBoxOpen(a_event.opening)" in inject, "MainMenuOpenCloseSink must report MessageBoxMenu open/close"
print("message box step-aside: ok")
