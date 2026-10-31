from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "CO-BETA" / "plugin"
UI = ROOT / "CommonwealthOnlineUI"
UI_CLIENT_HEADER = (PLUGIN / "include" / "F4TUIClient.h").read_text(encoding="utf-8")
UI_CLIENT_SOURCE = (PLUGIN / "src" / "F4TUIClient.cpp").read_text(encoding="utf-8")
SESSION_ENDED_SOURCE = (PLUGIN / "src" / "F4TSessionEndedScreen.cpp").read_text(encoding="utf-8")
SESSION_LAUNCH_SOURCE = (PLUGIN / "src" / "F4TSessionLaunch.cpp").read_text(encoding="utf-8")
SESSION_LAUNCH_STATE_SOURCE = (PLUGIN / "src" / "F4TSessionLaunchState.cpp").read_text(encoding="utf-8")
NETWORK_IMPL_SOURCE = (PLUGIN / "src" / "F4TNetworking.cpp").read_text(encoding="utf-8")
INBOUND_SOURCE = (PLUGIN / "src" / "F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
MENU_SOURCE = (UI / "src" / "F4TPrismaUI_Menu.cpp").read_text(encoding="utf-8")

PAUSE_DIR = UI / "ui" / "views" / "CommonwealthOnline" / "pause-menu"
PAUSE_JS = (PAUSE_DIR / "script.js").read_text(encoding="utf-8")
PAUSE_HTML = (PAUSE_DIR / "index.html").read_text(encoding="utf-8")


def _body(source, signature):
    start = source.index(signature)
    depth = 0
    in_string = False
    for index in range(source.index("{", start), len(source)):
        char = source[index]
        if char == '"':
            in_string = not in_string
        elif in_string:
            continue
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(signature)


def test_plugin_declares_disconnect_screen_helper_on_the_ui_client_api():
    assert "void ShowDisconnectScreen(const char* a_code, const char* a_reason);" in UI_CLIENT_HEADER


def test_disconnect_screen_helper_skips_main_menu_and_null_checks_the_api():
    body = _body(SESSION_ENDED_SOURCE, "void ShowDisconnectScreen(")

    assert "!ui->ShowSessionLockout" in body
    assert "F4T::SessionWorldState::IsMainMenuOpen()" in body
    assert body.index("IsMainMenuOpen()") < body.index("ui->ShowSessionLockout(code.c_str(), reason.c_str());")
    assert '#include "F4TSessionWorldState.h"' in SESSION_ENDED_SOURCE


def test_transport_loss_shows_disconnected_screen_inside_the_game_task():
    body = _body(UI_CLIENT_SOURCE, "void NotifyTransportDisconnected(")

    task_start = body.index("taskInterface->AddTask(")
    call = body.index('ShowDisconnectScreen("disconnected", reasonCopy.c_str());')
    assert task_start < call
    assert body.index("CancelPendingJoin(reasonCopy)") < call


def test_every_disconnect_screen_is_refused_during_a_voluntary_exit():
    body = _body(SESSION_ENDED_SOURCE, "void ShowDisconnectScreen(")

    guard = body.index("IsVoluntaryExitRequested() || F4T::SessionSaveGuard::IsSessionExitPending()")
    show = body.index("ui->ShowSessionLockout(")
    assert guard < show, (
        "a receive loop that dies while a quit or host stop is in flight, a first-join failure after a "
        "cancelled join, and a late kick alert all route here and must not open the lockout"
    )

    transport = _body(UI_CLIENT_SOURCE, "void NotifyTransportDisconnected(")
    assert 'ShowDisconnectScreen("disconnected", reasonCopy.c_str());' in transport, (
        "transport loss must go through the guarded helper, not the lockout API directly"
    )
    fail = _body(SESSION_LAUNCH_STATE_SOURCE, "void FailFirstTimeJoin(")
    assert 'F4T::UIClient::ShowDisconnectScreen("disconnected"' in fail


def test_voluntary_disconnect_is_flagged_before_the_teardown():
    body = _body(UI_CLIENT_SOURCE, "void DisconnectNow()\n")

    flag = body.index("g_voluntaryExitRequested.store(true);")
    teardown = body.index("F4T::Networking::DisconnectFromLocalServer();")
    assert flag < teardown, "the voluntary exit flag must be set before the socket is closed"

    disconnect = _body(UI_CLIENT_SOURCE, "void Callback_Disconnect()")
    assert 'RunOrDeferSessionExit("disconnect requested", DisconnectNow)' in disconnect, (
        "every UI disconnect, including a host stop or a quit, must reach DisconnectNow"
    )


def test_new_sessions_clear_the_voluntary_exit_flag():
    join = _body(UI_CLIENT_SOURCE, "bool Callback_JoinSession(")
    local = _body(UI_CLIENT_SOURCE, "bool Callback_ConnectLocal(")

    assert "g_voluntaryExitRequested.store(false);" in join
    assert "g_voluntaryExitRequested.store(false);" in local
    assert join.index("g_joinInProgress.exchange(true)") < join.index("g_voluntaryExitRequested.store(false);")


def test_user_cancelled_join_completes_without_the_failure_screen():
    cancel = _body(SESSION_LAUNCH_SOURCE, "bool CancelPendingJoin(")

    assert "CompletePendingJoin(false" in cancel
    assert "FailFirstTimeJoin(" not in cancel, (
        "a user cancel completes the join itself; routing it through the failure path would open the lockout"
    )

    disconnect = _body(UI_CLIENT_SOURCE, "void DisconnectNow()\n")
    assert 'CancelPendingJoin("Connection cancelled.")' in disconnect
    assert "g_voluntaryExitRequested.store(true);" in disconnect
    assert disconnect.index("g_voluntaryExitRequested.store(true);") < disconnect.index(
        'CancelPendingJoin("Connection cancelled.")'
    )


def test_voluntary_disconnect_never_reaches_transport_notification():
    stop_body = _body(NETWORK_IMPL_SOURCE, "void StopReceiveThread()")
    loop_body = _body(NETWORK_IMPL_SOURCE, "void ReceiveLoop()")

    assert "g_receiveThreadRunning.store(false);" in stop_body
    assert loop_body.index("g_receiveThreadRunning.exchange(false)") < loop_body.index(
        "F4T::UIClient::NotifyTransportDisconnected();"
    )


def test_kick_and_ban_packet_passes_the_real_code_to_the_session_ended_screen():
    body = _body(SESSION_ENDED_SOURCE, "void ShowSessionEndedNotification(")
    inbound = _body(INBOUND_SOURCE, "void HandleSessionEndedPacket(")

    assert "ShowDisconnectScreen(a_code, a_reason);" in body
    assert body.index("ShowDisconnectScreen(a_code, a_reason);") < body.index("ui->ShowAlert(&request")
    assert "F4T::UIClient::ShowSessionEndedNotification(codeCopy.c_str(), reasonCopy.c_str());" in inbound


def test_first_join_failure_shows_disconnected_screen_after_completion():
    body = _body(SESSION_LAUNCH_STATE_SOURCE, "void FailFirstTimeJoin(")

    assert '#include "F4TUIClient.h"' in SESSION_LAUNCH_STATE_SOURCE
    assert body.index("CompletePendingJoin(false, a_reason);") < body.index(
        'F4T::UIClient::ShowDisconnectScreen("disconnected", std::string{ a_reason }.c_str());'
    )


def test_pause_menu_routes_lockout_main_menu_action_to_the_game_task():
    body = _body(MENU_SOURCE, "void BindPauseMenuDomHandlers(")

    assert 'actionName == "lockoutMainMenu"' in body
    assert "RequestMainMenuFromSessionLockout();" in body


def test_lockout_main_menu_sets_pending_quit_then_opens_pause_menu():
    body = _body(MENU_SOURCE, "void RequestMainMenuFromSessionLockout(")

    guard = body.index("if (!g_sessionLockoutActive)")
    pending = body.index("g_pendingSessionQuit = PendingSessionQuit::kMainMenu;")
    wait_log = body.index("g_pendingMainMenuWaitLogged = false;")
    show = body.index("queue->AddMessage(RE::PauseMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);")
    assert guard < pending < wait_log < show
    assert "Main::resetGame" not in body
    assert "resetGame" not in body


def test_lockout_clear_hides_overlay_and_resets_flags():
    body = _body(MENU_SOURCE, "void ClearSessionLockoutOverlay(")

    assert "g_sessionLockoutActive = false;" in body
    assert "g_sessionLockoutPending = false;" in body
    assert "window.hideSessionLockout" in body


def test_lockout_clear_runs_after_the_quit_is_queued_not_before():
    body = _body(MENU_SOURCE, "void BeginPauseMenuQuit(bool a_toMainMenu)")

    queued = body.index('ApplyPauseMenuState("QuitToMainMenuCover");')
    clear = body.index("ClearSessionLockoutOverlay();")
    assert queued < clear


def test_lockout_guards_keep_pause_menu_and_vanilla_suppression_blocked():
    apply_body = _body(MENU_SOURCE, "bool ApplyPauseMenuState(")
    hide_body = _body(MENU_SOURCE, "void HidePauseMenuInternal(")
    show_body = _body(MENU_SOURCE, "void ShowPauseMenuInternal(")
    quit_tick = _body(MENU_SOURCE, "void TickExitSaveDisconnect()")

    assert "if (!g_sessionLockoutActive) {\n\t\t\t\tSetVanillaPauseMenuSuppressed(true);" in apply_body
    assert "if (g_sessionLockoutActive) {" in hide_body
    assert "g_sessionLockoutActive" in show_body
    assert "g_pauseMenuQuitInProgress = false;" in quit_tick
    assert "BeginPauseMenuQuit(true);" in quit_tick


def test_lockout_titles_cover_banned_kicked_server_full_and_fallback():
    body = _body(PAUSE_JS, "function sessionLockoutTitleFor(")

    assert '"YOU HAVE BEEN BANNED"' in body
    assert '"YOU HAVE BEEN KICKED"' in body
    assert '"SERVER FULL"' in body
    assert '"DISCONNECTED FROM SERVER"' in body


def test_lockout_stores_code_instead_of_reparsing_the_title():
    show_body = _body(PAUSE_JS, "window.onPauseMenuShown = function")

    assert "var sessionLockoutCode" in PAUSE_JS
    assert "sessionLockoutCode," in show_body
    assert "/kicked/i" not in PAUSE_JS


def test_lockout_buttons_are_return_to_menu_first_then_quit_to_desktop():
    lockout = PAUSE_HTML[PAUSE_HTML.index('id="sessionLockoutPanel"'):]
    main_menu = lockout.index('data-choice="mainMenu"')
    desktop = lockout.index('data-choice="desktop"')

    assert lockout.index("RETURN TO MAIN MENU") < lockout.index("QUIT TO DESKTOP")
    assert main_menu < desktop
    assert 'class="menu-item quit-confirm__option is-selected" data-index="0" data-choice="mainMenu"' in lockout
    assert 'data-index="1" data-choice="desktop"' in lockout


def test_lockout_activation_sends_the_main_menu_action_or_quits():
    body = _body(PAUSE_JS, "function activateSessionLockoutSelected(")

    assert 'sendCoAction({ action: "lockoutMainMenu" });' in body
    assert 'requestQuit("quitToDesktop");' in body

    guard = body.index("if (lockoutMainMenuPending) {")
    claim = body.index("lockoutMainMenuPending = true;")
    send = body.index('sendCoAction({ action: "lockoutMainMenu" });')
    assert guard < claim < send, (
        "the first press must claim the main-menu quit before the action is sent, otherwise a held key "
        "or a double click sends lockoutMainMenu again for a quit that is already pending"
    )
    assert body.index("quitting = true;") < send


def test_lockout_keyboard_and_gamepad_drive_the_same_selection():
    keyboard = PAUSE_JS.index('if (viewMode === "sessionLockout" || sessionLockoutActive) {\n      if (event.key')
    keyboard_block = PAUSE_JS[keyboard:keyboard + 900]
    pad = PAUSE_JS.index('if (viewMode === "sessionLockout" || sessionLockoutActive) {\n      switch (button)')
    pad_block = PAUSE_JS[pad:pad + 500]

    assert "moveSessionLockoutSelection(-1);" in keyboard_block
    assert "moveSessionLockoutSelection(1);" in keyboard_block
    assert "activateSessionLockoutSelected();" in keyboard_block
    assert '"dpad_up"' in pad_block and '"dpad_down"' in pad_block
    assert "activateSessionLockoutSelected();" in pad_block


def test_lockout_mouse_click_selects_then_activates_the_clicked_item():
    assert "sessionLockoutItems.forEach(function (item, index) {" in PAUSE_JS
    assert "setSessionLockoutSelected(index);\n      activateSessionLockoutSelected();" in PAUSE_JS


def test_lockout_hide_restores_pause_viewmode_without_touching_quit_flag():
    body = _body(PAUSE_JS, "window.hideSessionLockout = function")

    assert "sessionLockoutActive = false;" in body
    assert 'pauseScreen.classList.remove("is-session-lockout-mode");' in body
    assert "quitting" not in body


def test_lockout_pending_main_menu_flag_resets_when_the_lockout_clears_or_the_quit_fails():
    hide_body = _body(PAUSE_JS, "window.hideSessionLockout = function")
    fail_body = _body(PAUSE_JS, "window.onPauseMenuQuitFailed = function")

    assert "lockoutMainMenuPending = false;" in hide_body
    assert "lockoutMainMenuPending = false;" in fail_body


LIFECYCLE_SOURCE = (UI / "src" / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")


def test_disconnect_screen_is_queued_onto_the_game_thread():
    body = _body(SESSION_ENDED_SOURCE, "void ShowDisconnectScreen(")
    assert body.index("AddTask(") < body.index("ui->ShowSessionLockout(")


def test_generic_disconnect_never_replaces_an_active_lockout():
    body = _body(LIFECYCLE_SOURCE, "void ShowSessionLockout(const char* a_code, const char* a_reason)")
    guard = 'if ((g_sessionLockoutActive || g_sessionLockoutPending) && a_code && std::string_view{ a_code } == "disconnected") {'
    assert guard in body
    assert body.index(guard) < body.index("g_sessionLockoutCode =")


def test_lockout_reentry_keeps_a_pending_main_menu_quit_claimed():
    start = PAUSE_JS.index("function enterSessionLockoutMode(")
    head = PAUSE_JS[start:PAUSE_JS.index("sessionLockoutActive = true;", start)]
    assert "if (!lockoutMainMenuPending) {" in head, (
        "re-entering the lockout must not clear the quit cover while a main-menu return is pending"
    )
    assert "quitting = false;" in head
    assert 'classList.remove("is-quitting")' in head


def test_lockout_main_menu_request_ignores_a_repeat_while_the_quit_is_pending():
    body = _body(MENU_SOURCE, "void RequestMainMenuFromSessionLockout(")

    guard = body.index("if (g_pendingSessionQuit == PendingSessionQuit::kMainMenu) {")
    pending = body.index("g_pendingSessionQuit = PendingSessionQuit::kMainMenu;")
    show = body.index("queue->AddMessage(RE::PauseMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);")
    assert guard < pending and guard < show, "a repeated lockout main-menu request must return before re-queueing"
    assert body.index("return;", guard) < pending


def test_quit_dialog_offers_disconnect_then_desktop_then_cancel_with_cancel_default():
    dialog = PAUSE_HTML[PAUSE_HTML.index('id="quitConfirmPanel"'):PAUSE_HTML.index('id="sessionLockoutPanel"')]
    assert dialog.index('data-index="0" data-choice="mainMenu"') < dialog.index('data-index="1" data-choice="desktop"') < dialog.index('data-index="2" data-choice="cancel"')
    assert "DISCONNECT TO MAIN MENU" in dialog
    assert "setQuitConfirmSelected(2);" in _body(PAUSE_JS, "function enterQuitConfirmMode(")


def test_quit_dialog_main_menu_choice_sends_the_native_quit_to_main_menu_action():
    body = _body(PAUSE_JS, "function activateQuitConfirmSelected(")
    assert 'requestQuit(choice === "mainMenu" ? "quitToMainMenu" : "quitToDesktop");' in body
    assert 'actionName == "quitToMainMenu"' in MENU_SOURCE


def main() -> int:
    test_plugin_declares_disconnect_screen_helper_on_the_ui_client_api()
    test_disconnect_screen_helper_skips_main_menu_and_null_checks_the_api()
    test_transport_loss_shows_disconnected_screen_inside_the_game_task()
    test_every_disconnect_screen_is_refused_during_a_voluntary_exit()
    test_voluntary_disconnect_never_reaches_transport_notification()
    test_voluntary_disconnect_is_flagged_before_the_teardown()
    test_new_sessions_clear_the_voluntary_exit_flag()
    test_kick_and_ban_packet_passes_the_real_code_to_the_session_ended_screen()
    test_first_join_failure_shows_disconnected_screen_after_completion()
    test_user_cancelled_join_completes_without_the_failure_screen()
    test_pause_menu_routes_lockout_main_menu_action_to_the_game_task()
    test_lockout_main_menu_sets_pending_quit_then_opens_pause_menu()
    test_lockout_main_menu_request_ignores_a_repeat_while_the_quit_is_pending()
    test_lockout_clear_hides_overlay_and_resets_flags()
    test_lockout_clear_runs_after_the_quit_is_queued_not_before()
    test_lockout_guards_keep_pause_menu_and_vanilla_suppression_blocked()
    test_lockout_titles_cover_banned_kicked_server_full_and_fallback()
    test_lockout_stores_code_instead_of_reparsing_the_title()
    test_lockout_buttons_are_return_to_menu_first_then_quit_to_desktop()
    test_lockout_activation_sends_the_main_menu_action_or_quits()
    test_lockout_keyboard_and_gamepad_drive_the_same_selection()
    test_lockout_mouse_click_selects_then_activates_the_clicked_item()
    test_lockout_hide_restores_pause_viewmode_without_touching_quit_flag()
    test_lockout_pending_main_menu_flag_resets_when_the_lockout_clears_or_the_quit_fails()
    test_lockout_reentry_keeps_a_pending_main_menu_quit_claimed()
    test_disconnect_screen_is_queued_onto_the_game_thread()
    test_generic_disconnect_never_replaces_an_active_lockout()
    test_quit_dialog_offers_disconnect_then_desktop_then_cancel_with_cancel_default()
    test_quit_dialog_main_menu_choice_sends_the_native_quit_to_main_menu_action()
    print("Disconnect screen contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
