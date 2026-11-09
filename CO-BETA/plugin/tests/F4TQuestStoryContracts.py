#!/usr/bin/env python3
"""F4TQuestStoryContracts: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts.py
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




def _section_001_f4tfactiongapcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TFactionGapContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TFactionGapContract.py")
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


    factions = read_code("src/F4TFactions.cpp")
    server_faction = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerFactions.cpp").read_text(encoding="utf-8"))
    server_invites = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerFactionInvites.cpp").read_text(encoding="utf-8"))
    server_maintenance = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerMaintenance.inc").read_text(encoding="utf-8"))
    ci_workflow = read_workflow("co-beta-ci.yml")


    parse_invite = function_body(factions, "std::optional<Invite> ParseInvite(")
    check(in_order(parse_invite, 'ReadToken(a_invite, "inviteId"', 'ReadName(a_invite, "factionName"',
                   'ReadName(a_invite, "fromName"', "ReadExpiresInSeconds(a_invite)"),
          "ParseInvite must read inviteId, factionName, fromName and expiresInSeconds in that order")
    check("!inviteId || !factionName || !fromName || !expiresInSeconds" in parse_invite,
          "ParseInvite must reject an invite that is missing any required field")
    check('ReadString(a_invite, "factionTag").value_or("")' in parse_invite and "kMaximumTagBytes" in parse_invite,
          "ParseInvite must read factionTag as optional text capped at kMaximumTagBytes")

    state_packet = function_body(factions, "void HandleStatePacket(")
    check('a_packet.find("faction")' in state_packet and "!factionJson->is_null()" in state_packet,
          "factionState must treat an absent or null faction as no faction")
    check(re.search(r"\n\s*g_faction = std::move\(faction\);", state_packet) is not None,
          "factionState must replace the stored faction, clearing it when the state has none")
    check(re.search(r'malformed faction\."\);\s*return;', state_packet) is not None,
          "factionState with a malformed faction must be ignored as a whole")
    check(re.search(r'malformed pending invite\."\);\s*return;', state_packet) is not None,
          "factionState with one malformed pending invite must be ignored as a whole")
    check('"faction_state_invites"' in state_packet and "invitesJson->size() > F4T::FactionCapacity::MaximumPendingInvitesPerTarget" in state_packet,
          "factionState must reject a pendingInvites array over MaximumPendingInvitesPerTarget")
    check(in_order(function_body(server_faction, "std::vector<OutboundFrame> FactionService::HandleFactionKick("),
                   'FactionResultPacket("kick", false, "kicked"', "FactionStatePacket(a_peers, *target, a_unixTimeSeconds)"),
          "the server must send the kicked player a kick result and then the factionState, which carries no faction")
    check(re.search(r"Json faction;\s*if \(auto\* membership = FactionOfProfile\(a_viewer\.profileId\)\)", server_faction) is not None,
          "the server must leave the faction null for a profile that is not in a faction")

    result_ops_match = re.search(r"std::array<std::string_view, (\d+)> kResultOps\{([^}]*)\}", factions)
    result_ops = re.findall(r'"(\w+)"', result_ops_match.group(2)) if result_ops_match else []
    check({"kick", "invite"} <= set(result_ops), "kResultOps must accept the kick and invite factionResult ops")
    check('FactionResultPacket("invite", false, "inviteExpired"' in server_invites,
          "the server must tell the inviter inviteExpired through an invite factionResult")
    check("protocol.PollFactionInvites(" in server_maintenance,
          "the server maintenance tick must poll faction invites so an idle server still sends inviteExpired")
    check("kMaximumReasonBytes = 64" in factions, "factionResult reasons must be capped at 64 bytes")
    result = function_body(factions, "void HandleResultPacket(")
    check(in_order(result, 'ReadString(a_packet, "op")', 'ReadBool(a_packet, "ok")', 'ReadString(a_packet, "reason")',
                   "g_lastResult = LastResult{ *op, *ok, reason };"),
          "factionResult must read op, ok and reason and store them as the last result")
    check('Payload::SanitizeText(ReadString(a_packet, "reason").value_or(""), kMaximumReasonBytes)' in result,
          "factionResult reason must be sanitized and capped at kMaximumReasonBytes")

    parse_member = function_body(factions, "std::optional<Member> ParseMember(")
    check('ReadPlayerId(a_member, "playerId").value_or(0)' in parse_member,
          "an offline member has no playerId and must parse as 0")
    check("PlayerIdJson(member.playerId)" in factions, "a member playerId of 0 must be emitted as null")
    check(in_order(parse_member, 'F4T::SocialNameplate::ResolveShownName(',
                   'ReadName(a_member, "displayName", kMaximumPlayerNameBytes)',
                   'ReadName(a_member, "name", kMaximumPlayerNameBytes)'),
          "ParseMember must resolve the shown name displayName-first with name as fallback")
    check('if (!profileId || !name)' in parse_member and 'return std::nullopt;' in parse_member,
          "a member with no profileId or no usable name must be dropped")
    check('"memberNames"' not in factions and '"officers"' not in factions,
          "the plugin must read per-member name and officer, not the server admin memberNames and officers maps")
    check('{ "displayName", view.displayName }' in server_faction
          and 'member["profileId"] = profileId;' in server_faction
          and 'member["name"] = *view.disclosedName;' in server_faction
          and 'member["name"] = view.displayName;' in server_faction
          and '{ "officer", a_faction.officers.contains(profileId) }' in server_faction,
          "the server member row must always carry displayName, add profileId and name only when disclosed, fall back to display text in name for legacy viewers, and keep the officer flag ParseMember reads")
    check('if (peer) {' in server_faction and 'member["playerId"] = peer->playerId;' in server_faction,
          "the server must omit playerId for offline members")
    parse_faction = function_body(factions, "std::optional<Faction> ParseFaction(")
    check("!members->is_array() || members->size() > F4T::FactionCapacity::MaximumMembers" in parse_faction,
          "ParseFaction must reject a members value that is not an array or is over MaximumMembers")
    check(re.search(r"ParseMember\(memberJson\);\s*if \(!member\) \{\s*return std::nullopt;", parse_faction) is not None,
          "ParseFaction must drop the whole faction when one member is unusable")

    roster = function_body(factions, "void HandleRosterPacket(")
    check("players->size() > F4T::FactionCapacity::MaximumRosterEntries" in roster and "Ignoring malformed playerRoster packet." in roster,
          "playerRoster over MaximumRosterEntries must be ignored as a whole")
    check(re.search(r'invalid entry\."\);\s*return;', roster) is not None and "*playerId == 0" in roster,
          "playerRoster with an entry that has no playerId must be ignored as a whole")
    check(in_order(roster, "g_roster = std::move(roster);", "QueueStateChanged();"),
          "playerRoster must replace the roster only after every entry has parsed")
    build_state = function_body(factions, "std::string BuildStateJson(")
    check("entry.playerId != localPlayerId.value_or(0)" in build_state,
          "the roster in the state JSON must leave out the local player")

    received = function_body(factions, "void HandleInviteReceivedPacket(")
    check("*expiresInSeconds == 0" in received and "Ignoring malformed factionInviteReceived packet." in received,
          "factionInviteReceived with zero seconds must be dropped as malformed")
    check('ReadToken(a_packet, "inviteId", kMaximumIdBytes)' in received,
          "factionInviteReceived must validate inviteId as a safe token")

    action_source = function_body(factions, "void HandleAction(")
    check("a_actionJson.size() > kMaximumActionJsonBytes" in action_source and "kMaximumActionJsonBytes = 4 * 1024" in factions,
          "a faction action over 4 KiB must be dropped before it is parsed; the limit must fit a full description and MOTD with JSON escapes")
    check("Json::parse(std::string{ a_actionJson }, nullptr, false)" in action_source and "action.is_object()" in action_source,
          "a faction action must parse without throwing and must be a JSON object")
    check("IsBoundedText(name, kMaximumFactionNameBytes)" in action_source and "tag->size() > kMaximumTagBytes" in action_source,
          "create must reject an over-long name or tag instead of truncating it")
    check("Payload::IsSafeToken(*text, a_maximumBytes)" in function_body(factions, "std::optional<std::string> ReadToken("),
          "ids and tokens must be validated with Payload::IsSafeToken")

    check("scripts/run-python-tests.py" in ci_workflow and "--group plugin" in ci_workflow, "co-beta-ci.yml must run the plugin contract group")


    list_header = (PLUGIN / "include/F4TFactionList.h").read_text(encoding="utf-8")
    list_parse = function_body(list_header, "inline std::optional<std::vector<Entry>> ParseFactionList(")
    list_entry = function_body(list_header, "inline std::optional<Entry> ParseEntry(")
    list_json = function_body(list_header, "inline nlohmann::json ToJson(")
    check(re.search(r"ParseEntry\(factionJson\);\s*if \(!entry\) \{\s*return std::nullopt;", list_parse) is not None,
          "ParseFactionList must drop the whole packet when one entry is unusable")
    list_count = function_body(list_header, "inline std::optional<std::uint32_t> ReadCount(")
    check("field->is_number_unsigned()" in list_count and "0xFFFFFFFFULL" in list_count,
          "member and online counts must be unsigned and no larger than 32 bits")
    handle_list = function_body(factions, "void HandleFactionListPacket(")
    check(re.search(r'malformed factionList packet\."\);\s*return;', handle_list) is not None and '"faction_list_malformed"' in handle_list,
          "a malformed factionList must be logged and ignored, leaving the stored list untouched")
    check("QueueStateChanged()" in handle_list, "a stored factionList must notify the UI")
    check("g_factionPage = std::move(*page);" in handle_list and handle_list.index("std::scoped_lock lock(g_mutex);") < handle_list.index("g_factionPage = std::move(*page);"),
          "the stored faction list must be written under the lock")
    check(re.search(r'\{ "type", "factionList" \}, \{ "offset", ', factions) is not None, "the factions module must send the factionList request with an offset")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TFactionGapContract: all checks passed.")

def _section_002_f4tfactioninviteoffercontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TFactionInviteOfferContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TFactionInviteOfferContract.py")
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


    def read_raw(relative):
        return (PLUGIN / relative).read_text(encoding="utf-8")


    def read_code(relative):
        return strip_comments(read_raw(relative))


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


    header_path = "include/F4TFactionInviteOffer.h"
    glue_path = "src/F4TFactionInviteOffer.cpp"
    test_path = "tests/F4TFactionInviteOfferTests.cpp"

    header = read_code(header_path)
    glue = read_code(glue_path)
    xmake = read_raw("xmake.lua")
    runtime = read_code("src/F4TReplicationRuntime.cpp")
    factions_cpp = read_raw("src/F4TFactions.cpp")
    factions_h = read_raw("include/F4TFactions.h")
    proxy_no_talk = read_raw("src/F4TProxyNoTalk.cpp")

    check("#include <cstdint>" in header, "the pure header must stay engine-free")
    check(not re.search(r'#include\s+"(RE|F4SE|REX)/|#include\s+<F4SE|#include\s+"F4T', header),
          "the pure header must not include engine or plugin headers")
    check("constexpr bool ShouldOffer(" in header, "ShouldOffer must be a constexpr decision in the pure header")
    check("constexpr bool IsActivationAim(" in header, "IsActivationAim must be a constexpr check in the pure header")
    check(re.search(r"a_localCanInvite\s*&&\s*a_targetPlayerId\s*!=\s*0\s*&&\s*a_targetPlayerId\s*!=\s*a_localPlayerId\s*&&\s*!a_targetInFaction\s*&&\s*!a_targetDowned",
                    header) is not None,
          "ShouldOffer must require invite rights, a non-zero target that is not the local player, not already in the faction and not downed")
    check("void Tick();" in header, "the pure header must declare the glue Tick")

    activate_edge = function_body(glue, "void Tick()")
    check("F4T::Downed::Engine::IsActivateHeld()" in activate_edge,
          "Tick must read the normal Activate key through the existing revive input handler")
    check(in_order(activate_edge, "activateHeld && !g_activateWasHeld", "F4T::InteractWheel::OpenForActivation();"),
          "Tick must open the wheel only on the rising edge of the Activate hold")
    check("F4T::Downed::Engine::CanTouchWorld()" in activate_edge,
          "Tick must respect the loading-screen guard before opening the wheel")
    check("ShowAlert" not in glue and "OfferForActivation" not in glue,
          "the old press-Activate invite alert must be gone; the wheel owns the offer now")
    check("FindOfferTarget" not in glue and "ReadFactionView" not in glue,
          "targeting and invite rights must live in the wheel")

    wheel_source = read_raw("src/F4TInteractWheel.cpp")
    check("F4T::ProxyActorController::GetActiveProxyActors()" in wheel_source,
          "the wheel target must be a remote player's active proxy actor")
    check("IsActivationAim(distance, cosine)" in wheel_source,
          "the wheel must apply the pure aim check")
    check("IsDowned(*target)" in wheel_source,
          "a downed player must not open the wheel")
    check("F4T::Factions::HandleAction" in wheel_source and '"invite"' in wheel_source,
          "Invite to Faction must still send the existing invite op through F4T::Factions::HandleAction")

    check("F4TFactionInviteOffer" not in factions_cpp and "F4TFactionInviteOffer" not in factions_h,
          "F4TFactions must not be edited for the wheel")
    check("FactionInviteOffer" not in proxy_no_talk,
          "the wheel must not hook F4TProxyNoTalk: the activation route is recorded in the code-notes entry")

    check("F4T::FactionInviteOffer::Tick();" in runtime and
          in_order(runtime, "F4T::Downed::Tick();", "F4T::FactionInviteOffer::Tick();"),
          "the Activate tick must run every frame after the downed tick that installs the handler")
    check("F4T::InteractWheel::Tick();" in runtime,
          "the wheel must be ticked every frame to close on cancel or a lost target")
    check('#include "F4TFactionInviteOffer.h"' in runtime, "the runtime must include the offer header")


    for relative in (header_path, glue_path, test_path):
        raw = (PLUGIN / relative).read_bytes()
        check(b"\r\n" in raw and raw.count(b"\n") == raw.count(b"\r\n"), f"{relative} must use CRLF line endings")
        check(len(raw.splitlines()) <= 1000, f"{relative} must stay at or under 1000 lines")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TFactionInviteOfferContract: all checks passed")

def _section_003_f4tfactionscontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TFactionsContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TFactionsContract.py")
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
    factions_header = (PLUGIN / "include/F4TFactions.h").read_text(encoding="utf-8")
    factions_raw = (PLUGIN / "src/F4TFactions.cpp").read_text(encoding="utf-8")
    factions = read_code("src/F4TFactions.cpp")
    faction_tasks = read_code("src/F4TFactionTasks.cpp")
    inbound = read_code("src/F4TNetworkingInbound.cpp")
    networking = read_code("src/F4TNetworking.cpp")
    ui_client = read_code("src/F4TUIClient.cpp")
    ui_api = (PLUGIN / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
    ci_workflow = read_workflow("co-beta-ci.yml")


    check("F4T_PLAYER_FACTIONS" not in factions_header and "kEnablePlayerFactions" not in factions_header,
          "F4TFactions.h must carry no feature switch")
    check('#include "F4TFactions.h"' in factions_raw, "F4TFactions.cpp must include its header")
    check("RE::" not in factions, "F4TFactions.cpp must not touch the engine")

    for target_name in ("CommonwealthOnline",):
        block = re.search(rf'target\("{target_name}"\)(.*?)(?=\ntarget\(|\Z)', xmake, re.S).group(1)
        check("F4T_PLAYER_FACTIONS" not in block and 'remove_files("src/F4TFactions.cpp")' not in block and "co_test_features" not in block,
              f"{target_name} must compile F4TFactions.cpp unconditionally")

    inbound_welcome = function_body(inbound, "void HandleWelcomePacket(")
    check("F4T::Factions::NoteWelcome(a_packet);" in inbound and "NoteWelcome(" in inbound_welcome,
          "welcome.capabilities must be read by NoteWelcome inside HandleWelcomePacket")

    packet_line = function_body(inbound, "void HandlePacketLine(")
    check(re.search(r"if \(F4T::Factions::TryHandlePacket\(\*packetType, packet\)(?: \|\| F4T::Admin::TryHandlePacket\(\*packetType, packet\))?\) \{\s*return;\s*\}", inbound) is not None,
          "the factions inbound dispatch must run in HandlePacketLine")
    check(inbound.count("Factions::TryHandlePacket(") == 1, "the factions inbound dispatch must appear exactly once")
    check(in_order(packet_line, "TryHandlePacket(", '*packetType == "welcome"'),
          "the factions inbound dispatch must run before the packet chain")

    received = function_body(factions, "void HandleInviteReceivedPacket(")
    check("QueueGameTask(" in received and "ShowInviteAlert(" in received,
          "the invite pop-up must be queued to the game task, the receive thread must not call ShowAlert directly")
    alert = function_body(factions, "void ShowInviteAlert(")
    check("ShowAlert(" in alert, "the invite pop-up must use the UI V1 ShowAlert")
    check('{ "accept", "Accept", "Y" }' in alert and '{ "decline", "Decline", "N" }' in alert,
          "the invite alert must offer accept and decline actions")
    check('"Faction invite"' in alert and '"Social"' in alert and '"invite"' in alert,
          "the invite alert must use title Faction invite, category Social and tone invite")
    check("a_expiresInSeconds * 1000U" in alert, "the invite alert duration must be expiresInSeconds * 1000")
    response = function_body(factions, "void OnInviteAlertResponse(")
    check('"accept"' in response and '"decline"' in response and '"factionInviteResponse"' in factions,
          "the alert response must map accept and decline to factionInviteResponse")

    action = function_body(factions, "void HandleAction(")
    check("SendPacketBytes(" not in action and "QueuePacket(" in action,
          "HandleAction runs on the UI thread and must queue its send, never send directly")
    queue_packet = function_body(factions, "void QueuePacket(")
    check("QueueGameTask(" in queue_packet and "SendPacketBytes(" in queue_packet,
          "QueuePacket must send from the game task")
    check(("F4SE::GetTaskInterface()" in factions or "F4SE::GetTaskInterface()" in faction_tasks) and ("AddTask(" in factions or "AddTask(" in faction_tasks),
          "game-thread work must go through the F4SE task interface")

    on_ready = function_body(ui_client, "void OnGameDataReady()")
    check(re.search(r"g_uiFactions = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<\s*COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3>\(\);", ui_client) is not None,
          "OnGameDataReady must acquire the UI V3 interface")
    check("g_uiFactions->SetFactionCallbacks(&kFactionCallbacks);" in on_ready,
          "OnGameDataReady must register the faction callbacks with SetFactionCallbacks")
    check(re.search(r"bool Callback_GetFactionStateJson\(", ui_client) is not None
          and "F4T::Factions::BuildStateJson()" in ui_client and "F4T::Factions::HandleAction(" in ui_client,
          "the faction callbacks must be defined and route to the factions module")
    check("if (g_uiFactions && g_uiFactions->NotifyFactionStateChanged)" in function_body(ui_client, "void NotifyFactionStateChanged()"),
          "NotifyFactionStateChanged must call the V3 notify only when the interface is present")

    disconnect = function_body(networking, "void DisconnectFromLocalServer()")
    check("F4T::Factions::Reset();" in disconnect, "DisconnectFromLocalServer must call Factions::Reset")
    check(in_order(disconnect, "ClearRemotePlayers();", "F4T::Factions::Reset();"),
          "Factions::Reset must run after the remote players are cleared on disconnect")

    check(re.search(r"V2 = 2,\s*V3 = 3\s*(?:,|\};)", ui_api) is not None, "the API copy must add V3 = 3 after V2")
    check(re.search(r"using FactionStateJsonFn = bool \(\*\)\(\s*char\*\s+a_jsonOut,\s*std::size_t\s+a_jsonOutSize,\s*char\*\s+a_errorBuf,\s*std::size_t\s+a_errorBufSize\);", ui_api) is not None,
          "the API copy must declare FactionStateJsonFn exactly")
    check("using FactionActionFn = void (*)(const char* a_actionJson);" in ui_api, "the API copy must declare FactionActionFn exactly")
    callbacks_struct = re.search(r"struct FactionCallbacks\s*\{(.*?)\n\t\};", ui_api, re.S)
    check(callbacks_struct is not None and re.findall(r"\b(GetStateJson|SendAction)\b", callbacks_struct.group(1)) == ["GetStateJson", "SendAction"],
          "FactionCallbacks must hold GetStateJson then SendAction")
    v3 = re.search(r"struct IVCommonwealthOnlineUI3\s*\{(.*?)\n\t\};", ui_api, re.S)
    check(v3 is not None, "the API copy must declare IVCommonwealthOnlineUI3")
    if v3 is not None:
        pointers = re.findall(r"\(\*(\w+)\)", v3.group(1))
        check(pointers == ["SetFactionCallbacks", "NotifyFactionStateChanged", "OpenFactionWindow", "CloseFactionWindow", "IsFactionWindowVisible",
                           "SetChatCallbacks", "PushChatEvent", "SetPasswordJoinCallback"],
              f"IVCommonwealthOnlineUI3 must hold the five faction pointers, the two chat pointers, then the password pointer in order, got {pointers}")
    check(re.search(r"template <>\s*struct InterfaceVersionMap<IVCommonwealthOnlineUI3>\s*\{\s*static constexpr InterfaceVersion version = InterfaceVersion::V3;\s*static constexpr std::uint32_t\s+layout = 0x[0-9A-Fa-f]{8};\s*\};", ui_api) is not None,
          "InterfaceVersionMap<IVCommonwealthOnlineUI3> must map to V3")

    for state_key in ("enabled", "localPlayerId", "faction", "pendingInvites", "roster", "factions", "factionsLoaded", "lastResult"):
        check(f'"{state_key}"' in function_body(factions, "std::string BuildStateJson("),
              f"BuildStateJson must emit the {state_key} key")
    for packet_type in ("factionState", "factionInviteReceived", "factionResult", "playerRoster", "factionList"):
        check(f'"{packet_type}"' in function_body(factions, "bool TryHandlePacket("), f"TryHandlePacket must route {packet_type}")
    for packet_type in ("factionCreate", "factionInvite", "factionInviteResponse", "factionLeave", "factionKick"):
        check(f'"{packet_type}"' in factions, f"the factions module must send {packet_type}")
    check('"factions-v1"' in factions, "the factions module must read the factions-v1 capability")

    check("scripts/run-python-tests.py" in ci_workflow and "--group plugin" in ci_workflow, "co-beta-ci.yml must run the plugin contract group")

    payload = (PLUGIN / "include/F4TFactionPayload.h").read_text(encoding="utf-8")
    ui_header = (PLUGIN / "include/F4TUIClient.h").read_text(encoding="utf-8")
    action_source = function_body(factions, "void HandleAction(")

    check('#include "F4TFactionPayload.h"' in factions_raw, "F4TFactions.cpp must include the payload helpers")
    check("ReadUint32(" not in factions, "expiresInSeconds must not be read through the integer-only ReadUint32")
    expires = function_body(factions, "std::optional<std::uint32_t> ReadExpiresInSeconds(")
    check("ReadDouble(" in expires and "Payload::ToWholeSeconds(" in expires,
          "expiresInSeconds must be read as a number and rounded by Payload::ToWholeSeconds")
    check("std::ceil(" in payload and "std::isfinite(" in payload and "kMaximumInviteSeconds = 3600" in payload,
          "ToWholeSeconds must ceil finite non-negative seconds and clamp to 3600")

    for limit in ("kMaximumPlayerNameBytes = 128", "kMaximumFactionNameBytes = 64", "kMaximumTagBytes = 8"):
        check(limit in factions, f"F4TFactions.cpp must declare {limit}")
    for limit in ("F4T::FactionCapacity::MaximumPendingInvitesPerTarget", "F4T::FactionCapacity::MaximumRosterEntries", "F4T::FactionCapacity::MaximumMembers"):
        check(limit in factions, f"F4TFactions.cpp must read {limit}")
    check('ReadName(a_invite, "fromName", kMaximumPlayerNameBytes)' in factions
          and 'ReadName(a_member, "name", kMaximumPlayerNameBytes)' in factions
          and 'ReadName(a_packet, "fromName", kMaximumPlayerNameBytes)' in factions
          and 'ReadName(player, "name", kMaximumPlayerNameBytes)' in factions,
          "member, roster and invite player names must use the 128-byte player name limit")
    read_name = function_body(factions, "std::optional<std::string> ReadName(")
    check("SanitizeText(" in read_name and "size() >" not in read_name,
          "over-long player names must be truncated at a UTF-8 boundary, not rejected")
    check("SanitizeText(" in payload and "0xC0U) == 0x80U" in payload,
          "SanitizeText must step back over UTF-8 continuation bytes when truncating")

    received = function_body(factions, "void HandleInviteReceivedPacket(")
    check(in_order(received, "PruneExpiredInvitesLocked(now);", "g_pendingInvites.size() >= F4T::FactionCapacity::MaximumPendingInvitesPerTarget"),
          "HandleInviteReceivedPacket must prune expired invites before the capacity check")
    check(in_order(received, "std::erase_if(g_pendingInvites", "PruneExpiredInvitesLocked(now);"),
          "HandleInviteReceivedPacket must replace a duplicate id before pruning")
    check("PruneExpiredInvitesLocked(now)" in function_body(factions, "std::string BuildStateJson("),
          "BuildStateJson must prune expired invites")
    check("std::erase_if(g_pendingInvites" in function_body(factions, "void PruneExpiredInvitesLocked("),
          "PruneExpiredInvitesLocked must erase expired invites with std::erase_if")
    check("invitesJson->size() > F4T::FactionCapacity::MaximumPendingInvitesPerTarget" in factions,
          "factionState must reject more than MaximumPendingInvitesPerTarget invites")

    reset = function_body(factions, "void Reset()")
    check(in_order(reset, "inviteIds.push_back(", "g_pendingInvites.clear()", "DismissInviteAlerts(std::move(inviteIds))"),
          "Reset must collect every pending invite id before clearing and dismiss those alerts")
    check("QueueStateChanged()" in reset, "Reset must notify the UI so an open window re-renders")
    dismiss = function_body(factions, "void DismissInviteAlerts(")
    check("DismissAlert(" in dismiss and "kInviteAlertPrefix" in dismiss,
          "DismissInviteAlerts must dismiss faction-invite alert ids")
    respond = function_body(factions, "void RespondToInvite(")
    check(in_order(respond, "std::erase_if(g_pendingInvites", "DismissInviteAlerts({ a_inviteId })", "QueueStateChanged()")
          and "QueuePacket(" in respond,
          "RespondToInvite must erase the invite, dismiss its alert, send the response and notify the UI")
    check("RespondToInvite(" in function_body(factions, "void OnInviteAlertResponse("),
          "the alert response must route through RespondToInvite")
    check("RespondToInvite(*inviteId, *accept)" in action_source,
          "the window invite response must route through RespondToInvite")

    check('"refresh"' in action_source and '"playerRosterRequest"' in factions,
          "the refresh op must map to playerRosterRequest")
    check("kRosterRefreshInterval = std::chrono::seconds{ 2 }" in factions,
          "the roster refresh throttle must be at or above the server 1.0 s limit")
    check(in_order(action_source, "TakeThrottleSlot(g_lastRosterRefresh, kRosterRefreshInterval)", '"playerRosterRequest"'),
          "the refresh op must take the throttle slot before queuing the request")

    check(re.search(r"COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3\* g_uiFactions", ui_client) is not None,
          "g_uiFactions must be declared")
    check(re.search(r"void NotifyFactionStateChanged\(\)\s*\{", ui_client) is not None,
          "UIClient::NotifyFactionStateChanged must be defined")
    check(re.search(r"void NotifyFactionStateChanged\(\);", ui_header) is not None
          and '#include "F4TFactions.h"' in ui_header,
          "UIClient::NotifyFactionStateChanged must be declared and F4TUIClient.h must include F4TFactions.h")
    transport = function_body(ui_client, "void NotifyTransportDisconnected(")
    check(in_order(transport, "g_connectionOwnerToken.store(0);", "F4T::Factions::Reset();"),
          "NotifyTransportDisconnected must reset the factions state")


    respond_now = function_body(factions, "void RespondToInvite(")
    check("std::size_t removed = 0;" in respond_now
          and in_order(respond_now, "removed = std::erase_if(g_pendingInvites", "DismissInviteAlerts({ a_inviteId })", "if (removed == 0)", "QueuePacket(", "QueueStateChanged()"),
          "RespondToInvite must always dismiss its alert and send or notify only when an invite was removed")
    state_packet = function_body(factions, "void HandleStatePacket(")
    check(in_order(state_packet, "dropped.push_back(", "g_pendingInvites = std::move(invites);")
          and re.search(r"\}\s*DismissInviteAlerts\(std::move\(dropped\)\);", state_packet) is not None
          and "std::none_of(" in state_packet,
          "HandleStatePacket must collect invites missing from the new state and dismiss their alerts after the lock is released")
    get_state = function_body(ui_client, "bool Callback_GetFactionStateJson(")
    check("try {" in get_state and "catch (...)" in get_state and "return false;" in get_state,
          "Callback_GetFactionStateJson must catch build errors, write an error and return false")
    send_action = function_body(ui_client, "void Callback_SendFactionAction(")
    check("try {" in send_action and "catch (...)" in send_action and "REX::WARN(" in send_action,
          "Callback_SendFactionAction must catch errors and log the dropped action")

    def op_branch(code, op):
        start = code.index(f'op == "{op}"')
        return code[start:code.find("} else", start)]


    server_faction = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerFactions.cpp").read_text(encoding="utf-8"))
    validation = strip_comments((ROOT / "co-core/src/validation/F4TFactionValidation.cpp").read_text(encoding="utf-8"))

    check("kMaximumProfileIdBytes = 128;" in factions and 'ReadToken(a_member, "profileId", kMaximumProfileIdBytes)' in factions,
          "faction member profile ids must accept the 128 bytes the hello allows, or the whole factionState is dropped")
    check("kMaximumProfileIdLength = 128;" in validation and '"targetProfileId", kMaximumProfileIdLength)' in validation,
          "faction kick, promote and setRank must accept 128-byte target profile ids")
    promote_branch = op_branch(action_source, "promote")
    disband_branch = op_branch(action_source, "disband")
    check('"factionPromote"' in promote_branch and 'ReadToken(action, "targetProfileId", kMaximumProfileIdBytes)' in promote_branch
          and '{ "targetProfileId", *target }' in promote_branch,
          "HandleAction must route promote to a factionPromote packet with a validated targetProfileId, like kick")
    check('"factionDisband"' in disband_branch and "ReadToken(" not in disband_branch and "ReadString(" not in disband_branch,
          "HandleAction must route disband to a bare factionDisband packet with no fields")
    for packet_type in ("factionPromote", "factionDisband"):
        check(f'a_type == "{packet_type}"' in server_faction, f"the server must dispatch {packet_type}")
    check('ReadString(*normalized, "targetProfileId")' in function_body(server_faction, "std::vector<OutboundFrame> FactionService::HandleFactionPromote("),
          "the server promote handler must read targetProfileId, the field the plugin sends")
    check('"targetProfileId"' in function_body(validation, "std::optional<nlohmann::json> NormalizeFactionPromote("),
          "the server promote validator must read targetProfileId")
    check("NormalizeFactionDisband(a_packet)" in function_body(server_faction, "std::vector<OutboundFrame> FactionService::HandleFactionDisband("),
          "the server disband handler must validate the packet through NormalizeFactionDisband")

    result_ops_match = re.search(r"std::array<std::string_view, (\d+)> kResultOps\{([^}]*)\}", factions)
    result_ops = re.findall(r'"(\w+)"', result_ops_match.group(2)) if result_ops_match else []
    check(result_ops_match is not None and int(result_ops_match.group(1)) == len(result_ops),
          "kResultOps must size its array to the number of ops it lists")
    check({"promote", "disband"} <= set(result_ops), "kResultOps must accept the promote and disband factionResult ops")
    server_result_ops = (set(re.findall(r'FactionResultPacket\("(\w+)"', server_faction))
                         | set(re.findall(r'FactionFailure\(connectionId, "(\w+)"', server_faction))
                         | set(re.findall(r'RestoreAndFail\([^,]+, connectionId, "(\w+)"', server_faction)))
    check(server_result_ops and server_result_ops <= set(result_ops),
          f"kResultOps must accept every op the server answers, missing {sorted(server_result_ops - set(result_ops))}")

    map_presence = read_code("src/F4TMapPresence.cpp")
    map_callback = function_body(map_presence, "nlohmann::json BuildMapPlayers(")
    check("F4T::Factions::MemberName(remote.playerId)" in map_callback and 'player["faction"] = true;' in map_callback and 'player["name"] = *memberName;' in map_callback,
          "the remote players map callback must tag faction members with faction and name through Factions::MemberName")
    check('"worldspaceId", remote.worldspaceId' in map_callback and '"cellId", remote.cellId' in map_callback
          and '"angleZ", remote.angleZ' in map_callback,
          "the remote players map callback must forward the ally's worldspace, cell and heading")
    check("std::scoped_lock lock(g_mutex);" in function_body(factions, "std::optional<std::string> MemberName("),
          "MemberName must read the faction under g_mutex")
    pause_map = (ROOT / "CommonwealthOnlineUI/src/F4TPauseMenuMap.cpp").read_text(encoding="utf-8")
    build_others = function_body(pause_map, "nlohmann::json BuildOthers(")
    check('"faction"' in build_others and '"name"' in build_others and "is_boolean()" in build_others and "is_string()" in build_others,
          "BuildOthers must forward the faction flag and the name, validating their types")
    parse_form = function_body(pause_map, "std::optional<RE::TESFormID> ParseFormId(")
    check('a_entry.find(a_key)' in parse_form and "stoul(text, nullptr, 16)" in parse_form
          and "return std::nullopt;" in parse_form,
          "ParseFormId must read a hex form id and treat a missing, empty or unparsable one as unknown")
    check('ParseFormId(entry, "worldspaceId")' in build_others and "a_elsewhere" in build_others
          and "*remoteWorld != a_world->GetFormID()" in build_others,
          "BuildOthers must hide the allies outside the drawn worldspace and count them")
    interior_cell = function_body(pause_map, "bool IsInteriorCell(")
    check("GetFormByID(a_cellId)" in interior_cell and "IsInterior()" in interior_cell,
          "IsInteriorCell must resolve the cell and ask the engine whether it is an interior")
    check('ParseFormId(entry, "cellId")' in build_others and "IsInteriorCell(*remoteCell)" in build_others,
          "BuildOthers must hide an ally whose cell is an interior, whose position has no exterior meaning")
    check('"rotDeg"' in build_others and "kRadToDeg" in build_others and "std::isfinite" in build_others,
          "BuildOthers must convert the ally heading into degrees and guard a non-finite value")
    check('root["othersElsewhere"] = othersElsewhere;' in pause_map,
          "the map payload must carry the count of allies it did not draw")
    check('ReadBool(a_packet, "publicPlayersVisible").value_or(true)' in function_body(map_presence, "void HandlePacket("),
          "HandlePacket must read publicPlayersVisible and default to a public list")
    check("if (!PublicPlayersVisible())" in map_callback,
          "BuildMapPlayers must not fall back to the unfiltered remote player snapshot while the server hides public players")
    check("if (!ServerPresenceSeen())" in map_callback and "g_serverPresenceSeen = true;" in function_body(map_presence, "void HandlePacket("),
          "BuildMapPlayers may use the nearby fallback only before the first server presence packet, which HandlePacket must record")
    check('root["publicPlayersVisible"] = publicVisible;' in pause_map and 'parsed["publicPlayersVisible"] != false' in build_others,
          "the map payload must tell the view when the server hides public players")
    check(re.search(r"bool leader\{ false \};\s*bool officer\{ false \};", factions) is not None,
          "Member must carry an officer flag that defaults to false")
    check('ReadBool(a_member, "officer").value_or(false)' in function_body(factions, "std::optional<Member> ParseMember("),
          "ParseMember must read the officer flag next to leader")
    check('F4T::SocialNameplate::ResolveShownName(' in function_body(factions, "std::optional<Member> ParseMember(")
          and 'ReadName(a_member, "displayName", kMaximumPlayerNameBytes)' in function_body(factions, "std::optional<Member> ParseMember(")
          and 'ReadName(a_member, "name", kMaximumPlayerNameBytes)' in function_body(factions, "std::optional<Member> ParseMember(")
          and 'if (!profileId || !name)' in function_body(factions, "std::optional<Member> ParseMember(")
          and "return std::nullopt;" in function_body(factions, "std::optional<Member> ParseMember("),
          "ParseMember must resolve the shown name displayName-first with name as fallback and drop a member with no profileId or no usable name")
    check(in_order(factions, '{ "leader", member.leader }', '{ "officer", member.officer }'),
          "the faction state JSON must emit officer next to leader")
    set_rank_branch = op_branch(action_source, "setRank")
    check('"factionSetRank"' in set_rank_branch and 'ReadToken(action, "targetProfileId", kMaximumProfileIdBytes)' in set_rank_branch
          and 'ReadToken(action, "rank", kMaximumIdBytes)' in set_rank_branch and '{ "rank", *rank }' in set_rank_branch,
          "HandleAction must route setRank to a factionSetRank packet with a validated targetProfileId and rank")
    check('(*rank != "officer" && *rank != "member")' in set_rank_branch and '"faction_action_malformed"' in set_rank_branch,
          "setRank must accept only the officer and member ranks and log the malformed warning otherwise")
    check("setRank" in result_ops, "kResultOps must accept the setRank factionResult op")

    set_profile_branch = op_branch(action_source, "setProfile")
    check('"factionSetProfile"' in set_profile_branch and 'ReadProfileDescription(action)' in set_profile_branch
          and 'ReadProfileColor(action)' in set_profile_branch and '{ "description", *description }' in set_profile_branch
          and '{ "color", *color }' in set_profile_branch,
          "HandleAction must route setProfile to a factionSetProfile packet with a validated description and color")
    check('"faction_action_malformed"' in set_profile_branch and "return;" in set_profile_branch,
          "setProfile must log the malformed warning and send nothing when the input is invalid")
    check('ReadProfileMotd(action)' in set_profile_branch and 'if (motd->has_value()) {' in set_profile_branch
          and 'packet["motd"] = **motd;' in set_profile_branch,
          "setProfile must read motd and send it only when the action carries the key")
    profile_description = function_body(payload, "std::optional<std::string> ReadProfileDescription(")
    check("kMaximumDescriptionBytes" in profile_description and "IsProfileDescriptionChar" in profile_description
          and "find_first_not_of(' ')" in profile_description,
          "the setProfile description must be trimmed, capped at kMaximumDescriptionBytes and limited to printable ASCII without quote or backslash")
    profile_char = function_body(payload, "bool IsProfileDescriptionChar(")
    check("'\"'" in profile_char and "'\\\\'" in profile_char,
          "the setProfile description must reject a double quote and a backslash, which SanitizeText strips later")
    profile_color = function_body(payload, "std::optional<std::string> ReadProfileColor(")
    check("kMaximumColorBytes" in profile_color and "IsHexDigit" in profile_color and "std::toupper(" in profile_color,
          "the setProfile colour must be empty or #RRGGBB and uppercased")
    profile_motd = function_body(payload, "std::optional<std::optional<std::string>> ReadProfileMotd(")
    check("kMaximumMotdBytes" in profile_motd and "IsProfileDescriptionChar" in profile_motd
          and "find_first_not_of(' ')" in profile_motd and 'a_json.find("motd") == a_json.end()' in profile_motd,
          "the setProfile motd must be trimmed, capped at kMaximumMotdBytes, limited to printable ASCII without quote or backslash, and absent when the key is missing")
    check("kMaximumMotdBytes = 1000" in payload, "the motd cap must be 1000 bytes")
    parse_faction = function_body(factions, "std::optional<Faction> ParseFaction(")
    check('ReadString(a_faction, "description")' in parse_faction and 'ReadString(a_faction, "color")' in parse_faction,
          "ParseFaction must read description and color from the faction state")
    check(in_order(factions, '{ "tag", g_faction->tag }', '{ "description", g_faction->description }', '{ "color", g_faction->color }'),
          "the faction state JSON must emit description and color next to tag")
    check('ReadString(a_faction, "motd")' in parse_faction and "kMaximumMotdBytes" in parse_faction,
          "ParseFaction must read motd capped at kMaximumMotdBytes")
    check(in_order(factions, '{ "description", g_faction->description }', '{ "motd", g_faction->motd }', '{ "color", g_faction->color }'),
          "the faction state JSON must emit motd right after description")
    check("setProfile" in result_ops, "kResultOps must accept the setProfile factionResult op")
    parse_invite = function_body(factions, "std::optional<Invite> ParseInvite(")
    check('{ "factionTag", invite.factionTag }' in function_body(factions, "std::string BuildStateJson("),
          "BuildStateJson must emit factionTag for each pending invite")
    check('ReadString(a_invite, "factionTag")' in parse_invite and "*factionName, factionTag, *fromName" in parse_invite,
          "ParseInvite must read factionTag into the pending invite")

    list_header = (PLUGIN / "include/F4TFactionList.h").read_text(encoding="utf-8")
    check(len(factions_raw.splitlines()) < 750, "F4TFactions.cpp must stay under 750 lines")
    check('#include "F4TFactionList.h"' in factions_raw, "F4TFactions.cpp must include the faction list helpers")
    check("RE::" not in list_header and "F4TNetworking" not in list_header, "F4TFactionList.h must stay engine-free")
    check("kMaximumEntries = F4T::FactionCapacity::ListPageEntries" in list_header, "the faction list must cap each page at the shared page size")
    check("kMaximumJsonBytes" not in list_header, "the faction list must not truncate by a byte budget")
    list_parse = function_body(list_header, "inline std::optional<std::vector<Entry>> ParseFactionList(")
    check("!factions->is_array() || factions->size() > kMaximumEntries" in list_parse,
          "ParseFactionList must reject a factions value that is not an array or is over kMaximumEntries")
    list_entry = function_body(list_header, "inline std::optional<Entry> ParseEntry(")
    check("Payload::IsSafeToken(*id, kMaximumIdBytes)" in list_entry and "name.empty()" in list_entry
          and "!memberCount || !onlineCount" in list_entry and "yours->is_boolean()" in list_entry,
          "ParseEntry must require a safe id, a non-empty name, two counts and a boolean yours")
    check('Payload::ReadTextField(a_entry, "tag").value_or("")' in list_entry
          and 'Payload::ReadTextField(a_entry, "leader").value_or("")' in list_entry,
          "ParseEntry must read tag and leader as optional text")
    list_json = function_body(list_header, "inline nlohmann::json ToJson(")
    check('{ "yours", entry.yours }' in list_json and "kMaximumJsonBytes" not in list_json,
          "ToJson must keep every entry of a page; the browse list is paged, not truncated")
    check('"motd"' not in list_json and '"motd"' not in list_entry, "the browse list must not carry motd")
    handle_list = function_body(factions, "void HandleFactionListPacket(")
    check(in_order(handle_list, "List::ParseFactionPage(a_packet)", "g_factionPage = std::move(*page);", "g_factionsLoaded = true;", "QueueStateChanged();")
          and "std::scoped_lock lock(g_mutex);" in handle_list,
          "HandleFactionListPacket must parse, then store the list and the loaded flag under g_mutex, then notify the UI")
    check('{ "factions", List::ToJson(g_factionPage.entries) }' in function_body(factions, "std::string BuildStateJson(")
          and '{ "factionsLoaded", g_factionsLoaded }' in function_body(factions, "std::string BuildStateJson(")
          and '{ "factionsOffset", g_factionPage.offset }' in function_body(factions, "std::string BuildStateJson(")
          and '{ "factionsTotal", g_factionPage.total }' in function_body(factions, "std::string BuildStateJson("),
          "BuildStateJson must emit the page entries, its offset, its total and the factionsLoaded flag")
    check("g_factionPage = {};" in function_body(factions, "void Reset()") and "g_factionsLoaded = false;" in function_body(factions, "void Reset()"),
          "Reset must clear the faction page and its loaded flag")
    note_welcome = function_body(factions, "void NoteWelcome(")
    check(in_order(note_welcome, "firstSupport = supported && !g_serverSupportsFactions;", "g_serverSupportsFactions = supported;",
                   'QueuePacket(Json{ { "type", "factionList" }, { "offset", 0 } })', "QueueStateChanged();"),
          "NoteWelcome must request the faction list once, on the welcome or capability that first enables factions")
    refresh_branch = op_branch(action_source, "refresh")
    check(in_order(refresh_branch, "TakeThrottleSlot(g_lastRosterRefresh, kRosterRefreshInterval)", '"playerRosterRequest"', '"factionList"'),
          "the refresh op must take the throttle slot, then request the roster and the faction list")
    list_branch = op_branch(action_source, "listFactions")
    check(in_order(list_branch, "TakeThrottleSlot(g_lastFactionListRequest, kFactionListRequestInterval)", "return;", '"factionList"'),
          "the listFactions op must take its own throttle slot and then queue factionList")
    check("kFactionListRequestInterval = std::chrono::seconds{ 1 }" in factions,
          "the listFactions throttle must match the server 1.0 s factionList limit")
    check("g_lastFactionListRequest.reset();" in function_body(factions, "void Reset()"),
          "Reset must clear the listFactions throttle slot")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TFactionsContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TFactionGapContract.py", _section_001_f4tfactiongapcontract),
    ("CO-BETA/plugin/tests/F4TFactionInviteOfferContract.py", _section_002_f4tfactioninviteoffercontract),
    ("CO-BETA/plugin/tests/F4TFactionsContract.py", _section_003_f4tfactionscontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
