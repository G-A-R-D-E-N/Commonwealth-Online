from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    source = read("CO-SERVER/server-cpp/src/AdminTokenStore.cpp")
    open_handle = source[source.index("static NativeHandle OpenHandle"):source.index("void Verify()")]
    require(
        "O_NONBLOCK" in open_handle,
        "ADMIN-TOKEN-FIFO: POSIX token open must be non-blocking before fstat",
        failures,
    )

    tests = read("CO-SERVER/server-cpp/tests/AdminTokenStoreTests.cpp")
    fifo_test = tests[tests.index("void AssertFifoDoesNotBlockLoad"):tests.index("namespace", tests.index("void AssertFifoDoesNotBlockLoad"))]
    require(
        all(token in fifo_test for token in ("mkfifo", "fork", "waitpid", "WNOHANG", "SIGKILL")),
        "ADMIN-TOKEN-FIFO: FIFO coverage must use a bounded child process",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Admin token FIFO regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
