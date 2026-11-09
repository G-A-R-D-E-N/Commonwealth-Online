from __future__ import annotations

import re
from pathlib import Path

# Constraint: a MutateProfilesAndPersistLocked caller may change only the profile ids it passes; a new caller must be pinned here.

ROOT = Path(__file__).resolve().parents[2]
SERVER_ROOT = ROOT / "CO-SERVER" / "server-cpp"
SAVES = SERVER_ROOT / "src" / "DurableStateSaves.cpp"
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inc"}
DECLARATION_PREFIX = re.compile(r"[ \t]*(?:bool|void)[ \t]*")

CALL = re.compile(r"MutateProfilesAndPersistLocked\(")
DEFINITION = re.compile(r"^[ \t]*[\w:<>\[\],&*\t ]*?DurableStateFileStore::(\w+)\(", re.M)
QUALIFIED_DEFINITION = "DurableStateFileStore::"

PINNED = [
    ("DurableStateFileStore.cpp", "InitializeInventory", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "AckStartingGrant", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "BeginStartingGrant", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "ConsumeStartingGrantApproval", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "ResolveStartingGrant", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "DropInventory", "{a_profileId}"),
    ("DurableStateFileStore.cpp", "PickupInventory", "{a_profileId}"),
    ("DurableStateFileStore.cpp", "ReconcileInventory", "{std::string{a_profileId}}"),
    ("DurableStateSurvival.cpp", "BeginSurvivalClaim", "{std::string{a_profileId}}"),
    ("DurableStateSurvival.cpp", "MutateSurvivalNeeds", "a_profileIds"),
    ("DurableStateSurvival.cpp", "SettleSurvivalNeeds", "{std::string{a_profileId}}"),
    ("DurableStateFileStoreCamps.cpp", "ConsumeLiveDrop", "{}"),
    ("DurableStateFileStore.cpp", "TransferContainer", "{a_profileId}"),
    ("DurableStateFileStore.cpp", "TransferTrade", "{a_request.first.profileId,a_request.second.profileId}"),
    ("DurableStateFileStore.cpp", "SaveResumeCredential", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "DeleteResumeCredential", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "EnsureProfileSecret", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "RecordSaveAttestation", "{std::string{a_profileId}}"),
    ("DurableStateCharacterSlots.cpp", "ScopeLegacySaveAttestations", "{std::string{a_account}}"),
    ("DurableStateCharacterSlots.cpp", "EraseSlotSaveAttestations", "{std::string{a_account}}"),
    ("DurableStateFileStore.cpp", "UpdatePlayerState", "{std::string{a_profileId}}"),
    ("DurableStateFileStore.cpp", "UpdatePlayerStates", "ProfileIds(a_playerStates)"),
    ("DurableStateFileStoreCamps.cpp", "CommitCamp", "{a_profileId}"),
    ("DurableStateFileStoreCamps.cpp", "CommitCampMove", "{a_profileId}"),
    ("DurableStateFileStoreCamps.cpp", "CommitChargeRefund", "{a_profileId}"),
    ("DurableStateFileStoreCamps.cpp", "AckCampGrant", "{a_profileId}"),
    ("DurableStateFileStoreCamps.cpp", "CommitCampStore", "{}"),
    ("DurableDeathIntent.cpp", "CreateAndDepositDeathBag", "owners"),
    ("DurableDeathIntent.cpp", "WithdrawDeathBag", "{std::string{a_profile}}"),
    ("DurableDeathIntent.cpp", "VoidDeathBag", "owners"),
    ("DurableDeathIntent.cpp", "RecoverArchivedDeathBag", "owners"),
    ("DurableDeathIntent.cpp", "QueueDeathBagRefundGrant", "{std::string{a_profile}}"),
    ("DurableDeathIntent.cpp", "ReportDeathBagUnplaced", "{std::string{a_profile}}"),
    ("DurableDeathIntent.cpp", "ExpirePendingDeathBags", "owners"),
    ("DurableDeathIntent.cpp", "MarkDeathBagGrantAttempted", "{std::string{a_profile}}"),
    ("DurableDeathIntent.cpp", "ClaimDeathBagGrant", "{std::string{a_profile}}"),
    ("DurableStateCharacterSlots.cpp", "CreateCharacterSlot", "{profileKey}"),
]


def first_argument(source: str, start: int) -> str:
    depth = 0
    out: list[str] = []
    for char in source[start:]:
        if char in "({[":
            depth += 1
        elif char in ")}]":
            if depth == 0:
                break
            depth -= 1
        elif char == "," and depth == 0:
            break
        out.append(char)
    return re.sub(r"\s+", "", "".join(out))


def call_sites(path: Path) -> list[tuple[str, str, str]]:
    source = path.read_text(encoding="utf-8", errors="replace")
    definitions = list(DEFINITION.finditer(source))
    sites: list[tuple[str, str, str]] = []
    for call in CALL.finditer(source):
        if source[: call.start()].endswith(QUALIFIED_DEFINITION):
            continue
        line_start = source.rfind("\n", 0, call.start()) + 1
        if DECLARATION_PREFIX.fullmatch(source[line_start : call.start()]):
            continue
        enclosing = [definition for definition in definitions if definition.start() < call.start()]
        method = enclosing[-1].group(1) if enclosing else "<none>"
        sites.append((path.name, method, first_argument(source, call.end())))
    return sites


def main() -> int:
    failures: list[str] = []

    actual: list[tuple[str, str, str]] = []
    for path in sorted(SERVER_ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        if "tests" in path.relative_to(SERVER_ROOT).parts:
            continue
        actual.extend(call_sites(path))

    expected = sorted(PINNED)
    actual_sorted = sorted(actual)
    for site in actual_sorted:
        if site not in expected:
            failures.append(
                f"unpinned MutateProfilesAndPersistLocked caller {site[1]} passes {site[2]} in {site[0]}; "
                "pin it in tests/audit/test_durable_profile_scope_contract.py after confirming it mutates only those profiles"
            )
    for site in expected:
        if site not in actual_sorted:
            failures.append(f"pinned caller {site[1]} passing {site[2]} is no longer in {site[0]}")

    saves = SAVES.read_text(encoding="utf-8")
    if "_auditUnlistedProfiles" not in saves or "FingerprintUnlisted(" not in saves:
        failures.append("DurableStateSaves.cpp must keep the unlisted-profile audit wired into MutateLocked")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Durable profile scope contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
