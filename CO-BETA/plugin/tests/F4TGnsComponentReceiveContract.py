from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

# The shim was 954 lines before I6; the component receive path moved out of it.
K_MAXIMUM_SHIM_LINES = 953

CAPABILITIES = (
    "inventory-stable-v1",
    "component-snapshot-v1",
    "component-snapshot-channels-v1",
)

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def read_code(relative):
    return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


def brace_body(text, start):
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError("unterminated body")


def function_body(code, signature):
    return brace_body(code, code.index(signature))


def regex_in_order(text, *patterns):
    position = 0
    for pattern in patterns:
        found = re.compile(pattern, re.S).search(text, position)
        if not found:
            return False
        position = found.end()
    return True


shim_source = (PLUGIN / "src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
shim = strip_comments(shim_source)
receive = read_code("src/F4TGnsComponentReceive.cpp")
receive_header = read_code("include/F4TGnsComponentReceive.h")
sync = read_code("src/F4TInventorySync.cpp")
sync_header = read_code("include/F4TInventorySync.h")
identity = read_code("src/F4TClientIdentity.cpp")
compatibility = strip_comments(
    (ROOT / "co-core/src/session/F4TClientCompatibility.cpp").read_text(encoding="utf-8"))

check(len(shim_source.splitlines()) <= K_MAXIMUM_SHIM_LINES,
      f"F4TGnsWinsockShim.cpp must not grow past the pinned {K_MAXIMUM_SHIM_LINES} lines; "
      f"found {len(shim_source.splitlines())}")
check('#include "F4TGnsComponentReceive.h"' in shim, "the shim must take the component receive path from the new module")

component_branch = ""
branch_start = shim.find("F4T::Compatibility::ReceiveClassification::Component")
if branch_start < 0:
    failures.append("the shim has no component classification branch")
else:
    component_branch = brace_body(shim, shim.rindex("if (", 0, branch_start))
    check("F4T::ComponentReceive::HandleComponentFrame(classified)" in component_branch,
          "the component branch must delegate to the new module")
    check(regex_in_order(component_branch, r"const auto componentError = ", r"if \(componentError\.empty\(\)\) \{ continue; \}",
                         r"DisconnectForProtocolViolation\(state, componentError\)"),
          "the component branch must keep the disconnect for a component error and continue otherwise")
check("F4T::ComponentReceive::ClassifyReceive(*state," in shim,
      "the shim must classify through the component receive module")
check("component receive path is unavailable" not in shim,
      "the shim must not keep the old component disconnect literal")
check('kComponentPathUnavailable = "component receive path is unavailable"' in receive_header,
      "the module must own the historical component disconnect string")
check("kComponentPathUnavailable" not in receive,
      "the component receive path has no gate-off answer; it always decodes")
check("F4T::Drops::DecodeStableDroppedObject" in receive,
      "the component payload must be decoded by co-core")
check("F4T::InventorySync::ApplyDropFrame(" in receive,
      "a decoded component frame must feed the drop mirror table")
check("RE::" not in receive and "SendPacketBytes" not in receive,
      "the component receive path must not call the engine or send packets")
check("kCoTestFeatures" not in receive and "kEnableInventorySync" not in receive,
      "the component receive path must not be behind a gate")

for capability in CAPABILITIES:
    check(capability not in compatibility,
          f"co-core canonical metadata must not carry {capability}")
check(compatibility.count("capabilities = {}") == 3,
      "the co-core canonical metadata tables must stay capability-free")

for declaration, assignment in (
    ('kInventoryProtocolCapability = "inventory-stable-v1"', CAPABILITIES[0]),
    ('kSnapshotCapability = "component-snapshot-v1"', CAPABILITIES[1]),
    ('kSnapshotChannelCapability = "component-snapshot-channels-v1"', CAPABILITIES[2]),
):
    check(declaration in sync_header, f"the inventory sync header must name {assignment}")

capability_add = function_body(sync_header, "inline void AddSessionCapabilities(")
check(regex_in_order(capability_add, r"for \(const auto capability : kSessionCapabilities\)", r"a_metadata\.capabilities\.emplace_back\(capability\)"),
      "AddSessionCapabilities must advertise the session capabilities unconditionally")
check("F4T::InventorySync::AddSessionCapabilities(metadata);" in identity,
      "the packaged client metadata must take the gated capabilities")

install = function_body(receive, "void InstallComponentReceive(")
check(regex_in_order(install, r"componentReceiveInstalled",
                     r"RegisterDropFamily\(", r"componentCapabilities\.values\.emplace_back\(capability\)",
                     r"componentReceiveInstalled = true"),
      "the receive path must install the decoder and the capabilities once")
check("InstallComponentReceive(a_state);" in function_body(receive, "F4T::Compatibility::ReceiveDemuxResult ClassifyReceive("),
      "the classify entry point must install the gated decode path")
check("F4T::Compatibility::FamilyAdapter" in receive and "ComponentToLegacyDirection" in receive,
      "the drop family must be registered as a component family for the demux")
check("F4T::Replication::Channel::Reliable" in receive_header,
      "the drop family is published on the reliable channel")
check("F4T::Replication::Family::StableDroppedItem" in receive_header,
      "the receive path must be pinned to the stable drop family")

check("F4T::Drops::DropMirrorTable g_dropMirror;" in sync,
      "the inventory sync state must own one drop mirror table")
check(regex_in_order(function_body(sync, "bool ApplyDropFrame("),
                     r"std::scoped_lock lock\(g_mutex\)",
                     r"g_dropMirror\.ApplyFrame\(a_frame\) != F4T::Drops::MirrorApply::Rejected"),
      "ApplyDropFrame must lock and reject only what the mirror rejects")
check(regex_in_order(function_body(sync, "F4T::Drops::MirrorDiff TakeDropMirrorDiff("),
                     r"std::scoped_lock lock\(g_mutex\)",
                     r"g_dropMirror\.TakeDiff\(\)", r"g_bindings\.ShouldMaterialize\(entry\.entityId\)"),
      "the diff consumer must clear materialize for an entity the bindings already own")
check("g_dropMirror.Clear();" in function_body(sync, "void Reset()"),
      "a disconnect must clear the drop mirror")
check("status.mirroredEntities = g_dropMirror.Size();" in function_body(sync, "Status GetStatus()"),
      "the status must report the mirrored entity count")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TGnsComponentReceiveContract: all checks passed.")
