#!/usr/bin/env python3
"""F4TNpcCombatContracts_part6: loot take session and record contract sections.

Run it directly, exactly like the other F4TNpcCombatContracts parts:

    python CO-BETA/plugin/tests/F4TNpcCombatContracts_part6.py
"""
import os
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[3]


class _SectionExit(Exception):
    """Raised where a contract section used to call sys.exit()."""

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


def _section_loot_take_sessions():
    plugin = _MERGE_ROOT / "CO-BETA/plugin"
    failures = []

    def read(path):
        return path.read_text(encoding="utf-8")

    def body(text, signature):
        start = text.find(signature)
        if start < 0:
            return ""
        start = text.find("{", start)
        depth = 1
        end = start + 1
        while end < len(text) and depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]

    def check(condition, message):
        if not condition:
            failures.append(message)

    take = read(plugin / "src/F4TNpcSyncLootTake.cpp")
    inventory = read(plugin / "src/F4TInventoryGrant.cpp")
    inventory_header = read(plugin / "include/F4TInventoryGrant.h")
    core_header = read(_MERGE_ROOT / "co-core/include/F4TNpcLootSync.h")
    core_tests = read(_MERGE_ROOT / "co-core/tests/F4TNpcLootSyncTests.cpp")

    check("std::uint64_t loadEpoch{};" in core_header, "co-core TakeContext must carry the load epoch")
    check('IsLiveTakeContext(reconnected, TakeContext{ "server-a", "world-1", "alice", 1 })' in core_tests,
          "co-core tests must pin that an operation from an older load epoch is not live")
    check("std::uint64_t LoadEpoch();" in inventory_header and "bool UnattestedLoad();" in inventory_header,
          "F4TInventoryGrant.h must declare LoadEpoch and UnattestedLoad")
    check("bool g_unattestedLoad{};" in inventory, "F4TInventoryGrant.cpp must track an unattested load")
    check("g_unattestedLoad = true;" in body(inventory, "void OnPreLoad("), "a load must start unattested")
    check("g_unattestedLoad = false;" in body(inventory, "void OnPostLoad("), "only an attested CO load may clear the unattested flag")
    check("g_unattestedLoad = false;" in body(inventory, "void OnNewGame()"), "a new game must clear the unattested flag")
    check("return g_loadEpoch;" in body(inventory, "std::uint64_t LoadEpoch()"), "LoadEpoch must return the load epoch")
    check("return g_unattestedLoad;" in body(inventory, "bool UnattestedLoad()"), "UnattestedLoad must return the unattested flag")
    context = body(take, "NpcLootSync::TakeContext CurrentTakeContext(")
    check(bool(context) and "F4T::InventoryGrant::UnattestedLoad()" in context,
          "CurrentTakeContext must return no session while the load is unattested")
    check("std::move(characterId), F4T::InventoryGrant::LoadEpoch() };" in context,
          "CurrentTakeContext must stamp the session with the load epoch")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)
    print("F4TNpcCombatContracts_part6: loot take session contracts all checks passed.")


def _section_held_late_accepts():
    plugin = _MERGE_ROOT / "CO-BETA/plugin"
    failures = []

    def read(path):
        return path.read_text(encoding="utf-8")

    def body(text, signature):
        start = text.find(signature)
        if start < 0:
            return ""
        start = text.find("{", start)
        depth = 1
        end = start + 1
        while end < len(text) and depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]

    def check(condition, message):
        if not condition:
            failures.append(message)

    take = read(plugin / "src/F4TNpcSyncLootTake.cpp")
    apply_late = body(take, "bool ApplyLateResult(")
    check("return false;" in apply_late and "no player cell" in apply_late and "return true;" in apply_late,
          "ApplyLateResult must keep a held accept when the player has no cell, and settle it otherwise")
    replay = body(take, "void ReplayHeldLateResults(")
    check("!ApplyLateResult(*entry, *entry->lateResult)" in replay and "entry = g_timedOutTakes.erase(entry);" in replay,
          "ReplayHeldLateResults must erase a timed-out take only after its late accept is applied")
    guard = replay.find("if (g_timedOutTakes.empty()) {")
    check(guard >= 0 and guard < replay.find("CurrentTakeContext()") and "return;" in replay[guard:replay.find("CurrentTakeContext()")],
          "ReplayHeldLateResults must return before CurrentTakeContext() when no timed-out take is held")
    add_timed_out = body(take, "void AddTimedOutTake(")
    check("!a_timedOut.lateResult" in add_timed_out and "held={}" in add_timed_out and "g_timedOutTakes.erase(victim)" in add_timed_out,
          "the timed-out cap must evict an unheld record first and log every eviction")
    tick_take = body(take, "void TakeTick(")
    check("return NpcLootSync::TimedOutTakeExpired(a_entry.lateResult.has_value(), a_entry.timestampMs, now);" in tick_take,
          "the 60 s sweep must never age out a record that holds a late accept")
    check("AddTimedOutTake(TimedOutTake{ entry->first, entry->second.containerFormId" in tick_take,
          "an unresolved take must be remembered through AddTimedOutTake")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)


