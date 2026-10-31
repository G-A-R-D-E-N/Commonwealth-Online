from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
INBOUND = ROOT / "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
EXPECTED_INCLUDE = '#include "F4TConsoleGuard.h"'
EXPECTED_CALL = "F4T::ConsoleGuard::NoteWelcome(a_packet);"
OWNER = "F4TNetworkingInbound.cpp belongs to the sync-proxy workstream; the console-block branch must not edit it"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


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
                return source[brace + 1:index]
    return None


text = INBOUND.read_text(encoding="utf-8") if INBOUND.exists() else ""
check(bool(text), f"missing {INBOUND}")
check(EXPECTED_INCLUDE in text, f"F4TNetworkingInbound.cpp must include F4TConsoleGuard.h ({OWNER})")

body = body_after(text, "void HandleWelcomePacket(")
check(body is not None, "F4TNetworkingInbound.cpp must define HandleWelcomePacket")
if body is not None:
    check(
        body.count(EXPECTED_CALL) == 1,
        f"HandleWelcomePacket must call {EXPECTED_CALL} exactly once ({OWNER})",
    )
    inventory = body.find("F4T::InventorySync::NoteWelcome(a_packet);")
    objects = body.find("F4T::ObjectSync::NoteWelcome(a_packet);")
    guard = body.find(EXPECTED_CALL)
    check(
        0 <= inventory < objects < guard,
        "the console guard welcome must follow the inventory and object sync welcomes",
    )

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    print(
        "F4TConsoleGuardWelcomeIntegrationContract: EXPECTED RED on the console-block branch. "
        "The server role is inert until F4TNetworkingInbound.cpp:HandleWelcomePacket calls "
        "F4T::ConsoleGuard::NoteWelcome; the include and call lines are listed under INTEGRATION LINES "
        "in docs/code-notes/CO-BETA-plugin.md."
    )
    sys.exit(1)

print("F4TConsoleGuardWelcomeIntegrationContract: all checks passed.")
