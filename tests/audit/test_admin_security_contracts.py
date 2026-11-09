#!/usr/bin/env python3
"""test_admin_security_contracts: consolidated contract sections.

Consolidated from 7 single-contract scripts so the group runs one process
instead of 7. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_admin_security_contracts.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_001_test_admin_and_connpolicy_contract():
    """Contract moved verbatim from tests/audit/test_admin_and_connpolicy_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_admin_and_connpolicy_contract.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []

        admin = read("CO-SERVER/server-cpp/src/AdminControlServer.cpp")
        start = admin[admin.index("bool AdminControlServer::Start"):admin.index("void AdminControlServer::Stop")]
        require(
            "SO_EXCLUSIVEADDRUSE" in admin and "SetExclusiveAddressUse" in start,
            "AUDIT-11: the admin listener must set SO_EXCLUSIVEADDRUSE on Windows",
            failures,
        )
        exclusive_call = start.index("if (!SetExclusiveAddressUse(socketHandle))")
        exclusive_branch = start[start.rindex("#ifdef _WIN32", 0, exclusive_call):start.index("#else", exclusive_call)]
        require(
            "SO_REUSEADDR" not in exclusive_branch,
            "AUDIT-11: the Windows admin listener must not set SO_REUSEADDR",
            failures,
        )
        posix_at = start.index("#else", exclusive_call)
        posix_branch = start[posix_at:start.index("#endif", posix_at)]
        require(
            "SO_REUSEADDR" in posix_branch,
            "AUDIT-11: the POSIX admin listener keeps its SO_REUSEADDR behaviour",
            failures,
        )
        require(
            "if (!SetExclusiveAddressUse(socketHandle))" in start,
            "AUDIT-11: a failed exclusive-use setsockopt must fail closed",
            failures,
        )
        require(
            "reinterpret_cast<const char*>(&exclusive)" in admin and "static_cast<int>(sizeof(exclusive))) == 0" in admin,
            "AUDIT-11: the exclusive-use option result must be checked, not discarded",
            failures,
        )
        bind_block = start[start.index("if (bind("):start.index("if (listen(")]
        require(
            "SocketErrorDetail()" in bind_block and "admin_port" in bind_block,
            "AUDIT-11: a failed bind must report an actionable error",
            failures,
        )
        require(
            "boundPort != _requestedPort" in start,
            "AUDIT-11: the bound port must be verified against the requested port",
            failures,
        )
        require(
            "if (!HoldsLoopbackPortExclusively(boundPort))" in start,
            "AUDIT-11: the exclusivity of the bound loopback port must be verified",
            failures,
        )
        probe = admin[admin.index("bool HoldsLoopbackPortExclusively"):admin.index("bool IsRetryableReceiveError")]
        require(
            "SO_REUSEADDR" in probe and "bind(" in probe and "return !competitorBound;" in probe,
            "AUDIT-11: the exclusivity probe must prove a competing SO_REUSEADDR bind is refused",
            failures,
        )
        require(
            start.index("if (listen(") < start.index("if (!HoldsLoopbackPortExclusively"),
            "AUDIT-11: exclusivity is verified after the listener is bound and listening",
            failures,
        )

        server_tests = read("CO-SERVER/server-cpp/tests/AdminControlServerTests.cpp")
        require(
            "bool CanBindLoopback(" in server_tests and "assert(!CanBindLoopback(server.LocalPort()));" in server_tests,
            "AUDIT-11: a competing loopback listener must be refused (regression row)",
            failures,
        )

        token_store = read("CO-SERVER/server-cpp/src/AdminTokenStore.cpp")
        posix_write = token_store[
            token_store.index("void WriteSync(const std::filesystem::path& a_path, std::string_view a_contents)",
                              token_store.index("#else\n\t\tstd::runtime_error PosixFailure")):
            token_store.index("void SyncDirectory(const std::filesystem::path& a_directory)")
        ]
        require(
            "fchmod(descriptor, 0600)" in posix_write,
            "AUDIT-17: the POSIX token mode must be set on the open descriptor",
            failures,
        )
        require(
            posix_write.index("fchmod(descriptor, 0600)") < posix_write.index("fsync(descriptor)"),
            "AUDIT-17: the descriptor mode must be set before the file is made durable",
            failures,
        )
        require(
            posix_write.index("fchmod(descriptor, 0600)") < posix_write.index("close(descriptor)"),
            "AUDIT-17: the descriptor mode must be set before the descriptor is closed",
            failures,
        )
        rename_at = token_store.index("if (rename(a_source.c_str(), a_destination.c_str()) != 0)")
        posix_replace = token_store[rename_at:token_store.index("SyncDirectory(a_destination.parent_path());", rename_at)]
        require(
            "chmod(" not in posix_replace,
            "AUDIT-17: no pathname chmod may run after the rename",
            failures,
        )
        require(
            "chmod(a_destination" not in token_store,
            "AUDIT-17: the post-rename pathname chmod must be deleted",
            failures,
        )
        require(
            "MoveFileExW(" in token_store and "FILE_FLAG_WRITE_THROUGH" in token_store,
            "AUDIT-17: the Windows replace path must stay unchanged",
            failures,
        )
        token_tests = read("CO-SERVER/server-cpp/tests/AdminTokenStoreTests.cpp")
        require(
            'AdminTokenStore::Replace(tokenPath, "descriptor-mode-token")' in token_tests
            and "(replaced.st_mode & 0777) == 0600" in token_tests,
            "AUDIT-17: Replace must end at 0600 (regression row)",
            failures,
        )

        policy_header = read("co-core/include/F4TConnectionPolicy.h")
        require(
            "inline constexpr std::size_t MaximumConnectAttemptHistory = 256;" in policy_header,
            "AUDIT-20: the per-peer attempt history needs a fixed maximum constant",
            failures,
        )
        policy = read("co-core/src/session/F4TConnectionPolicy.cpp")
        require(
            re.search(r"perPeerCap\s*=\s*preferredCap\s*<\s*MaximumConnectAttemptHistory", policy) is not None,
            "AUDIT-20: the per-peer cap must be clamped to the fixed maximum",
            failures,
        )
        require(
            "attempts.size() >= perPeerCap" in policy and "attempts.pop_front();" in policy,
            "AUDIT-20: a saturated history must drop the oldest attempt instead of growing",
            failures,
        )
        saturated = policy[policy.index("attempts.size() >= perPeerCap"):policy.index("attempts.push_back(now);\n\t\treturn attempts.size() <= a_config.maximumAttempts;")]
        require(
            "return false;" in saturated,
            "AUDIT-20: a saturated history keeps refusing, so decisions are unchanged",
            failures,
        )
        require(
            "kAttemptHistoryFloor" in policy,
            "AUDIT-20: the previous per-peer floor must be kept",
            failures,
        )
        policy_tests = read("co-core/tests/F4TConnectionPolicyTests.cpp")
        require(
            "flooded.Allow" in policy_tests and "100'000" in policy_tests,
            "AUDIT-20: a rejected-attempt flood row must pin the decisions",
            failures,
        )
        require(
            "oversizedAccepted == MaximumConnectAttemptHistory" in policy_tests,
            "AUDIT-20: the history bound must be observable in the tests",
            failures,
        )

        admin_header = read("CO-SERVER/server-cpp/include/AdminControlServer.h")
        require("kMaximumAdminAuthFailures" in admin and "kAdminAuthLockout" in admin,
                "#11: repeated local admin-token failures must trigger a lockout", failures)
        require("AuthenticationLockedOut" in admin_header and "FailedAuthenticationCount" in admin_header,
                "#11: the admin authentication throttle must be observable", failures)
        require("WeakAdminToken(_adminToken)" in admin and "Admin token is too weak" in admin,
                "#11: Start must refuse a weak admin token", failures)
        bootstrap = read("CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
        weak_check = bootstrap.find("AdminControlServer::WeakAdminToken(adminToken)")
        replace_call = bootstrap.find("AdminTokenStore::Replace(adminTokenPath, adminToken)")
        server_start = bootstrap.find("a_out.admin->Start(adminError)")
        require(0 <= weak_check < replace_call < server_start,
                "#11: the bootstrap must replace a weak stored token before starting admin control", failures)
        smokes = read("CO-SERVER/server-cpp/scripts/run-live-smokes.sh")
        require("weak admin token was not replaced" in smokes,
                "#11: a live smoke must prove a weak stored token is replaced", failures)
        require("too weak" in server_tests and "std::string(40, 'a')" in server_tests,
                "#11: a C++ test must cover the weak token refusal", failures)
        require("temporarily locked out" in admin,
                "#11: a locked-out request must get a distinct response", failures)
        require("FailedAuthenticationCount() == 5" in server_tests and "locked out" in server_tests,
                "#11: a C++ test must cover the admin auth lockout", failures)
        require("_adminToken)) { if (AuthenticationLockedOut()) { return Failure(" in " ".join(admin.split()),
                "#11: the lockout must only refuse failed tokens, so a correct token is checked first", failures)
        require('lockedAccepted.value("ok", false)' in server_tests,
                "#11: a correct token must still be accepted during the lockout", failures)
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Admin bind, token mode and connection history contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_002_test_admin_token_fifo_regressions():
    """Contract moved verbatim from tests/audit/test_admin_token_fifo_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_admin_token_fifo_regressions.py")
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


    if True:
        _merge_exit(main())

def _section_003_test_admin_token_posix_mode_regressions():
    """Contract moved verbatim from tests/audit/test_admin_token_posix_mode_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_admin_token_posix_mode_regressions.py")
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


    if True:
        _merge_exit(main())

def _section_004_test_admin_token_toctou_regressions():
    """Contract moved verbatim from tests/audit/test_admin_token_toctou_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_admin_token_toctou_regressions.py")
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


    if True:
        _merge_exit(main())

def _section_005_test_pr70_admin_token_owner_regressions():
    """Contract moved verbatim from tests/audit/test_pr70_admin_token_owner_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pr70_admin_token_owner_regressions.py")
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


    if True:
        _merge_exit(main())

def _section_006_test_client_packet_bounds_security_contract():
    """Contract moved verbatim from tests/audit/test_client_packet_bounds_security_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_client_packet_bounds_security_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    READERS = "CO-BETA/plugin/src/F4TNetworkingPacketReaders.cpp"
    INBOUND = "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
    INTERNAL = "CO-BETA/plugin/src/F4TNetworkingInternal.h"
    CORE_PLAYER_STATE = "co-core/src/validation/F4TPlayerStateValidation.cpp"
    CORE_APPEARANCE = "co-core/src/validation/F4TAppearanceValidation.cpp"

    APPEARANCE_ARRAYS = (
        ("headParts", "kMaximumAppearanceHeadParts"),
        ("morphs", "kMaximumAppearanceMorphs"),
        ("morphRegions", "kMaximumAppearanceMorphRegions"),
        ("facialBoneMorphs", "kMaximumAppearanceFacialBoneMorphs"),
        ("tints", "kMaximumAppearanceTints"),
    )


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def block(text: str, start: str, end: str) -> str:
        begin = text.find(start)
        if begin < 0:
            return ""
        finish = text.find(end, begin + len(start))
        return text[begin:] if finish < 0 else text[begin:finish]


    def core_cap(path: str, name: str) -> int | None:
        found = re.search(rf"constexpr std::size_t {name} = (\d+);", read(path))
        return int(found.group(1)) if found else None


    def main() -> int:
        failures: list[str] = []

        internal = read(INTERNAL)
        readers = read(READERS)
        inbound = read(INBOUND)

        header_caps = {
            name: (int(found.group(1)) if (found := re.search(rf"inline constexpr std::size_t {name} = (\d+);", internal)) else None)
            for name in ("kMaximumEquippedItems",) + tuple(entry[1] for entry in APPEARANCE_ARRAYS)
        }
        for name, value in header_caps.items():
            require(value is not None, f"L5: {INTERNAL} must declare inline constexpr {name}", failures)

        require(header_caps["kMaximumEquippedItems"] == core_cap(CORE_PLAYER_STATE, "kMaximumEquippedItems"),
                "L5: the reader equippedItems cap must equal co-core F4TPlayerStateValidation kMaximumEquippedItems", failures)
        for core_name, header_name in (
            ("kMaximumHeadParts", "kMaximumAppearanceHeadParts"),
            ("kMaximumMorphs", "kMaximumAppearanceMorphs"),
            ("kMaximumMorphRegions", "kMaximumAppearanceMorphRegions"),
            ("kMaximumFacialBoneMorphs", "kMaximumAppearanceFacialBoneMorphs"),
            ("kMaximumTints", "kMaximumAppearanceTints"),
        ):
            require(header_caps[header_name] == core_cap(CORE_APPEARANCE, core_name),
                    f"L5: {header_name} must equal co-core F4TAppearanceValidation {core_name}", failures)

        require("std::optional<std::vector<F4T::RemotePlayerState::RemoteEquippedItem>> ReadEquippedItems(" in internal,
                "L5: ReadEquippedItems must return std::optional so an oversized array can reject the packet", failures)
        require("std::optional<F4T::RemotePlayerState::RemoteAppearance> ReadAppearance(" in internal,
                "L5: ReadAppearance must return std::optional so an oversized array can reject the packet", failures)
        require("std::optional<std::vector<F4T::RemotePlayerState::RemoteEquippedItem>> ReadEquippedItems(" in readers,
                "L5: the reader definition must match the optional-returning declaration", failures)
        require("std::optional<F4T::RemotePlayerState::RemoteAppearance> ReadAppearance(" in readers,
                "L5: the reader definition must match the optional-returning declaration", failures)

        read_equipped = block(readers, "ReadEquippedItems(", "F4T::PowerArmorSync::PowerArmorState ReadPowerArmor(")
        size_check = read_equipped.find("items->size() > kMaximumEquippedItems")
        decode_call = read_equipped.find("F4T::EquipmentState::Decode(*items)")
        require(size_check >= 0, "L5: ReadEquippedItems must cap the equippedItems array size", failures)
        require(0 <= size_check < (decode_call if decode_call >= 0 else len(read_equipped)),
                "L5: the equippedItems size cap must run before the equipment decode", failures)
        require("return std::nullopt;" in read_equipped, "L5: an oversized equippedItems array must reject the packet", failures)

        read_appearance = block(readers, "ReadAppearance(const Json& a_packet", "void ResetPacketDiagnostics()")
        for field, cap_name in APPEARANCE_ARRAYS:
            size_check = read_appearance.find(f"{field}->size() > {cap_name}")
            loop = read_appearance.find(f"for (const auto& ", read_appearance.find(f"{field} = appearanceObject->find(\"{field}\")"))
            require(size_check >= 0, f"L5: ReadAppearance must cap the appearance {field} array", failures)
            require(loop >= 0, f"L5: ReadAppearance must still read the appearance {field} array", failures)
            require(0 <= size_check < loop, f"L5: the {field} size cap must be checked before its per-element loop", failures)
        require(read_appearance.count("return std::nullopt;") >= len(APPEARANCE_ARRAYS),
                "L5: every oversized appearance array must reject the packet with std::nullopt", failures)

        transform = block(inbound, "void HandleTransformPacket(", "void HandleDisconnectPacket(")
        equipped_reject = transform.find("if (!equippedItems) {")
        appearance_reject = transform.find("if (!appearance) {")
        update_call = transform.find("F4T::RemotePlayerState::UpdateRemotePlayer(remoteState);")
        require(equipped_reject >= 0 and "return;" in transform[equipped_reject:equipped_reject + 80],
                "L5: HandleTransformPacket must drop the whole packet when equippedItems is rejected", failures)
        require(appearance_reject >= 0 and "return;" in transform[appearance_reject:appearance_reject + 80],
                "L5: HandleTransformPacket must drop the whole packet when appearance is rejected", failures)
        require(0 <= equipped_reject < update_call and 0 <= appearance_reject < update_call,
                "L5: the packet must be rejected before the remote state update", failures)
        require("std::move(*equippedItems)" in transform and "std::move(*appearance)" in transform,
                "L5: the accepted readers must be dereferenced into the remote state", failures)

        combat = block(inbound, "void HandleCombatHitPacket(", "void HandlePacketLine(")
        queue_hit = combat.find("F4T::RemotePlayerState::QueueIncomingCombatHit(*senderId, *sequence, *damage)")
        require(queue_hit >= 0, "L6: HandleCombatHitPacket must still queue the hit through QueueIncomingCombatHit", failures)
        require("combat_hit_sender_missing" not in combat and "HasRemotePlayer(" not in combat,
                "L6: a combatHit from a sender missing from the remote-player table must not be dropped by a sender gate", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Client packet bounds security contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_007_test_utf8_boundary_and_dll_path_contract():
    """Contract moved verbatim from tests/audit/test_utf8_boundary_and_dll_path_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_utf8_boundary_and_dll_path_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    UTF8_HEADER = "CO-BETA/plugin/include/F4TUtf8Text.h"
    LAN_DISCOVERY = "CO-BETA/plugin/src/F4TLanDiscovery.cpp"
    SHIM_FRAMES = "CO-BETA/plugin/src/F4TGnsWinsockShimFrames.cpp"
    MODPACK_PROBE = "CO-BETA/plugin/src/F4TClientModpackProbe.cpp"
    SHIM_BRIDGE = "CO-BETA/plugin/src/F4TGnsWinsockShimBridge.cpp"
    HANDSHAKE_ALERT = "CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp"
    UI_CLIENT = "CO-BETA/plugin/src/F4TUIClient.cpp"
    UI_CLIENT_LAN_SCAN = "CO-BETA/plugin/src/F4TUIClientLanScan.cpp"
    ALERT_SYSTEM = "CommonwealthOnlineUI/src/F4TAlertSystem.cpp"
    SERVER_BROWSER_BRIDGE = "CommonwealthOnlineUI/src/F4TServerBrowserBridge.cpp"

    MAXIMUM_NAME_BYTES = 64
    MAXIMUM_DESCRIPTION_BYTES = 256

    REPLACEMENT_CHARACTER = "\\xEF\\xBF\\xBD"


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def block(text: str, start: str, end: str | None = None) -> str:
        begin = text.find(start)
        if begin < 0:
            return ""
        if end is None:
            return text[begin:]
        finish = text.find(end, begin + len(start))
        return text[begin:] if finish < 0 else text[begin:finish]


    def main() -> int:
        failures: list[str] = []

        helper = read(UTF8_HEADER)
        require("namespace F4T::Utf8" in helper, "#9: F4TUtf8Text.h must expose the shared F4T::Utf8 helper", failures)
        require("constexpr bool IsValid(" in helper, "#9: the helper must expose a constexpr IsValid", failures)
        require("inline std::string TruncateToBytes(" in helper, "#9: the helper must expose inline TruncateToBytes", failures)
        require(not (ROOT / "CO-BETA/plugin/src/F4TUtf8Text.cpp").exists(),
                "#9: the helper must stay header-only so no build file change is needed", failures)

        sequence = block(helper, "constexpr Sequence SequenceAt(")
        for lead_byte in ("0xC2", "0xE0", "0xED", "0xF0", "0xF4"):
            require(lead_byte in sequence,
                    f"#9: the decoder must validate {lead_byte} lead bytes (overlongs, surrogates, >U+10FFFF)", failures)
        require(REPLACEMENT_CHARACTER in helper, "#9: invalid bytes must be replaced with U+FFFD", failures)

        truncate = block(helper, "inline std::string TruncateToBytes(")
        require("a_maximumBytes" in truncate and "break;" in truncate,
                "#9: TruncateToBytes must stop before exceeding the byte cap", failures)
        require("substr(0," not in truncate, "#9: TruncateToBytes must not cut the input at a raw byte offset", failures)
        require("resize(" not in truncate, "#9: TruncateToBytes must not resize by bytes", failures)
        require("a_text.substr(offset, sequence.bytes)" in truncate,
                "#9: a whole validated code point must be copied, never a partial one", failures)

        lan = read(LAN_DISCOVERY)
        require(re.search(r"kMaximumNameBytes\s*=\s*64;", lan) is not None,
                f"#9: LAN discovery must keep the {MAXIMUM_NAME_BYTES}-byte name cap", failures)
        require(re.search(r"kMaximumDescriptionBytes\s*=\s*256;", lan) is not None,
                f"#9: LAN discovery must keep the {MAXIMUM_DESCRIPTION_BYTES}-byte description cap", failures)
        parse = block(lan, "ParseDiscoverResponse(", "std::string MakeServerKey(")
        require(parse.count("F4T::Utf8::TruncateToBytes(") >= 2,
                "#9: LAN name and description must both be truncated by the UTF-8 helper", failures)
        require(".substr(0, 64)" not in lan and ".substr(0, 256)" not in lan,
                "#9: LAN discovery must not cut names or descriptions at a byte offset", failures)

        frames = read(SHIM_FRAMES)
        frame_sanitize = block(frames, "std::string SanitizeFailureText(", "std::optional<SnapshotFamily> SnapshotFamilyForPayload(")
        require("F4T::Utf8::TruncateToBytes(" in frame_sanitize,
                "#10: the shim failure-text sanitiser must truncate on UTF-8 boundaries", failures)
        require("resize(" not in frame_sanitize,
                "#10: the shim failure-text sanitiser must not resize the string by bytes", failures)

        probe = read(MODPACK_PROBE)
        detail_sanitize = block(probe, "std::string SanitizeFailureDetail(", "std::optional<std::wstring> WidenUtf8(")
        require("F4T::Utf8::TruncateToBytes(" in detail_sanitize,
                "#10: the session-rejection detail sanitiser must truncate on UTF-8 boundaries", failures)
        require("substr(0," not in detail_sanitize,
                "#10: the session-rejection detail sanitiser must not cut the reason at a byte offset", failures)
        rejection = block(probe, "Networking::ConnectFailure MakeSessionRejectionFailure(")
        require(rejection.count("SanitizeFailureDetail(") >= 3,
                "#10: rejection code, reason and detail must all pass through the sanitiser", failures)

        alert = read(HANDSHAKE_ALERT)
        handshake = block(alert, "bool ShowHandshakeRejection(", "std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 4>")
        require("F4T::Utf8::TruncateToBytes(code" in handshake,
                "#10: the alert boundary must sanitise the rejection code", failures)
        require("F4T::Utf8::TruncateToBytes(reason" in handshake,
                "#10: the alert boundary must sanitise the rejection reason", failures)
        require("kMaximumRejectionCodeBytes" in handshake and "kMaximumRejectionReasonBytes" in handshake,
                "#10: the alert boundary must keep explicit code and reason byte caps", failures)

        alert_system = read(ALERT_SYSTEM)
        require("std::string BuildFallbackAlertJson(" in alert_system,
                "#10: the alert system must be able to emit a fallback payload that cannot throw", failures)
        fallback = block(alert_system, "std::string BuildFallbackAlertJson(", "void InvokeShow(")
        require(REPLACEMENT_CHARACTER not in fallback and all(ord(character) < 0x80 for character in fallback),
                "#10: the fallback alert payload must be ASCII-only", failures)
        show = block(alert_system, "bool Show(", "void Dismiss(")
        try_at = show.find("try {")
        dump_at = show.find("F4T::ViewJson::Dump(payload)")
        catch_at = show.find("catch (const std::exception&")
        require(0 <= try_at < dump_at, "#10: the alert JSON build and dump must run inside the try block", failures)
        require(dump_at < catch_at, "#10: the dump failure must be caught before it can terminate the process", failures)
        require("BuildFallbackAlertJson(" in show, "#10: a serialisation failure must fall back to the safe payload", failures)

        ui_client = read(UI_CLIENT)
        callback = block(ui_client, "bool Callback_ScanLocalServers(", "bool Callback_GetRemotePlayersMapJson(")
        require("UIClientLanScan::ScanToJson(" in callback,
                "#9: the UI LAN callback must delegate to the guarded scan helper", failures)
        scan = block(read(UI_CLIENT_LAN_SCAN), "bool ScanToJson(")
        try_at = scan.find("try {")
        dump_at = scan.find("servers.dump()")
        require(0 <= try_at < dump_at, "#9: the LAN JSON build and dump must run inside the try block", failures)
        require(scan.count("catch (const std::exception&") >= 1, "#9: LAN serialisation failures must be caught", failures)
        require("catch (...) {" in scan,
                "#9: no exception may cross the C callback boundary", failures)
        require("return false;" in scan, "#9: a failed LAN scan must report failure instead of throwing", failures)

        browser = read(SERVER_BROWSER_BRIDGE)
        scan = block(browser, "void ScanLocalServers()", "}));")
        call_at = scan.find("COUI::Session::ScanLocalServers(")
        try_at = scan.find("try {")
        require(0 <= try_at < call_at, "#9: the plugin LAN callback must be invoked inside a try block", failures)
        require("catch (const std::exception&" in scan and "catch (...) {" in scan,
                "#9: a plugin-side LAN failure must be caught on the UI worker path", failures)

        bridge = read(SHIM_BRIDGE)
        require("std::filesystem::current_path(" not in bridge,
                "#16: the bridge loader must never resolve a DLL from the process working directory", failures)
        require("LOAD_WITH_ALTERED_SEARCH_PATH" in bridge,
                "#16: the verified candidates must keep LOAD_WITH_ALTERED_SEARCH_PATH", failures)
        require("if (!a_path.is_absolute())" in bridge,
                "#16: relative bridge candidates must be rejected", failures)

        module_directory = block(bridge, "std::filesystem::path GetCurrentModuleDirectory()", "template <class T>")
        require("GetModuleHandleExW(" in module_directory and "GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS" in module_directory,
                "#16: the plugin directory must be derived from the module handle, not a relative path", failures)
        require("GetModuleFileNameW(" in module_directory,
                "#16: the module directory must come from the loaded plugin module path", failures)

        get_api = block(bridge, "BridgeApi* GetBridgeApi(", "void PreserveThreadConnectFailure(")
        require("current_path" not in get_api,
                "#16: GetBridgeApi must not fall back to the current working directory", failures)
        require("if (moduleDirectory.empty())" in get_api,
                "#16: GetBridgeApi must fail closed when the module directory is unknown", failures)
        require("Could not determine the Commonwealth Online plugin directory" in get_api,
                "#16: a missing module directory must produce a clear diagnostic", failures)
        require("moduleDirectory / kBridgeDllName" in get_api,
                "#16: the packaged layout must resolve the bridge next to the plugin", failures)
        require('moduleDirectory / L"Data" / L"F4SE" / L"Plugins" / kBridgeDllName' in get_api,
                "#16: the fallback must stay under the plugin module directory", failures)
        require("std::array<std::filesystem::path, 2> candidates" in get_api,
                "#16: only the two verified module-relative candidates may be tried", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("UTF-8 boundary and DLL path security contracts passed")
        return 0


    if True:
        _merge_exit(main())

_SECTIONS = [
    ("tests/audit/test_admin_and_connpolicy_contract.py", _section_001_test_admin_and_connpolicy_contract),
    ("tests/audit/test_admin_token_fifo_regressions.py", _section_002_test_admin_token_fifo_regressions),
    ("tests/audit/test_admin_token_posix_mode_regressions.py", _section_003_test_admin_token_posix_mode_regressions),
    ("tests/audit/test_admin_token_toctou_regressions.py", _section_004_test_admin_token_toctou_regressions),
    ("tests/audit/test_pr70_admin_token_owner_regressions.py", _section_005_test_pr70_admin_token_owner_regressions),
    ("tests/audit/test_client_packet_bounds_security_contract.py", _section_006_test_client_packet_bounds_security_contract),
    ("tests/audit/test_utf8_boundary_and_dll_path_contract.py", _section_007_test_utf8_boundary_and_dll_path_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_admin_security_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