def _section_held_grant_undos():
    plugin = _MERGE_ROOT / "CO-BETA/plugin"
    failures = []

    def read(path):
        return path.read_text(encoding="utf-8")

    def body(text, signature):
        start = text.find(signature)
        if start < 0:
            return ""
        start = text.find("{", start)
        depth = 1
        end = start + 1
        while end < len(text) and depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]

    def check(condition, message):
        if not condition:
            failures.append(message)

    take = read(plugin / "src/F4TNpcSyncLootTake.cpp")
    declarations = take + read(plugin / "include/F4TNpcSyncLootInternal.h")
    check("NpcLootSync::TakeContext context;" in body(declarations, "struct GrantAwaitingAck"),
          "a host grant must record the session it was made in")
    tick_take = body(take, "void TakeTick(")
    check("NowMilliseconds(), CurrentTakeContext() };" in tick_take,
          "the host grant must be stored with the session it was made in")
    check("ReplayHeldGrantUndos(a_scope, a_runGameplay);" in tick_take and "g_heldGrantUndos" not in tick_take,
          "held grant undos must be retried at the start of each take tick, and the tick must not erase them")
    permitted = body(take, "bool UndoPermitted(")
    check("IsLiveTakeContext(CurrentTakeContext(), a_grant.context)" in permitted and "a_grant.identity.authorityEpoch == a_scope.epoch" in permitted,
          "an undo needs its own live session and the scope authority epoch")
    check("if (UndoPermitted(awaiting, a_scope)) {" in body(take, "void ResolveGrantAck(") and "HoldGrantUndo(awaiting);" in body(take, "void ResolveGrantAck("),
          "an expired ack must undo only when permitted, and hold the undo otherwise")
    held = body(take, "void HoldGrantUndo(")
    check("kMaximumHeldGrantUndos" in held and "evicted at cap" in held and "g_heldGrantUndos.push_back(a_grant);" in held,
          "held grant undos must be capped and every eviction logged")
    check("kMaximumHeldGrantUndos = 64;" in take, "the held grant undo cap must be 64")
    replay = body(take, "void ReplayHeldGrantUndos(")
    check("if (g_heldGrantUndos.empty()) {" in replay and "!UndoPermitted(*entry, a_scope)" in replay and "g_heldGrantUndos.erase(entry)" in replay,
          "ReplayHeldGrantUndos must return at once when empty and erase only the undos it permits")
    check("undone = UndoGrant(held, a_scope);" in replay and "if (undone) {" in replay,
          "ReplayHeldGrantUndos must erase a held undo only after UndoGrant reports that it wrote the container")
    undo_grant = body(take, "bool UndoGrant(")
    check(bool(undo_grant) and "return false;" in undo_grant and "return true;" in undo_grant,
          "UndoGrant must report whether it wrote the container")
    grant_ack = body(take, "void ResolveGrantAck(")
    check("undone = UndoGrant(awaiting, a_scope);" in grant_ack and "if (!undone) {" in grant_ack,
          "an expired ack must hold the undo again when UndoGrant did not write the container")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)


