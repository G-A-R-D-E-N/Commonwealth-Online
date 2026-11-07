#!/usr/bin/env python3
"""test_ui_browser_contracts: consolidated contract sections.

Consolidated from 6 single-contract scripts so the group runs one process
instead of 6. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_ui_browser_contracts.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


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




def _section_001_test_browser_text_focus_contract():
    """Contract moved verbatim from tests/audit/test_browser_text_focus_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_browser_text_focus_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    TEXT_FOCUS_JS = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/text-focus.js"
    MAIN_MENU_CPP = ROOT / "CommonwealthOnlineUI/src/F4TPrismaUI_MainMenu.cpp"
    BROWSER_CPP = ROOT / "CommonwealthOnlineUI/src/F4TPrismaUI_Browser.cpp"
    INTERNAL_H = ROOT / "CommonwealthOnlineUI/src/F4TPrismaUIInternal.h"


    def test_browser_js_requests_text_focus_through_the_bound_event() -> None:
        assert "window.coRequestTextFocus(JSON.stringify({ active: !!active }));" in TEXT_FOCUS_JS.read_text(encoding="utf-8")


    def test_native_binds_the_text_focus_event_to_the_browser_handler() -> None:
        main_menu = MAIN_MENU_CPP.read_text(encoding="utf-8")
        assert 'g_api->BindUIEvent(a_view, "coRequestTextFocus"' in main_menu
        assert 'const bool active = nlohmann::json::parse(a_json).value("active", false);' in main_menu
        assert "taskInterface->AddTask([active]() {" in main_menu
        assert "ApplyBrowserTextFocus(active);" in main_menu
        assert "void ApplyBrowserTextFocus(bool a_active)" in BROWSER_CPP.read_text(encoding="utf-8")
        assert "void ApplyBrowserTextFocus(bool);" in INTERNAL_H.read_text(encoding="utf-8")


    def test_deactivating_the_browser_clears_text_focus_state() -> None:
        browser = BROWSER_CPP.read_text(encoding="utf-8")
        assert "g_browserTextFocusActive = false;" in browser[browser.index("void DeactivateEmbeddedBrowser"):]


    _merge_ns = dict(locals())
    if True:
        for name, function in list(_merge_ns.items()):
            if name.startswith("test_") and callable(function):
                function()
        print("Browser text focus contract passed")

