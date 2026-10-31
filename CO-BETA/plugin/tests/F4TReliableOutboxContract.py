from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    path = PLUGIN / relative
    if not path.exists():
        failures.append(f"missing {relative}")
        return ""
    return path.read_text(encoding="utf-8")


def body_after(text, anchor):
    start = text.find(anchor)
    if start < 0:
        return None
    brace = text.index("{", start + len(anchor))
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
    return None


shim = "\n".join(read(f"src/{name}") for name in ("F4TGnsWinsockShim.cpp", "F4TGnsWinsockShimBridge.cpp", "F4TGnsWinsockShimFrames.cpp"))
state_header = read("src/F4TGnsWinsockShimState.h")
glue = read("src/F4TGnsReliableSend.cpp")
outbox = read("include/F4TReliableOutbox.h")

check('#include "F4TReliableOutbox.h"' in shim, "shim must use the reliable outbox")
check("#include \"RE/" not in outbox and "Windows.h" not in outbox, "outbox header must stay engine and platform independent")
check("sleep_for" not in outbox, "the outbox must never sleep")

state = body_after(state_header, "struct SocketState")
check(state is not None and "F4T::ReliableOutbox::Outbox reliableOutbox;" in state, "each connection must own one reliable outbox")

send = body_after(shim, "bool SendAdaptedMessages(")
check(send is not None, "SendAdaptedMessages not found")
if send is not None:
    check("sleep_for" not in send, "SendAdaptedMessages must not sleep on backpressure")
    lock = send.find("const std::scoped_lock bridgeLock(a_state->bridgeMutex);")
    flush = send.find("DrainReliableOutboxLocked(*api, *a_state)")
    submit = send.find("a_state->reliableOutbox.Submit(")
    check(0 <= lock < flush < submit, "gameplay sends must drain queued reliable messages before submitting new ones, under the bridge lock")
    unreliable = send.find("if (message.delivery == F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced) {")
    tolerated = send.find("if (result != kSendSent && result != kSendDropped && result != kSendBackpressure) {", unreliable)
    check(0 <= unreliable < tolerated < submit, "UnreliableSequenced snapshots must keep dropping on backpressure and never enter the outbox")
    check("SubmitResult::Full" in send, "a full outbox must fail the send")

recv = body_after(shim, "int Recv(SOCKET a_socket")
check(recv is not None, "Recv not found")
if recv is not None:
    check("sleep_for" not in recv, "Recv must not sleep")
    flush = recv.find("FlushReliableOutbox(state)")
    poll = recv.find("api->poll(")
    check(0 <= flush < poll, "the receive loop must drain the reliable outbox before polling")

flush_fn = body_after(shim, "bool FlushReliableOutbox(")
check(flush_fn is not None, "FlushReliableOutbox not found")
if flush_fn is not None:
    lock = flush_fn.find("const std::scoped_lock bridgeLock(a_state->bridgeMutex);")
    flush = flush_fn.find("DrainReliableOutboxLocked(*api, *a_state)")
    check(0 <= lock < flush, "FlushReliableOutbox must drain under the bridge lock")

drain = body_after(glue, "bool DrainReliableOutboxLocked(")
check(drain is not None, "DrainReliableOutboxLocked not found")
if drain is not None:
    check("a_state.reliableOutbox.Flush(" in drain, "the drain must flush the connection outbox")
    check("FlushResult::TimedOut" in drain and "SetError(WSAENOBUFS)" in drain, "a timed-out reliable message must fail the send like the old retry window")
check((shim + glue).count("reliableOutbox.Flush(") == 1, "only DrainReliableOutboxLocked may flush the outbox")
check(shim.count("DrainReliableOutboxLocked(*api, *a_state)") == 2, "exactly the gameplay send and the receive loop drain the outbox")

close = body_after(shim, "int CloseSocket(SOCKET a_socket)")
check(close is not None and "state->reliableOutbox.Clear();" in close, "closing a connection must discard its queued reliable messages")

check("kBackpressureSleep" not in shim, "the backpressure sleep constant must be gone")
check("kReliableBackpressureTimeout" not in shim, "the acceptance timeout must live in the outbox")
check("std::chrono::milliseconds{ 250 }" in outbox, "the per-message acceptance timeout must stay 250 ms")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TReliableOutboxContract: all checks passed.")
