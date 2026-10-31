from pathlib import Path
import re
import sys

PLUGIN = Path(__file__).resolve().parents[1]
REPO = PLUGIN.parents[1]
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(path):
    return re.sub(r"//[^\n]*", "", (PLUGIN / path).read_text(encoding="utf-8"))


def read_root(path):
    return (REPO / path).read_text(encoding="utf-8")


commands = read("include/F4TVatsPolicyCommands.h")
glue = read("src/F4TVatsPolicy.cpp")
glue_header = read("include/F4TVatsPolicy.h")
enforcer = read("src/F4TWorldPolicyEnforcer.cpp")
session = read("src/F4TSessionLaunch.cpp")
main = read("src/main.cpp")

check("F4T::VatsPolicy" not in main,
      "main.cpp must stay untouched by the VATS policy; the enforcer and session hooks own it")

check("enum class Vats" in read_root("co-core/include/F4TWorldPolicy.h"),
      "the VATS mode must live in the shared co-core world policy")
check("Vats vats{ Vats::Disabled }" in read_root("co-core/include/F4TWorldPolicy.h"),
      "a policy without a VATS decision must default to disabled")

check('kMaxEngageDistanceSetting{ "fVATSMaxEngageDistance" }' in commands,
      "the disable mechanism must be the fVATSMaxEngageDistance game setting")
check('kVanillaMaxEngageDistance{ 5000.0F }' in commands,
      "the fallback distance must be the vanilla 5000 before any snapshot")
check("bVATSDisable" not in commands and "bVATSDisable" not in glue,
      "bVATSDisable is an INI-only key: setgs cannot resolve it and it must not be used")
check('"setgs " + std::string{ kMaxEngageDistanceSetting } + " 0"' in commands,
      "the disabled mode must block VATS by zeroing fVATSMaxEngageDistance")
check(commands.count('"setgs "') == 15,
          "the command list must be exactly 1 disabled + 6 real-time + 1 vanilla + 1 fallback + 6 snapshot restore commands")
for setting in ("fVATSTimeMultEnter", "fVATSTimeMultExit", "fVATSTimeMultTargetSelect",
                "fVATSPlayerMagicTimeSlowdownMult", "bVATSMovementEvents"):
    check('"' + setting in commands, "the real-time list must set " + setting)
    check('" 1"' in commands, "the real-time list must remove slow motion with 1")
check('case Vats::RealTime:' in commands and 'case Vats::Vanilla:' in commands,
      "every server mode must map to an explicit command list")

check("RestoreCommands(const Snapshot& a_snapshot)" in commands,
      "the restore path must take the pre-session snapshot")
check("SetBinary" not in commands and "SetFloat" not in commands,
      "the pure command list must not touch engine settings; the glue owns the snapshot")

check("GameSettingCollection::GetSingleton()" in glue,
      "the snapshot must read the running game settings through the commonlib collection")
check("GetBinary()" in glue and "GetFloat()" in glue,
      "the snapshot must read the binary and float settings with the typed accessors")
check("GetType() == RE::Setting::SETTING_TYPE::kBinary" in glue,
      "a binary snapshot read must check the setting type first")
check("GetType() == RE::Setting::SETTING_TYPE::kFloat" in glue,
      "a float snapshot read must check the setting type first")
check(":VATS" in glue, "the snapshot must try the sectioned game setting names first")

check("ExecutePluginCommand" in glue,
      "VATS commands must go through the console guard's internal call path")
check("RE::Console::ExecuteCommand" not in glue,
      "VATS commands must not call the raw console entry point")

check("ApplyForPolicy(const F4T::WorldPolicy::Policy& a_policy)" in glue,
      "the policy apply entry point must take the adopted world policy")
check("g_applied && *g_applied == a_policy.vats" in glue,
      "re-applying the same VATS mode must be a no-op")
check("Reassert()" in glue and "Restore()" in glue and "OnGameLoaded()" in glue,
      "the glue must expose reassert, restore and game-loaded entry points")
check("IsConnectedToServer()" in glue, "a game load without a server must restore, not apply")

check("void Tick(" not in glue, "the VATS policy must do no per-tick work")
check("AddTask" not in glue and "GetTaskInterface" not in glue,
      "the VATS module must run on the caller's game thread without queueing tasks")
check(glue.count('DebugLogging::Log(') == 3,
      "the VATS module must log exactly one line per application, reassert and restore")

check('#include "F4TVatsPolicy.h"' in enforcer, "the enforcer must include the VATS policy header")
check("F4T::VatsPolicy::ApplyForPolicy(g_applied);" in enforcer,
      "an adopted policy must apply its VATS mode")
check("F4T::VatsPolicy::Reassert();" in enforcer,
      "the post-save reapply must reassert the VATS mode")
check("F4T::VatsPolicy::Restore();" in enforcer,
      "clearing the session policy must restore the pre-session settings")
check("F4T::VatsPolicy::OnGameLoaded();" in session,
      "a game load must reassert while connected and restore while offline")

ci_workflow = read_root(".github/workflows/co-beta-ci.yml")
compile_ci = read_root("scripts/windows-compile-ci.py")
check("F4TVatsPolicyContract.py" in ci_workflow, "co-beta-ci.yml must run the VATS policy contract")
check("F4TVatsPolicyContract.py" in compile_ci, "windows-compile-ci.py must run the VATS policy contract")
check("F4TVatsPolicyCommandsTests.cpp" in read("xmake.lua"),
      "the plugin build must compile the VATS command list tests")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("VATS policy contract ok")
