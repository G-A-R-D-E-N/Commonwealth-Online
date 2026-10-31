from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI_SOURCE = ROOT / "CO-BETA" / "plugin" / "src" / "F4TUIClient.cpp"

def test_join_worker_is_owned_and_never_detached():
    source = UI_SOURCE.read_text(encoding="utf-8")

    assert ".detach()" not in source, (
        "the join worker must stay owned by g_joinWorker so a new join can refuse while it runs "
        "and reap it once finished; a detached thread has no owner and can outlive the join state "
        "it reports into."
    )

    join_start = source.index("bool Callback_JoinSession(")
    join_end = source.index("\n\t\tvoid Callback_Disconnect()", join_start)
    join = source[join_start:join_end]

    assert "if (g_joinWorkerRunning.load() || g_joinInProgress.exchange(true)) {" in join, (
        "a join must be refused while the previous worker still runs, even after a cancel cleared "
        "g_joinInProgress, so two connect attempts never overlap."
    )
    assert join.index("g_joinWorker->join();") < join.index("g_joinWorker = new std::thread("), (
        "the finished previous worker must be joined before the owned pointer is replaced."
    )
    assert join.index("taskInterface->AddTask([") < join.index("g_joinWorkerRunning.store(false);"), (
        "the worker must clear its running flag only after it has posted its completion task."
    )
    assert "dropped stale join completion" in source, (
        "a stale deferred completion must be logged when it is dropped."
    )


def test_stale_attempt_disconnects_only_its_own_connection():
    source = UI_SOURCE.read_text(encoding="utf-8")

    worker_start = source.index("auto worker = [host,")
    stale_start = source.index("if (g_joinGeneration.load() != joinGeneration) {")
    worker = source[worker_start:stale_start]
    assert worker.index("if (connected) {") < worker.index("g_connectionOwnerToken.store(attemptToken);") < worker.index("taskInterface->AddTask("), (
        "the worker must publish the owner token right after a successful connect and before it queues "
        "the completion, so a superseded completion finds its own connection already owned."
    )

    stale_end = source.index("\n\t\t\t\t\tif (!connected) {", stale_start)
    stale = source[stale_start:stale_end]
    own = "if (connected && g_connectionOwnerToken.load() == attemptToken) {"
    assert own in stale
    own_body = stale[stale.index(own):stale.index("g_joinInProgress.store(false);", stale.index(own))]
    assert "F4T::Networking::DisconnectFromLocalServer();" in own_body
    assert "g_connectionOwnerToken.store(0);" in own_body
    assert "g_connectionOwnerToken.load() == 0" not in stale, (
        "a stale completion must disconnect only a connection its own attempt owns. A token of 0 means "
        "the cancel, failure or transport loss that zeroed it already tore the connection down."
    )


def test_join_worker_creation_failure_releases_join_state():
    source = UI_SOURCE.read_text(encoding="utf-8")

    join_start = source.index("bool Callback_JoinSession(")
    join_end = source.index("\n\t\tvoid Callback_Disconnect()", join_start)
    join = source[join_start:join_end]

    assert "} catch (const std::exception& a_error) {" in join, (
        "a failed std::thread construction must be caught so the join reports an error instead of "
        "leaving g_joinInProgress and g_joinWorkerRunning set forever."
    )
    catch_start = join.index("} catch (const std::exception& a_error) {")
    catch_block = join[catch_start:join.index("return false;", catch_start)]
    assert join.index("try {") < join.index("g_joinWorker = new std::thread(") < catch_start
    assert join.index("delete g_joinWorker;") < join.index("g_joinWorker = nullptr;") < join.index("g_joinWorker = new std::thread("), (
        "a reaped worker must be cleared before the new thread is created, or a failed creation leaves a dangling pointer."
    )
    assert "g_joinWorkerRunning.store(false);" in catch_block
    assert "g_joinInProgress.store(false);" in catch_block
    assert "CopyError(a_errorBuf, a_errorBufSize," in catch_block
    assert "delete" not in catch_block, (
        "a failed construction leaves g_joinWorker null, so the catch must not delete anything."
    )


_TEST_PREFIX = "test_"


def main() -> int:
    failures: list[str] = []
    tests = [
        (name, value)
        for name, value in sorted(globals().items())
        if name.startswith(_TEST_PREFIX) and callable(value)
    ]

    for name, func in tests:
        try:
            func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"join worker ownership contracts passed ({len(tests)} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
