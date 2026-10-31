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
    sys.exit(1)

print("F4TQuestUIContract: all checks passed.")
