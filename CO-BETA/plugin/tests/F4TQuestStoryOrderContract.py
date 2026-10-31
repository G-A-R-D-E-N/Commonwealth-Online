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
    sys.exit(1)

print("F4TQuestStoryOrderContract: all checks passed.")
