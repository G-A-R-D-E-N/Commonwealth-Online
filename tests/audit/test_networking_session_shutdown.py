import inspect
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NETWORK_IMPL_SOURCE = ROOT / "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
UI_SOURCE = ROOT / "CO-BETA/plugin/src/F4TUIClient.cpp"
UI_HEADER = ROOT / "CO-BETA/plugin/include/F4TUIClient.h"

def test_server_session_end_invalidates_pending_join_on_the_game_thread():
    network = NETWORK_IMPL_SOURCE.read_text(encoding="utf-8")

    assert '#include "F4TSessionLaunch.h"' in network

    packet_start = network.index("void HandleSessionEndedPacket(")
    packet_end = network.index("\n\tvoid HandleUnknownPacket", packet_start)
    packet = network[packet_start:packet_end]

    assert "F4T::Networking::DisconnectFromLocalServer();" in packet
    assert 'F4T::UIClient::NotifyTransportDisconnected("Server ended the session.");' in packet, (
        "a server-initiated session end must route through NotifyTransportDisconnected with its "
        "own specific reason, the same seam a receive-thread transport loss uses, so both paths "
        "cancel a pending join and clear connection ownership identically."
    )
    assert "taskInterface->AddTask(" in packet

    disconnect_index = packet.index("F4T::Networking::DisconnectFromLocalServer();")
    notify_index = packet.index('F4T::UIClient::NotifyTransportDisconnected("Server ended the session.");')
    add_task_index = packet.index("taskInterface->AddTask(")
    assert disconnect_index < notify_index < add_task_index, (
        "the transport must be torn down, then the pending join invalidated, before the "
        "notification task is even queued, so a superseded/cancelled join can never be "
        "reported as successful by a task still in flight."
    )

    ui_source = UI_SOURCE.read_text(encoding="utf-8")
    notify_start = ui_source.index("void NotifyTransportDisconnected(")
    notify_end = ui_source.index("\n\tvoid ShowOfflineSaveIgnoredNotification", notify_start)
    notify = ui_source[notify_start:notify_end]

    assert "g_connectionOwnerToken.store(0);" in notify, (
        "NotifyTransportDisconnected must clear connection ownership synchronously so a stale "
        "token cannot outlive the transport, regardless of which caller (session-ended packet or "
        "receive-thread loss) observed the disconnect."
    )
    assert "taskInterface->AddTask(" in notify
    inner_task_start = notify.index("taskInterface->AddTask(")
    inner_task = notify[inner_task_start:]
    assert "F4T::SessionLaunch::CancelPendingJoin(" in inner_task, (
        "pending-join invalidation must run inside NotifyTransportDisconnected's own F4SE "
        "game-thread task, not on the receive thread that dispatches server packets."
    )
    assert "g_joinGeneration.fetch_add(1);" in inner_task, (
        "every transport-disconnect notification must also bump the join generation so any "
        "in-flight join continuation queued before the disconnect revalidates itself as stale."
    )

def test_connection_ownership_token_is_reset_when_the_owned_connection_is_torn_down():
    source = UI_SOURCE.read_text(encoding="utf-8")

    disconnect_start = source.index("void Callback_Disconnect()")
    disconnect_end = source.index("\n\t\t}\n", disconnect_start) + len("\n\t\t}\n")
    disconnect = source[disconnect_start:disconnect_end]
    assert "RunOrDeferSessionExit(" in disconnect and "DisconnectNow" in disconnect, (
        "Callback_Disconnect must route the explicit disconnect through the save guard into DisconnectNow."
    )

    now_start = source.index("void DisconnectNow()\n")
    now_end = source.index("\n\t\t}\n", now_start) + len("\n\t\t}\n")
    now = source[now_start:now_end]
    assert "g_connectionOwnerToken.store(0);" in now, (
        "an explicit disconnect must clear connection ownership so a later stale "
        "attempt sharing that token cannot believe it still owns the torn-down connection."
    )

    launch_start = source.index("auto deferredCompletion =")
    launch_end = source.index("\n\t\tvoid Callback_Disconnect()", launch_start)
    launch = source[launch_start:launch_end]
    failed_launch_index = launch.index("if (!ok) {")
    failed_launch_end = launch.index("g_joinInProgress.store(false);", failed_launch_index)
    failed_launch_block = launch[failed_launch_index:failed_launch_end]
    assert "g_connectionOwnerToken.store(0);" in failed_launch_block, (
        "a first-time join that connects and then fails LaunchValidatedSession must also "
        "clear connection ownership when it tears its own connection back down."
    )

    assert "void ResetConnectionOwnership()" in source
    reset_start = source.index("void ResetConnectionOwnership()")
    reset_end = source.index("\n\t}", reset_start)
    reset = source[reset_start:reset_end]
    assert "g_connectionOwnerToken.store(0);" in reset

    header = UI_HEADER.read_text(encoding="utf-8")
    assert "void ResetConnectionOwnership();" in header, (
        "ResetConnectionOwnership must be exported so other translation units (the "
        "server-session-end path) can clear ownership without reaching into UIClient's "
        "anonymous namespace."
    )

    network = NETWORK_IMPL_SOURCE.read_text(encoding="utf-8")
    packet_start = network.index("void HandleSessionEndedPacket(")
    packet_end = network.index("\n\tvoid HandleUnknownPacket", packet_start)
    packet = network[packet_start:packet_end]
    assert "F4T::UIClient::ResetConnectionOwnership();" in packet, (
        "a server-initiated session end must also clear connection ownership, matching "
        "the explicit-disconnect path, so a stale token cannot outlive its connection."
    )
    disconnect_call_index = packet.index("F4T::Networking::DisconnectFromLocalServer();")
    reset_call_index = packet.index("F4T::UIClient::ResetConnectionOwnership();")
    assert reset_call_index > disconnect_call_index


def main() -> int:
    failures: list[str] = []
    tests = [(name, value) for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for name, func in tests:
        try:
            if "tmp_path" in inspect.signature(func).parameters:
                with tempfile.TemporaryDirectory() as directory:
                    func(Path(directory))
            else:
                func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
