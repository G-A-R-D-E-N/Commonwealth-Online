import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
PLUGIN_DLLS = (("CommonwealthOnline", "CommonwealthOnline.dll"),)
BRIDGE_DLL = ROOT / "CO-BETA/plugin/build/gns-bridge/bin/commonwealth_online_gns_bridge.dll"
BRIDGE_EXPORTS = ("co_gns_client_create", "co_gns_client_destroy", "co_gns_client_poll",
                  "co_gns_client_send", "co_gns_client_disconnect")
COMPILE_ONLY_CL = "/Od /Z7"


def tool(name):
    found = shutil.which(name)
    if not found:
        candidate = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "LLVM/bin" / (name + ".exe")
        if candidate.is_file():
            found = str(candidate)
    if not found:
        raise RuntimeError(f"Required tool is missing: {name}")
    return found


def run(*args, cwd=ROOT, capture=False, timeout=None):
    print("+", subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    return subprocess.run(
        [str(arg) for arg in args], cwd=cwd, check=True, timeout=timeout,
        text=True, encoding="utf-8", capture_output=capture,
    ).stdout


PLUGIN_EXPORTS = ("F4SEPlugin_Load", "F4SEPlugin_Version", "F4SEPlugin_Query", "RequestPluginAPI", "RequestPluginAPIChecked")


def audit_dll(path, exports=("F4SEPlugin_Load", "F4SEPlugin_Version")):
    output = run(tool("llvm-readobj"), "--file-headers", "--coff-exports", path, capture=True)
    if "Format: COFF-x86-64" not in output:
        raise RuntimeError(f"Expected a 64-bit PE: {path}")
    names = {line.strip().removeprefix("Name: ") for line in output.splitlines() if line.strip().startswith("Name: ")}
    for symbol in exports:
        if symbol not in names:
            raise RuntimeError(f"Missing {symbol} export: {path}")
    print(f"PASS: {path.name}: x64 PE and required exports", flush=True)
    return path


def build_dll(project, target, filename, exports=("F4SEPlugin_Load", "F4SEPlugin_Version")):
    run(tool("xmake"), "build", "-y", "-j4", target, cwd=project)
    return audit_dll(project / "build-ci/windows/x64/releasedbg" / filename, exports)


def bridge():
    run(ROOT / "CO-BETA/build-gns-bridge.bat")
    return audit_dll(BRIDGE_DLL, BRIDGE_EXPORTS)


def session_callbacks_retry():
    compiler = tool("clang++")
    project = ROOT / "CommonwealthOnlineUI"
    with tempfile.TemporaryDirectory(prefix="co-session-callbacks-") as temporary:
        source = Path(temporary) / "SessionCallbacks.cpp"
        source.write_text((project / "src/SessionCallbacks.cpp").read_text(encoding="utf-8").split("\n", 1)[1], encoding="utf-8")
        executable = Path(temporary) / "session-callbacks-retry-tests.exe"
        run(compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror", f"-I{project / 'ci/hosted'}", f"-I{project / 'include'}",
            source, project / "ci/SessionCallbacksRetryTests.cpp", "-o", executable)
        run(executable)


def ui():
    xmake = tool("xmake")
    tool("llvm-readobj")
    project = ROOT / "CommonwealthOnlineUI"
    run(xmake, "f", "--file=xmake-parser-ci.lua", "-y", "-p", "windows", "-a", "x64",
        "-m", "debug", "-o", "build-parser-ci", cwd=project)
    for target in ("MultiplayerProfileTests", "ServerBrowserBoundsTests", "LauncherEventProvenanceTests",
                   "SettingsRefreshRouteTests", "ExitSaveGateTests", "WorldModeTests", "DownedStateTests",
                   "SteamActionTests", "VoiceBridgeTests", "QuestCallbacksTests"):
        run(xmake, "build", "--file=xmake-parser-ci.lua", "-y", "-j4", target, cwd=project)
        run(xmake, "run", "--file=xmake-parser-ci.lua", target, cwd=project, timeout=300)
    session_callbacks_retry()
    for source in (project / "src").rglob("*"):
        if source.is_file() and re.search(r'#include\s*["<][^">]*\.cpp[">]', source.read_text(encoding="utf-8")):
            raise RuntimeError(f"Production source includes a C++ translation unit: {source}")


def beta_tests():
    xmake, cmake = tool("xmake"), tool("cmake")
    project = ROOT / "CO-BETA/plugin"
    for test in ("F4TCommonLibCheckoutContract.py",
                 "F4TConflictingDllContract.py",
                 "F4TEquipmentModSyncContract.py",
                 "F4TExplosionSyncContract.py",
                 "F4TLoadOrderIntegrationContract.py",
                 "F4TInventoryContainerContract.py",
                 "F4TNetworkServerContracts.py",
                 "F4TNetworkServerContracts_part2.py",
                 "F4TNpcCombatContracts_part3.py",
                 "F4TNpcCombatContracts_part4.py",
                 "F4TNpcCombatContracts_part5.py",
                 "F4TNpcCombatContracts_part6.py",
                 "F4TNpcSyncContract.py",
                 "F4TParkedSuitContract.py",
                 "F4TPowerArmorPieceGateContract.py",
                 "F4TProbeDecoderContract.py",
                 "F4TProbeSitesContract.py",
                 "F4TProxyConsoleContracts.py",
                 "F4TProxyConsoleContracts_part2.py",
                 "F4TProxyPathPredicateContract.py",
                 "F4TPublicCiCredentialContract.py",
                 "F4TQuestStoryContracts.py",
                 "F4TQuestStoryContracts_part2.py",
                 "F4TQuestStoryContracts_part3.py",
                 "F4TQuestStoryContracts_part4.py",
                 "F4TQuestStoryContracts_part5.py",
                 "F4TQuestStoryContracts_part7.py",
                 "F4TSaveModpackContracts.py",
                 "F4TSaveModpackContracts_part2.py",
                 "F4TServerRelayContracts.py",
                 "F4TShotReplayContract.py",
                 "F4TThrowableSyncContract.py",
                 "F4TTradeContract.py",
                 "F4TUiToolingContracts.py",
                 "F4TWorkshopCommitHooksContract.py"):
        run(tool("python"), project / "tests" / test)
    for test in ("F4TJoinCancellationTests.py", "F4TConnectionLifecycleTests.py", "F4TNpcPendingHitTests.py", "F4TReceiveNotificationTests.py",
                 "F4TReceiveThreadOwnershipTests.py", "F4TSendCleanupRaceTests.py", "F4TReconnectWindowTests.py"):
        run(tool("python"), project / "tests" / test, "--cmake", cmake)
    with tempfile.TemporaryDirectory(prefix="co-object-sync-json-") as temporary:
        run(tool("python"), project / "tests" / "F4TObjectSyncAdmissionTests.py", "--cmake", cmake, "--json", fetch_json(Path(temporary)))
    run(tool("python"), ROOT / "tests/audit/test_survival_items_contract.py")
    combat_contract()
    run(xmake, "clean", "-a", cwd=project)
    run(xmake, "f", "-y", "-p", "windows", "-a", "x64", "-m", "releasedbg", "-o", "build-ci", cwd=project)
    run(xmake, "test", "-v", "-y", "-j4", cwd=project)
    run(xmake, "build", "-y", "-j4", "CommonwealthOnlineProbe", cwd=project)
    run(xmake, "build", "-y", "-j4", "CommonwealthOnlineNpcSyncCompileCheck", cwd=project)


COMBAT_TESTS = ("F4TActionSequenceTests", "F4TCombatActionBehaviorTests", "F4TPvPCombatBehaviorTests",
                "F4TReplicationRuntimeTests", "F4TReliableOutboxTests")
JSON_URL = "https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp"
JSON_SHA256 = "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63"


def fetch_json(directory):
    header = directory / "json/nlohmann/json.hpp"
    header.parent.mkdir(parents=True)
    for attempt in range(3):
        try:
            header.write_bytes(urllib.request.urlopen(JSON_URL, timeout=120).read())
            break
        except OSError:
            if attempt == 2:
                raise
            time.sleep(5)
    if hashlib.sha256(header.read_bytes()).hexdigest() != JSON_SHA256:
        raise RuntimeError("nlohmann json checksum mismatch")
    return directory / "json"


def combat_contract():
    compiler = tool("clang++")
    plugin = ROOT / "CO-BETA/plugin"
    with tempfile.TemporaryDirectory(prefix="co-combat-contract-") as temporary:
        json_include = fetch_json(Path(temporary))
        for name in COMBAT_TESTS:
            executable = Path(temporary) / f"{name}.exe"
            run(compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", f"-I{plugin / 'include'}", f"-I{ROOT / 'co-core/include'}",
                f"-I{json_include}", plugin / "tests" / f"{name}.cpp", "-o", executable)
            run(executable)


def beta_dll(target, filename):
    tool("llvm-readobj")
    project = ROOT / "CO-BETA/plugin"
    run(tool("xmake"), "f", "-y", "-p", "windows", "-a", "x64", "-m", "releasedbg", "-o", "build-ci", cwd=project)
    build_dll(project, target, filename, PLUGIN_EXPORTS)


def release(outdir):
    xmake = tool("xmake")
    tool("llvm-readobj")
    plugin = ROOT / "CO-BETA/plugin"
    run(xmake, "f", "-y", "-p", "windows", "-a", "x64", "-m", "releasedbg", "-o", "build-ci", cwd=plugin)
    dlls = [build_dll(plugin, target, filename, PLUGIN_EXPORTS) for target, filename in PLUGIN_DLLS]
    dlls.append(bridge())
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    for dll in dlls:
        shutil.copy2(dll, out / dll.name)


def main():
    dlls = dict(PLUGIN_DLLS)
    parser = argparse.ArgumentParser()
    parser.add_argument("component", choices=("ui", "beta-tests", "bridge", "release", *dlls))
    parser.add_argument("outdir", nargs="?")
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("This lane requires native Windows")
    if args.component in (*dlls, "ui"):
        os.environ["_CL_"] = COMPILE_ONLY_CL
    if args.component == "release":
        if not args.outdir:
            parser.error("release needs an output directory")
        release(args.outdir)
    elif args.component in dlls:
        beta_dll(args.component, dlls[args.component])
    else:
        {"ui": ui, "beta-tests": beta_tests, "bridge": bridge}[args.component]()


if __name__ == "__main__":
    main()
