import argparse
import hashlib
import importlib.util
import json
import platform
import subprocess
import uuid
from pathlib import Path


spec = importlib.util.spec_from_file_location("restart", Path(__file__).with_name("run-server-persistence-benchmark.py"))
restart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(restart)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workload", choices=("durable", "movement", "reconnect", "world"), default="durable")
    parser.add_argument("--samples", type=int, default=400)
    parser.add_argument("--players", type=int, nargs="+", default=[1, 2, 4, 8, 16])
    parser.add_argument("--persistence", choices=("on", "off"), default="on")
    args = parser.parse_args()
    restart.check((4 if args.workload in ("reconnect", "world") else (2 if args.workload == "movement" else 1)) <= args.samples <= 600 and args.players and
                  all(1 <= count <= 16 for count in args.players) and len(set(args.players)) == len(args.players),
                  "invalid workload size")
    restart.check(args.persistence == "on" or args.workload in ("durable", "movement"),
                  "persistence off supports the durable and movement workloads only")
    persistence = args.persistence == "on"
    root = Path(__file__).resolve().parents[1]
    server, client = args.server.resolve(strict=True), args.client.resolve(strict=True)
    binaries = [server, client]
    if platform.system() == "Windows":
        bridges = [path.parent / "commonwealth_online_gns_bridge.dll" for path in binaries]
        restart.check(bridges[0].read_bytes() == bridges[1].read_bytes(), "server/client bridge mismatch")
        binaries += list(dict.fromkeys(bridges))
    output = args.output.resolve() / ("load-" + uuid.uuid4().hex)
    if output.is_relative_to(root):
        restart.check(subprocess.run(["git", "-C", str(root), "check-ignore", "--quiet", str(output)]).returncode == 0,
                      "in-repository output must be ignored because it contains credentials")
    output.mkdir(parents=True, exist_ok=False)
    sources = [Path(__file__), Path(restart.__file__), root / "CO-SERVER/server-cpp/tests/LivePersistenceGnsSmoke.cpp",
               root / "CO-SERVER/server-cpp/src/ServerRuntime.inc", root / "co-core/include/F4TConnectionPolicy.h"]
    world = args.workload == "world"
    reconnect = args.workload in ("reconnect", "world")
    movement = args.workload != "durable"
    mode, interval = (args.workload, 100) if movement else ("load", 50)
    sources += [root / "co-core/src/protocol/F4TGnsProtocolAdapter.cpp", root / "co-core/include/F4TGnsSnapshotEnvelope.h",
                root / "co-core/include/F4TGnsProtocolAdapter.h", root / "CO-SERVER/server-cpp/xmake.lua",
                root / "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc",
                root / "CO-SERVER/server-cpp/src/ServerProtocolImpl.h", root / "CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp",
                root / "CO-SERVER/server-cpp/src/ServerProtocolPublish.cpp", root / "CO-SERVER/server-cpp/src/ServerProtocolCompatRoutes.cpp",
                root / "CO-SERVER/server-cpp/src/ServerProtocolReconcile.cpp",
                root / "CO-SERVER/server-cpp/src/AdminCommandDispatcher.cpp", root / "CO-SERVER/server-cpp/src/DurableStateFileStore.cpp",
                root / "CO-SERVER/server-cpp/src/PersistenceGate.cpp"]
    if reconnect:
        scope = ("Three same-server authenticated primary reconnect cycles; other moving profiles continue nominal 100 ms "
                 "movement with actual per-cycle send count/min/max gap verification (95 ms minimum, under 500 ms maximum, "
                 "at least 90 sends per remaining moving profile per policy-paced cycle). "
                 "1 moving-profile case uses a temporary observer with one initial transform: actual peak 2 connections/profiles. "
                 "Server-restored durable name/equipment is received under each new player ID before a deliberately changed "
                 "replacement name is sent. Resume credentials compare only in memory. Synthetic equipment engine identity "
                 "is concept-level/unverified. Initial durable state per actual profile plus three primary replacements. "
                 "No Fallout, multi-machine or capacity acceptance. ")
    elif movement:
        scope = ("Synthetic localhost normal movement every 100 ms, 4 units per sample, same cell/world. "
                 "Production adapter sends one initial reliable durable state and suppresses unchanged fields. "
                 "Bounded decoded receipts verify every directed moving-peer pair saw durable state and a latter-half transform. "
                 "No game, multi-machine, world-transition, reconnect or capacity acceptance. ")
    else:
        scope = ("Synthetic localhost durable-name changes every 50 ms per connected profile. "
                 "No transform/world/reconnect load, Fallout or capacity acceptance. ")
    if world:
        scope += ("Three authenticated world_time writes (1201/1202/1203), one after each primary disconnect during "
                  "policy-paced reconnect movement. Every actual native connection receives each current time, including "
                  "primary replay before replacement state; final 1203 survives a real graceful server restart and authenticated "
                  "native primary resume without client state. No weather or engine execution acceptance. ")
    if not persistence:
        scope += ("Persistence disabled (enable_persistence=false): no durable-state file exists, so file-based final state "
                  "checks are skipped; delivery is verified by server status counts and client receipts only. ")
    scope += "Unreliable delivery is not guaranteed. Elapsed phase timings include handshakes, admission pacing, updates and cleanup."
    report = {"source_head": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
              "working_tree_status": subprocess.check_output(["git", "-C", str(root), "status", "--short"], text=True),
              "platform": platform.platform(),
              "source_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
              "binary_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in binaries},
              "workload": args.workload, "persistence": args.persistence,
              "scope": scope, "cases": []}
    for players in args.players:
        profiles = max(2, players) if reconnect else players
        directory = output / str(players)
        directory.mkdir()
        result = restart.phase(server, client, directory, mode, "graceful",
                               (players, args.samples), args.samples * interval / 1000 + players * 10 + 20 + (40 if reconnect else 0),
                               persistence=persistence)
        state = json.loads((directory / "durable-state.json").read_text()) if persistence else None
        if persistence:
            restart.check(sum(key.endswith("#0") for key in state["profiles"]) == profiles and all(
                state["profiles"][f"{'movement' if movement else 'persistence'}-load-{index}#0"]["playerState"]["characterName"] ==
                (("Reconnected 0 cycle 3" if reconnect and index == 0 else f"Movement {index}") if movement else f"Load {index} sample {args.samples - 1}") for index in range(profiles)), "final durable load states mismatch")
        status = result["server_status"]
        restart.check(status["packets_rejected"] == 0 and (movement or status["packets_received"] == players * (args.samples + 2)),
                      "load packets rejected or missing")
        if movement:
            restart.check(status["packets_received"] > players * 3, "no movement transform reached server")
            restart.check(status["connected_clients"] == 0 and status["pending_connections"] == 0, "movement clients not cleaned up")
            prefix = "CO_RECONNECT_LOAD " if reconnect else "CO_MOVEMENT_LOAD "
            records = [line[len(prefix):] for line in (directory / f"{mode}-client.log").read_text().splitlines() if line.startswith(prefix)]
            restart.check(len(records) == 1, "movement client report missing")
            result["movement"] = json.loads(records[0])
            restart.check((result["movement"]["sent_transforms"] >= players * args.samples + int(players == 1) if reconnect else
                           result["movement"]["sent_transforms"] == players * args.samples) and
                          result["movement"]["sent_durable_states"] == (max(2, players) + 3 if reconnect else players) and
                          result["movement"]["verified_peer_pairs"] == players * (players - 1) and
                          result["movement"]["corrections"] == 0 and (not persistence or all(
                          state["profiles"][f"movement-load-{index}#0"]["playerState"]["equippedItems"] ==
                          [{"slot": "right", "formId": "00001234"}]
                          for index in range(profiles))), "concept-level equipment fixture mismatch in movement workload")
        if reconnect:
            restart.check(len(result["movement"]["cycles"]) == 3 and all(
                              cycle["rotated"] and cycle["restored_before_replacement"] and
                              (players == 1 or (cycle["movement_sends"] >= (players - 1) * 90 and
                               cycle["min_background_gap_ms"] >= 95 and cycle["max_background_gap_ms"] < 500)) for cycle in result["movement"]["cycles"]) and
                          result["movement"]["peak_connections"] == max(2, players) and
                          (players == 1 or result["movement"]["movement_during_reconnect"] > 0),
                          "authenticated reconnect restoration or concurrent movement missing")
        if world:
            restart.check(state["worldState"]["timeHHmm"] == "1203" and
                          result["movement"]["final_world_time"] == "1203" and
                          result["movement"]["world_receipts"] >= profiles * 3 and all(
                          cycle["world_replayed_before_replacement"] and cycle["world_time"] == f"{1200 + cycle['cycle']:04d}"
                          for cycle in result["movement"]["cycles"]), "world broadcast, replay or persistence missing")
            restarted = restart.phase(server, client, directory, "world-resume", "graceful")
            restart.check('CO_WORLD_RESTART ' in (directory / "world-resume-client.log").read_text(), "native restart world replay missing")
            after_restart = json.loads((directory / "durable-state.json").read_text())
            restart.check(after_restart["worldState"]["timeHHmm"] == "1203" and
                          after_restart["serverInstanceId"] == state["serverInstanceId"] and
                          set(after_restart["profiles"]) == set(state["profiles"]) and all(
                          after_restart["profiles"][profile]["playerState"] == before["playerState"]
                          for profile, before in state["profiles"].items() if profile.endswith("#0")) and
                          restarted["server_pid"] != result["server_pid"] and
                          restarted["server_status"]["packets_rejected"] == 0 and
                          restarted["server_status"]["connected_clients"] == 0 and
                          restarted["server_status"]["pending_connections"] == 0, "world restart identity or cleanup mismatch")
            result["server_instance_id"] = state["serverInstanceId"]
            result["world_restart"] = restarted
        result.update({"players": players, "moving_profiles": players if movement else 0, "actual_profiles": profiles, "samples": args.samples, "interval_ms": interval,
                       "final_states_verified": persistence})
        report["cases"].append(result)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{players} profiles: PASS", flush=True)
    print(output / "report.json")


if __name__ == "__main__":
    main()
