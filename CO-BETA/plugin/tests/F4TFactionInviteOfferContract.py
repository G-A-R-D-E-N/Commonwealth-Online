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
check(in_order(activate_edge, "activateHeld && !g_activateWasHeld", "OfferForActivation();"),
      "Tick must offer only on the rising edge of the Activate hold")
check("F4T::Downed::Engine::IsActivateHeld" not in function_body(glue, "void OfferForActivation()"),
      "OfferForActivation must not poll the key itself")

offer = function_body(glue, "void OfferForActivation()")
check("F4T::Downed::Engine::CanTouchWorld()" in offer, "OfferForActivation must respect the loading-screen guard")
check("ReadFactionView()" in offer and "view->localCanInvite" in offer,
      "OfferForActivation must gate on the local invite right")

view_body = function_body(glue, "std::optional<FactionView> ReadFactionView()")
check("F4T::Factions::InFaction()" in view_body, "ReadFactionView must check membership with InFaction")
check("F4T::Factions::BuildStateJson()" in view_body, "ReadFactionView must read the rank data the plugin already keeps")
check('member.value("leader", false) || member.value("officer", false)' in glue,
      "invite rights must come from the leader or officer flags, matching the server rule")

target_body = function_body(glue, "std::optional<std::uint32_t> FindOfferTarget(")
check("F4T::ProxyActorController::GetActiveProxyActors()" in target_body,
      "the target must be a remote player's active proxy actor")
check("IsActivationAim(distance, cosine)" in target_body and
      "ShouldOffer(a_view.localCanInvite, a_view.localPlayerId, proxy.remotePlayerId, inFaction, downed)" in target_body,
      "FindOfferTarget must apply the pure aim and decision checks")
check("const bool downed = IsDowned(proxy.remotePlayerId);" in target_body,
      "FindOfferTarget must read the remote snapshot downed flag before the decision")
is_downed = function_body(glue, "bool IsDowned(")
check("GetRemotePlayerSnapshot()" in is_downed and "vitals->isDowned" in is_downed and "vitals->isDead" in is_downed,
      "IsDowned must read vitals.isDowned and vitals.isDead from the remote snapshot, the same downed test as the revive pose")

show = function_body(glue, "void ShowOffer(")
check('"Invite {} to {}?"' in show, "the prompt text must be 'Invite <name> to <faction>?'")
check("ui->ShowAlert(&request, &OnOfferResponse" in show, "the prompt must use the existing CO UI ShowAlert facility")
check('request.tone = "invite";' in show and 'request.category = "Social";' in show,
      "the prompt must use the invite tone and the Social category like the faction invite alert")

response = function_body(glue, "void OnOfferResponse(")
check('std::string_view{ a_actionId } != kConfirmAction' in response, "only the confirm action may send an invite")
check(re.search(r'Json\{\s*\{\s*"op",\s*"invite"\s*\},\s*\{\s*"targetPlayerId",\s*targetPlayerId\s*\}\s*\}\.dump\(\)', response) is not None,
      "confirm must send the existing invite op with targetPlayerId")
check("F4T::Factions::HandleAction(" in response, "confirm must send through F4T::Factions::HandleAction")

check("F4TFactionInviteOffer" not in factions_cpp and "F4TFactionInviteOffer" not in factions_h,
      "F4TFactions must not be edited for the invite offer")
check("FactionInviteOffer" not in proxy_no_talk,
      "the offer must not hook F4TProxyNoTalk: the activation route is recorded in the code-notes entry")

check("F4T::FactionInviteOffer::Tick();" in runtime and
      in_order(runtime, "F4T::Downed::Tick();", "F4T::FactionInviteOffer::Tick();"),
      "the offer tick must run every frame after the downed tick that installs the Activate handler")
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
