from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []

    bridge = read("CO-SERVER/server/native_transport/co_gns_server_bridge.cpp") + read("CO-SERVER/server/native_transport/co_gns_server_bridge_internal.h")
    transport_failure = bridge.find("networking_->AcceptConnection(info.m_hConn) != k_EResultOK")
    transport_return = bridge.find("return;", transport_failure)
    transport_block = bridge[transport_failure:transport_return] if transport_failure >= 0 and transport_return >= 0 else ""
    require("applicationAdmitted" in bridge, "PR70-TRANSPORT: application admission must be tracked separately from GNS admission", failures)
    require("events_.push_back" in transport_block and "CO_GNS_EVENT_DISCONNECTED" in transport_block,
            "PR70-TRANSPORT: GNS failure after application admission must emit cleanup", failures)

    endpoint = read("CO-SERVER/server/native_transport/co_gns_server_endpoint.cpp")
    remote_start = endpoint.find("int co_gns_server_remote_ipv4(")
    remote_query = endpoint[remote_start:] if remote_start >= 0 else ""
    require("SteamNetworkingSockets()" not in remote_query and "server->Networking()" in remote_query,
            "PR70-ENDPOINT: remote IPv4 query must use the bridge handle interface, not the global GNS accessor", failures)
    require(0 <= remote_query.find("server->OwnsConnection(") < remote_query.find("GetConnectionInfo("),
            "PR70-ENDPOINT: remote IPv4 query must check handle ownership before querying GNS", failures)
    owns_start = bridge.find("bool OwnsConnection(")
    owns_block = bridge[owns_start:bridge.find("    };", owns_start)] if owns_start >= 0 else ""
    require("connections_.contains(connection)" in owns_block,
            "PR70-ENDPOINT: OwnsConnection must answer only for connections accepted by this bridge", failures)

    worker = read("CO-SERVER/server-cpp/include/WorkerExit.h")
    worker_grace = worker.find("WorkerStopGrace")
    require(worker_grace >= 0 and "a_exit.exited.wait_for(lock, WorkerStopGrace" in worker,
            "PR70-WORKER: a stop must wait a bounded grace for the worker before abandoning it", failures)
    abandon_store = worker.find("a_exit.abandoned.store(true")
    worker_join = worker.find("a_thread.join()")
    worker_detach = worker.find("a_thread.detach()")
    require(0 <= abandon_store < worker_detach,
            "PR70-WORKER: the abandon flag must be set before the worker is detached", failures)
    require(0 <= worker.find("if (finished) {") < worker_join < worker_detach,
            "PR70-WORKER: only a worker that reported finished may be joined", failures)

    verifier = read("CO-SERVER/server-cpp/src/SteamWebApiVerifier.cpp")
    verifier_header = read("CO-SERVER/server-cpp/include/SteamWebApiVerifier.h")
    require("std::thread([this]" not in verifier and "std::thread([state]" in verifier,
            "PR70-VERIFIER: the Steam worker must not capture the verifier it belongs to", failures)
    require("static void Run(std::shared_ptr<State> a_state);" in verifier_header,
            "PR70-VERIFIER: the Steam worker state must be shared with the thread", failures)
    require("std::shared_ptr<State> _state;" in verifier_header,
            "PR70-VERIFIER: the verifier must hold its worker state through a shared_ptr", failures)
    verifier_stop = verifier.find("void SteamWebApiVerifier::Stop()")
    verifier_stop_block = verifier[verifier_stop:verifier_stop + 200] if verifier_stop >= 0 else ""
    require("StopWorker(_thread, _state->exit)" in verifier_stop_block,
            "PR70-VERIFIER: shutdown must go through the bounded worker stop", failures)
    verifier_run = verifier.find("void SteamWebApiVerifier::Run(")
    verifier_run_block = verifier[verifier_run:] if verifier_run >= 0 else ""
    verifier_abandon = verifier_run_block.find("a_state->exit.abandoned.load(")
    require(0 <= verifier_abandon < verifier_run_block.find("SteamTicketOutcome::Retry && job.attempt"),
            "PR70-VERIFIER: an abandoned transfer must not be retried", failures)
    require(0 <= verifier_abandon < verifier_run_block.find("a_state->results.push_back("),
            "PR70-VERIFIER: an abandoned transfer must not deliver a verdict", failures)

    registrar = read("CO-SERVER/server-cpp/src/DirectoryRegistrar.cpp")
    registrar_header = read("CO-SERVER/server-cpp/include/DirectoryRegistrar.h")
    require("std::thread([this]" not in registrar and "std::thread([state, exit]" in registrar,
            "PR70-DIRECTORY: the directory worker must not capture the registrar it belongs to", failures)
    require("static void Worker(std::shared_ptr<State> a_state, std::shared_ptr<WorkerExit> a_exit);" in registrar_header,
            "PR70-DIRECTORY: the directory worker state must be shared with the thread", failures)
    registrar_stop = registrar.find("void DirectoryRegistrar::Stop()")
    registrar_stop_block = registrar[registrar_stop:registrar_stop + 200] if registrar_stop >= 0 else ""
    require(0 <= registrar_stop_block.find("if (StopWorker(_thread, *_exit)) {") <
            registrar_stop_block.find("Unregister();"),
            "PR70-DIRECTORY: the listing may only be deleted when the worker exited in time", failures)
    for symbol, label in (("DirectoryRegistrar::RegisterServer", "registration"),
                          ("DirectoryRegistrar::SendHeartbeat", "heartbeat")):
        call_start = registrar.find("std::chrono::seconds " + symbol + "(")
        call_block = registrar[call_start:call_start + 3000] if call_start >= 0 else ""
        sender_call = call_block.find("a_state.sender(")
        exit_lock = call_block.find("const auto exitLock = LockExit(a_exit);")
        abandoned_check = call_block.find("if (Abandoned(a_exit)) {")
        require(0 <= sender_call < exit_lock < abandoned_check,
                f"PR70-DIRECTORY: an abandoned {label} call must be dropped under the exit lock before it reports status", failures)
    require(registrar.count("activePlayers->load(") == 4,
            "PR70-DIRECTORY: the player count may only be read by the owner or under the worker's exit lock", failures)
    worker_start = registrar.find("void DirectoryRegistrar::Worker(")
    worker_block = registrar[worker_start:registrar.find("MarkWorkerExited", worker_start)] if worker_start >= 0 else ""
    require(0 <= worker_block.find("std::scoped_lock lock{ a_exit->mutex };")
            < worker_block.find("if (a_exit->stopping) {")
            < worker_block.find("players = a_state->activePlayers->load(")
            < worker_block.find("TickServer("),
            "PR70-DIRECTORY: the worker must read the player count under the exit lock after its stopping check", failures)
    require("activePlayers" not in registrar[registrar.find("Json DirectoryRegistrar::HeartbeatBody("):registrar.find("std::chrono::seconds DirectoryRegistrar::FailStatus(")],
            "PR70-DIRECTORY: the heartbeat body must use the player snapshot, not the live counter", failures)

    discovery = read("CO-SERVER/server-cpp/src/LanDiscoveryService.cpp")
    timeout_start = discovery.find("SO_RCVTIMEO")
    timeout_block = discovery[max(0, timeout_start - 120):timeout_start + 900] if timeout_start >= 0 else ""
    require("!ConfigureSocketTimeout(socketHandle, SO_RCVTIMEO" in timeout_block
            and "!ConfigureSocketTimeout(socketHandle, SO_SNDTIMEO" in timeout_block,
            "PR70-LAN: LAN discovery must install its receive and send timeouts", failures)
    require("return false" in timeout_block,
            "PR70-LAN: LAN discovery must fail startup when timeout installation fails", failures)
    helper_start = discovery.find("bool ConfigureSocketTimeout(")
    helper_block = discovery[helper_start:discovery.find("\n\t\t}", helper_start)] if helper_start >= 0 else ""
    require("setsockopt(" in helper_block and "a_option" in helper_block and "SocketTimeoutMilliseconds" in helper_block,
            "PR70-LAN: the socket timeout helper must install the requested option", failures)
    require("a_error = SocketError(a_operation)" in helper_block and "return !failed" in helper_block,
            "PR70-LAN: the socket timeout helper must report the failing call and fail startup", failures)

    stop_start = discovery.find("void LanDiscoveryService::Stop()")
    stop_block = discovery[stop_start:discovery.find("std::uint16_t LanDiscoveryService::LocalPort", stop_start)] if stop_start >= 0 else ""
    require(stop_block != "" and "shutdown(" not in stop_block,
            "PR70-LAN: shutdown() is inert on a datagram socket; Stop must close it instead", failures)
    windows_start = stop_block.find("#ifdef _WIN32")
    windows_block = stop_block[windows_start:stop_block.find("#else", windows_start)] if windows_start >= 0 else ""
    require(windows_block != "" and 0 <= windows_block.find("CloseSocket(") < windows_block.find("_thread.join()"),
            "PR70-LAN: Windows Stop must close the socket before joining the reader", failures)

    runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
    runtime_support = read("CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp")
    runtime_support_header = read("CO-SERVER/server-cpp/include/ServerRuntimeSupport.h")
    signal_units = runtime + runtime_support
    require("volatile std::sig_atomic_t g_running" in runtime_support,
            "PR70-SIGNAL: process shutdown state must use sig_atomic_t in the signal handler", failures)
    require("extern volatile std::sig_atomic_t g_running" in runtime_support_header,
            "PR70-SIGNAL: sig_atomic_t shutdown state must be shared through its own header", failures)
    require("std::atomic_bool g_running" not in signal_units + runtime_support_header,
            "PR70-SIGNAL: signal handler must not depend on std::atomic_bool::store", failures)
    require("g_running.store" not in signal_units and "g_running.load" not in signal_units,
            "PR70-SIGNAL: sig_atomic_t state must use signal-safe reads and writes", failures)
    require("g_running = 0" in runtime_support and "g_running = 0" in runtime,
            "PR70-SIGNAL: sig_atomic_t state must use assignment", failures)
    require("while (g_running != 0)" in runtime,
            "PR70-SIGNAL: sig_atomic_t state must use comparison", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("PR 70 transport/shutdown regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
