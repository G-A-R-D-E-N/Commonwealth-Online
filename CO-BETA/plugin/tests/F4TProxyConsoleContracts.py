#!/usr/bin/env python3
"""F4TProxyConsoleContracts: consolidated contract sections.

Consolidated from 8 single-contract scripts so the group runs one process
instead of 8. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TProxyConsoleContracts.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[3]


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


def _section_001_f4tconsoleguardseconddoorcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TConsoleGuardSecondDoorContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TConsoleGuardSecondDoorContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    GUARD = ROOT / "CO-BETA/plugin/src/F4TConsoleGuard.cpp"
    POLICY = ROOT / "CO-BETA/plugin/include/F4TConsoleGuardPolicy.h"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def body_after(source, anchor):
        start = source.find(anchor)
        if start < 0:
            return None
        brace = source.index("{", start + len(anchor))
        depth = 0
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[brace + 1:index]
        return None


    text = GUARD.read_text(encoding="utf-8") if GUARD.exists() else ""
    policy = POLICY.read_text(encoding="utf-8") if POLICY.exists() else ""

    check(bool(text), f"missing {GUARD}")
    check(bool(policy), f"missing {POLICY}")

    check(
        "RE::ID::Script::CompileAndRun.address()" in text,
        "F4TConsoleGuard.cpp must hook Script::CompileAndRun (the Flash-callable GenericMenu callback index 1)",
    )
    check("void CompileAndRunDetour(" in text, "F4TConsoleGuard.cpp must define CompileAndRunDetour")
    check("DecideConsoleUse(" in text, "the console guard must decide through DecideConsoleUse")
    check("DecideConsoleUse(" in policy, "F4TConsoleGuardPolicy.h must declare DecideConsoleUse")
    check("a_consoleMenuOpen" not in policy, "the console decision must not depend on the menu state")
    check("g_consoleMenuOpen" not in text, "F4TConsoleGuard.cpp must not keep a menu-open gate for the command doors")
    check("DecideScriptCompileUse" not in text + policy, "the second door must share DecideConsoleUse, not a copy of it")

    detour = body_after(text, "void CompileAndRunDetour(")
    check(detour is not None, "F4TConsoleGuard.cpp must define CompileAndRunDetour")
    if detour is not None:
        check("IsBlocked(ClassifyCaller(returnAddress)) && !TryTakeToken(g_pendingAllowedCommands)" in detour,
              "CompileAndRunDetour must block a blocked caller unless it atomically takes a queued-command token")
        blocked = detour.find("AnnounceBlocked();")
        original = detour.find("original(a_script, a_compiler, a_compilerIndex, a_ownerObject);")
        check(0 <= blocked < original,
              "CompileAndRunDetour must announce and return before running the script")
        check("return;" in detour[blocked:original] if 0 <= blocked < original else False,
              "CompileAndRunDetour must return on a block, not fall through to the script")

    install = body_after(text, "void InstallCompileAndRunHook(")
    check(install is not None, "F4TConsoleGuard.cpp must install the Script::CompileAndRun hook")
    if install is not None:
        check("REX::UNRESTRICTED_CAST<void*>(&CompileAndRunDetour)" in install,
              "InstallCompileAndRunHook must install CompileAndRunDetour")
        check("AtVerifiedRva(target, a_runtime.compileAndRunRva)" in install,
              "InstallCompileAndRunHook must compare the resolved address with the table entry")

    install_execute = body_after(text, "void InstallExecuteCommandHook(")
    check(install_execute is not None, "F4TConsoleGuard.cpp must install the Console::ExecuteCommand hook")
    if install_execute is not None:
        check("F4T::InlineHook::Install(request, nullptr, result)" in install_execute,
              "InstallExecuteCommandHook must go through F4T::InlineHook::Install")
        check("request.expectedPrologue = a_runtime.executePrologue;" in install_execute,
              "InstallExecuteCommandHook must pass the per-runtime expected bytes")
        check("AtVerifiedRva(target, a_runtime.executeCommandRva)" in install_execute,
              "InstallExecuteCommandHook must compare the resolved address with the table entry")
        check("REX::UNRESTRICTED_CAST<void*>(&ExecuteCommandDetour)" in install_execute,
              "InstallExecuteCommandHook must install ExecuteCommandDetour")
        publish = install_execute.find("request.publishOriginal = ")
        call = install_execute.find("F4T::InlineHook::Install(request, nullptr, result)")
        check(0 <= publish < call and "g_originalExecuteCommand.store(" in install_execute[publish:call],
              "InstallExecuteCommandHook must publish the trampoline through the request before the hook is enabled")
        check(call >= 0 and "g_originalExecuteCommand.store(" not in install_execute[call:],
              "InstallExecuteCommandHook must not publish the trampoline after Install has enabled the hook")

    inline_hook = ROOT / "CO-BETA/plugin/src/F4TInlineHook.cpp"
    inline_text = inline_hook.read_text(encoding="utf-8") if inline_hook.exists() else ""
    check(bool(inline_text), f"missing {inline_hook}")
    published = inline_text.find("a_request.publishOriginal(a_result.hook->original<void*>());")
    enabled = inline_text.find("a_result.hook->enable()")
    check(0 <= published < enabled,
          "F4T::InlineHook::Install must publish the trampoline before it enables the hook")

    try_install = body_after(text, "void TryInstallHooks(")
    check(try_install is not None, "F4TConsoleGuard.cpp must retry both hooks from TryInstallHooks")
    if try_install is not None:
        check("InstallCompileAndRunHook(*runtime);" in try_install,
              "TryInstallHooks must install the Script::CompileAndRun hook")
        check("InstallExecuteCommandHook(*runtime);" in try_install,
              "TryInstallHooks must install the Console::ExecuteCommand hook")
        check("ResolveVerifiedRuntime()" in try_install,
              "TryInstallHooks must resolve the runtime table entry before installing either hook")

    reset = body_after(text, "void Reset(")
    check(reset is not None and "g_pendingAllowedCommands.store(0" in reset,
          "Reset must clear the queued-command tokens")

    tick = body_after(text, "void Tick(")
    check(tick is not None and "TryInstallHooks();" in tick,
          "Tick must install the console guard hooks")

    execute = body_after(text, "void ExecuteCommandDetour(")
    check(execute is not None, "F4TConsoleGuard.cpp must define ExecuteCommandDetour")
    if execute is not None:
        scope = execute.find("const InternalCallScope scope;")
        original = execute.find("original(a_command);")
        check(0 <= scope < original,
              "ExecuteCommandDetour must mark an approved command internal so the second door lets it through")
        grant = execute.find("TryAddToken(g_pendingAllowedCommands);")
        check(0 <= grant < scope,
              "ExecuteCommandDetour must record the approved command before the original enqueues it")
        blocked = execute.find("AnnounceBlocked();")
        check(0 <= blocked < original and "return;" in execute[blocked:original],
              "ExecuteCommandDetour must return at entry on a block, before the original runs")
        check("const auto source = ClassifyCaller(returnAddress);" in execute and "IsBlocked(source)" in execute,
              "ExecuteCommandDetour must block on the caller classified by return address")
        gate = execute[blocked:grant]
        check("source == CommandSource::kInternal || source == CommandSource::kPluginCode" in gate,
              "ExecuteCommandDetour must grant tokens only to CO internal or plugin-code calls")
        check("IsBlocked(CommandSource::kGameCode)" in gate,
              "ExecuteCommandDetour must grant tokens only while the guard is enforcing")

    welcome = body_after(text, "void NoteWelcome(")
    check(welcome is not None and "g_pendingAllowedCommands.store(0" in welcome,
          "NoteWelcome must clear tokens minted before the welcome or under an earlier role")

    check("F4T_CO_TEST_FEATURES" not in text,
          "the console guard hooks must stay always on, not gated behind the test-feature switch")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("F4TConsoleGuardSecondDoorContract: the Flash-callable console door is not covered.")
        _merge_exit(1)

    print("F4TConsoleGuardSecondDoorContract: all checks passed.")

def _section_002_f4tconsoleguardsingleentrycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TConsoleGuardSingleEntryContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TConsoleGuardSingleEntryContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN_SRC = ROOT / "CO-BETA/plugin/src"
    UI_SRC = ROOT / "CommonwealthOnlineUI/src"
    GUARD = PLUGIN_SRC / "F4TConsoleGuard.cpp"
    SETUP_FLOW = PLUGIN_SRC / "F4TSessionCharacterSetupFlow.cpp"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_line_comments(text):
        return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


    def direct_console_calls(path):
        text = path.read_text(encoding="utf-8") if path.exists() else ""
        return [
            (number, line)
            for number, line in enumerate(strip_line_comments(text).splitlines(), 1)
            if "RE::Console::ExecuteCommand" in line
        ]


    check(GUARD.exists(), f"missing {GUARD}")

    for directory in (PLUGIN_SRC, UI_SRC):
        if not directory.exists():
            check(False, f"missing source dir {directory}")
            continue
        for path in sorted(directory.glob("*.cpp")):
            if path == GUARD:
                continue
            for number, line in direct_console_calls(path):
                check(False, f"{path.name}:{number} calls RE::Console::ExecuteCommand directly; route it through F4TConsoleGuard.cpp:ExecutePluginCommand")

    guard_text = strip_line_comments(GUARD.read_text(encoding="utf-8")) if GUARD.exists() else ""
    allowed = [line for line in guard_text.splitlines() if "RE::Console::ExecuteCommand" in line]
    check(len(allowed) == 1, f"F4TConsoleGuard.cpp must contain exactly one RE::Console::ExecuteCommand (the ExecutePluginCommand body), found {len(allowed)}")
    execute_body_start = guard_text.find("void ExecutePluginCommand(")
    check(execute_body_start >= 0, "F4TConsoleGuard.cpp must define ExecutePluginCommand")
    if execute_body_start >= 0:
        brace = guard_text.find("{", execute_body_start)
        depth = 0
        body = None
        for index in range(brace, len(guard_text)):
            if guard_text[index] == "{":
                depth += 1
            elif guard_text[index] == "}":
                depth -= 1
                if depth == 0:
                    body = guard_text[brace + 1:index]
                    break
        check(body is not None and "RE::Console::ExecuteCommand(a_command);" in body,
              "the single RE::Console::ExecuteCommand must live inside ExecutePluginCommand")

    flow_text = strip_line_comments(SETUP_FLOW.read_text(encoding="utf-8")) if SETUP_FLOW.exists() else ""
    check('ExecutePluginCommand("cqf CO_CharacterSetupQuest Prime")' in flow_text,
          "EnsureRunning must prime the setup quest through ExecutePluginCommand")
    check('ExecutePluginCommand("startquest CO_CharacterSetupQuest")' in flow_text,
          "EnsureRunning must start the setup quest through ExecutePluginCommand")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("F4TConsoleGuardSingleEntryContract: plugin console entry is not single.")
        _merge_exit(1)

    print("F4TConsoleGuardSingleEntryContract: all checks passed.")

def _section_003_f4tconsoleguardtrampolinewarncontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TConsoleGuardTrampolineWarnContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TConsoleGuardTrampolineWarnContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    GUARD = ROOT / "CO-BETA/plugin/src/F4TConsoleGuard.cpp"
    POLICY = ROOT / "CO-BETA/plugin/include/F4TConsoleGuardPolicy.h"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def body_after(source, anchor):
        start = source.find(anchor)
        if start < 0:
            return None
        brace = source.index("{", start + len(anchor))
        depth = 0
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[brace + 1:index]
        return None


    def missing_trampoline(detour):
        start = detour.find("if (!original)")
        if start < 0:
            return None
        end = detour.find("return;", start)
        if end < 0:
            return None
        return detour[start:end]


    text = GUARD.read_text(encoding="utf-8") if GUARD.exists() else ""
    policy = POLICY.read_text(encoding="utf-8") if POLICY.exists() else ""
    check(bool(text), f"missing {GUARD}")
    check("inline bool ClaimLogOnce(" in policy, "ClaimLogOnce must live in F4TConsoleGuardPolicy.h")
    check("return !a_flag.exchange(true, std::memory_order_acq_rel);" in policy,
          "ClaimLogOnce must publish the flag and report only the first claim")

    for name, flag in (
        ("void ExecuteCommandDetour(", "g_executeTrampolineWarned"),
        ("void CompileAndRunDetour(", "g_compileTrampolineWarned"),
    ):
        detour = body_after(text, name)
        check(detour is not None, f"{name} must be defined")
        if detour is None:
            continue
        gap = missing_trampoline(detour)
        check(gap is not None, f"{name} must return when the trampoline is missing")
        if gap is None:
            continue
        check("WarnOnce(" in gap and flag in gap, f"{name} must warn once through {flag}")
        check("REX::WARN" not in gap, f"{name} must not warn on every missing trampoline")

    install = body_after(text, "void TryInstallHooks(")
    check(install is not None, "TryInstallHooks must be defined")
    if install is not None:
        start = install.find("if (!runtime)")
        end = install.find("} else {", start)
        gap = install[start:end] if start >= 0 and end > start else ""
        check("WarnOnce(" in gap and "g_unverifiedRuntimeWarned" in gap,
              "an unverified runtime must warn once")
        check("REX::WARN" not in gap, "an unverified runtime must not warn on every install retry")

    reset = body_after(text, "void Reset(")
    check(reset is not None, "Reset must be defined")
    if reset is not None:
        check("g_executeTrampolineWarned.store(false" in reset, "Reset must clear the execute trampoline warning")
        check("g_compileTrampolineWarned.store(false" in reset, "Reset must clear the compile trampoline warning")
        check("g_unverifiedRuntimeWarned.store(false" in reset, "Reset must clear the unverified-runtime warning")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("F4TConsoleGuardTrampolineWarnContract: missing trampoline still warns every call.")
        _merge_exit(1)

    print("missing trampoline warns once: execute and compile")
    print("unverified runtime warns once")
    print("F4TConsoleGuardTrampolineWarnContract: all checks passed.")

def _section_004_f4tconsoleguardwelcomeintegrationcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TConsoleGuardWelcomeIntegrationContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TConsoleGuardWelcomeIntegrationContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    INBOUND = ROOT / "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
    EXPECTED_INCLUDE = '#include "F4TConsoleGuard.h"'
    EXPECTED_CALL = "F4T::ConsoleGuard::NoteWelcome(a_packet);"
    OWNER = "F4TNetworkingInbound.cpp belongs to the sync-proxy workstream; the console-block branch must not edit it"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def body_after(source, anchor):
        start = source.find(anchor)
        if start < 0:
            return None
        brace = source.index("{", start + len(anchor))
        depth = 0
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[brace + 1:index]
        return None


    text = INBOUND.read_text(encoding="utf-8") if INBOUND.exists() else ""
    check(bool(text), f"missing {INBOUND}")
    check(EXPECTED_INCLUDE in text, f"F4TNetworkingInbound.cpp must include F4TConsoleGuard.h ({OWNER})")

    body = body_after(text, "void HandleWelcomePacket(")
    check(body is not None, "F4TNetworkingInbound.cpp must define HandleWelcomePacket")
    if body is not None:
        check(
            body.count(EXPECTED_CALL) == 1,
            f"HandleWelcomePacket must call {EXPECTED_CALL} exactly once ({OWNER})",
        )
        inventory = body.find("F4T::InventorySync::NoteWelcome(a_packet);")
        objects = body.find("F4T::ObjectSync::NoteWelcome(a_packet);")
        guard = body.find(EXPECTED_CALL)
        check(
            0 <= inventory < objects < guard,
            "the console guard welcome must follow the inventory and object sync welcomes",
        )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(
            "F4TConsoleGuardWelcomeIntegrationContract: EXPECTED RED on the console-block branch. "
            "The server role is inert until F4TNetworkingInbound.cpp:HandleWelcomePacket calls "
            "F4T::ConsoleGuard::NoteWelcome; the include and call lines are listed under INTEGRATION LINES "
            "in docs/code-notes/CO-BETA-plugin.md."
        )
        _merge_exit(1)

    print("F4TConsoleGuardWelcomeIntegrationContract: all checks passed.")


def _section_006_f4tpolicynointeractcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TPolicyNoInteractContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TPolicyNoInteractContract.py")
    """Contract: policy noInteract (free-roam NPC gating, #669). NoTalk hooks consult the policy block set, vendors and the player stay talkable, and the hooks keep only the TESNPC slots 0x40/0x60 (#568)."""
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    src = root / "CO-BETA/plugin/src"
    inc = root / "CO-BETA/plugin/include"


    def read(path):
        return path.read_text(encoding="utf-8")


    def block(text, start, end):
        body = text[text.index(start):]
        return body[:body.index(end)]


    nt = read(src / "F4TProxyNoTalk.cpp")
    nt_h = read(inc / "F4TProxyNoTalk.h")
    po = read(src / "F4TPolicyNoInteract.cpp")
    po_h = read(inc / "F4TPolicyNoInteract.h")

    for text in (nt, nt_h, po, po_h):
        assert "MarkAsDeleted" not in text

    assert "void Apply(const std::vector<F4T::WorldPolicy::FormKey>& a_keys);" in po_h
    assert "void Clear();" in po_h
    assert "bool Active();" in po_h
    assert "void Apply(const std::vector<F4T::WorldPolicy::FormKey>& a_keys)" in po
    assert "void Clear()" in po
    assert "bool Active()" in po
    assert '#include "F4TProxyNoTalk.h"' in po

    assert "bool InstallOnBase(RE::TESBoundObject* a_base);" in nt_h
    assert "void SetPolicyBlock(std::vector<std::uint32_t> a_baseFormIds);" in nt_h
    assert "bool PolicyBlockActive();" in nt_h

    assert "constexpr std::size_t kActivateSlot = 0x40;" in nt
    assert "constexpr std::size_t kGetActivateTextSlot = 0x60;" in nt
    assert nt.count("constexpr std::size_t k") == 2
    assert nt.count("write_vfunc(") == 3
    assert "write_vfunc" not in po and "VtableGuard" not in po
    assert "return InstallOnBase(a_proxy->GetObjectReference());" in nt

    policy = block(nt, "bool IsPolicyBlocked(RE::TESObjectREFR& a_ref)", "bool IsNoTalkTarget(")
    assert "// TODO-verify(#669): non-proxy NPC activation routes through these TESNPC slots" in nt
    assert "actor->vendorFaction != nullptr" in policy
    assert "a_ref.IsPlayerRef()" in policy
    assert policy.index("actor->vendorFaction != nullptr") < policy.index("g_policyBlocked.contains(")
    assert policy.count("std::scoped_lock lock(g_policyMutex);") == 1
    assert "actor->IsDead(true)" in policy and "IsDead(false)" not in policy
    assert policy.index("actor->IsDead(true)") < policy.index("g_policyBlocked.contains(")

    targets = block(nt, "bool IsNoTalkTarget(RE::TESObjectREFR* a_ref)", "bool ActivateHook(")
    assert "ProxyPuppet::IsProxyFormID(a_ref->GetFormID()) || IsPolicyBlocked(*a_ref)" in targets

    hooks = block(nt, "bool ActivateHook(", "VtableGuard::SlotRequest MakeRequest(")
    assert hooks.count("if (IsNoTalkTarget(a_ref)) {") == 2

    install = block(nt, "bool InstallOnBase(RE::TESBoundObject* a_base)", "bool Installed()")
    assert "VtableGuard::VerifySlot(activateRequest)" in install
    assert "VtableGuard::SlotMatchesExpectedRva(activateRequest.slotValue" in install
    assert install.count("table.write_vfunc(") == 2

    set_block = block(nt, "void SetPolicyBlock(std::vector<std::uint32_t> a_baseFormIds)", "bool PolicyBlockActive()")
    assert set_block.index("std::unordered_set<std::uint32_t> next(") < set_block.index("std::scoped_lock lock(g_policyMutex);")
    assert "g_policyBlocked.swap(next);" in set_block

    apply = block(po, "void Apply(", "void Clear()")
    assert "dataHandler->LookupFormID(key.localId, key.plugin)" in apply
    assert "if (resolvedId == 0) {" in apply
    assert "RE::TESForm::GetFormByID<RE::TESNPC>(resolvedId)" in apply
    assert "npc->IsPlayer()" in apply
    assert "F4T::ProxyNoTalk::SetPolicyBlock(blocked);" in apply
    assert "F4T::ProxyNoTalk::InstallOnBase(install);" in apply
    assert "if (dataHandler == nullptr) {" in apply and apply.index("if (dataHandler == nullptr) {") < apply.index("REX::WARN(")
    assert apply.index("F4T::ProxyNoTalk::InstallOnBase(install);") < apply.index("!F4T::ProxyNoTalk::Installed()")
    assert apply.count("REX::WARN(") == 2 and apply.count("TODO-verify(#669)") == 2
    assert "F4T::ProxyNoTalk::SetPolicyBlock({});" in po
    assert "F4T::ProxyNoTalk::PolicyBlockActive()" in po

    print("F4TPolicyNoInteractContract: OK")

def _section_007_f4tproxylivefirecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TProxyLiveFireContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TProxyLiveFireContract.py")
    """Contract: proxy live-fire damage guard (FathersFallout item 3). A proxy fires live only after Arm() sets attackDamageMult to 0 and the TESHitEvent give-back sink is registered; otherwise the shot is skipped."""
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    src = root / "CO-BETA/plugin/src"
    inc = root / "CO-BETA/plugin/include"


    def read(path):
        return path.read_text(encoding="utf-8")


    guard = read(src / "F4TProxyLiveFireGuard.cpp")
    guard_h = read(inc / "F4TProxyLiveFireGuard.h")
    hit_guard = read(src / "F4TProxyAttackHitGuard.cpp")

    for text in (guard, guard_h):
        assert text.startswith("// License: GPL-3.0 with additional terms (FatherLamb)")
    assert len(guard.splitlines()) <= 1000

    assert "actorValues->attackDamageMult" in guard
    assert "SetBaseActorValue(*actorValues->attackDamageMult, 0.0F);" in guard
    assert "return a_proxy->GetBaseActorValue(*actorValues->attackDamageMult) == 0.0F;" in guard
    assert "return SuppressDamage(a_proxy);" in guard
    assert "RE::TESHitEvent::GetEventSource()" in guard
    assert "eventSource->RegisterSink(&g_giveBackSink);" in guard
    assert "ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, healthDamage" in guard
    assert "victim.get() != localPlayer" in guard
    assert "IsProxyFormID(aggressor->GetFormID())" in guard
    assert "!std::isfinite(healthDamage) || healthDamage <= 0.0F" in guard
    assert "if (!localPlayer || !a_event.usesHitData) {" in guard
    assert guard.index("if (!localPlayer || !a_event.usesHitData) {") < guard.index("const auto aggressor = a_event.hitData.aggressor.get();")
    assert "if (!g_sinkRegistered.exchange(true)) {" in guard
    assert "g_sinkRegistered = false;" in guard
    assert '#include "F4THookThreadTable.h"' in guard
    assert "F4T::HookThread::Note(F4T::HookThread::Id::LiveFireGiveBack);" in guard
    hook_ids = read(inc / "F4THookThreadTable.h")
    assert "ProxyActivate = 18," in hook_ids and "ProxyGetActivateText = 19," in hook_ids and "LiveFireGiveBack = 20," in hook_ids

    weapon = hit_guard[hit_guard.index("bool WeaponFireHandlerHook("):hit_guard.index("void Install()")]
    assert weapon.index("IsProxyActor(a_actor)") < weapon.index("F4T::ProxyLiveFire::Arm(&a_actor)")
    assert weapon.index("F4T::ProxyLiveFire::Arm(&a_actor)") < weapon.index("return true;") < weapon.index("g_originalWeaponFireHandler(")
    assert "#include \"F4TProxyLiveFireGuard.h\"" in hit_guard
    assert "WeaponFireHandler fires live only with proxy damage suppression" in hit_guard

    print("F4TProxyLiveFireContract: OK")

def _section_008_f4tproxynotalkcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TProxyNoTalkContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TProxyNoTalkContract.py")
    """Contract: proxy NoTalk (FathersFallout item 4). Slot indices are TESNPC vslots 0x40/0x60; only the OG RVAs are confirmed; the NG and AE RVAs are structural (tagged in F4TProxyNoTalk.cpp), and the E-key -> ActivateRef route and the dialogue start remain unproven (#568)."""
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    src = root / "CO-BETA/plugin/src"
    inc = root / "CO-BETA/plugin/include"


    def read(path):
        return path.read_text(encoding="utf-8")


    def block(text, start, end):
        body = text[text.index(start):]
        return body[:body.index(end)]


    nt = read(src / "F4TProxyNoTalk.cpp")
    nt_h = read(inc / "F4TProxyNoTalk.h")
    sync = read(src / "F4TProxyAnimationSync.cpp")

    for text in (nt, nt_h):
        assert text.startswith("// License: GPL-3.0 with additional terms (FatherLamb)")
    assert len(nt.splitlines()) <= 1000

    assert "constexpr std::size_t kActivateSlot = 0x40;" in nt
    assert "constexpr std::size_t kGetActivateTextSlot = 0x60;" in nt
    assert "constexpr NoTalkRvas kOgRvas{ 0x5B6050, 0x5B6FE0 };" in nt
    assert "constexpr NoTalkRvas kNgRvas{ 0x602090, 0x603090 }; // structural" in nt
    assert "constexpr NoTalkRvas kAeRvas{ 0x655D60, 0x656D60 }; // structural" in nt
    assert "InstallNoTalk(" not in nt + nt_h + sync

    install = block(nt, "bool InstallOnBase(RE::TESBoundObject* a_base)", "bool Installed()")
    assert "return InstallOnBase(a_proxy->GetObjectReference());" in nt
    assert "VtableGuard::VerifySlot(activateRequest)" in install
    assert install.index("VtableGuard::VerifySlot(activateRequest)") < install.index("VtableGuard::VerifySlot(textRequest)")
    assert install.index("VtableGuard::VerifySlot(textRequest)") < install.index("VtableGuard::SlotMatchesExpectedRva(activateRequest.slotValue")
    assert install.index("VtableGuard::SlotMatchesExpectedRva(activateRequest.slotValue") < install.index("g_originalActivate = ")
    assert install.index("g_originalGetActivateText = ") < install.index("table.write_vfunc(kActivateSlot, &ActivateHook);")
    assert install.index("table.write_vfunc(kGetActivateTextSlot, &GetActivateTextHook);") < install.index("g_installed =")
    assert install.count("table.write_vfunc(") == 2
    assert "*reinterpret_cast<std::uintptr_t*>(&vtbl" not in nt
    assert "if (!a_base->As<RE::TESNPC>()) {" in install

    hooks = block(nt, "bool ActivateHook(", "VtableGuard::SlotRequest MakeRequest(")
    assert hooks.count("if (IsNoTalkTarget(a_ref)) {") == 2
    assert "return true;" in hooks and "return false;" in hooks
    assert "ProxyPuppet::IsProxyFormID(a_ref->GetFormID())" in nt
    assert "if (!a_ref) {" in nt
    assert '#include "F4THookThreadTable.h"' in nt
    assert hooks.index("Id::ProxyActivate);") < hooks.index("if (IsNoTalkTarget(a_ref)) {")
    assert hooks.index("Id::ProxyGetActivateText);") > hooks.index("bool GetActivateTextHook(")
    restore = block(nt, "void RestoreSlotIfReplaced(", "VtableGuard::SlotRequest MakeRequest(")
    assert "VtableGuard::ReadSlot(a_request) == a_request.replacement" in restore
    assert "a_table.write_vfunc(a_request.slotIndex, a_original);" in restore
    assert install.index("RestoreSlotIfReplaced(table, activateRequest, g_originalActivate);") > install.index("g_installed =")
    assert install.index("RestoreSlotIfReplaced(table, textRequest, g_originalGetActivateText);") > install.index("g_installed =")

    assert "#include \"F4TProxyNoTalk.h\"" in sync
    assert sync.index("F4T::AnimationGraphOverride::Install(&a_proxy);") < sync.index("F4T::ProxyNoTalk::Install(&a_proxy);")

    print("F4TProxyNoTalkContract: OK")


def _section_010_f4tproxyprivatebasecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TProxyPrivateBaseContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TProxyPrivateBaseContract.py")
    import re
    from pathlib import Path


    plugin = Path(__file__).resolve().parents[1]
    repo = plugin.parents[1]
    private_base = (plugin / "src/F4TProxyPrivateBase.cpp").read_text(encoding="utf-8")
    internal = (plugin / "include/F4TProxyActorControllerInternal.h").read_text(encoding="utf-8")
    save_safety_header = (plugin / "include/F4TProxySaveSafety.h").read_text(encoding="utf-8")
    appearance = (plugin / "src/F4TProxyActorControllerAppearance.cpp").read_text(encoding="utf-8")
    controller = (plugin / "src/F4TProxyActorController.cpp").read_text(encoding="utf-8")
    placement = (plugin / "src/F4TProxyActorControllerSlotPlacement.cpp").read_text(encoding="utf-8")
    resolution = (plugin / "src/F4TProxyActorControllerSlotResolution.cpp").read_text(encoding="utf-8")
    lifecycle = (plugin / "src/F4TProxyActorControllerSlotLifecycle.cpp").read_text(encoding="utf-8")
    actor_resolution = (plugin / "src/F4TProxyActorControllerActorResolution.cpp").read_text(encoding="utf-8")
    runtime_spawn = (plugin / "src/F4TProxyActorControllerRuntimeSpawn.cpp").read_text(encoding="utf-8")
    save_safety = (plugin / "src/F4TProxySaveSafety.cpp").read_text(encoding="utf-8")
    main_cpp = (plugin / "src/main.cpp").read_text(encoding="utf-8")
    workflow_path = next((p for p in (repo / ".github/public-ci/workflows/co-contracts.yml", repo / ".github/workflows/co-contracts.yml") if p.is_file()), None)
    workflow = workflow_path.read_text(encoding="utf-8") if workflow_path else ""

    assert "g_allowSharedProxyBaseAppearanceWrites" not in appearance, "the global shared-base write gate must be gone"
    for name, text in (("internal", internal), ("appearance", appearance), ("controller", controller), ("placement", placement), ("resolution", resolution), ("lifecycle", lifecycle), ("save safety", save_safety), ("private base", private_base)):
        assert "privateProxyBaseFormId" not in text, f"{name}: the private base is tracked by pointer, not by form id"
    assert "npc != a_slot.privateProxyBase" in appearance, "the appearance apply must be guarded by the slot's private base"
    assert "AddChange(RE::CHANGE_TYPES::kNPCFace)" in appearance, "a changed face must mark the NPC change flag"
    assert "CreateDuplicateForm(false, nullptr)" in private_base, "the private base must be a runtime duplicate of the shared base"
    assert "base->faceNPC = a_sharedBase.GetRootFaceNPC();" in private_base, "the copy must keep the face chain rooted at the shared base so FaceGen head geometry resolves for the runtime duplicate"
    assert "delete base" not in private_base and "delete duplicate" not in private_base, "the copy must be destroyed through its slot 0 destructor, not a delete expression"
    assert "SetTemporary(" not in private_base, "the private base must not be marked temporary"
    assert "GetFormByID" not in private_base, "the private base is resolved from the slot pointer, not a form id lookup"
    assert re.search(r"if \(!sharedBase\) \{\s*a_slot\.privateProxyBaseBuildFailed = true;", private_base), "an unresolved shared base must mark the build failed so spawn falls back"
    assert re.search(r"if \(!base\) \{\s*a_slot\.privateProxyBaseBuildFailed = true;", private_base), "a failed duplicate must mark the build failed so spawn falls back"

    release_body = private_base.split("void ReleaseSlotPrivateProxyBase(ProxyActorSlot& a_slot)", 1)[1].split("void ParkPrivateBase(", 1)[0]
    assert "DestroyDuplicateForm" not in release_body, "a release only queues the copy; the graveyard sweep parks it"
    assert "->" not in release_body and "GetFormID" not in release_body, "a release must not dereference the copy, which may already be dangling"
    sweep_body = private_base.split("void SweepPrivateProxyBaseGraveyard()", 1)[1].split("RE::TESNPC* EnsureSlotPrivateProxyBase", 1)[0]
    assert "a_entry.proxyHandle.get()" in sweep_body and "a_entry.pendingSpawnHandle.get()" in sweep_body, "the copy is parked only after both proxy handles stop resolving"
    assert "ParkPrivateBase(*a_entry.base);" in sweep_body, "the sweep parks the copy for reuse instead of freeing it"
    park_body = private_base.split("void ParkPrivateBase(RE::TESNPC& a_base)", 1)[1].split("void SweepPrivateProxyBaseGraveyard()", 1)[0]
    assert "g_parkedPrivateBases.push_back(std::addressof(a_base));" in park_body and "MayParkPrivateBase(parkedForSex)" in park_body, "a park is refused once the per-sex cap is full"
    assert "DestroyDuplicateForm" not in park_body and "delete" not in park_body, "a park never frees the copy"
    assert "IsDeleted" not in sweep_body, "the park waits for the handle, not for the deleted flag"
    assert "DestroyDuplicateForm" not in private_base, "a runtime duplicate is never freed by hand; the engine keeps its form record"
    assert "TakeParkedPrivateBase(a_wantFemale)" in private_base, "a build must reuse a parked copy before duplicating the shared base again"

    drop_body = private_base.split("void DropPrivateProxyBasesWithoutFreeing()", 1)[1]
    assert "g_privateBaseGraveyard.clear();" in drop_body and "g_parkedPrivateBases.clear();" in drop_body, "a load drops the queued and the parked copies"
    assert "DestroyDuplicateForm" not in drop_body, "a load drops the copies without running a destructor"
    assert "void DropPrivateProxyBasesWithoutFreeing();" in save_safety_header, "the drop is declared on the save safety interface"
    assert save_safety.split("void AfterLoadSweep(bool a_loadSucceeded, bool a_clearStandIns)", 1)[1].split("{", 1)[1].lstrip().startswith("DropPrivateProxyBasesWithoutFreeing();"), "AfterLoadSweep drops the private bases before anything else touches them"
    assert re.match(r"\s*F4T::SurvivalPolicy::g_clientState\.BeginTrackedLoad\(\);\s*F4T::ProxySaveSafety::DropPrivateProxyBasesWithoutFreeing\(\);", main_cpp.split("kPreLoadGame: {", 1)[1]), "kPreLoadGame closes the Survival barrier then drops the private bases"

    update_body = controller.split("\tvoid Update()", 1)[1].split("\n\t}\n", 1)[0]
    assert "SweepPrivateProxyBaseGraveyard();" in update_body, "the graveyard is swept from the per-tick update"

    for name, text in (("controller", controller), ("lifecycle", lifecycle), ("resolution", resolution), ("save safety", save_safety)):
        assert not re.search(r"(?:proxyHandle|pendingSpawnHandle)\.reset\(\);[^{}]*?ReleaseSlotPrivateProxyBase\(", text), f"{name}: a proxy or pending handle must not be reset before its private base is queued"

    assert re.search(r"ReleaseSlotPrivateProxyBase\(a_slot\);\s*a_slot\.pendingSpawnHandle\.reset\(\);", controller), "a released settling spawn queues its private base before its pending handle is reset"
    assert re.search(r"ReleaseSlotPrivateProxyBase\(slot\);\s*slot\.pendingSpawnHandle\.reset\(\);", lifecycle), "the pending-handle cell release queues its private base before the handle is reset"
    assert re.search(r"ReleaseSlotPrivateProxyBase\(slot\);\s*slot\.proxyHandle\.reset\(\);", lifecycle), "the expired-handle cell release queues its private base before the handle is reset"
    assert re.search(r"if \(auto\* reusable = FindSlot\(a_reusablePlayerId\)\) \{\s*ReleaseSlotPrivateProxyBase\(\*reusable\);\s*\}\s*auto node = g_proxySlots\.extract\(a_reusablePlayerId\);", controller), "a reassigned slot queues the old player's private base before its node is extracted"
    assert re.search(r"ReleaseSlotPrivateProxyBase\(slot\);\s*slot\.lifecycleState = ProxyLifecycleState::kReusable;", lifecycle), "a reusable slot releases the old player's private base"
    assert "IsCustomPlayerProxyActor(a_proxy)" in controller, "only custom player proxies are respawned onto the private base"
    assert "IsPrivateProxyBase(*baseActor)" in controller, "a stale private base forces a respawn even without an appearance update"
    assert "IsPrivateProxyBase(*baseActor)" in actor_resolution, "actor resolution recognises private bases as custom proxies"
    assert "IsPrivateProxyBase(*baseActor)" in runtime_spawn, "runtime spawn recognises private bases as proxy bases"

    respawn_body = controller.split("void RespawnSlotProxy(", 1)[1].split("enum class SlotRespawnResult", 1)[0]
    assert respawn_body.index("DestroyProxyActor(a_proxy);") < respawn_body.index("ResetSlotForRespawn(a_slot);"), "a respawn destroys the proxy before resetting the slot"

    reset_body = resolution.split("void ResetSlotForRespawn(ProxyActorSlot& a_slot)", 1)[1].split("RemoveProxyPipboyLight", 1)[0]
    assert "ReleaseSlotPrivateProxyBase(a_slot);" in reset_body, "a slot reset releases its private base before the handles drop"

    clear_body = save_safety.split("void ClearProxySessionState()", 1)[1].split("g_proxySlots.clear();", 1)[0]
    assert "ReleaseSlotPrivateProxyBase(slot);" in clear_body, "session clear releases the private bases before the slots are cleared"

    destroy_index = lifecycle.index("F4T::ProxySaveSafety::DestroyProxyActor(*proxy);")
    release_index = lifecycle.index("ReleaseSlotPrivateProxyBase(slot);", destroy_index)
    assert destroy_index < release_index, "the private base is released only after its proxy is destroyed"

    spawn_body = placement.split("bool TryBeginSlotPlaceAtMeSpawn", 1)[1]
    assert spawn_body.index("EnsureSlotPrivateProxyBase(a_slot, wantFemale)") < spawn_body.index("ResolveProxyActorBaseForSex(wantFemale)"), "spawn prefers the slot's private base over the shared base"

    apply_body = appearance.split("void ApplyProxyAppearanceFromRemoteState(", 1)[1]
    assert apply_body.index("npc != a_slot.privateProxyBase") < apply_body.index("hasAppliedAppearanceState = true"), "the private base guard runs before the applied state is recorded"

    assert "plugin/tests/F4TProxyConsoleContracts.py" in workflow, "the contract must be registered in co-contracts.yml"

    assert "MayBuildPrivateBase(CountLivePrivateBasesForSex(a_wantFemale))" in private_base, "a build is refused once the per-sex live cap is reached"
    ensure_body = private_base.split("RE::TESNPC* EnsureSlotPrivateProxyBase(ProxyActorSlot& a_slot, bool a_wantFemale)", 1)[1]
    assert ensure_body.index("MayBuildPrivateBase(") < ensure_body.index("ResolveProxyActorBaseForSex(a_wantFemale)"), "the live cap is checked before the shared base is resolved"
    capped_window = ensure_body.split("MayBuildPrivateBase(", 1)[1].split("ResolveProxyActorBaseForSex", 1)[0]
    assert "kPrivateBaseCapped" in capped_window and "SweepPrivateProxyBaseGraveyard();" in capped_window, "a capped build first reclaims released copies"
    assert "privateProxyBaseBuildFailed = true" in capped_window, "a capped build latches the failure so the spawn falls back to the shared base"
    assert "AttachParkedPrivateBase(a_slot, *reclaimed)" in capped_window, "a reclaimed copy is attached before the cap latches"
    assert "g_loadRetainedPrivateBases" in drop_body and "delete" not in drop_body, "a load retains the copies for the process lifetime instead of freeing them"

    form_destruction = re.compile(
        r"deletingDestructor|DeletingDestructor|DestroyDuplicateForm|->~TES|\.~TES|reinterpret_cast<\s*void\s*\*\*\s*>\s*\([^;]*\)\s*\[0\]|\bdelete\s+[&*]?\s*\w*(?:form|base|npc|ref|actor|duplicate|proxy)\w*\s*;",
        re.IGNORECASE,
    )
    assert form_destruction.search("deletingDestructor(&a_form, 1);"), "the scan must catch the deleting destructor call"
    assert form_destruction.search("reinterpret_cast<DeletingDestructor*>(*reinterpret_cast<void**>(&a_form))[0](&a_form, 1);"), "the scan must catch a slot-0 virtual destructor call"
    assert form_destruction.search("delete duplicateForm;"), "the scan must catch a delete expression on a form-like variable"
    assert not form_destruction.search("DeletePuppet(*actor);"), "the scan must not flag the puppet delete helper"

    offenders = []
    for scan_root in (plugin / "src", plugin / "include", repo / "co-core" / "src", repo / "co-core" / "include"):
        for source in sorted(scan_root.rglob("*")):
            if not source.is_file() or source.suffix not in (".cpp", ".h", ".hpp", ".inl"):
                continue
            text = source.read_text(encoding="utf-8", errors="replace")
            for match in form_destruction.finditer(text):
                line_number = text.count("\n", 0, match.start()) + 1
                offenders.append(f"{source.relative_to(repo).as_posix()}:{line_number}: {match.group(0)}")
    assert not offenders, "no code may run a form's deleting destructor or delete an engine form:\n" + "\n".join(offenders)
    print("F4TProxyPrivateBaseContract: ok")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TConsoleGuardSecondDoorContract.py", _section_001_f4tconsoleguardseconddoorcontract),
    ("CO-BETA/plugin/tests/F4TConsoleGuardSingleEntryContract.py", _section_002_f4tconsoleguardsingleentrycontract),
    ("CO-BETA/plugin/tests/F4TConsoleGuardTrampolineWarnContract.py", _section_003_f4tconsoleguardtrampolinewarncontract),
    ("CO-BETA/plugin/tests/F4TConsoleGuardWelcomeIntegrationContract.py", _section_004_f4tconsoleguardwelcomeintegrationcontract),
    ("CO-BETA/plugin/tests/F4TPolicyNoInteractContract.py", _section_006_f4tpolicynointeractcontract),
    ("CO-BETA/plugin/tests/F4TProxyLiveFireContract.py", _section_007_f4tproxylivefirecontract),
    ("CO-BETA/plugin/tests/F4TProxyNoTalkContract.py", _section_008_f4tproxynotalkcontract),
    ("CO-BETA/plugin/tests/F4TProxyPrivateBaseContract.py", _section_010_f4tproxyprivatebasecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TProxyConsoleContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
