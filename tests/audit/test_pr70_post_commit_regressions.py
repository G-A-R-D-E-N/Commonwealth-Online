from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    durable_header = read("CO-SERVER/server-cpp/include/DurableStateFileStore.h")
    durable = read("CO-SERVER/server-cpp/src/DurableStateFileStore.cpp")
    ban_header = read("CO-SERVER/server-cpp/include/BanStore.h")
    ban = read("CO-SERVER/server-cpp/src/BanStorePersistence.cpp")
    runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")

    require("TakePersistenceWarning" in durable_header, "PR70-COMMIT: durable state must expose committed-with-warning status", failures)
    require("if (!committed)" in durable and "_persistenceWarning" in durable, "PR70-COMMIT: durable state must roll back only before commit", failures)
    require("a_committed && _postCommitHook" in durable, "PR70-COMMIT: durable state tests need a post-commit fault seam", failures)
    require("TakePersistenceWarning" in ban_header, "PR70-COMMIT: ban state must expose committed-with-warning status", failures)
    require("if (a_committed)" in ban and "_persistenceWarning" in ban and "return;" in ban, "PR70-COMMIT: ban state must retain committed memory and return a warning", failures)
    require("AppendPersistenceWarning" in runtime and "ReportPersistenceWarning" in runtime, "PR70-COMMIT: runtime must surface persistence warnings", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("PR 70 post-commit regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