def _section_002_test_alert_controller_answers():
    """Contract moved verbatim from tests/audit/test_alert_controller_answers.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_alert_controller_answers.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    ALERTS = ROOT / "CommonwealthOnlineUI/src/F4TAlertSystem.cpp"
    ALERTS_HEADER = ROOT / "CommonwealthOnlineUI/include/F4TAlertSystem.h"
    MENU_INPUT = ROOT / "CommonwealthOnlineUI/src/F4TMenuInput.cpp"
    ALERT_VIEW = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/alerts"


    def read(path):
        return path.read_text(encoding="utf-8")


    def function_body(source, signature):
        start = source.index(signature)
        return source[start:source.index("\n\t}\n", start)]


    def test_alert_controller_answers_ride_the_shared_menu_poll():
        menu = read(MENU_INPUT)
        start = menu.index("void PollGamepadInternal()")
        poll = menu[start:menu.index("bool ForwardButtonEvent(", start)]
        assert '#include "F4TAlertSystem.h"' in menu
        assert "const bool alertAnswers = !menuActive && F4T::Alerts::AcceptsControllerAnswers();" in poll
        assert "if (!menuActive && !alertAnswers && !AnyScrollViewVisible()) {" in poll and "if (browserOwned) {" in poll
        assert "F4T::Alerts::ResolveControllerButton(kGamepadButtons[i].name);" in poll
        assert poll.index("if (alertAnswers) {") < poll.index("NotifyGamepadActivity();")
        assert "if (alertAnswers && !g_alertAnswersLastPoll) {" in poll and "g_alertAnswersLastPoll = alertAnswers;" in poll
        assert poll.index("if (alertAnswers && !g_alertAnswersLastPoll) {") < poll.index("if (browserFocused) {")


    def test_alert_system_maps_x_to_accept_and_y_to_decline_without_its_own_poll():
        alerts = read(ALERTS)
        header = read(ALERTS_HEADER)
        resolve = function_body(alerts, "void ResolveControllerButton(const char* a_button)")
        assert 'std::strcmp(a_button, "x") == 0' in resolve
        assert 'std::strcmp(a_button, "y") == 0' in resolve
        assert 'hotkey = "Y";' in resolve and 'hotkey = "N";' in resolve
        assert 'std::strcmp(a_button, "a") == 0' in resolve and 'hotkey = "A";' in resolve
        assert "HotkeyAccepted(name)" in resolve and "ForwardHotkey(hotkey)" in resolve
        assert "bool AcceptsControllerAnswers();" in header
        assert "void ResolveControllerButton(const char* a_button);" in header
        assert "XInputGetState" not in alerts
        accepts = function_body(alerts, "bool AcceptsControllerAnswers()")
        assert "g_hasActionable.load() && !VanillaMenuOwnsController()" in accepts
        vanilla = function_body(alerts, "bool VanillaMenuOwnsController()")
        assert '"PipboyMenu"' in vanilla and '"MessageBoxMenu"' in vanilla
        poll_hotkeys = function_body(alerts, "void PollHotkeys()")
        assert "HotkeyAccepted(pressed)" in poll_hotkeys and "ForwardHotkey(pressed)" in poll_hotkeys


    def test_alert_prompt_shows_controller_glyphs_for_controller_input():
        alerts = read(ALERT_VIEW / "alerts.js")
        index = read(ALERT_VIEW / "index.html")
        assert index.index("../browser/components/controller-glyphs.js") < index.index("alerts.js")
        assert "window.setCoAlertInputMode = function (gamepad, style)" in alerts
        assert "CO_ControllerGlyphs.render(button)" in alerts
        assert 'CONTROLLER_BUTTON_FOR_HOTKEY = { Y: "X", N: "Y", F1: "A", T: "A" }' in alerts
        assert 'var keys = key === "A" ? ["F1", "T"] : [key];' in alerts


    def test_retry_of_a_passworded_join_opens_the_password_prompt():
        bridge = read(ROOT / "CommonwealthOnlineUI/src/F4TServerBrowserBridge.cpp")
        start = bridge.index("void RetryLastJoin()")
        retry = bridge[start:bridge.index("void CompleteDirectoryRefresh(", start)]
        queued = retry.index('{ "type", "joinPasswordRequired" }')
        assert retry.index("if (entry.passworded) {") < queued < retry.index("F4T::PrismaUI::ShowServerBrowser();") < retry.index("COUI::Session::RetryLastJoin(")

def _section_003_test_chat_controller():
    """Contract moved verbatim from tests/audit/test_chat_controller.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_chat_controller.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"


    def read(path):
        return path.read_text(encoding="utf-8")


    def test_chat_takes_pad_buttons_and_opens_with_chord():
        menu = read(UI / "src/F4TMenuInput.cpp")
        chat = read(UI / "src/F4TChatView.cpp")
        browser = read(UI / "src/F4TPrismaUI_Browser.cpp")
        life = read(UI / "src/F4TPrismaUI_Lifecycle.cpp")
        assert "g_chatActive.load()" in menu and "void SetChatActive(bool a_active)" in menu
        assert "XINPUT_GAMEPAD_LEFT_SHOULDER | REX::W32::XINPUT_GAMEPAD_RIGHT_SHOULDER | REX::W32::XINPUT_GAMEPAD_DPAD_UP" in menu
        assert 'F4T::ChatView::Open("ControllerChord")' in menu
        assert "for (DWORD i = 0; i < 4; ++i)" in menu and "xinputGetState(0," not in menu
        assert "F4T::MenuInput::SetChatActive(open)" in chat
        assert browser.index("F4T::ChatView::TryDispatchControllerInput") < browser.index("F4T::CharacterSelectView::TryDispatchControllerInput")
        assert "F4T::ChatView::SyncControllerActions();" in life


    def test_chat_view_has_onscreen_keyboard():
        js = read(UI / "ui/views/CommonwealthOnline/chat/chat.js")
        html = read(UI / "ui/views/CommonwealthOnline/chat/index.html")
        assert "window.handleControllerInput = handleControllerInput" in js
        assert "CO_Keyboard.open(elements.input)" in js
        assert "keyboard.js" in html

