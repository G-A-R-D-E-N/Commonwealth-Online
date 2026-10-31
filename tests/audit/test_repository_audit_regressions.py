from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def read_proxy_controller() -> str:
    parts = sorted((ROOT / "CO-BETA/plugin/src").glob("F4TProxyActorController*.cpp")) + [ROOT / "CO-BETA/plugin/include/F4TProxyActorControllerInternal.h"]
    return "\n".join(part.read_text(encoding="utf-8") for part in parts)


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []

    for shadow in (".forgejo", ".gitea"):
        require(not (ROOT / shadow / "workflows").exists(), f"CI-DIR: {shadow}/workflows shadows .github/workflows (Forgejo reads only the first existing workflow directory)", failures)

    manifest = read("CO-BETA/tools/stage-client-manifest.ps1")
    require('"CommonwealthOnline-NG.dll"' in manifest, "AUD-04: NG DLL must use the canonical client-binary exclusion policy", failures)

    stage_mod = read("CO-BETA/stage-mod.bat")
    require("ThirdParty\\CommonwealthOnlineUI" not in stage_mod, "AUD-02: release staging must not source the detached ThirdParty UI mirror", failures)
    require("..\\CommonwealthOnlineUI" in stage_mod, "AUD-02: release staging must source the reviewed top-level UI package", failures)
    require(":AssembleRuntimePackages" in stage_mod, "AUD-PKG: stage-mod.bat must assemble isolated runtime packages", failures)
    require('"%BUILD_DIR%\\AE\\Fallout 4\\Data\\"' in stage_mod, "AUD-PKG: stage-mod.bat must assemble AE package", failures)
    require('"%BUILD_DIR%\\NG\\Fallout 4\\Data\\"' in stage_mod, "AUD-PKG: stage-mod.bat must assemble NG package", failures)
    require('copy /Y "%CO_NG_DLL%" "%PLUGINS_DIR%\\CommonwealthOnline-NG.dll"' not in stage_mod, "AUD-PKG: default staged Data must not copy sibling NG DLL", failures)
    require('copy /Y "%CO_OG_DLL%" "%PLUGINS_DIR%\\CommonwealthOnline-OG.dll"' not in stage_mod, "AUD-PKG: default staged Data must not copy sibling OG DLL", failures)

    deploy_ui = read("CO-BETA/deploy-ui.bat")
    require("ThirdParty\\CommonwealthOnlineUI" not in deploy_ui, "AUD-02: UI deployment must not source the detached ThirdParty mirror", failures)
    require("..\\CommonwealthOnlineUI\\package" in deploy_ui, "AUD-02: UI deployment must source the reviewed top-level UI package", failures)

    for xmake_path in ("co-core/xmake.lua", "CO-BETA/plugin/xmake.lua", "CO-SERVER/server-cpp/xmake.lua"):
        xmake = read(xmake_path)
        if 'set_policy("build.intermediate_directory", false)' in xmake:
            require(
                'set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)")' in xmake,
                f"BUILD-PATH: {xmake_path} disables build.intermediate_directory (needed so the "
                "shared-source test targets keep unique objectdirs), but that policy also relocates "
                "target output off xmake's default build/<plat>/<arch>/<mode> layout. The project must "
                "pin set_targetdir back to $(builddir)/$(plat)/$(arch)/$(mode) or workflow/staging "
                "paths such as build/linux/x86_64/debug/CommonwealthOnlineServerCpp stop resolving",
                failures,
            )

    browser = read("CommonwealthOnlineUI/src/F4TServerBrowserBridge.cpp")
    require("ProbeTcpConnectMs" not in browser, "AUD-06: browser refresh must not probe TCP on the UDP/GNS gameplay port", failures)
    snapshot_index = browser.find("const auto currentServerSnapshot = SnapshotCurrentServer();")
    thread_index = browser.find("std::jthread([")
    require(snapshot_index >= 0 and thread_index >= 0 and snapshot_index < thread_index, "AUD-05: current server state must be snapshotted before owned refresh work starts", failures)
    require("currentServerSnapshot" in browser[thread_index:] if thread_index >= 0 else False, "AUD-05: owned refresh must consume the immutable current-server snapshot", failures)

    browser_data = read("CommonwealthOnlineUI/src/F4TServerBrowserData.cpp")
    require("(a_entry.ping <= 0 || a_entry.ping >= 50)" in browser_data, "AUD-06: 50 ms filter must reject non-positive and out-of-bounds pings", failures)
    require("(a_entry.ping <= 0 || a_entry.ping >= 100)" in browser_data, "AUD-06: 100 ms filter must reject non-positive and out-of-bounds pings", failures)
    require("(a_entry.ping <= 0 || a_entry.ping >= 150)" in browser_data, "AUD-06: 150 ms filter must reject non-positive and out-of-bounds pings", failures)

    def passes_ping_filter(filter_name: str, ping: int) -> bool:
        if filter_name == "< 50 ms" and (ping <= 0 or ping >= 50):
            return False
        if filter_name == "< 100 ms" and (ping <= 0 or ping >= 100):
            return False
        if filter_name == "< 150 ms" and (ping <= 0 or ping >= 150):
            return False
        return True

    for tier, limit in (("< 50 ms", 50), ("< 100 ms", 100), ("< 150 ms", 150)):
        require(not passes_ping_filter(tier, -10), f"AUD-06: negative ping must fail {tier}", failures)
        require(not passes_ping_filter(tier, 0), f"AUD-06: unprobed zero ping must fail {tier}", failures)
        require(passes_ping_filter(tier, 1), f"AUD-06: valid positive ping must pass {tier}", failures)
        require(passes_ping_filter(tier, limit - 1), f"AUD-06: valid ping below limit must pass {tier}", failures)
        require(not passes_ping_filter(tier, limit), f"AUD-06: ping at threshold must fail {tier}", failures)
        require(not passes_ping_filter(tier, limit + 10), f"AUD-06: ping above threshold must fail {tier}", failures)

    session_players = int(re.search(r"MaximumSessionPlayers = (\d+);", read("co-core/include/F4TRemotePlayerCapacity.h")).group(1))
    config = json.loads(read("CO-SERVER/server-cpp/commonwealth-server-native.json"))
    require(int(config.get("max_players", 0)) == session_players, "AUD-07: shipped default capacity must equal MaximumSessionPlayers", failures)
    for key in ("enable_persistence", "enable_chat", "enable_factions"):
        require(config.get(key) is True, f"AUD-07: the shipped default config must ship the implemented {key} feature on", failures)
    require(f"kMaxRuntimeProxyActors = {session_players - 1};" in read_proxy_controller(), "AUD-07: client proxy cap must be MaximumSessionPlayers - 1 remote proxies", failures)
    host_config = read("CO-SERVER/host/Services/ServerConfig.cs")
    host_view_model = read("CO-SERVER/host/ViewModels/MainWindowViewModel.cs")
    for needle, text in ((f"MaxPlayers {{ get; set; }} = {session_players};", host_config), (f"?? {session_players},", host_config), (f"MaxPlayers is < 1 or > {session_players}", host_config), (f"_maxPlayers = {session_players};", host_view_model)):
        require(needle in text, f"AUD-07: host capacity must match MaximumSessionPlayers ({needle})", failures)

    xmake_core = read("co-core/xmake.lua") + read("co-core/xmake-tests.lua")
    xmake_plugin = read("CO-BETA/plugin/xmake.lua") + read("CO-BETA/plugin/xmake-quest-tests.lua")
    xmake_server = read("CO-SERVER/server-cpp/xmake.lua")
    for name, content in (("co-core", xmake_core), ("CO-BETA/plugin", xmake_plugin), ("CO-SERVER/server-cpp", xmake_server)):
        target_blocks = re.findall(r'target\("([^"]+)"\)(.*?)(?=\ntarget\(|\Z)', content, re.DOTALL)
        test_targets = [tname for tname, block in target_blocks if "add_tests(" in block]
        require(len(test_targets) > 0, f"AUD-03: {name} must have at least one test target", failures)
        for tname, block in target_blocks:
            if "add_tests(" in block:
                require('add_forceincludes("F4TAssertionsEnabled.h")' in block,
                        f"AUD-03: {name} target '{tname}' must include add_forceincludes(\"F4TAssertionsEnabled.h\")", failures)

    profile_store = read("CO-BETA/plugin/src/F4TServerProfileStore.cpp")
    rejection = read("CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp")
    require("ClearResumeCredential" in profile_store, "AUD-01: profile store needs explicit per-server resume credential removal", failures)
    require("ClearRejectedResumeCredentialAndRetry" in rejection, "AUD-01: invalid-resume recovery must be an explicit user action before retry", failures)
    require("ShowRetryFailed" in rejection and ("!a_serverKey" in rejection or "!a_serverKey[0]" in rejection), "AUD-01: invalid-resume recovery must fail closed when credential clearing fails", failures)

    admin = read("CO-SERVER/server-cpp/src/AdminControlServer.cpp")
    admin_header = read("CO-SERVER/server-cpp/include/AdminControlServer.h")
    require("SO_SNDTIMEO" in admin, "AUD-08: admin clients need a bounded send timeout", failures)
    require("ShutdownSocket" in admin, "AUD-08: Stop must cancel accepted client sockets", failures)
    require("_clientSockets" in admin_header, "AUD-08: accepted admin sockets must be tracked for cancellation", failures)

    durable = read("co-core/src/persistence/F4TDurableState.cpp")
    durable_header = read("co-core/include/F4TDurableState.h")
    require("Unchanged" in durable and "Unchanged" in durable_header, "AUD-09: unchanged durable updates must be distinguishable so disk writes can be suppressed", failures)

    protocol = read(".github/workflows/co-client-protocol-acceptance.yml")
    runs_on_every_push = "paths:" not in protocol
    require(runs_on_every_push or ('"CO-BETA/plugin/src/**"' in protocol and '"CO-BETA/plugin/include/**"' in protocol), "validation: protocol acceptance must trigger on real plugin source/header changes", failures)
    require('for target in current ng og' in protocol and '"$client" "$CO_GNS_CLIENT_BRIDGE" 127.0.0.1 "$server_port" "$target"' in protocol, "validation: protocol acceptance must exercise NG explicitly", failures)

    for tname in ("CommonwealthOnline", "CommonwealthOnlineNG", "CommonwealthOnlineOG"):
        target_block = re.search(rf'target\("{tname}"\)(.*?)(?=\ntarget\(|\Z)', xmake_plugin, re.DOTALL)
        require(target_block is not None, f"DEV-SCOPE: target {tname} must exist in CO-BETA/plugin/xmake.lua", failures)
        if target_block:
            require("co_test_features" not in target_block.group(1) and "F4T_ENABLE_NPC_SYNC" not in target_block.group(1),
                    f"DEV-SCOPE: {tname} must build NPC sync unconditionally", failures)

    for path in ("CO-BETA/plugin/include/F4TNpcSync.h", "CO-BETA/plugin/src/F4TNpcSync.cpp",
                 "CO-BETA/plugin/src/F4TNpcSyncGns.cpp", "CO-BETA/plugin/src/F4TReplicationRuntime.cpp"):
        require("F4T_ENABLE_NPC_SYNC" not in read(path), f"DEV-SCOPE: {path} must not gate NPC sync on F4T_ENABLE_NPC_SYNC", failures)

    remote_state = read("CO-BETA/plugin/src/F4TRemotePlayerState.cpp")
    require("g_incomingCombatHits.PurgeSender(a_playerId)" in remote_state, "PVP-DAMAGE: RemoveRemotePlayer must reset shared hit state", failures)
    require("g_incomingCombatHits.QueueHit(a_senderId, a_sequence, a_damage)" in remote_state, "PVP-DAMAGE: QueueIncomingCombatHit must use shared tracker", failures)

    capture_cpp = read("CO-BETA/plugin/src/F4TRangedCombatCapture.cpp")
    require("F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId" in capture_cpp, "PVP-DAMAGE: capture must target remote proxy mappings", failures)

    networking_cpp = read("CO-BETA/plugin/src/F4TNetworking.cpp")
    inbound_cpp = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
    require("F4T::PvPCombatContract::ValidateHitRouting(" in inbound_cpp, "PVP-DAMAGE: networking must use shared routing validation", failures)
    require("F4T::UIClient::NotifyTransportDisconnected();" in networking_cpp, "SESSION-UI: unexpected transport loss must notify the UI client", failures)
    require("if (g_receiveThreadRunning.load())" in networking_cpp, "SESSION-UI: intentional disconnect must not be reported as transport loss", failures)

    receive_start = networking_cpp.index("void ProcessReceiveBuffer(")
    receive_end = networking_cpp.index("\n\tvoid ReceiveLoop(", receive_start)
    process_receive = networking_cpp[receive_start:receive_end]
    require(process_receive.count("F4T::UIClient::NotifyTransportDisconnected(") >= 2,
            "SESSION-UI: every receive-boundary terminal close (frame-limit, inbound-budget, and "
            "oversized unterminated pending frame) must reconcile transport loss, otherwise the "
            "receive loop breaks silently on the invalidated socket and a pending onboarding join "
            "outlives the connection", failures)

    ui_client = read("CO-BETA/plugin/src/F4TUIClient.cpp")
    require("SessionLaunch::CancelPendingJoin(reasonCopy)" in ui_client, "SESSION-UI: transport loss must cancel pending onboarding", failures)
    require("g_joinGeneration.fetch_add(1)" in ui_client, "SESSION-UI: transport loss must invalidate stale join completions", failures)
    require("PushConnectionStatus();" in ui_client, "SESSION-UI: transport loss must publish disconnected UI state", failures)
    require("if (g_joinGeneration.load() != joinGeneration)" in ui_client, "SESSION-UI: launch completion must reject a disconnect that races the load decision", failures)

    persistence_smoke = read("CO-SERVER/server-cpp/tests/LivePersistenceGnsSmoke.cpp")
    seed = persistence_smoke.split("int Seed(", 1)[1].split("\n\tint Resume(", 1)[0]
    require("WaitForSeedCommit(" in seed and "milliseconds(300)" not in seed,
            "PERSISTENCE-SEED: seed must observe committed player state instead of sleeping before restart", failures)
    require("kPrimarySlotProfile" in seed, "PERSISTENCE-SEED: legacy-client seed must wait on the slot profile that holds player state", failures)
    linux_smoke = read("CO-SERVER/server-cpp/scripts/smoke-native-linux-release.sh")
    windows_smoke = read("CO-SERVER/server-cpp/scripts/smoke-native-windows-package.ps1")
    require('"$resume_token" "$scratch/durable-state.json"' in linux_smoke
            and '"$persistencePort", $resumeToken, $durableState' in windows_smoke,
            "PERSISTENCE-SEED: both package smokes must supply the committed state path", failures)


    def else_without_if(text: str) -> list[int]:
        stack: list[list] = []
        depth = 0
        orphaned: list[int] = []
        for number, line in enumerate(text.split("\n"), 1):
            code = re.sub(r"//.*$", "", line)
            if code.lstrip().startswith("#"):
                continue
            code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
            rest = code.lstrip()
            lead = 0
            while rest.startswith("}"):
                lead += 1
                rest = rest[1:].lstrip()
            effective = depth - lead
            for token in re.finditer(r"\b(if|else|for|while|switch|do)\b", rest):
                if token.group(1) != "else":
                    stack.append([token.group(1), effective])
                    continue
                while stack and stack[-1][1] > effective:
                    stack.pop()
                if stack and stack[-1][0] in ("if", "else"):
                    stack[-1][0] = "else"
                else:
                    orphaned.append(number)
            depth += code.count("{") - code.count("}")
        return orphaned

    for source in sorted((ROOT / "CO-BETA/plugin/src").glob("*.cpp")) + sorted((ROOT / "CO-BETA/plugin/include").glob("*.h")):
        orphaned = else_without_if(source.read_text(encoding="utf-8"))
        require(not orphaned, f"STRUCTURE: {source.name} has an else with no matching if at line(s) {orphaned}", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Repository audit regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
