#!/usr/bin/env python3
"""F4TUiToolingContracts: consolidated contract sections.

Consolidated from 5 single-contract scripts so the group runs one process
instead of 5. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TUiToolingContracts.py
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


def _section_001_f4tadminwindowcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TAdminWindowContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TAdminWindowContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def read_code(relative):
        return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


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


    def in_order(text, *needles):
        positions = []
        for needle in needles:
            index = text.find(needle)
            if index < 0:
                return False
            positions.append(index)
        return positions == sorted(positions)


    admin_header = (PLUGIN / "include/F4TAdminClient.h").read_text(encoding="utf-8")
    admin_raw = (PLUGIN / "src/F4TAdminClient.cpp").read_text(encoding="utf-8")
    admin = read_code("src/F4TAdminClient.cpp")
    inbound = read_code("src/F4TNetworkingInbound.cpp")
    networking = read_code("src/F4TNetworking.cpp")
    ui_client = read_code("src/F4TUIClient.cpp")
    ui_client_header = read_code("include/F4TUIClient.h")
    main_source = read_code("src/main.cpp")
    ui_api = (PLUGIN / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
    layout_tests = (PLUGIN / "tests/F4TUIApiLayoutTests.cpp").read_text(encoding="utf-8")

    check('#include "F4TAdminClient.h"' in admin_raw, "F4TAdminClient.cpp must include its header")
    check("RE::" not in admin, "F4TAdminClient.cpp must not touch the engine")
    check("//" not in admin_raw and "/*" not in admin_raw, "F4TAdminClient.cpp must carry no comments")
    for name in ("Attach", "TryHandlePacket", "Reset", "BuildStateJson", "HandleAction"):
        check(re.search(rf"\b{name}\(", admin_header) is not None, f"F4TAdminClient.h must declare {name}")

    inbound_packet_line = function_body(inbound, "void HandlePacketLine(")
    check(re.search(r"Factions::TryHandlePacket\(\*packetType, packet\) \|\| F4T::Admin::TryHandlePacket\(\*packetType, packet\)", inbound) is not None,
          "the admin inbound dispatch must share the factions TryHandlePacket return in HandlePacketLine")
    check(inbound.count("Admin::TryHandlePacket(") == 1, "the admin inbound dispatch must appear exactly once")
    check(in_order(inbound_packet_line, "Factions::TryHandlePacket(", "Admin::TryHandlePacket(", '*packetType == "welcome"'),
          "the admin inbound dispatch must run after the factions dispatch and before the packet chain")
    check('#include "F4TAdminClient.h"' in ui_client_header, "F4TUIClient.h must include F4TAdminClient.h so the inbound and UI client units see the admin module")

    try_handle = function_body(admin, "bool TryHandlePacket(")
    for packet_type in ("adminState", "adminResult"):
        check(f'"{packet_type}"' in try_handle, f"TryHandlePacket must route {packet_type}")
    check("return false;" in try_handle, "TryHandlePacket must decline unknown packet types")

    action = function_body(admin, "void HandleAction(")
    check("SendPacketBytes(" not in action and "SendAdminAction(" in action,
          "HandleAction runs on the UI thread and must hand its send to SendAdminAction, never send directly")
    send_action = function_body(admin, "void SendAdminAction(")
    check("QueuePacket(" in send_action and "SendPacketBytes(" not in send_action,
          "SendAdminAction must queue its packet, never send it directly")
    check("std::ranges::contains(g_actions, a_action)" in send_action,
          "SendAdminAction must only send an action that the latest adminState listed")
    check('"adminAction"' in send_action and '"requestId"' in send_action, "SendAdminAction must build an adminAction packet with a requestId")
    queue_packet = function_body(admin, "void QueuePacket(")
    check("QueueGameTask(" in queue_packet and "SendPacketBytes(" in queue_packet and "IsConnectedToServer()" in queue_packet,
          "QueuePacket must send from the game task, after the connection check")
    check(admin.count("SendPacketBytes(") == 1, "SendPacketBytes must be reached only through QueuePacket")
    check("F4SE::GetTaskInterface()" in admin and "AddTask(" in admin, "game-thread work must go through the F4SE task interface")

    check("kMaximumActionJsonBytes = 1024" in admin, "the admin action size cap must be 1024 bytes")
    check("a_actionJson.size() > kMaximumActionJsonBytes" in action, "HandleAction must enforce the size cap")
    check('op == "refresh"' in action and 'SendAdminAction(std::string{ kPlayersAction }' in action,
          "refresh must send the players query through the same gated path")

    mutating_actions = (
        "kick", "ban", "unban", "profile_unban", "revive", "kill", "voice_mute", "voice_unmute",
        "world_time", "world_weather", "world_mode", "faction_disband", "faction_promote", "faction_kick",
        "ensure", "stop", "approve", "quest_mode", "shutdown", "identity_reset", "admin_grant", "admin_revoke",
    )
    for name in mutating_actions:
        check(f'"{name}"' not in admin_raw, f"the admin module must not hardcode the {name} action")

    check("kQueryActions{ \"status\", \"clients\", \"bans\", \"admin_list\", \"factions\", \"resources\", \"map_public\", \"reports\" }" in admin_raw,
          "the query actions whose data is cached must be status, clients, bans, admin_list, factions, resources, map_public and reports")
    check("kMaximumResults = 20" in admin_raw, "the result ring must keep 20 entries")

    state_json = function_body(admin, "std::string BuildStateJson(")
    for key in ("connected", "isAdmin", "serverTime", "actions", "menus", "players", "data", "results"):
        check(f'"{key}"' in state_json, f"BuildStateJson must emit the {key} key")
    result_handler = function_body(admin, "void HandleResultPacket(")
    for key in ("requestId", "action", "ok", "message", "data"):
        check(f'"{key}"' in result_handler, f"adminResult handling must keep the {key} field")
    check('"players"' in admin_raw and "ParsePlayers(" in result_handler, "the players result must be parsed into the players list")
    check("if (!resultOk || (!playersAction && !queryAction)) {" in result_handler, "ok query and players results must stay out of the result list")

    state_handler = function_body(admin, "void HandleStatePacket(")
    check("QueueStateChanged();" in state_handler and "g_api" not in state_handler,
          "adminState handling must queue the UI notification and never call the UI from the receive thread")
    check("QueueStateChanged();" in result_handler and "g_api" not in result_handler,
          "adminResult handling must queue the UI notification and never call the UI from the receive thread")
    notify_window = function_body(admin, "void NotifyWindow(")
    check("SetAdminAvailable(" in notify_window and "NotifyAdminStateChanged(" in notify_window,
          "NotifyWindow must set availability and notify the window")
    check(admin.count("SetAdminAvailable(") == 1, "SetAdminAvailable must be called only from the game-task NotifyWindow")

    reset_body = function_body(admin, "void Reset(")
    check("ClearLocked();" in reset_body and "QueueStateChanged();" in reset_body,
          "Reset must clear the admin state and queue the availability update")

    disconnect = function_body(networking, "void DisconnectFromLocalServer()")
    check("F4T::Admin::Reset();" in disconnect, "DisconnectFromLocalServer must call Admin::Reset")
    check(in_order(disconnect, "ClearRemotePlayers();", "F4T::Admin::Reset();"),
          "Admin::Reset must run after the remote players are cleared on disconnect")
    check(networking.count("F4T::Admin::Reset();") == 2, "both networking disconnect paths must call Admin::Reset")
    transport = function_body(ui_client, "void NotifyTransportDisconnected(")
    check("F4T::Admin::Reset();" in transport, "NotifyTransportDisconnected must call Admin::Reset")

    attach = function_body(admin, "void Attach()")
    check(re.search(r"RequestPluginAPI<\s*COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI18>\(\)", attach) is not None,
          "Attach must acquire the UI V18 interface")
    check("g_api->SetAdminCallbacks(&kAdminCallbacks);" in attach, "Attach must register the admin callbacks with SetAdminCallbacks")
    check("kAdminCallbacks{\n\t\t\tCallback_GetStateJson,\n\t\t\tCallback_SendAction\n\t\t};" in admin,
          "kAdminCallbacks must hold GetStateJson then SendAction")
    check(in_order(main_source, "F4T::QuestUI::Attach();", "F4T::Admin::Attach();", "F4T::StateBagSync::Attach();"),
          "main.cpp must attach the admin module in kGameDataReady next to QuestUI::Attach")
    check(main_source.count("F4T::Admin::Attach();") == 1, "main.cpp must call Admin::Attach exactly once")

    callbacks_get = function_body(admin, "bool Callback_GetStateJson(")
    check("BuildStateJson()" in callbacks_get and "exceeds output buffer" in callbacks_get and "catch (...)" in callbacks_get,
          "the state callback must build the state, refuse a short buffer and catch failures")
    callbacks_send = function_body(admin, "void Callback_SendAction(")
    check("HandleAction(a_actionJson)" in callbacks_send and "catch (...)" in callbacks_send,
          "the send callback must route to HandleAction inside a try block")

    v17 = re.search(r"struct IVCommonwealthOnlineUI18\s*\{(.*?)\n\t\};", ui_api, re.S)
    check(v17 is not None, "the API copy must declare IVCommonwealthOnlineUI18")
    if v17 is not None:
        pointers = re.findall(r"\(\*(\w+)\)", v17.group(1))
        check(pointers == ["SetAdminCallbacks", "NotifyAdminStateChanged", "OpenAdminWindow", "CloseAdminWindow", "IsAdminWindowVisible", "SetAdminAvailable"],
              f"IVCommonwealthOnlineUI18 must hold the admin pointers in order, got {pointers}")
    admin_callbacks = re.search(r"struct AdminCallbacks\s*\{(.*?)\n\t\};", ui_api, re.S)
    check(admin_callbacks is not None and re.findall(r"\b(GetStateJson|SendAction)\b", admin_callbacks.group(1)) == ["GetStateJson", "SendAction"],
          "AdminCallbacks must hold GetStateJson then SendAction")
    check(re.search(r"struct InterfaceVersionMap<IVCommonwealthOnlineUI18>\s*\{\s*static constexpr InterfaceVersion version = InterfaceVersion::V18;\s*static constexpr std::uint32_t\s+layout = 0x6F2D9C12;\s*\};", ui_api) is not None,
          "InterfaceVersionMap<IVCommonwealthOnlineUI18> must map to V18 with layout 0x6F2D9C12")
    check("CheckLayoutGate<IVCommonwealthOnlineUI18>();" in layout_tests, "the layout gate test must cover IVCommonwealthOnlineUI18")

    check("kMaximumStoredDataBytes = 16384" in admin_raw and "kMaximumStateBytes = 60000" in admin_raw,
          "the stored data cap must be 16384 bytes and the state cap 60000 bytes")
    check("bytes > kMaximumStoredDataBytes ? TruncatedMarker(bytes) : a_payload" in admin_raw,
          "BoundedPayload must replace an oversized payload with the truncated marker")
    fit_state = function_body(admin, "std::string FitStateJson(")
    check("while (text.size() > kMaximumStateBytes)" in fit_state and "LargestPayload(a_state)" in fit_state
          and "TruncatedMarker(" in fit_state and 'a_state["results"].erase(a_state["results"].begin());' in fit_state,
          "FitStateJson must truncate the largest payloads and drop the oldest results only when no payload is left, until the state fits")
    check(in_order(fit_state, "LargestPayload(a_state)", 'a_state["results"].erase('),
          "FitStateJson must try the payload truncation before dropping any result")
    check("dump(" not in state_json and re.search(r"\}\s*return FitStateJson\(std::move\(state\)\);", state_json) is not None
          and in_order(state_json, "scoped_lock", "return FitStateJson("),
          "BuildStateJson must copy the state under the lock, release it, then dump outside the lock")
    check(in_order(result_handler, "BoundedPayload(payload)", "scoped_lock"),
          "HandleResultPacket must bound the payload before taking the state lock")
    check('"Player list was invalid or too large."' in result_handler and "ParsePlayers(payload)" in result_handler
          and "Json::array()" in result_handler,
          "a players result that fails to parse must clear the roster and be marked not ok")
    check(in_order(attach, "SetAdminCallbacks(", "QueueStateChanged();"),
          "Attach must queue an initial window notification after registering callbacks, so an adminState received before Attach is shown")
    check(re.search(r"catch \(\.\.\.\) \{\s*try \{\s*F4T::DebugLogging::Log\(", callbacks_send) is not None,
          "the send callback must guard its own logging with a nested try, because the logger can throw")

    for relative in ("src/F4TAdminClient.cpp","include/F4TAdminClient.h", "src/F4TUIClient.cpp", "src/F4TNetworkingInbound.cpp", "src/F4TNetworking.cpp", "src/main.cpp", "include/F4TUIClient.h"):
        limit = 775 if relative == "src/F4TNetworkingInbound.cpp" else 750
        check(len((PLUGIN / relative).read_text(encoding="utf-8").splitlines()) < limit, f"{relative} must stay under {limit} lines")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TAdminWindowContract: all checks passed.")

def _section_002_f4tchatcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TChatContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TChatContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    def read_workflow(name):
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def read_code(relative):
        return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


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


    def in_order(text, *needles):
        positions = []
        for needle in needles:
            index = text.find(needle)
            if index < 0:
                return False
            positions.append(index)
        return positions == sorted(positions)


    xmake = (PLUGIN / "xmake.lua").read_text(encoding="utf-8")
    chat_header = (PLUGIN / "include/F4TChat.h").read_text(encoding="utf-8")
    chat_raw = (PLUGIN / "src/F4TChat.cpp").read_text(encoding="utf-8")
    chat = read_code("src/F4TChat.cpp")
    inbound = read_code("src/F4TNetworkingInbound.cpp")
    networking = read_code("src/F4TNetworking.cpp")
    ui_client = read_code("src/F4TUIClient.cpp")
    ui_api = (PLUGIN / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
    ui_api_copy = (ROOT / "CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
    ci_workflow = read_workflow("co-beta-ci.yml")


    check("F4T_PLAYER_CHAT" not in chat_header and "F4T_PLAYER_CHAT" not in chat_raw,
          "the player chat module must carry no F4T_PLAYER_CHAT switch")
    check('#include "F4TChat.h"' in chat_raw, "F4TChat.cpp must include its header")
    check("RE::" not in chat, "F4TChat.cpp must not touch the engine")

    for target_name in ("CommonwealthOnline",):
        block = re.search(rf'target\("{target_name}"\)(.*?)(?=\ntarget\(|\Z)', xmake, re.S).group(1)
        check("F4T_PLAYER_CHAT" not in block and 'remove_files("src/F4TChat.cpp")' not in block and "co_test_features" not in block,
              f"{target_name} must compile F4TChat.cpp unconditionally")

    inbound_welcome = function_body(inbound, "void HandleWelcomePacket(")
    check("F4T::Chat::NoteWelcome(a_packet);" in inbound and in_order(inbound_welcome, "SetAssignedPlayerId(", "Chat::NoteWelcome("),
          "HandleWelcomePacket must pass the welcome to Chat::NoteWelcome after the player id is set")

    packet_line = function_body(inbound, "void HandlePacketLine(")
    check(re.search(r"if \(F4T::Chat::TryHandleChatPacket\(\*packetType, packet\)\) \{\s*return;\s*\}", inbound) is not None,
          "the chat inbound dispatch must run in HandlePacketLine")
    check(inbound.count("TryHandleChatPacket(") == 1, "the chat inbound dispatch must appear exactly once")
    check(in_order(packet_line, "TryHandleChatPacket(", '*packetType == "welcome"'),
          "the chat inbound dispatch must run before the packet chain")

    try_handle = function_body(chat, "bool TryHandleChatPacket(")
    for packet_type in ("chatMessage", "chatHistory", "chatRejected"):
        check(f'"{packet_type}"' in try_handle, f"TryHandleChatPacket must route {packet_type}")
    check("IsEnabled()" in try_handle, "TryHandleChatPacket must drop chat packets while chat is disabled")
    for kind in ("message", "history", "rejected", "state"):
        check(f'{{ "kind", "{kind}" }}' in chat or f'"kind", "{kind}"' in chat, f"chat events must carry kind {kind}")
    for key in ("enabled", "localPlayerId", "message", "messages", "reason"):
        check(f'"{key}"' in chat, f"chat events must carry the {key} key")

    parse_message = function_body(chat, "std::optional<Json> ParseMessage(")
    for needle in ("ReadString(", "ReadPlayerId(", "ReadDouble(", "IsBoundedText(", "is_number_unsigned()"):
        check(needle in parse_message, f"ParseMessage must validate with {needle}")
    check("kMaximumNameBytes" in parse_message and "kMaximumTextBytes" in parse_message,
          "ParseMessage must bound fromName and text")
    check("kMaximumTextBytes = 1024" in chat and "kMaximumNameBytes = 128" in chat,
          "the chat limits must be 1024 text bytes and 128 name bytes")
    history = function_body(chat, "void HandleHistoryPacket(")
    check("kMaximumHistoryMessages" in history and "ParseMessage(messageJson)" in history and "return;" in history,
          "HandleHistoryPacket must cap the history length and drop the whole packet on a malformed entry")

    rejected = function_body(chat, "void HandleRejectedPacket(")
    for reason in ("invalidText", "tooLong", "rateLimited", "channelUnavailable", "muted", "duplicate", "notInFaction"):
        check(f'"{reason}"' in chat, f"the chat rejection reasons must include {reason}")
    check("kRejectReasons.end()" in rejected, "HandleRejectedPacket must check the reason against the known list")

    queue_event = function_body(chat, "void QueueEvent(")
    check("QueueGameTask(" in queue_event and "PushChatEvent(" in queue_event,
          "QueueEvent must push chat events through the UI V3 PushChatEvent from the game task")
    check("kMaximumEventBytes = 32 * 1024" in chat and "payload.size() > kMaximumEventBytes" in queue_event,
          "QueueEvent must drop payloads larger than the UI 32 KiB limit")
    queue_packet = function_body(chat, "void QueuePacket(")
    check("QueueGameTask(" in queue_packet and "SendPacketBytes(" in queue_packet,
          "QueuePacket must send from the game task")
    check("F4SE::GetTaskInterface()" in chat and "AddTask(" in chat,
          "game-thread work must go through the F4SE task interface")
    check('"chatSend"' in chat and '{ "channel", *channel }' in chat, "the chat module must send chatSend with the channel the UI chose")
    check('kFactionChannel = "faction"' in chat and "IsKnownChannel(" in chat, "the chat module must accept the global and faction channels")
    check("!IsKnownChannel(*channel)" in function_body(chat, "void HandleAction("), "HandleAction must reject channels outside global and faction")
    check('"chatHistoryRequest"' in chat, "the chat module must send chatHistoryRequest")

    note_welcome = function_body(chat, "void NoteWelcome(")
    check('"chat-v1"' in chat and "std::any_of(" in note_welcome and "QueueState(supported)" in note_welcome,
          "NoteWelcome must read the chat-v1 capability and push the state")
    check("RequestHistory();" in note_welcome and "if (supported)" in note_welcome,
          "NoteWelcome must request history once when chat-v1 is present")

    reset = function_body(chat, "void Reset()")
    check("g_enabled = false;" in reset and "QueueState(false);" in reset,
          "Reset must clear chat and push the disabled state")
    check("QueueGameTask([]() { F4T::TextEntryMode::Reset(); });" in reset,
          "Reset must release text-entry mode so a dropped connection cannot leave game keybinds held")

    request = function_body(chat, "void RequestHistory()")
    check(in_order(request, "TakeHistoryRequestSlot()", '"chatHistoryRequest"'),
          "RequestHistory must take the throttle slot before sending chatHistoryRequest")
    slot = function_body(chat, "bool TakeHistoryRequestSlot()")
    check("g_lastHistoryRequest && now - *g_lastHistoryRequest < kHistoryRequestWindow" in slot,
          "TakeHistoryRequestSlot must refuse a second request inside the window")
    check("kHistoryRequestWindow = std::chrono::seconds{ 2 }" in chat,
          "the history request window must be two seconds")

    action = function_body(chat, "void HandleAction(")
    check("IsEnabled()" in action and "IsConnectedToServer()" in action,
          "HandleAction must ignore actions when chat is disabled or not connected")
    check(in_order(action, "Json::parse(", "IsConnectedToServer()", "QueuePacket("),
          "HandleAction must parse, then check the connection gate before it queues the send; only the focus op runs ahead of the gate")
    check(re.search(r'if \(op == "send"\) \{\s*QueueRejected\(IsEnabled\(\) \? "notConnected" : "chatUnavailable"\);', action) is not None,
          "HandleAction must answer a refused send with notConnected (enabled) or chatUnavailable (disabled)")
    check("nullptr, false)" in action, "HandleAction must parse the action without throwing")
    check('"invalidText"' in action and '"tooLong"' in action and "QueueRejected(" in action,
          "HandleAction must reject empty and over-long text locally")
    check("RequestHistory();" in action and 'op == "history"' in action,
          "HandleAction must route the history op through the throttle")
    check("SendPacketBytes(" not in action, "HandleAction must not send directly from the UI callback thread")
    check("Ignoring a chat action with an unknown op." in action, "HandleAction must ignore unknown ops")

    check(re.search(r'if \(text->size\(\) > kMaximumTextBytes\) \{\s*QueueRejected\("tooLong"\);', action) is not None,
          "HandleAction must reject text over kMaximumTextBytes with tooLong")
    check(re.search(r'if \(!text \|\| text->empty\(\)\) \{\s*QueueRejected\("invalidText"\);', action) is not None,
          "HandleAction must reject empty text with invalidText")
    check(re.search(r'if \(packet\.dump\(\)\.size\(\) > kMaximumChatSendBytes\) \{\s*QueueRejected\("tooLong"\);', action) is not None
          and "kMaximumChatSendBytes = 4 * 1024" in chat,
          "HandleAction must drop a chatSend over the 4 KB cap and push tooLong")
    check("g_lastHistoryRequest = now;" in function_body(chat, "bool TakeHistoryRequestSlot()"),
          "TakeHistoryRequestSlot must record the request time")
    check('"chatHistoryRequest"' not in note_welcome,
          "NoteWelcome must not send chatHistoryRequest directly; it must go through RequestHistory")
    check(chat.count('"chatHistoryRequest"') == 1,
          "the chatHistoryRequest literal must appear exactly once, in RequestHistory")

    disconnect = function_body(networking, "void DisconnectFromLocalServer()")
    check(in_order(disconnect, "ClearRemotePlayers();", "F4T::Chat::Reset();"),
          "DisconnectFromLocalServer must reset chat after the remote players are cleared")
    check(networking.count("F4T::Chat::Reset();") == 2, "the connect path and the disconnect path must both reset chat")

    transport = function_body(ui_client, "void NotifyTransportDisconnected(")
    check(in_order(transport, "g_connectionOwnerToken.store(0);", "F4T::Chat::Reset();"),
          "NotifyTransportDisconnected must reset chat")

    on_ready = function_body(ui_client, "void OnGameDataReady()")
    check(re.search(r"auto\* uiChat = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<\s*COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>\(\);\s*if \(uiChat && uiChat->SetChatCallbacks\) \{\s*uiChat->SetChatCallbacks\(&kChatCallbacks\);\s*\}", ui_client) is not None
          and "kChatCallbacks" in ui_client,
          "OnGameDataReady must register the chat callbacks through SetChatCallbacks")
    send_action = function_body(ui_client, "void Callback_SendChatAction(")
    check("try {" in send_action and "catch (...)" in send_action and "F4T::Chat::HandleAction(" in send_action,
          "Callback_SendChatAction must route to the chat module and catch errors")
    check(re.search(r"void Callback_SendChatAction\(", ui_client) is not None,
          "Callback_SendChatAction must be defined")

    check(re.search(r"struct ChatCallbacks", ui_api) is not None
          and "using ChatActionFn = void (*)(const char* a_actionJson);" in ui_api
          and "ChatActionFn SendAction{ nullptr };" in ui_api,
          "the plugin API copy must declare ChatActionFn and ChatCallbacks")
    for copy_name, copy in (("plugin", ui_api), ("UI", ui_api_copy)):
        check("void (*SetChatCallbacks)(const ChatCallbacks* a_callbacks){ nullptr };" in copy
              and "void (*PushChatEvent)(const char* a_eventJson){ nullptr };" in copy,
              f"the {copy_name} API copy must declare SetChatCallbacks and PushChatEvent")

    check("scripts/run-python-tests.py" in ci_workflow and "--group plugin" in ci_workflow, "co-beta-ci.yml must run the plugin contract group")


    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TChatContract: all checks passed.")


def _section_005_f4tfriendswheelcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TFriendsWheelContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TFriendsWheelContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    UI = ROOT / "CommonwealthOnlineUI"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(relative):
        return (PLUGIN / relative).read_text(encoding="utf-8")


    def read_ui(relative):
        return (UI / relative).read_text(encoding="utf-8")


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


    friends_header = read("include/F4TFriends.h")
    friends = read("src/F4TFriends.cpp")
    wheel_header = read("include/F4TInteractWheel.h")
    wheel = read("src/F4TInteractWheel.cpp")
    runtime = read("src/F4TReplicationRuntime.cpp")
    inbound = read("src/F4TNetworkingInbound.cpp")
    ui_client = read("src/F4TUIClient.cpp")
    wheel_ui = read("src/F4TInteractWheelUi.cpp")
    ui_client_header = read("include/F4TUIClient.h")
    factions = read("src/F4TFactions.cpp")
    networking = read("src/F4TNetworking.cpp")
    downed = read("src/F4TDownedEngine.cpp") + read("src/F4TDownedEngineRecovery.cpp")
    voice_runtime = read("src/F4TVoiceRuntime.cpp")
    invite_offer = read("src/F4TFactionInviteOffer.cpp")

    api_ui = read_ui("include/CommonwealthOnlineUI_API.h")
    api_plugin = (PLUGIN / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
    plugin_api = read_ui("src/PluginAPI.cpp")
    wheel_view = read_ui("src/F4TInteractWheelView.cpp")
    wheel_view_header = read_ui("include/F4TInteractWheelView.h")
    browser = read_ui("src/F4TPrismaUI_Browser.cpp")
    cursor = read_ui("src/F4TPrismaUI_Cursor.cpp")
    lifecycle = read_ui("src/F4TPrismaUI_Lifecycle.cpp")
    menu = read_ui("src/F4TPrismaUI_Menu.cpp")
    chat = read_ui("src/F4TChatView.cpp")
    faction_view = read_ui("src/F4TFactionView.cpp")
    faction_html = read_ui("ui/views/CommonwealthOnline/faction/index.html")
    friends_js = read_ui("ui/views/CommonwealthOnline/faction/faction.friends.js")
    wheel_js = read_ui("ui/views/CommonwealthOnline/interact-wheel/wheel.js")

    check("enum class Relationship" in friends_header, "the client must expose the relationship the wheel needs")
    check("BuildStateJson" in friends_header, "friend state must reach the Social window")

    for packet_type in ("friendState", "friendRequestReceived", "friendResult"):
        check(f'"{packet_type}"' in friends, f"the client must handle the {packet_type} packet")
    check('{ "type", "friendRequest" }' in friends, "Add Friend must send friendRequest")
    check('{ "type", "friendRequestResponse" }' in friends, "Accept and Decline must send friendRequestResponse")
    check('{ "type", "friendRemove" }' in friends, "Remove must send friendRemove")
    check('{ "type", "friendListRequest" }' in friends, "the client must ask for the list on welcome")
    check("wants to add you as a friend" in friends, "the request alert must use the required wording")
    check("request.accept" not in friends and '"accept"' in friends, "the alert actions must accept and decline")
    check("durationMs" in friends, "the request alert must expire with the server lifetime")
    check("QueueKnownSetChanged" in friends, "an accepted friendship must refresh nameplates and voice keys")
    check("CommandLineToArgv" not in friends, "the friend client must not grow shell parsing")

    check("ServerSupports" in wheel, "friend slices must be gated on the friends capability")
    check("RelationshipWith" in wheel, "friend slices must follow the current relationship")
    check("IncomingRequestId" in wheel, "Accept Friend Request must use the request the server sent")
    check('Slice("requestSent", "Friend Request Sent", true)' in wheel, "a pending request must render disabled")
    check('Slice("trade", "Trade")' in wheel and "F4T::TradeClient::RequestTrade(target)" in wheel, "the Trade slice must send a real trade request")
    wheel_view_gate = function_body(wheel_view, "bool IsActionKnown(")
    for op in ("trade", "tradeAccept", "tradeDecline", "tradeConfirm", "tradeCancel"):
        check(f'"{op}"' in wheel_view_gate and f"{op}: true" in wheel_js, f"the wheel must pass {op} through the view gate and wheel.js")
    check('Slice("invite", "Invite to Faction")' in wheel, "Invite to Faction must be a wheel slice")
    check('op == "invite"' in wheel and '"invite"' in wheel, "the invite slice must send the existing invite op")
    check("localCanInvite" in wheel and "TargetInFaction" in wheel, "invite must require leader or officer and a player outside the faction")
    check('op == "mute" || op == "unmute"' in wheel, "the wheel must offer mute and unmute")
    check('"mutePlayer"' in wheel and "VoiceRuntime::SubmitAction" in wheel, "mute must reuse the existing mutePlayer op")
    check("IsPlayerMuted" in wheel, "the mute slice must read the current mute state")
    check('send("close")' in wheel_js and '"Escape"' in wheel_js, "Esc must close the wheel through the view")
    check('button === "b"' in wheel_js and 'send("close")' in wheel_js, "B must close the wheel through the view")
    check('\\"op\\":\\"close\\"' in wheel_view and "token" in wheel_view, "a view-side close must tell the plugin which open it ends")
    check("ViewGone" in wheel, "the wheel must close when the view closes itself")
    check("IsInteractWheelVisible" in ui_client_header, "the plugin must be able to read whether the wheel view is still open")
    check("TargetLost" in wheel, "the wheel must close when the target moves away, disconnects, or goes down")
    check("kActivationRange" in wheel, "the wheel must use the existing activation range")
    check("IsDowned" in wheel and "TryGetProxyActorForRemotePlayerId" in wheel, "a downed or missing target must close the wheel")
    check("MenuBlocksWheel" in wheel, "the wheel must not open over another menu")
    check("F4T::JoinFlow::IsActive()" in function_body(wheel, "bool CoWindowOpen()"),
          "the wheel must not open or stay open over the join overlay")
    check("TakeCancelPressed" not in downed and "kCancelUserEvent" not in downed, "the Cancel latch must stay gone; the view owns Esc and B")
    check("IsChatVisible" in wheel and "IsSlotScreenVisible" in wheel, "the wheel must not open over the chat window or the character-slot screen")
    check("IsFactionWindowVisible" in wheel and "IsQuestWindowVisible" in wheel, "the wheel must not open over the Social or Quest window")
    check("IsMainMenuVisible" in wheel and "IsBrowserVisible" in wheel, "the wheel must not open over the menu or browser")

    check("F4T::FactionInviteOffer::Tick();" in runtime, "the Activate entry point must stay wired")
    check("F4T::InteractWheel::Tick();" in runtime, "the wheel must be ticked every frame")
    check(runtime.index("F4T::FactionInviteOffer::Tick();") < runtime.index("F4T::InteractWheel::Tick();"),
          "the wheel tick must run after the Activate edge")
    check("ShowAlert" not in invite_offer, "Activate must open the wheel instead of the old invite alert")

    check("F4T::Friends::NoteWelcome(a_welcome)" in factions, "the welcome must be read for friends-v1")
    check("F4T::Friends::TryHandlePacket" in inbound, "friend packets must reach the friend client")
    check("F4T::Friends::Reset()" in networking, "a disconnect must clear friend state")
    check("F4T::Friends::HandleAction" in factions, "the Social window must route friend actions")
    check("IVCommonwealthOnlineUI17" in wheel_ui, "the plugin must request the wheel interface")
    check("InteractWheel::BuildStateJson" in wheel_ui, "the wheel view must pull its state through the plugin")
    check("OpenInteractWheel" in ui_client_header and "CloseInteractWheel" in ui_client_header, "the plugin must open and close the wheel")
    check("F4T::Friends::BuildStateJson" in factions, "the Social window state must carry friends")

    check("V17 = 17" in api_ui, "the API must gain V17 without changing V1 to V16")
    check("IVCommonwealthOnlineUI17" in api_ui and "IVCommonwealthOnlineUI17" in api_plugin, "both API headers must declare V17")
    check(api_ui == api_plugin, "the two API headers must stay identical")
    check("InterfaceVersionMap<IVCommonwealthOnlineUI17>" in api_ui, "V17 needs its own layout fingerprint")
    check("0xE5C2A71F" in api_ui, "the V17 layout must be unique")
    check("SetInteractWheelCallbacks" in plugin_api and "OpenInteractWheel" in plugin_api, "the UI plugin must export the wheel interface")
    check("case InterfaceVersion::V17:" in plugin_api, "the API dispatcher must serve V17")

    check("CommonwealthOnline/interact-wheel/index.html" in wheel_view, "the wheel view must load its page")
    check('BindUIEvent(a_view, "coInteractWheelAction"' in wheel_view, "the wheel view must bind its action event")
    check("IsActionKnown" in wheel_view, "the wheel view must gate unknown ops")

    menu_input = read_ui("src/F4TMenuInput.cpp")
    menu_input_header = read_ui("include/F4TMenuInput.h")
    check("void SetWheelActive(bool a_active);" in menu_input_header, "the wheel input gate must be declared")
    any_menu = function_body(menu_input, "bool AnyMenuActive()")
    check("g_wheelActive.load()" in any_menu, "the wheel must hold the menu gate open or no gamepad input arrives")
    check("g_wheelActive.load()" in menu_input.split("void ForwardButtonPhase")[1].split("void ")[0], "buttons must reach the wheel")
    check("g_wheelActive.load()" in function_body(menu_input, "void ForwardMovedAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY, bool a_axesActive)"), "axes must reach the wheel")
    stick_paths = menu_input.split("const auto* stickButton = StickAxesToButton")
    check(len(stick_paths) == 2 and "g_wheelActive.load()" in stick_paths[0][-500:],
          "the stick must not also emit dpad steps while the wheel selects by angle")
    check("ForwardThumbstickEvent" not in menu_input, "the engine thumbstick route is gone, so it cannot double-step the wheel")
    check("SetWheelActive(false)" in wheel_view.split("void CloseInternal")[1].split("void ")[0], "every view close must drop the input gate")
    check("SetWheelActive(true)" in wheel_view.split("void OpenInternal")[1].split("void Open(")[0], "opening the wheel must take the input gate")
    check("SetWheelActive" in wheel_view and menu_input.count("AnyMenuActive()") >= 4, "the widened gate must stay wired to the block release paths")
    escape_sync = function_body(wheel_view, "void SyncEscapeOwnership()")
    check("F4T::PrismaCapabilities::GetV12()" in escape_sync, "the wheel must claim Escape through the V12 capability")
    check(
        "api->SetViewOwnsEscape(handle, g_viewState.IsDesiredVisible() && g_viewState.IsCurrentlyFocused());" in escape_sync,
        "the wheel must own Escape only while it is desired-visible and focused",
    )
    check("SyncEscapeOwnership();" in function_body(wheel_view, "void OpenInternal("), "opening the wheel must claim Escape")
    check("SyncEscapeOwnership();" in function_body(wheel_view, "void FinishDomReady("), "the DOM-ready focus must claim Escape")
    faction_view = read_ui("src/F4TFactionView.cpp")
    faction_escape = function_body(faction_view, "void SyncEscapeOwnership()")
    check("api->SetViewOwnsEscape(handle, g_viewState.IsDesiredVisible() && g_viewState.IsCurrentlyFocused());" in faction_escape,
          "the Social window must own Escape while visible so its page can close on it")
    check("SyncEscapeOwnership();" in function_body(faction_view, "void CloseInternal("), "closing the Social window must release Escape")
    check("F4T::MenuInput::SetWorldPanelActive(true);" in function_body(faction_view, "void ApplyWindowState("), "the world Social window must take its input block only once it is visible")
    check("F4T::MenuInput::SetWorldPanelActive(false);" in function_body(faction_view, "void CloseInternal("), "closing the Social window must drop the world input block")
    check("SyncEscapeOwnership();" in function_body(wheel_view, "void CloseInternal("), "every view-side close must release Escape")
    check(
        "USER_EVENT_FLAG::kMovement" in menu_input and "USER_EVENT_FLAG::kLooking" in menu_input,
        "the wheel layer must stop movement and look, not only the menu capture",
    )
    wheel_toggle = function_body(menu_input, "void SetWheelActive(bool a_active)")
    check(
        "AcquireWheelInputBlock()" in wheel_toggle and "ReleaseWheelInputBlock()" in wheel_toggle,
        "the wheel must take its blocking layer on open and drop it on every close",
    )
    check("InteractWheelView::TryDispatchControllerInput" in browser, "controller input must reach the wheel")
    check("InteractWheelView::TryDispatchControllerAxes" in browser, "stick input must reach the wheel")
    check("TryDispatchControllerInput" in wheel_view_header and "TryDispatchControllerAxes" in wheel_view_header, "the wheel view must expose the input take-over")
    check("InteractWheelView::IsCurrentlyVisible()" in cursor, "the wheel must hide the native cursor")
    check("InteractWheelView::Initialize()" in lifecycle, "the wheel view must initialize with the other views")
    check("InteractWheelView::Close" in menu, "the wheel must close with the pause menu and lockout")
    check("InteractWheelView::IsVisible()" in chat, "chat must not open over the wheel")
    buffer_pin = re.search(r"kStateBufferSize = (\d+) \* (\d+);", faction_view)
    check(buffer_pin, "the Social window buffer must stay a literal size")
    if buffer_pin:
        buffer_bytes = int(buffer_pin.group(1)) * int(buffer_pin.group(2))
        # Constraint: 200 friends at the longest displayed name, worst-case JSON escaping, plus the capture budget.
        display_pin = re.search(r"kMaximumDisplayNameBytes = (\d+);", friends)
        check(display_pin, "the friend display name limit must stay a literal")
        name_bytes = int(display_pin.group(1)) if display_pin else 128
        required_bytes = 200 * (name_bytes * 6 + 64) + 64 * 1024
        check(buffer_bytes >= required_bytes, "the Social window buffer must fit 200 friends")

    check("faction.friends.js" in faction_html, "the Social window must load the Friends section")
    check("friendAccept" in friends_js and "friendRemove" in friends_js, "the Friends section must accept and remove")
    check("escape" in friends_js.lower() or "esc(" in friends_js, "the Friends section must escape names")
    check("does not support friends" in friends_js, "the Friends section must explain a server without friends-v1")
    check("sliceIndexForAxes" in wheel_js and "sliceIndexForAngle" in wheel_js, "slice selection must stay testable in wheel.js")
    check("coInteractWheelController" in wheel_js, "the wheel must accept controller input")

    gate = function_body(faction_view, "bool IsActionKnown(")
    friend_ops = sorted(set(re.findall(r'"(friend[A-Z]\w*)"', friends_js)))
    check(friend_ops, "the Friends section must send friend ops")
    for op in friend_ops:
        check(f'"{op}"' in gate, f"the Social window action gate must pass {op}")
    check("F4T::Friends::HandleAction" in factions, "the Social window must route friend ops to the friend client")

    for relative in ("include/F4TFriends.h", "src/F4TFriends.cpp", "include/F4TInteractWheel.h", "src/F4TInteractWheel.cpp", "include/F4TInteractWheelUi.h", "src/F4TInteractWheelUi.cpp"):
        raw = (PLUGIN / relative).read_bytes()
        check(b"\r\n" not in raw or raw.count(b"\n") == raw.count(b"\r\n"), f"{relative} must not mix CRLF and LF line endings")
        check(len(raw.splitlines()) <= 1000, f"{relative} must stay at or under 1000 lines")

    for relative in ("src/ServerFriends.cpp", "src/ServerFriends.h"):
        raw = (ROOT / "CO-SERVER/server-cpp" / relative).read_bytes()
        check(len(raw.splitlines()) <= 1000, f"{relative} must stay at or under 1000 lines")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TFriendsWheelContract: all checks passed")

def _section_006_f4togpluginquerycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TOGPluginQueryContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TOGPluginQueryContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    templates = {
        "plugin": root / "CO-BETA/plugin/res/commonwealthonline-plugin.cpp.in",
    }

    for label, path in templates.items():
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        assert "F4SE_PLUGIN_VERSION" in text, f"{label}: NG/AE F4SE needs F4SEPlugin_Version"
        assert "F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)" in text, \
            f"{label}: OG F4SE 0.6.x skips plugins without F4SEPlugin_Query"
        assert "a_info->infoVersion = F4SE::PluginInfo::kVersion;" in text, f"{label}: Query must fill PluginInfo"
        assert "a_f4se->RuntimeVersion() == F4SE::RUNTIME_1_10_163" in text, \
            f"{label}: Query must accept only the OG runtime"

    plugin = templates["plugin"].read_text(encoding="utf-8").replace("\r\n", "\n")
    assert "F4T_TARGET_" not in plugin, "plugin: one DLL serves OG, NG and AE, so the Query export is unconditional"

    for source in (root / "CommonwealthOnlineUI/src").glob("*.cpp"):
        body = source.read_text(encoding="utf-8")
        assert "F4SE_PLUGIN_" not in body, f"{source.name}: the merged DLL exports one VERSION, QUERY and LOAD"

    print("OG plugin query contract passed")

def _section_007_f4tpermanenttaskpolicycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TPermanentTaskPolicyContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TPermanentTaskPolicyContract.py")
    import os
    import re
    from collections import defaultdict
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    header = "F4TF4SEPermanentTask.h"
    policy = "F4TF4SECapabilityPolicy.cpp"
    skipped_dirs = {".git", "ThirdParty", "build", "lib", "node_modules"}


    def repo_files(keep):
        for directory, dirs, names in os.walk(root):
            dirs[:] = [name for name in dirs if name not in skipped_dirs]
            for name in names:
                if keep(name):
                    yield Path(directory, name)


    sources = {path: path.read_text(encoding="utf-8", errors="replace")
               for path in repo_files(lambda name: name.endswith((".h", ".cpp")))}
    by_name = defaultdict(list)
    for path in sources:
        by_name[path.name].append(path)
    includers = defaultdict(set)
    for path, text in sources.items():
        for included in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
            for target in by_name[Path(included).name]:
                includers[target].add(path)

    assert by_name[header], f"{header} not found under {root}"
    reached = set()
    pending = list(by_name[header])
    while pending:
        for parent in includers[pending.pop()]:
            if parent not in reached:
                reached.add(parent)
                pending.append(parent)
    needs_policy = {path.name for path in reached if path.suffix == ".cpp"}

    blocks = defaultdict(list)
    block_start = re.compile(r'^\s*(?:core_)?target\("(\w+)"', re.M)
    for xmake in repo_files(lambda name: name.startswith("xmake") and name.endswith(".lua")):
        text = xmake.read_text(encoding="utf-8")
        starts = list(block_start.finditer(text))
        for index, match in enumerate(starts):
            end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
            body = text[match.end():end].split("target_end()", 1)[0]
            if match.group(1) == "CommonwealthOnlineWindowsHostRequired":
                assert re.match(r'\s*\)\s*set_kind\("phony"\)', body)
                continue
            blocks[match.group(1)].append((xmake.parent, body))


    def listed(xmake_dir, body):
        names = set()
        for entry in re.findall(r'"([^"\n]+)"', body):
            pattern = entry.split("|")[0]
            if not pattern.endswith(".cpp"):
                continue
            if "*" in pattern:
                names |= {match.name for match in xmake_dir.glob(pattern.replace("**.cpp", "**/*.cpp"))}
            else:
                names.add(Path(pattern).name)
        return names


    def compiled(name, seen=frozenset()):
        files = set()
        for xmake_dir, body in blocks.get(name, []):
            files |= listed(xmake_dir, body)
            for call in re.findall(r'add_deps\(([^)]*)\)', body):
                for dep in re.findall(r'"(\w+)"', call):
                    if dep not in seen and dep != name:
                        files |= compiled(dep, seen | {name})
        return files


    compile_only = {name for name, entries in blocks.items()
                    if all(re.search(r'set_kind\("(?:static|object)"\)', body) for _, body in entries)}
    needed = {name: files for name in blocks if name not in compile_only and (files := compiled(name)) & needs_policy}
    missing = sorted(name for name, files in needed.items() if policy not in files)
    assert not missing, f"targets compile {header} sources without {policy}: {missing}"
    assert {"CommonwealthOnline", "CommonwealthOnlineProbe"} <= set(needed), "contract matched no plugin or probe target"

    print(f"permanent task policy linkage contract passed: {len(needed)} targets")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TAdminWindowContract.py", _section_001_f4tadminwindowcontract),
    ("CO-BETA/plugin/tests/F4TChatContract.py", _section_002_f4tchatcontract),
    ("CO-BETA/plugin/tests/F4TFriendsWheelContract.py", _section_005_f4tfriendswheelcontract),
    ("CO-BETA/plugin/tests/F4TOGPluginQueryContract.py", _section_006_f4togpluginquerycontract),
    ("CO-BETA/plugin/tests/F4TPermanentTaskPolicyContract.py", _section_007_f4tpermanenttaskpolicycontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TUiToolingContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
