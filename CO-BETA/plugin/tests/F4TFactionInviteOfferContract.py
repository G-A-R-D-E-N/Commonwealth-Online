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
test_cpp = read_code(test_path)
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
check("IsDowned(proxy.remotePlayerId)" in wheel_source,
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

check('target("CommonwealthOnlineFactionInviteOfferTests")' in xmake and
      f'add_files("{test_path}")' in xmake,
      "xmake.lua must build the engine-free invite offer test")

check("assert(ShouldOffer(true, localId, remoteId, false, false));" in test_cpp,
      "the engine-free test must pin the offer case")
check("assert(!ShouldOffer(true, localId, remoteId, true, false));" in test_cpp,
      "the engine-free test must pin the in-faction refusal")
check("assert(!ShouldOffer(true, localId, remoteId, false, true));" in test_cpp,
      "the engine-free test must pin the downed refusal so revive keeps priority")

for relative in (header_path, glue_path, test_path):
    raw = (PLUGIN / relative).read_bytes()
    check(b"\r\n" in raw and raw.count(b"\n") == raw.count(b"\r\n"), f"{relative} must use CRLF line endings")
    check(len(raw.splitlines()) <= 750, f"{relative} must stay at or under 750 lines")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TFactionInviteOfferContract: all checks passed")
