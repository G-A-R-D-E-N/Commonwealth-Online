#!/usr/bin/env python3
"""F4TQuestStoryContracts_part4: consolidated contract sections.

Consolidated from 2 single-contract scripts so the group runs one process
instead of 2. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts_part4.py
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




def _section_008_f4tqueststoryordercontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TQuestStoryOrderContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TQuestStoryOrderContract.py")
    from pathlib import Path
    import json
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    SERVER_SRC = ROOT / "CO-SERVER/server-cpp/src"
    STORY_JSON = ROOT / "co-core/data/quest-story-order.json"
    STORY_H = ROOT / "co-core/include/F4TQuestStoryOrder.h"
    STORY_CPP = ROOT / "co-core/src/world/F4TQuestStoryOrder.cpp"
    SERVER_QUEST = SERVER_SRC / "ServerQuest.cpp"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        try:
            return path.read_text(encoding="utf-8")
        except OSError:
            failures.append(f"missing {path}")
            return ""


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
                    return source[brace : index + 1]
        return None


    def norm(text):
        return text.replace("\r\n", "\n")


    story_text = read(STORY_JSON)
    story_h = read(STORY_H)
    story_cpp = read(STORY_CPP)
    server_quest = read(SERVER_QUEST)

    document = None
    quests = None
    try:
        document = json.loads(story_text)
        quests = document.get("quests")
        check(isinstance(quests, list), "quest-story-order.json must carry a quests array")
    except (ValueError, AttributeError):
        failures.append("quest-story-order.json must be valid JSON with a quests array")
        quests = None

    if isinstance(quests, list):
        seen = set()
        for row in quests:
            key = row.get("quest") if isinstance(row, dict) else None
            if isinstance(key, str) and key in seen:
                check(False, f"quest-story-order.json must not list {key} twice")
            if isinstance(key, str):
                seen.add(key)
        for row in quests:
            if not isinstance(row, dict):
                check(False, "every story-order row must be an object")
                continue
            key = row.get("quest", "<missing>")
            requires = row.get("requires", [])
            check(isinstance(requires, list), f"{key}: requires must be an array")
            for entry in requires if isinstance(requires, list) else []:
                target = entry.get("quest") if isinstance(entry, dict) else None
                check(target in seen, f"{key}: requires {target} which is not in the table")
            source = row.get("source")
            check(isinstance(source, str) and source.strip() != "", f"{key}: a row with no source is refused")
        by_quest = {row["quest"]: row for row in quests if isinstance(row, dict) and isinstance(row.get("quest"), str)}
        visiting = set()
        done = set()
        has_cycle = []

        def visit(key, stack):
            if key in done:
                return False
            if key in visiting:
                has_cycle.append(" -> ".join(stack + [key]))
                return True
            visiting.add(key)
            row = by_quest.get(key, {})
            for entry in row.get("requires", []) if isinstance(row.get("requires", []), list) else []:
                target = entry.get("quest") if isinstance(entry, dict) else None
                if isinstance(target, str) and target in by_quest:
                    if visit(target, stack + [key]):
                        return True
            visiting.discard(key)
            done.add(key)
            return False

        for key in by_quest:
            visit(key, [])
        check(not has_cycle, f"quest-story-order.json must not contain a requires cycle: {has_cycle[:1]}")

    marker = 'R"STORYJSON('
    start = story_cpp.find(marker)
    end = story_cpp.find(")STORYJSON", start + len(marker)) if start >= 0 else -1
    check(start >= 0 and end > start, "F4TQuestStoryOrder.cpp must embed the table in kEmbeddedStoryOrder (R\"STORYJSON(...)\")")
    if start >= 0 and end > start:
        embedded = norm(story_cpp[start + len(marker) : end])
        check(norm(story_text).strip() == embedded.strip(), "the embedded story table must stay byte-identical to co-core/data/quest-story-order.json")

    check("class Order" in story_h and "static std::optional<Order> Load(" in story_h, "F4TQuestStoryOrder.h must declare Order::Load")
    check("static const Order& Default();" in story_h or "static const Order &Default();" in story_h or "Default()" in story_h, "F4TQuestStoryOrder.h must declare Order::Default")
    check("HasCycle" in story_cpp, "Order::Load must reject a requires cycle")
    check("order.Find(requirement.quest) == nullptr" in story_cpp, "Order::Load must reject a requires entry outside the table")
    check("order.Find(*key) != nullptr" in story_cpp, "Order::Load must reject a duplicate quest")
    check('source->get_ref<const std::string&>().empty()' in story_cpp or "empty()" in story_cpp, "Order::Load must reject a row with an empty source")

    handle_report = body_after(server_quest, "std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestReport(")
    check(handle_report is not None, "ServerQuest.cpp must still define HandleQuestReport")
    check(handle_report is not None and "StoryReachable(a_session.profileId" in handle_report, "HandleQuestReport must call StoryReachable before any state change (revert breaks the guard)")
    check(handle_report is not None and "QuestNotReachableFrame(" in handle_report, "HandleQuestReport must answer a refused write with QuestNotReachableFrame")

    broadcast = body_after(
        server_quest,
        "std::vector<OutboundFrame> ProtocolHost::Impl::BroadcastQuestStage(",
    )
    check(broadcast is not None, "ServerQuest.cpp must still define BroadcastQuestStage")
    check(broadcast is not None and "StoryStageVisible(recipient.profileId" in broadcast, "BroadcastQuestStage must withhold stages for unreached players (revert leaks ahead-of-story state)")

    sync_frames = body_after(
        server_quest,
        "std::vector<OutboundFrame> ProtocolHost::Impl::QuestSyncFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds) const",
    )
    check(sync_frames is not None, "ServerQuest.cpp must still define QuestSyncFrames")
    check(sync_frames is not None and "StoryStageVisible(a_target.profileId" in sync_frames, "QuestSyncFrames must withhold stages and completions for unreached players (revert leaks ahead-of-story state)")

    visible = body_after(server_quest, "bool ProtocolHost::Impl::StoryStageVisible(")
    check(visible is not None and "StoryReachable(a_profileId" in visible, "StoryStageVisible must delegate to StoryReachable so the recipient filters keep the guard")

    check("StoryContentAllowed(F4T::QuestProtocol::Mode" in server_quest or "StoryContentAllowed(" in server_quest, "ServerQuest.cpp must gate story content on the mode through StoryContentAllowed")
    check("case F4T::QuestProtocol::Mode::FreeRoam" in server_quest, "StoryContentAllowed must name FreeRoam so every story write is refused there")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(
            "F4TQuestStoryOrderContract: the story-order guard is incomplete. The JSON table is the "
            "allow-list, Order::Load is strict, and StoryReachable must guard HandleQuestReport, "
            "QuestSyncFrames and BroadcastQuestStage; see docs/code-notes/CO-SERVER-quests.md."
        )
        _merge_exit(1)

    print("F4TQuestStoryOrderContract: all checks passed.")

def _section_009_f4tquestuicontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TQuestUIContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TQuestUIContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    MAIN = PLUGIN / "src/main.cpp"
    QUESTUI = PLUGIN / "src/F4TQuestUI.cpp"
    QUESTSYNC = PLUGIN / "src/F4TQuestSync.cpp"
    RUNTIME = PLUGIN / "src/F4TReplicationRuntime.cpp"
    WIRE = PLUGIN / "include/F4TQuestWire.h"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        if not path.exists():
            failures.append(f"missing {path.relative_to(ROOT)}")
            return ""
        return path.read_text(encoding="utf-8")


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
                    return source[brace : index + 1]
        return None


    main = read(MAIN)
    questui = read(QUESTUI)
    questsync = read(QUESTSYNC)
    runtime = read(RUNTIME)
    wire = read(WIRE)

    attach_call = body_after(main, "void F4SEAPI OnF4SEMessage(") or ""
    check(
        "F4T::QuestUI::Attach();" in attach_call,
        "the game-data-ready message must attach the quest window host, or the window can never be available",
    )

    attach = body_after(questui, "bool Attach()") or ""
    check(
        "COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI11" in questui,
        "the attach path must request the V11 quest interface",
    )
    check(
        "SetQuestCallbacks(&kQuestCallbacks)" in attach,
        "Attach must register the quest callbacks on the layout-checked interface",
    )
    check("static bool attached" in attach, "Attach must be idempotent")
    check(
        "RequestPluginAPI<" in questui and "IVCommonwealthOnlineUI11>()" in questui,
        "the V11 request must go through the layout-checked RequestPluginAPI<IVCommonwealthOnlineUI11>()",
    )

    callbacks = body_after(questui, "bool GetStateJsonCallback(") or ""
    check(
        "BuildStateJson(" in callbacks,
        "the quest state callback must serve F4T::QuestUI::BuildStateJson",
    )
    check("catch (...)" in callbacks, "a state build failure must not escape the callback")
    check(
        "COMMONWEALTH_ONLINE_UI_API::QuestCallbacks kQuestCallbacks" in questui,
        "the plugin must declare one QuestCallbacks table",
    )

    actions = body_after(questui, "void SendActionCallback(const char* a_actionJson)") or ""
    check(
        "HandleAction(a_actionJson)" in actions,
        "a quest action from the window must reach F4T::QuestUI::HandleAction",
    )
    check("catch (...)" in actions, "a malformed quest action must not escape the callback")

    build = body_after(questui, "bool BuildStateJson(") or ""
    for field in ('{ "enabled", IsEnabled() }', '{ "mode", std::string(F4T::QuestSync::ViewMode()) }',
                  '{ "quests", Json::array() }', '{ "notices", notices }'):
        check(field in build, f"the quest state JSON must carry {field}")
    check(
        "F4T::QuestSync::ViewQuests()" in build,
        "the quest rows must come from the live quest sync state, not a static list",
    )
    check(
        '{ "friends", friends }' in build and "row.friends" in build,
        "friend rows must come from the tracker rows, not an empty array",
    )
    check(
        '{ "id", friendStage.playerId }' in build and "FriendName(names, friendStage.playerId)" in build,
        "each friend must carry its id and a roster name with the Player <id> fallback",
    )
    check(
        "kMaximumFriendsPerRow" in build,
        "friend rows must be capped in the state JSON",
    )
    check(
        "ScopeOf(row)" in build and "a_row.held" in questui and "a_row.solo" in questui and "a_row.party ? \"party\"" in questui,
        "the scope badge must follow the tracker's solo list, held stages and party list",
    )
    check(
        "IsStoryReached" in questsync and "g_tracker.IsStoryReached" in questsync,
        "ViewQuests must withhold unreached quests so the window never lists them",
    )
    check(
        "NextStoryQuest()" in questsync and '{ "nextStep", true }' in build,
        "the next reachable story step must ride along as a hint row without controls",
    )
    check(
        "a_jsonOutSize" in build and "text.size() >= a_jsonOutSize" in build,
        "the state JSON must be refused, not truncated, when it does not fit the caller buffer",
    )

    enabled = body_after(questui, "bool IsEnabled()") or ""
    check(
        "F4T::Networking::IsConnectedToServer()" in enabled and "F4T::QuestInbound::IsActive()" in enabled,
        "enabled must mean connected and the server advertised quest-sync-v1 on this connection",
    )

    tick = body_after(questui, "void Tick()") or ""
    check(
        "NotifyIfChanged(" in tick,
        "Tick must push a state change to the UI instead of latching availability at startup",
    )
    changed = body_after(questui, "void NotifyIfChanged(bool a_force)") or ""
    check(
        "BuildSignature()" in changed and "signature == g_lastSignature" in changed,
        "NotifyIfChanged must compare a live signature so a capability or stage change reaches the window",
    )
    signature = body_after(questui, "std::string BuildSignature()") or ""
    check(
        "F4T::QuestNotices::Revision()" in signature,
        "BuildSignature must include the notices revision so a new notice repaints the window",
    )
    notify = body_after(questui, "void NotifyWindow()") or ""
    check(
        "NotifyQuestStateChanged" in notify,
        "the plugin must announce state changes through IVCommonwealthOnlineUI11::NotifyQuestStateChanged",
    )

    check(
        "F4T::QuestUI::RequestRefresh();" in (body_after(questsync, "void NoteWelcome(const nlohmann::json& a_packet)") or ""),
        "QuestSync::NoteWelcome must request a refresh so the capability change is re-evaluated at once",
    )
    check(
        "F4T::QuestUI::RequestRefresh();" in (body_after(questsync, "void ClearSession()") or ""),
        "QuestSync::ClearSession must request a refresh so a disconnect removes the row and window",
    )
    check(
        "F4T::QuestUI::Tick();" in (body_after(runtime, "void Tick()") or ""),
        "ReplicationRuntime::Tick must tick the quest window host",
    )

    check(
        "questReport" in wire and "questStage" in wire and "questSync" in wire and "questPolicy" in wire
        and "questScope" in wire,
        "F4TQuestWire.h must keep the four server-to-client and report types plus the client scope request",
    )
    handle = body_after(questui, "void HandleAction(std::string_view a_actionJson)") or ""
    check(
        'if (name == "setScope")' in handle,
        "the window's setScope action must be handled instead of dropped",
    )
    check(
        "F4T::QuestSync::RequestScope(" in handle and "ParseScope(scopeName)" in handle,
        "setScope must reach the quest sender that owns the socket",
    )
    request = body_after(questsync, "void RequestScope(std::string_view a_quest, QuestLogic::Scope a_scope)") or ""
    check(
        "QuestKey::Parse(" in request and "EncodeScope(" in request and "g_send(packet.dump())" in request,
        "RequestScope must canonicalise the key and send one questScope packet",
    )
    check(
        "IsConnectedToServer()" in request and "QuestInbound::IsActive()" in request,
        "a scope request must not be sent outside a live quest-sync connection",
    )
    check(
        "SendPacketBytes" not in questui,
        "the quest window host must not send packets: quest-sync-v1 is unchanged by this slice",
    )

    check(
        "F4T::QuestNotices::Snapshot()" in build and '{ "notices", notices }' in build,
        "the quest state JSON must carry the notices fed from F4T::QuestNotices::Snapshot",
    )
    check(
        '#include "F4TQuestNotices.h"' in questui,
        "F4TQuestUI.cpp must include the notices module",
    )
    check(
        "g_noticeResetRequested = true;" in (body_after(questsync, "void ClearSession()") or ""),
        "QuestSync::ClearSession must request the notice reset so the clear runs on the tick thread",
    )
    check(
        "F4T::QuestNotices::Clear();" in (body_after(questsync, "void Tick()") or ""),
        "QuestSync::Tick must clear the completion notices when a reset was requested",
    )
    check(
        "g_questReadyResetRequested = true;" in (body_after(questsync, "void ClearSession()") or ""),
        "QuestSync::ClearSession must request the quest-ready reset so the flag clears on the tick thread",
    )
    check(
        "g_questReadyResetRequested.exchange(false)" in (body_after(questsync, "void Tick()") or ""),
        "QuestSync::Tick must consume the quest-ready reset so a reconnect resends questReady",
    )
    check(
        "g_questReadySent = false;" not in (body_after(questsync, "void ClearSession()") or ""),
        "QuestSync::ClearSession must not clear the quest-ready flag itself (network thread vs tick thread)",
    )
    check(
        "F4T::QuestNotices::Clear();" in (body_after(questsync, "void Reset()") or ""),
        "QuestSync::Reset must clear the completion notices with the session generation",
    )
    note_completion = body_after(questsync, "void NoteCompletion(") or ""
    check(
        "g_noticedCompletions.emplace(" in note_completion
        and "F4T::QuestNotices::Push(" in note_completion
        and "RE::SendHUDMessage::ShowHUDMessage(" in note_completion,
        "a completion notice must be deduplicated per session, pushed to the notices module and shown as a HUD message",
    )
    check(
        "NoteCompletion(a_key, RemoteCompleterName(a_key))" in questsync
        and 'NoteCompletion(report->quest, "You")' in questsync,
        "remote and local completions must both feed NoteCompletion",
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(
            "F4TQuestUIContract: the quest window has no live host. The V11 registration, the state JSON and "
            "the change notifications are the three pieces that make the QUESTS row reflect the server; see "
            "docs/code-notes/CO-BETA-plugin.md."
        )
        _merge_exit(1)

    print("F4TQuestUIContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TQuestStoryOrderContract.py", _section_008_f4tqueststoryordercontract),
    ("CO-BETA/plugin/tests/F4TQuestUIContract.py", _section_009_f4tquestuicontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts_part4: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
