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
    verifier = source[source.index("void Verify()"):source.index("std::filesystem::path _path")]
    require(
        "fchmod(_descriptor, 0600)" in verifier,
        "ADMIN-TOKEN-POSIX-MODE: trusted permissive files must be tightened through the opened descriptor",
        failures,
    )
    require(
        "chmod(a_path.c_str(), 0600)" not in verifier,
        "ADMIN-TOKEN-POSIX-MODE: token verification must not chmod the pathname",
        failures,
    )

    tests = read("CO-SERVER/server-cpp/tests/AdminTokenStoreTests.cpp")
    require(
        'AdminTokenStore::Load(loose) == "loose-token"' in tests and "refusedLooseMode" not in tests,
        "ADMIN-TOKEN-POSIX-MODE: a trusted permissive token must remain loadable after tightening",
        failures,
    )
    require(
        "others_read" in tests and "group_read" in tests,
        "ADMIN-TOKEN-POSIX-MODE: executable coverage must verify group and other access was removed",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Admin token POSIX mode regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