def _section_persisted_loot_records():
    plugin = _MERGE_ROOT / "CO-BETA/plugin"
    failures = []

    def read(path):
        return path.read_text(encoding="utf-8")

    def body(text, signature):
        start = text.find(signature)
        if start < 0:
            return ""
        start = text.find("{", start)
        depth = 1
        end = start + 1
        while end < len(text) and depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]

    def check(condition, message):
        if not condition:
            failures.append(message)

    take = read(plugin / "src/F4TNpcSyncLootTake.cpp")
    internal = read(plugin / "include/F4TNpcSyncLootInternal.h")
    ledger_header = read(plugin / "include/F4TNpcSyncLootLedger.h")
    ledger = read(plugin / "src/F4TNpcSyncLootLedger.cpp")
    main_source = read(plugin / "src/main.cpp")
    xmake = read(plugin / "xmake.lua")
    core_header = read(_MERGE_ROOT / "co-core/include/F4TNpcLootSync.h")
    core = read(_MERGE_ROOT / "co-core/src/npc/F4TNpcLootSync.cpp")

    check("inline constexpr std::uint32_t RecordType = 'NLTK';" in ledger_header and "inline constexpr std::uint32_t RecordVersion = 1;" in ledger_header,
          "the loot record must be NLTK version 1")
    load = body(ledger, "void LoadRecord(")
    check("a_version != RecordVersion" in load and "a_length > kMaximumRecordBytes" in load,
          "a loot record must be read only at its version and within the length bound")
    check("SessionIsSaveable(entry.context, loadEpoch)" in body(ledger, "void SaveRecords("),
          "only records of the current load epoch with a session may be saved")
    publish = body(ledger, "void PublishLoadedRecords(")
    check("Loot::DeferRollback(" in publish and "Loot::AddTimedOutTake(" in publish and "Loot::HoldGrantUndo(" in publish and publish.count("entry.context.loadEpoch = loadEpoch;") == 3,
          "published records must take the epoch of this load and go through the live functions")
    check("g_staged" in load and "Loot::DeferRollback(" not in load and "LoadEpoch()" not in load and "Loot::NowMilliseconds()" in load,
          "LoadRecord must only stage records; the epoch is stamped at the load message, not at the cosave callback")
    check("PublishLoadedRecords();" in ledger_header, "the ledger header must declare PublishLoadedRecords")
    number = body(ledger, "std::uint32_t U32(")
    check(bool(number) and "value < 0" in number and "std::out_of_range" in number,
          "a loot record number must reject negatives and values over 32 bits")
    resolved = body(ledger, "std::optional<std::uint32_t> ResolveSaved(")
    check("a_intfc->ResolveFormID(a_savedFormId)" in resolved and "does not resolve" in resolved,
          "a saved form id must resolve through SerializationInterface::ResolveFormID, and an unresolved one must be logged")
    check('U32(record.at("count"))' in load and 'U32(record.at("removed"))' in load and "ResolveSaved(a_intfc, lateItemId)" in load,
          "record counts and the late result item id must go through the bounded and resolved readers")
    check("F4T::NpcLootLedger::SaveRecords(a_intfc);" in main_source and "F4T::NpcLootLedger::LoadRecord(a_intfc, version, length);" in main_source,
          "main.cpp must save and load the loot records")
    pre_load = body(main_source, "case F4SE::MessagingInterface::kPreLoadGame")
    post_load_call = main_source.find("F4T::InventoryGrant::OnPostLoad(loadSucceeded);")
    publish_call = main_source.find("F4T::NpcLootLedger::PublishLoadedRecords();")
    check(bool(pre_load) and "NpcLootLedger" not in pre_load,
          "kPreLoadGame must not touch the staged loot records, since the cosave may load before it")
    check(post_load_call >= 0 and publish_call > post_load_call,
          "kPostLoadGame must publish the staged loot records right after InventoryGrant::OnPostLoad")
    check('"src/F4TNpcSyncLootLedger.cpp",' in xmake, "the ledger source must be in the plugin build")
    check("DropQuarantined" not in take and "TakeQuarantine" not in take + core_header + core,
          "the quarantine must be removed")
    check("timestampMs" not in body(internal, "struct DeferredRollback"), "a deferred rollback has no timer")
    check("for (auto& timedOut : g_timedOutTakes) {" in body(take, "void ResolveTimedOutTake("),
          "a late result must settle every record of its operation")
    check("return !a_held && a_nowMilliseconds >= a_createdMilliseconds" in core and "TimedOutTakeMilliseconds = 60000;" in core_header,
          "a held timed-out take must never expire, and the window must be 60 s")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)
    print("F4TNpcCombatContracts_part6: persisted loot record contracts all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TNpcCombatContracts_part6.py:held-grant-undos", _section_held_grant_undos),
    ("CO-BETA/plugin/tests/F4TNpcCombatContracts_part6.py:held-late-accepts", _section_held_late_accepts),
    ("CO-BETA/plugin/tests/F4TNpcCombatContracts_part6.py:loot-take-sessions", _section_loot_take_sessions),
    ("CO-BETA/plugin/tests/F4TNpcCombatContracts_part6.py:persisted-loot-records", _section_persisted_loot_records),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNpcCombatContracts_part6: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
