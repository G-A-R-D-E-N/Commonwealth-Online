from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
src = root / "CO-BETA/plugin/src"
session = (src / "F4TSteamSession.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
launcher = (src / "F4TLocalServerLauncher.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
ui_client = (src / "F4TUIClient.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
server_main = (root / "CO-SERVER/server-cpp/src/ServerMain.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
gate = (root / "CO-BETA/plugin/include/F4TSteamJoinGate.h").read_text(encoding="utf-8").replace("\r\n", "\n")


def body(text, signature):
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[open_brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


assert "std::uint64_t Begin(Operation a_operation)" in gate, \
    "Begin must return the epoch the operation registered under"
assert "bool Finish(Operation a_operation, std::uint64_t a_epoch)" in gate, \
    "Finish must take the registration epoch so a stale worker cannot clear a newer operation"
assert "if (epoch != a_epoch)" in gate, "Finish must only clear the operation while its epoch is current"
assert "Operation Cancel()" in gate and "std::scoped_lock" in gate, \
    "the epoch and pending set are shared by the game thread and detached workers and need one lock"

for name, failure, operation in (
        ("OnGuestJoinComplete", "steamJoinFailed", "Join"),
        ("OnHostJoinComplete", "steamHostFailed", "HostStart")):
    handler = body(session, f"void {name}(")
    assert "Running()" not in handler, f"{name} must not drop a failed join because the tunnel already stopped"
    assert f"FinishOperation(SteamJoinGate::Operation::{operation}, epoch)" in handler, \
        f"{name} must claim the completion with the epoch that started it"
    assert f'PushFailed("{failure}"' in handler, f"{name} must report the failure to the UI"

run_async = body(session, "void RunAsync(")
assert "g_gate.Begin(a_operation)" in run_async, "the async Steam operations must register with the join gate"
assert "std::function<void(std::uint64_t)>" in session, \
    "the worker must run under the epoch it registered with, not a separately read epoch"
assert "if (FinishOperation(operation, epoch))" in run_async, \
    "a superseded worker must drop its failure because the cancel already reported it"

assert len(re.findall(r"BeginLocalJoin\([^;]*\bepoch, joinError\)", session)) == 2, \
    "both Steam joins must pass their epoch to BeginLocalJoin"
assert "reinterpret_cast<void*>(static_cast<std::uintptr_t>(a_epoch))" in body(session, "bool BeginLocalJoin("), \
    "BeginLocalJoin must hand the epoch to the completion callback"

join_worker = body(session, "void OnJoin(")
assert "if (FinishOperation(SteamJoinGate::Operation::Join, epoch))" in join_worker, \
    "the join worker must check the epoch before it pushes a failure"
assert "if (!EpochCurrent(epoch))" in join_worker, "the join worker must stop its tunnel when it was superseded"
host_worker = body(session, "void OnHostStart(")
assert "if (FinishOperation(SteamJoinGate::Operation::HostStart, epoch))" in host_worker, \
    "the host worker must check the epoch before it pushes a failure"
assert "if (!EpochCurrent(epoch))" in host_worker, "the host worker must stop its tunnel when it was superseded"

disconnect_now = body(ui_client, "void DisconnectNow()\n")
assert disconnect_now.index("F4T::SteamSession::CancelJoins();") < disconnect_now.index("CancelPendingJoin("), \
    "a cancel must bump the Steam epoch before CancelPendingJoin can complete the join"

cancel_joins = body(session, "void CancelJoins()")
assert "g_gate.Cancel()" in cancel_joins, "CancelJoins must supersede pending Steam join completions"
assert 'PushFailed("steamJoinFailed", "The join was cancelled.")' in cancel_joins, \
    "a cancelled Steam join must reach the UI instead of leaving the panel on connecting"
assert 'PushFailed("steamHostFailed", "Hosting was cancelled.")' in cancel_joins, \
    "a cancelled Steam host start must reach the UI instead of leaving the panel on starting"
assert "F4T::Steam::StopHost()" in cancel_joins and "F4T::Steam::StopGuest()" in cancel_joins, \
    "CancelJoins must drop the tunnels it cancelled"
assert "Has(cancelled, SteamJoinGate::Operation::Join)" in cancel_joins and \
    "Has(cancelled, SteamJoinGate::Operation::HostStart)" in cancel_joins, \
    "CancelJoins must report only the operations that were actually in flight"

assert "g_gate.Cancel()" in body(session, "void StopTunnels()"), \
    "StopTunnels must supersede pending Steam joins before stopping the tunnels"
assert "error_handler_t::replace" in body(session, "void Push("), \
    "Steam events carry server output and must not throw on invalid UTF-8"

ensure = body(launcher, "std::string EnsureRunning(")
assert 'L"server-console.log"' in ensure, "the launcher must capture the server output to server-console.log"
assert "PROC_THREAD_ATTRIBUTE_HANDLE_LIST" in ensure, "only the log handle may be inherited by the server"
assert "LastLogLine(consoleLog)" in ensure, "an early server exit must report the server's last output line"
assert "RotateConsoleLog(consoleLog)" in ensure, "a retry must rotate the previous server output instead of overwriting it"
rotate = body(launcher, "void RotateConsoleLog(")
assert 'L"server-console.prev.log"' in rotate and "MOVEFILE_REPLACE_EXISTING" in rotate, \
    "the rotated server output must land in server-console.prev.log"
assert "SetHandleInformation(logHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) != FALSE" in ensure, \
    "the launch must fall back to the console path when the log handle cannot be made inheritable"
assert "SetHandleInformation(logHandle, HANDLE_FLAG_INHERIT, 0)" in ensure, \
    "the log handle must stop being inheritable after the launch"
assert "attributeListReady" in ensure and "DeleteProcThreadAttributeList" in ensure, \
    "the process attribute list must be deleted whenever it was initialized"
assert "sizeof(STARTUPINFOW)" in ensure, \
    "the launch without captured output must use the plain STARTUPINFO size"
assert "inheritLog ? LastLogLine(consoleLog)" in ensure, \
    "an early exit must not read a stale log from a previous launch"
assert '" Full output: Data\\\\server\\\\server-console.log."' in ensure, \
    "the reported server line must be separated from the rest of the error"
assert "MentionsFailure" not in launcher, \
    "an earlier line that mentions missing must not replace the last log line"
assert "return SelectLastLogLine(text);" in body(launcher, "std::string LastLogLine("), \
    "LastLogLine must return the last non-empty line of the captured log"

assert "std::setvbuf(stdout, nullptr, _IONBF, 0)" in server_main, \
    "the server must not lose its redirected output tail to stdio buffering"
assert "std::setvbuf(stderr, nullptr, _IONBF, 0)" in server_main, \
    "the server must not lose its redirected error tail to stdio buffering"

print("steam join failure contract: ok")
