import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


# They read docs/, so the export drops them and private-only-checks.yml runs them.
PRIVATE_CONTRACTS = {f"F4T{name}Contract.py" for name in (
    "Chat", "Downed", "Factions", "FrameLimiter", "NpcCoopCombat", "ProxySaveSafety",
    "SavePluginPreflight", "SessionSaveGuard", "TextEntryMode", "LoadOrder",
    "GraphOverride", "ProxyLiveFire", "ProxyNoTalk", "ShotReplay")}


def tool(name):
    found = shutil.which(name)
    if not found:
        candidate = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "LLVM/bin" / (name + ".exe")
        if candidate.is_file():
            found = str(candidate)
    if not found:
        raise RuntimeError(f"Required tool is missing: {name}")
    return found


def run(*args, cwd=ROOT, capture=False):
    print("+", subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    return subprocess.run(
        [str(arg) for arg in args], cwd=cwd, check=True,
        text=True, encoding="utf-8", capture_output=capture,
    ).stdout


def audit_dll(path):
    output = run(tool("llvm-readobj"), "--file-headers", "--coff-exports", path, capture=True)
    if "Format: COFF-x86-64" not in output:
        raise RuntimeError(f"Expected a 64-bit PE: {path}")
    names = {line.strip().removeprefix("Name: ") for line in output.splitlines() if line.strip().startswith("Name: ")}
    for symbol in ("F4SEPlugin_Load", "F4SEPlugin_Version"):
        if symbol not in names:
            raise RuntimeError(f"Missing {symbol} export: {path}")
    print(f"PASS: {path.name}: x64 PE and F4SE exports", flush=True)


def ui():
    xmake, node = tool("xmake"), tool("node")
    tool("llvm-readobj")
    project = ROOT / "CommonwealthOnlineUI"
    views = project / "ui/views/CommonwealthOnline"
    for source in ("browser/browser.js", "browser/components/direct-connect.js", "browser/components/select-menus.js",
                   "browser/components/browser-markup.js", "browser/components/server-records.js",
                   "browser/components/recent-servers.js", "browser/components/filter-state.js",
                   "browser/components/server-counts.js", "browser/components/join-flow.js",
                   "browser/components/controller-navigation.js", "browser/components/password-prompt.js",
                   "browser/components/steam-lan.js",
                   "browser/components/steam-friends.js",
                   "browser/components/server-settings.js",
                   "browser/components/directory-refresh.js",
                   "alerts/alerts.js", "faction/faction.members.js", "faction/faction.browse.js", "faction/faction.js", "faction/faction.harness.js",
                   "chat/chat.js", "chat-feed/feed.js",
                   "main-menu/script.js", "launcher/script.js",
                   "pause-menu/session-mode-badge.js", "downed/downed.js", "downed/respawn.js",
                   "settings/settings-data.js", "settings/settings-model.js", "settings/settings-native.js",
                   "settings/settings-render.js", "settings/settings-input.js", "settings/settings-voice.js",
                   "settings/settings.js", "pause-menu/script.js", "voice-hud/voice-hud.js", "capture-hud/capture-hud.js",
                   "faction/faction.ownership.js", "faction/voice-players.js", "faction/faction.friends.js",
                   "interact-wheel/wheel.js"):
        run(node, "--check", views / source)
    for test in ("main-menu/main-menu.test.js", "launcher/launcher.test.js", "settings/settings.test.js",
                 "alerts/alerts.test.js", "faction/faction.test.js", "faction/faction.rank.test.js", "faction/faction.browse.test.js", "chat/chat.test.js", "chat-feed/feed.test.js",
                 "downed/downed.test.js", "stabilization-contract.test.js", "settings/settings-voice.test.js",
                 "voice-hud/voice-hud.test.js", "capture-hud/capture-hud.test.js", "faction/faction.ownership.test.js",
                 "faction/voice-players.test.js", "faction/faction.friends.test.js",
                 "interact-wheel/wheel.test.js"):
        path = views / test
        run(node, path.name, cwd=path.parent)
    run(node, "--test", "controller.test.js", "controller-port.test.js", "directory-refresh.test.js",
        "server-settings.test.js", cwd=views / "browser/tests")
    run(tool("python"), project / "ci/ProductionCompileCoverageTests.py")
    run(tool("python"), project / "ci/ExitSaveOrderingTests.py")
    run(tool("python"), project / "ci/VoiceBridgeContractTests.py")
    run(tool("python"), project / "ci/CaptureHudContractTests.py")
    run(xmake, "f", "--file=xmake-parser-ci.lua", "-y", "-p", "windows", "-a", "x64",
        "-m", "debug", "-o", "build-parser-ci", cwd=project)
    for target in ("MultiplayerProfileTests", "ServerBrowserBoundsTests", "LauncherEventProvenanceTests",
                   "SettingsRefreshRouteTests", "ExitSaveGateTests", "WorldModeTests", "DownedStateTests",
                   "SteamActionTests", "VoiceBridgeTests"):
        run(xmake, "build", "--file=xmake-parser-ci.lua", "-y", "-j4", target, cwd=project)
        run(xmake, "run", "--file=xmake-parser-ci.lua", target, cwd=project)
    for source in (project / "src").rglob("*"):
        if source.is_file() and re.search(r'#include\s*["<][^">]*\.cpp[">]', source.read_text(encoding="utf-8")):
            raise RuntimeError(f"Production source includes a C++ translation unit: {source}")
    run(xmake, "f", "-y", "-p", "windows", "-a", "x64", "-m", "releasedbg", "-o", "build-ci", cwd=project)
    run(xmake, "build", "-y", "-j4", "CommonwealthOnlineUI", cwd=project)
    audit_dll(project / "build-ci/windows/x64/releasedbg/CommonwealthOnlineUI.dll")


def beta():
    xmake, cmake = tool("xmake"), tool("cmake")
    tool("llvm-readobj")
    project = ROOT / "CO-BETA/plugin"
    for test in ("F4TCommonLibCheckoutContract.py", "F4TIdentityOrderingContract.py",
                 "F4TServerProfileStoreSourceContract.py", "F4TServerPasswordStoreContract.py",
                 "F4TGnsFailureSourceContract.py",
                 "F4THandshakeRejectionActionContract.py",
                 "F4TWorkshopCommitHooksContract.py", "F4TProxyPathPredicateContract.py",
                 "F4TReplicationRuntimeContract.py", "F4TEquipmentModSyncContract.py",
                 "F4TNpcSyncContract.py", "F4TNpcCoopHealthContract.py",
                 "F4TNpcCoopCombatContract.py",
                 "F4TFactionsContract.py", "F4TFactionGapContract.py", "F4TChatContract.py",
                 "F4TFriendsWheelContract.py",
                 "F4TSaveLineageTimingContract.py",
                 "F4TConsoleGuardWelcomeIntegrationContract.py",
                 "F4TQuestWiringContract.py",
                 "F4TQuestUIContract.py",
                 "F4TQuestStoryOrderContract.py",
                 "F4TConsoleGuardSecondDoorContract.py",
                 "F4TSaveLineageEnforcementContract.py", "F4TThrowableSyncContract.py",
                 "F4TGnsVoiceContract.py", "F4TGraphOverrideContract.py",
                 "F4TConsoleGuardTrampolineWarnContract.py", "F4TProxyNoTalkContract.py", "F4TProxyLiveFireContract.py", "F4TShotReplayContract.py", "F4TVatsPolicyContract.py", "F4TProbeDecoderContract.py", "F4TProbeSitesContract.py", "F4TProxyWeaponDrawContract.py", "F4TJoinFlyOverContract.py"):
        if test not in PRIVATE_CONTRACTS:
            run(tool("python"), project / "tests" / test)
    for test in ("F4TJoinCancellationTests.py", "F4TConnectionLifecycleTests.py", "F4TReceiveNotificationTests.py"):
        run(tool("python"), project / "tests" / test, "--cmake", cmake)
    run(xmake, "clean", "-a", cwd=project)
    run(xmake, "f", "-y", "-p", "windows", "-a", "x64", "-m", "releasedbg", "-o", "build-ci", cwd=project)
    run(xmake, "test", "-v", "-y", "-j4", cwd=project)
    for target, filename in (("CommonwealthOnline", "CommonwealthOnline.dll"),
                             ("CommonwealthOnlineNG", "CommonwealthOnline-NG.dll"),
                             ("CommonwealthOnlineOG", "CommonwealthOnline-OG.dll")):
        run(xmake, "build", "-y", "-j4", target, cwd=project)
        audit_dll(project / "build-ci/windows/x64/releasedbg" / filename)
    run(xmake, "build", "-y", "-j4", "CommonwealthOnlineNpcSyncCompileCheck", cwd=project)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("component", choices=("ui", "beta"))
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("This lane requires native Windows")
    {"ui": ui, "beta": beta}[args.component]()


if __name__ == "__main__":
    main()
