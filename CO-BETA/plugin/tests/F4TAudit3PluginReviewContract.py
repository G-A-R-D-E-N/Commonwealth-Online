"""Contract: audit 3 plugin review fixes. Verified record source:CodeNotes-GARDEN history/audit3-plugin-review.md."""
from pathlib import Path

plugin = Path(__file__).resolve().parents[1]


def read(name):
    return (plugin / name).read_text(encoding="utf-8")


def function_body(code, signature):
    start = code.index(signature)
    indent = code[code.rindex("\n", 0, start) + 1:start]
    return code[start:code.index("\n" + indent + "}\n", start) + len(indent) + 3]


net = read("src/F4TNetworking.cpp")
recv_loop = function_body(net, "void ReceiveLoop()")
assert "recv(socket," in recv_loop and "WSAEWOULDBLOCK" in recv_loop and "sleep_for(kReceiveIdleSleep)" in recv_loop, "the only Recv caller polls again after an idle sleep"
shim = read("src/F4TGnsWinsockShim.cpp")
recv = function_body(shim, "int Recv(SOCKET a_socket")
assert "PollDeferredHello()" in recv and "SetError(WSAEWOULDBLOCK);" in recv, "the shim Recv never blocks and polls the deferred hello whenever the bridge is idle"
assert recv.index("PollDeferredHello()") < recv.index("SetError(WSAEWOULDBLOCK);"), "the deferred hello is polled before an idle Recv returns"

refs = read("src/F4TWorkshopWorldRefs.cpp")
restore = function_body(refs, "void Restore(")
assert "g_hidden.contains(formId)" in restore and "g_hiddenMutex" in restore and restore.index("g_hidden.contains(formId)") < restore.index("Enable(false)"), "a restore must skip refs the new session currently hides"

assert "g_retiring.erase(formId);" in restore, "a restore must clear the retiring mark"
detach = function_body(refs, "std::set<std::uint32_t> Detach()")
assert "g_retiring.insert(g_hidden.begin(), g_hidden.end());" in detach, "detached ids stay marked until their restore runs"
hide = function_body(refs, "void Hide(")
assert "g_retiring.contains(a_formId)" in hide and hide.index("kMatchDistanceSq") < hide.index("IsDisabled()") < hide.index("ref->Disable();"), "a new session adopts a still-disabled retiring ref instead of losing its hide"

bag = read("src/F4TDeathBagLocal.cpp")
stack_id = function_body(bag, "std::optional<std::uint32_t> CurrentStackId(")
assert "if (wanted) return data == wanted;" in stack_id and "plainCount == 1" in stack_id and "!wanted" in stack_id, "a plain stack is picked only when it is the sole plain stack"

assert "std::recursive_timed_mutex g_connectionOperationMutex;" in net
shutdown = function_body(net, "void DisconnectForShutdown()")
assert "std::unique_lock operationLock(g_connectionOperationMutex, std::chrono::seconds{" in shutdown and "if (!operationLock.owns_lock()) return;" in shutdown, "the exit cleanup must give up instead of hanging on a held connection lock"
assert shutdown.index("owns_lock()") < shutdown.index("StopReceiveThread();"), "the cleanup runs only once the lock is held"
print("audit3 plugin review contract ok")
