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
    writer = source[source.index("void WriteSync"):source.index("void ReplaceFile", source.index("void WriteSync"))]
    verifier = source[source.index("void Verify()"):source.index("std::filesystem::path _path", source.index("void Verify()"))]
    require(
        'L"O:' in writer or 'SetSecurityInfo' in writer,
        "PR70-TOKEN-OWNER: newly-created token files must set an explicit owner",
        failures,
    )
    require(
        "OWNER_SECURITY_INFORMATION" in verifier,
        "PR70-TOKEN-OWNER: existing-token verification must retrieve the file owner SID",
        failures,
    )
    require(
        "IsCurrentProcessUser" in source and "WinLocalSystemSid" in verifier,
        "PR70-TOKEN-OWNER: existing-token verification must accept only the process user or LocalSystem owner",
        failures,
    )
    require(
        "TokenUser" in source and "IsWellKnownSid(owner, WinLocalSystemSid)" in verifier and "IsCurrentProcessUser(owner, _path)" in verifier,
        "PR70-TOKEN-OWNER: unexpected owners must remain rejected against TokenUser",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("PR 70 admin-token owner regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
