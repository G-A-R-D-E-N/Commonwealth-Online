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
    sys.exit(1)

print("F4TConsoleGuardSecondDoorContract: all checks passed.")