def _section_004_test_disconnect_screen_contract():
    """Contract moved verbatim from tests/audit/test_disconnect_screen_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_disconnect_screen_contract.py")
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
        assert join.index("g_joinInProgress.exchange(true)") < join.index("g_voluntaryExitRequested.store(false);")
        clear = local.index("g_voluntaryExitRequested.exchange(false)")
        connect = local.index("const bool connected = F4T::Networking::ConnectToLocalServer(error);")
        assert clear < connect, "a local connect must clear the flag before the retry loop reads it"
        settle = "g_voluntaryExitRequested.store(!connected && (wasVoluntaryExit || g_voluntaryExitRequested.load()));"
        assert settle in local, (
            "a local connect must clear the flag on success, even if a disconnect landed during it, "
            "and put back the flag it found on failure, so the session-ended screen is unchanged"
        )
        assert connect < local.index(settle), "the flag must be settled after the connect attempt"
        assert "return connected;" in local
        assert "if (!connected) { CopyError(a_errorBuf, a_errorBufSize, error); }" in local, (
            "the error buffer must be written only on failure, so the success path leaves it untouched"
        )


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


    if True:
        _merge_exit(main())

def _section_005_test_dpad_gaps_contract():
    """Contract moved verbatim from tests/audit/test_dpad_gaps_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_dpad_gaps_contract.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    VIEWS = UI / "ui/views/CommonwealthOnline"
    SRC = UI / "src"
    INCLUDE = UI / "include"

    PASSTHROUGH_VIEWS = {
        "F4TAdminView.cpp": ("void SyncEscapeOwnership()", "SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());"),
        "F4TFactionView.cpp": ("void SyncEscapeOwnership()", "SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());"),
        "F4TQuestView.cpp": ("void SyncEscapeOwnership()", "SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());"),
        "F4TTradeView.cpp": ("void SyncEscapeOwnership()", "SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());"),
        "F4TInteractWheelView.cpp": ("void SyncEscapeOwnership()", "SyncPassthroughControllerActions(g_viewState, g_passthrough, g_viewState.IsCurrentlyVisible());"),
        "F4TSleepView.cpp": ("void SyncEscapeOwnership()", "SyncPassthroughControllerActions(g_view, g_passthrough, g_view.IsCurrentlyVisible());"),
        "F4TInfoViews.cpp": ("void SyncEscapeOwnership(Window& a_window)", "SyncPassthroughControllerActions(a_window.state, a_window.passthrough, a_window.state.IsCurrentlyVisible());"),
    }


    def read(path):
        return path.read_text(encoding="utf-8")


    def source(name):
        return read(SRC / name)


    def function_body(code, signature):
        start = code.find(signature)
        if start < 0:
            return ""
        depth = 0
        for index in range(code.index("{", start), len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    return code[start:index + 1]
        return ""


    def main():
        for owner, (signature, call) in PASSTHROUGH_VIEWS.items():
            assert call in function_body(source(owner), signature), (
                f"{owner} {signature} must sync the controller passthrough on show, hide and DOM-ready"
            )

        lifecycle = source("F4TPrismaUI_Lifecycle.cpp")
        assert "SyncMainMenuControllerActions();" in lifecycle, (
            "the frame poll must sync the main menu controller passthrough bindings"
        )
        menu = source("F4TPrismaUI_Menu.cpp")
        main_menu_sync = function_body(menu, "void SyncMainMenuControllerActions()")
        assert "g_browserCurrentlyVisible" in main_menu_sync, (
            "the main menu passthrough must yield to the embedded browser's bindings on the shared view"
        )
        assert "SyncPassthroughControllerActions(g_mainMenuState, g_mainMenuPassthrough, g_mainMenuState.IsCurrentlyVisible());" in main_menu_sync, (
            "the main menu passthrough must clear when the main menu view hides"
        )
        assert "void SyncMainMenuControllerActions();" in source("F4TPrismaUIInternal.h"), (
            "SyncMainMenuControllerActions must be declared in the PrismaUI internal header"
        )

        for path in list(SRC.glob("*.cpp")) + list(SRC.glob("*.h")) + list(INCLUDE.glob("*.h")):
            text = read(path)
            assert "ForwardThumbstickEvent" not in text and "ThumbstickDirectionToButton" not in text, (
                f"{path.name} must not forward the engine thumbstick event; the XInput poll steps the stick once"
            )

        menu_input = source("F4TMenuInput.cpp")
        for constant in (
            "constexpr std::size_t kDpadButtonCount = 4;",
            "constexpr auto kStickInitialRepeatDelay = std::chrono::milliseconds(500);",
            "constexpr auto kStickRepeatInterval = std::chrono::milliseconds(180);",
        ):
            assert constant in menu_input, f"F4TMenuInput.cpp must declare {constant}"
        assert "i < kDpadButtonCount && pollTime - g_buttonRepeatTime[i] >= kStickInitialRepeatDelay" in menu_input, (
            "only the D-pad buttons may repeat, after the initial delay"
        )
        assert "g_buttonRepeatTime[i] = pollTime - (kStickInitialRepeatDelay - kStickRepeatInterval);" in menu_input, (
            "a held D-pad must repeat at the repeat interval after the first repeat"
        )

        report = read(VIEWS / "report/report.js")
        report_input = function_body(report, "function handleControllerInput(button, phase)")
        for button in ("dpad_up", "dpad_down", "dpad_left", "dpad_right", "a", "b"):
            assert f'button === "{button}"' in report_input, f"report handleControllerInput must handle {button}"
        assert 'select[data-field]' in report and '[data-action="submit"]' in report, (
            "report focus must move over the selects and the Send button"
        )
        assert 'new Event("change", { bubbles: true })' in report, (
            "report select cycling must dispatch a bubbling change event"
        )

        tabs = read(VIEWS / "faction/faction.tabs.js")
        assert re.search(r'var SELECTOR = "[^"]*\bselect:not\(\[disabled\]\)', tabs), (
            "the faction tab focus selector must include enabled selects"
        )
        faction = read(VIEWS / "faction/faction.js")
        faction_input = function_body(faction, "function handleControllerInput(button, phase)")
        assert 'button === "dpad_left"' in faction_input and 'button === "dpad_right"' in faction_input, (
            "the faction controller must cycle selects with dpad_left and dpad_right"
        )
        assert 'new Event("change", { bubbles: true })' in function_body(faction, "function cycleSelect(delta)"), (
            "faction select cycling must dispatch a bubbling change event"
        )

        main_menu = read(VIEWS / "main-menu/script.js")
        assert re.search(r'case "dpad_left":\s*moveQuitConfirmSelection\(-1\);', main_menu), (
            "the quit confirm must move left with dpad_left"
        )
        assert re.search(r'case "dpad_right":\s*moveQuitConfirmSelection\(1\);', main_menu), (
            "the quit confirm must move right with dpad_right"
        )

        pause = read(VIEWS / "pause-menu/script.js")
        assert re.search(r'if \(viewMode === "map"\) \{\s*if \(button\.indexOf\("dpad_"\) === 0 && isPadStickActive\(\)\) \{\s*return;', pause), (
            "map mode must ignore D-pad events while a stick is moved"
        )
        assert "isPadStickActive()" in function_body(pause, "window.handleControllerAxes = function"), (
            "the stick loop must use the same stick check as the map D-pad guard"
        )

        print("D-pad gaps contract passed")


    def test_dpad_gaps_contract():
        main()


    if True:
        main()

def _section_006_test_escape_ownership_contract():
    """Contract moved verbatim from tests/audit/test_escape_ownership_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_escape_ownership_contract.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    VIEWS = UI / "ui/views/CommonwealthOnline"
    SRC = UI / "src"

    OWNERS = {
        "admin": "F4TAdminView.cpp",
        "browser": "F4TPrismaUI_Browser.cpp",
        "chat": "F4TChatView.cpp",
        "faction": "F4TFactionView.cpp",
        "help": "F4TInfoViews.cpp",
        "interact-wheel": "F4TInteractWheelView.cpp",
        "main-menu": "F4TPrismaUI_MainMenu.cpp",
        "parked-suit": "F4TParkedSuitView.cpp",
        "pause-menu": "F4TPrismaUI_Menu.cpp",
        "quests": "F4TQuestView.cpp",
        "report": "F4TInfoViews.cpp",
        "server": "F4TInfoViews.cpp",
        "settings": "F4TPrismaUI_MainMenu.cpp",
        "sleep": "F4TSleepView.cpp",
        "trade": "F4TTradeView.cpp",
    }
    UNGUARDED_OWNERS = {"F4TPrismaUI_Browser.cpp", "F4TChatView.cpp", "F4TPrismaUI_MainMenu.cpp"}
    HANDOFFS = {
        "F4TAdminView.cpp": ("void SuspendPauseFocus()", "void ResumePauseFocus()"),
        "F4TFactionView.cpp": ("void SuspendPauseFocus()", "void ResumePauseFocus()"),
        "F4TQuestView.cpp": ("void SuspendPauseFocus()", "void ResumePauseFocus()"),
        "F4TInfoViews.cpp": ("void SuspendPauseFocus(Window& a_window)", "void ResumePauseFocus(Window& a_window)"),
    }
    GUARDED_CLAIM = re.compile(
        r"SetViewOwnsEscape\(\s*\w+\s*,\s*([\w.]+)\.IsDesiredVisible\(\)\s*&&\s*\1\.IsCurrentlyFocused\(\)\s*\)"
    )


    def source(name):
        return (SRC / name).read_text(encoding="utf-8")


    def function_body(code, signature):
        start = code.find(signature)
        if start < 0:
            return ""
        depth = 0
        for index in range(code.index("{", start), len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    return code[start:index + 1]
        return ""


    def escape_views():
        return sorted({
            path.parent.name
            for path in VIEWS.glob("*/*.js")
            if not path.name.endswith(".test.js") and '"Escape"' in path.read_text(encoding="utf-8")
        })


    def main():
        views = escape_views()
        unmapped = [view for view in views if view not in OWNERS]
        assert not unmapped, f"views whose JS handles Escape must map to their owning C++ file: {unmapped}"
        for owner in sorted(set(OWNERS.values()) - UNGUARDED_OWNERS):
            assert GUARDED_CLAIM.search(source(owner)), (
                f"{owner} must claim Escape with SetViewOwnsEscape(view, IsDesiredVisible() && IsCurrentlyFocused())"
            )

        admin = source("F4TAdminView.cpp")
        assert GUARDED_CLAIM.search(function_body(admin, "void SyncEscapeOwnership()")), (
            "the Admin view must own Escape only while it is desired-visible and focused"
        )
        assert "SyncEscapeOwnership();" in function_body(admin, "void ApplyWindowState("), (
            "applying the Admin view state must sync Escape ownership"
        )
        assert "SyncEscapeOwnership();" in function_body(admin, "void CloseInternal("), (
            "closing the Admin view must release Escape ownership"
        )
        assert "SyncEscapeOwnership();" in function_body(admin, "void FinishDomReady("), (
            "the Admin DOM-ready handler must sync Escape ownership"
        )

        menu = source("F4TPrismaUI_Menu.cpp")
        assert GUARDED_CLAIM.search(function_body(menu, "void SyncPauseMenuEscapeOwnership()")), (
            "the pause menu must own Escape only while it is desired-visible and focused"
        )
        assert "SyncPauseMenuEscapeOwnership();" in function_body(menu, "bool ApplyPauseMenuState("), (
            "applying the pause menu state must sync Escape ownership"
        )
        assert "SyncPauseMenuEscapeOwnership();" in function_body(menu, "void RequestResumeFromPauseMenu()"), (
            "resuming from the pause menu must release Escape ownership"
        )
        assert "SyncPauseMenuEscapeOwnership();" in function_body(source("F4TPrismaUI_Cursor.cpp"), "void ApplyMenuFocusForPointerMode("), (
            "pointer-mode focus changes must sync pause Escape ownership"
        )
        assert GUARDED_CLAIM.search(function_body(menu, "void SyncMainMenuEscapeOwnership()")), (
            "the main menu must own Escape only while it is desired-visible and focused"
        )
        main_menu_apply = function_body(source("F4TPrismaUI_Browser.cpp"), "bool ApplyMainMenuState(")
        assert "SyncMainMenuEscapeOwnership();" in main_menu_apply, (
            "applying the main menu state must sync Escape ownership so settings and quit confirm receive Escape"
        )
        assert "SyncMainMenuEscapeOwnership();" in function_body(source("F4TPrismaUI_Browser.cpp"), "void ClearBrowserControllerActions()"), (
            "leaving the browser must hand Escape back to the main menu, not drop it"
        )
        assert "SetViewOwnsEscape(g_browserControllerView, false)" not in source("F4TPrismaUI_Browser.cpp"), (
            "leaving the browser must not release Escape while the main menu is still focused"
        )
        assert "SyncMainMenuEscapeOwnership();" in function_body(source("F4TPrismaUI_Cursor.cpp"), "void ApplyMenuFocusForPointerMode("), (
            "pointer-mode focus changes must sync main menu Escape ownership"
        )
        for js in VIEWS.glob("**/*.js"):
            assert "Escape keydown" not in js.read_text(encoding="utf-8"), f"{js.name} still carries the Escape debug log"
        for owner, signatures in HANDOFFS.items():
            for signature in signatures:
                assert "SyncPauseMenuEscapeOwnership();" in function_body(source(owner), signature), (
                    f"{owner} {signature} must sync pause Escape ownership when pause focus is handed off or resumed"
                )

        cursor = source("F4TPrismaUI_Cursor.cpp")
        assert "if (g_browserCurrentlyVisible || active == &g_pauseMenuState) {" in function_body(cursor, "void ApplyMenuFocusForPointerMode("), (
            "gamepad pointer mode must keep the pause menu focused so keyboard Escape steps back"
        )
        assert "SyncPauseControllerActions();" in source("F4TPrismaUI_Lifecycle.cpp"), (
            "the frame poll must sync the pause controller passthrough bindings"
        )
        assert "SyncPauseControllerActions();" not in function_body(menu, "bool ApplyPauseMenuState("), (
            "the pause passthrough bindings are synced per frame, not on apply"
        )
        assert 'kPassthroughControllerButtons[] = { "A", "B", "X", "Y", "LB", "RB", "DUp", "DDown", "DLeft", "DRight" }' in menu, (
            "passthrough must bind face, bumper and D-pad buttons only; the stick axes stay free for map pan"
        )
        passthrough = function_body(menu, "void SyncPassthroughControllerActions(")
        assert "BindControllerAction(" in passthrough and "GetControllerActionBridgeState(" in passthrough and "ClearControllerActions(" in passthrough, (
            "passthrough bindings must be bound, checked against the bridge state and cleared"
        )
        assert "g_pauseMenuState.IsCurrentlyVisible()" in function_body(menu, "void SyncPauseControllerActions()"), (
            "the pause passthrough must clear when the pause view hides"
        )

        charselect = source("F4TCharacterSelectView.cpp")
        assert "SyncPassthroughControllerActions(g_viewState, g_passthrough, IsCurrentlyVisible());" in function_body(charselect, "void Tick()"), (
            "the character select passthrough must sync each frame so synthetic Enter cannot double-activate"
        )
        assert "window.handleControllerInput" in function_body(charselect, "bool TryDispatchControllerInput("), (
            "the character select must forward controller input to window.handleControllerInput"
        )
        assert "SetCharacterSelectActive(true);" in function_body(charselect, "void OpenInternal("), (
            "opening character select must route XInput to it"
        )
        assert "SetCharacterSelectActive(false);" in function_body(charselect, "void CloseInternal("), (
            "closing character select must release XInput routing"
        )
        browser = source("F4TPrismaUI_Browser.cpp")
        assert "F4T::CharacterSelectView::TryDispatchControllerInput(a_button, a_phase)" in function_body(browser, "void DispatchPauseMenuControllerInputInternal("), (
            "the pause controller dispatch must route character select input"
        )
        menu_input = source("F4TMenuInput.cpp")
        assert "g_characterSelectActive.load()" in function_body(menu_input, "bool AnyMenuActive()"), (
            "character select must count as an active menu for XInput polling"
        )
        assert "g_characterSelectActive.load()" in function_body(menu_input, "void ForwardButtonPhase("), (
            "character select button presses must be forwarded to the pause dispatch chain"
        )
        assert "g_characterSelectActive.store(a_active);" in function_body(menu_input, "void SetCharacterSelectActive("), (
            "SetCharacterSelectActive must record the active state"
        )

        select_js = (VIEWS / "character-select/character-select.js").read_text(encoding="utf-8")
        assert "function handleControllerInput(button, phase)" in select_js and "window.handleControllerInput = handleControllerInput;" in select_js, (
            "character select must define and expose handleControllerInput"
        )
        for button in ('button === "dpad_up"', 'button === "dpad_down"', 'button === "a"', 'button === "b"'):
            assert button in select_js, f"character select handleControllerInput must handle {button}"

        print("Escape ownership contract passed")


    def test_escape_ownership_contract():
        main()


    if True:
        main()


_SECTIONS = [
    ("tests/audit/test_browser_text_focus_contract.py", _section_001_test_browser_text_focus_contract),
    ("tests/audit/test_alert_controller_answers.py", _section_002_test_alert_controller_answers),
    ("tests/audit/test_chat_controller.py", _section_003_test_chat_controller),
    ("tests/audit/test_disconnect_screen_contract.py", _section_004_test_disconnect_screen_contract),
    ("tests/audit/test_dpad_gaps_contract.py", _section_005_test_dpad_gaps_contract),
    ("tests/audit/test_escape_ownership_contract.py", _section_006_test_escape_ownership_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_ui_browser_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
