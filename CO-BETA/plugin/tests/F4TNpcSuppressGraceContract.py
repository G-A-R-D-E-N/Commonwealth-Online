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
    start = code.find(signature)
    if start < 0:
        return ""
    brace = code.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    return ""


internal = read_code("include/F4TNpcSyncInternal.h")
puppets = read_code("src/F4TNpcSyncPuppets.cpp")
suppress = read_code("src/F4TNpcSyncSuppress.cpp")

check("inline constexpr std::chrono::milliseconds kMissingNpcGrace{ 750 };" in internal,
      "the release grace must be one shared constant in F4TNpcSyncInternal.h")
check("constexpr auto kMissingNpcGrace" not in puppets,
      "F4TNpcSyncPuppets.cpp must not keep its own grace constant")
check("kMissingNpcGrace" in puppets, "puppet removal must keep using the shared grace")
check("Clock::time_point missingSince{};" in internal,
      "SuppressedActor must carry missingSince so release waits out the grace")
check("bool HasLocalFollowerMatch(const NpcState& a_state, RE::PlayerCharacter& a_player);" in internal,
      "F4TNpcSyncInternal.h must declare HasLocalFollowerMatch")

follower = function_body(suppress, "bool IsLocalPlayerFollower(")
check("niFlags.flags & kPlayerTeammateFlag" in follower and "1U << 26" in follower
      and follower.find("kPlayerTeammateFlag") < follower.find("commandingActor"),
      "IsLocalPlayerFollower must accept the player-teammate flag (niFlags bit 26) before the commandingActor test")
check("currentProcess->middleHigh" in follower and "commandingActor.get().get()" in follower
      and "std::addressof(a_player)" in follower,
      "IsLocalPlayerFollower must compare currentProcess->middleHigh->commandingActor with the local player")
suppress_raw = (PLUGIN / "src/F4TNpcSyncSuppress.cpp").read_text(encoding="utf-8")
check(re.search(r"// requires Actor\+0x2D0 bit 26 and middleHigh commandingActor offsets, source:E:/GardenEngine/re-answers/2026-10-27-commanding-actor-follower\.md\s*\n\s*bool IsLocalPlayerFollower\(", suppress_raw) is not None,
      "IsLocalPlayerFollower must carry its RE source citation directly above it")
for signature in ("RE::Actor* FindMatchingSourceActor(", "bool HasMatchingAliveLocalNpc(", "void SuppressDuplicateLocalNpcs("):
    check("IsLocalPlayerFollower(" in function_body(suppress, signature),
          f"{signature} must exclude local followers")

follower_match = function_body(suppress, "bool HasLocalFollowerMatch(")
check("IsLocalPlayerFollower(" in follower_match and "StableBaseFormId(" in follower_match,
      "HasLocalFollowerMatch must match a local follower by source and base identity")
update_puppets = function_body(puppets, "void UpdatePuppets(")
check(0 <= update_puppets.find("HasLocalFollowerMatch(state, a_player)") < update_puppets.find("states.emplace("),
      "UpdatePuppets must drop local-follower states before any puppet is kept or spawned")

suppress_body = function_body(suppress, "void SuppressDuplicateLocalNpcs(")
check("suppressed.missingSince = Clock::now();" in suppress_body,
      "the first missing tick must start the grace timer")
grace = suppress_body.find("Clock::now() - suppressed.missingSince < kMissingNpcGrace")
restore = suppress_body.find("SetAlpha(suppressed.originalAlpha)")
check(0 <= grace < restore,
      "the restore must sit behind the grace check so a one-tick gap never reveals the NPC")
hold = suppress_body[grace:restore]
check(0 <= hold.find("GetAlpha() != 0.0F") < hold.find("SetAlpha(0.0F)"),
      "the grace window must re-apply the hidden alpha, and only when GetAlpha differs from 0")
check(0 <= suppress_body.find("suppressed.missingSince = Clock::now();") < grace,
      "the first missing tick must enter the grace check")
check("suppressed.missingSince = {};" in suppress_body,
      "a matched actor must reset its grace timer")
check("suppressed.handle.get()" in suppress_body and "suppressIfDuplicate(*actor)" in suppress_body,
      "suppressed actors must be re-matched through their handle, not only through process-list visits")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TNpcSuppressGraceContract: all checks passed.")
