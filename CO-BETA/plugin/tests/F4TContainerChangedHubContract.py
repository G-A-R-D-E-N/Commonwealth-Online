from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
header = (plugin / "include/F4TContainerChangedHub.h").read_text(encoding="utf-8")
source = (plugin / "src/F4TContainerChangedHub.cpp").read_text(encoding="utf-8")
runtime = (plugin / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")
main = (plugin / "src/main.cpp").read_text(encoding="utf-8")


def body(text, marker):
    start = text.index("{", text.index(marker))
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start + 1:end - 1]


all_code = "\n".join(p.read_text(encoding="utf-8") for folder in ("include", "src")
                     for p in (plugin / folder).glob("**/*") if p.suffix in (".cpp", ".h"))
assert "kEnableContainerChangedHub" not in all_code and "kCoTestFeatures" not in all_code
assert "kRingCapacity = 1024;" in header, "ring capacity must stay 1024"
assert "std::array<ContainerChange, kRingCapacity> g_ring" in source
assert "std::is_trivial_v<ContainerChange>" in header
sink = body(source, "ProcessEvent(")
for forbidden in ("RemoveItem", "AddItem", "AddInventoryItem", "AddObjectToContainer", "GetInventory", "inventoryList", "GetFormByID", "RegisterSink", "DebugLogging", "Subscribe", "Drain"):
    assert forbidden not in sink, f"forbidden sink operation: {forbidden}"
assert not re.search(r"\bnew\b|\bwhile\s*\(|\bfor\s*\(", sink)
assert set(re.findall(r"\b(\w+)\s*\(", sink)) <= {"if", "constexpr", "Enqueue", "GetCurrentThreadId", "MonotonicMs"}, "sink may only copy fields and enqueue"
for field in ("oldContainerFormID", "newContainerFormID", "baseObjectFormID", "itemCount", "referenceFormID", "uniqueID"):
    assert sink.count("a_event." + field) == 1, field
assert sink.count("Enqueue(") == 1
assert sink.count("return RE::BSEventNotifyControl::kContinue;") == 1
assert "kStop" not in sink
queue = body(source, "void Enqueue(")
assert "std::try_to_lock" in queue and "!lock.owns_lock()" in queue, "sink must never wait for ring lock"
assert "g_size >= kRingCapacity" in queue, "ring cap check required"
assert queue.index("g_size >= kRingCapacity") < queue.index("g_ring[")
assert "g_dropped.fetch_add(1" in queue
assert "push_back" not in queue and "DebugLogging" not in queue
install = body(source, "void Install(")
assert install.index("GetEventSource()") < install.index("RegisterSink(&g_sink)")
assert "!OnGameThread() || g_registered" in install and "g_registered = true;" in install
assert source.count("RegisterSink(") == 1
assert '#if' not in install and 'NG getter id unverified' not in source
assert install.index('GetEventSource()') < install.index('RegisterSink(&g_sink)')
shutdown = body(source, "void Shutdown(")
assert shutdown.index("if (!g_registered)") < shutdown.index("GetEventSource()") < shutdown.index("UnregisterSink(&g_sink)")
assert "g_registered = false;" in shutdown, "Shutdown must clear the registration flag"
assert source.count("UnregisterSink(") == 1
assert "void Shutdown();" in header
assert "std::atexit(F4T::ContainerChangedHub::Shutdown);" in main, "Shutdown must be hooked from main.cpp"
for method in ("void Drain(", "bool Subscribe("):
    section = body(source, method)
    assert "!OnGameThread()" in section
drain = body(source, "void Drain(")
assert drain.index("!OnGameThread()") < drain.index("g_ring[") < drain.index("subscriber(batch[i])")
assert "g_draining" in drain and "const auto subscribers = g_subscribers;" in drain
assert drain.index("if (count == 0)") < drain.index("const auto subscribers = g_subscribers;"), "an empty ring must return before the subscriber copy"
assert re.search(r"g_size = 0;\s*}\s*if \(count == 0\)\s*\{\s*return;\s*\}\s*const auto subscribers", drain), "callbacks must run outside ring lock"
assert "g_logLines >= kDiagnosticLinesPerSecond" in source
assert "now - g_logWindowStart >= 1000" in source
assert "kDiagnosticLinesPerSecond = 8;" in header
assert '"old={:08X} new={:08X} base={:08X} count={} ref={:08X} unique={} thread={}' in source
assert "ContainerChangedHub::Install();\n\t\tF4T::ContainerChangedHub::Drain();" in runtime
for name in ("Install", "Drain"):
    callers = [p.name for p in (plugin / "src").glob("*.cpp") if f"ContainerChangedHub::{name}(" in p.read_text(encoding="utf-8")]
    assert callers == ["F4TReplicationRuntime.cpp"], (name, callers)
assert re.search(r"AddPermanentTask\(\[\]\(\)\s*{\s*F4T::ReplicationRuntime::Tick\(\);", main)
neutral = (root / "co-core/include/F4TEchoLedger.h").read_text(encoding="utf-8")
loot = (root / "co-core/include/F4TNpcLootSync.h").read_text(encoding="utf-8")
assert "namespace F4T\n{" in neutral and "NpcLootSync" not in neutral and "nlohmann" not in neutral
assert "using EchoLedger = F4T::EchoLedger;" in loot
print("F4TContainerChangedHubContract: all checks passed.")
