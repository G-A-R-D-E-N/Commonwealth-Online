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
    load = source[source.index("std::string AdminTokenStore::Load("):]
    load_or_create = source[source.index("std::string AdminTokenStore::LoadOrCreate"):source.index("std::string AdminTokenStore::Load(")]
    require(
        "VerifyExistingTokenFile(fullPath);\n\t\tconst auto token = ReadTokenFile(fullPath);" not in load,
        "ADMIN-TOKEN-TOCTOU: Load must not verify and reopen the token by pathname",
        failures,
    )
    require(
        "std::filesystem::exists(fullPath)" not in load_or_create,
        "ADMIN-TOKEN-TOCTOU: LoadOrCreate must use the open result instead of a pathname existence check",
        failures,
    )
    require(
        "class TokenFile" in source and "ReadVerifiedTokenFile" in source,
        "ADMIN-TOKEN-TOCTOU: Load and LoadOrCreate must use one verified file-object reader",
        failures,
    )
    require(
        "return TokenFile::Open(a_path)->Read();" in source and "ReadTokenFile" not in source and "ifstream" not in source,
        "ADMIN-TOKEN-TOCTOU: the verified object must be the object used for reading",
        failures,
    )
    verifier = source[source.index("void Verify()"):source.index("std::filesystem::path _path")]
    reader = source[source.index("std::string Read()"):source.index("private:", source.index("std::string Read()"))]
    require(
        "GetNamedSecurityInfoW" not in verifier and "GetFileAttributesW" not in verifier and "lstat(" not in verifier,
        "ADMIN-TOKEN-TOCTOU: verification must not inspect the token by pathname",
        failures,
    )
    require(
        "GetSecurityInfo(" in verifier and "fstat(" in verifier,
        "ADMIN-TOKEN-TOCTOU: verification must inspect the opened native object",
        failures,
    )
    require(
        "info.st_uid != owner && info.st_uid != 0" in verifier and "S_IRWXG | S_IRWXO" in verifier,
        "ADMIN-TOKEN-TOCTOU: POSIX owner and mode checks must use descriptor metadata",
        failures,
    )
    require(
        "ReadFile(_handle" in reader and "read(_descriptor" in reader,
        "ADMIN-TOKEN-TOCTOU: reading must use the verified native object",
        failures,
    )

    require(
        "FILE_FLAG_OPEN_REPARSE_POINT" in source,
        "ADMIN-TOKEN-TOCTOU: Windows must open without following reparse points",
        failures,
    )
    require(
        "GetFileInformationByHandleEx" in source and "GetSecurityInfo" in source,
        "ADMIN-TOKEN-TOCTOU: Windows metadata and security checks must use the opened handle",
        failures,
    )

    require(
        "O_NOFOLLOW" in source and "fstat" in source and "read(" in source,
        "ADMIN-TOKEN-TOCTOU: POSIX must open, inspect, and read the same descriptor",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Admin token TOCTOU regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
