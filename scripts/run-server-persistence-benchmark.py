import argparse
import hashlib
import json
import platform
import socket
import subprocess
import time
import uuid
from pathlib import Path


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as connection:
        connection.bind(("127.0.0.1", 0))
        return connection.getsockname()[1]


def admin(port, directory, command):
    token = (directory / ".admin-token").read_text().strip()
    with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
        deadline = time.perf_counter() + 3
        connection.sendall((json.dumps({**({"cmd": command} if isinstance(command, str) else command), "adminToken": token}) + "\n").encode())
        response = b""
        while b"\n" not in response:
            remaining = deadline - time.perf_counter()
            check(remaining > 0, "admin response timed out")
            connection.settimeout(remaining)
            chunk = connection.recv(4096)
            check(chunk and len(response) + len(chunk) <= 65536, "invalid admin response")
            response += chunk
        return json.loads(response)


def world_requests(text):
    return [line.removeprefix("CO_WORLD_COMMAND ") for line in text.split("\n")[:-1] if line.startswith("CO_WORLD_COMMAND ")]


def wait_for_seed_state(directory):
    deadline = time.perf_counter() + 5
    while True:
        try:
            flushed = "Persistence Smoke Survivor" in (directory / "durable-state.json").read_text(encoding="utf-8")
        except OSError:
            flushed = False
        if flushed:
            return
        check(time.perf_counter() < deadline, "seeded durable state was not flushed before force termination")
        time.sleep(.05)


def phase(server, client, directory, mode, stop_mode, client_arguments=(), client_timeout=30, persistence=True):
    game_port, admin_port = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
    config = directory / "config.json"
    config.write_text(json.dumps({"host": "127.0.0.1", "port": game_port, "admin_port": admin_port,
                                 "max_players": 16, "require_v2_identity": True, "enable_persistence": persistence,
                                 "durable_state_path": "durable-state.json"}), encoding="utf-8")
    result = {"mode": mode, "stop_mode": stop_mode}
    start = time.perf_counter()
    with (directory / f"{mode}-server.log").open("wb") as log:
        process = subprocess.Popen([str(server), "--config", str(config), "--no-lan-discovery"],
                                   cwd=server.parent, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        result["server_pid"] = process.pid
        try:
            deadline = start + 10
            while True:
                check(process.poll() is None, "server exited during startup")
                try:
                    if (directory / ".admin-token").is_file() and admin(admin_port, directory, "ping").get("ok"):
                        break
                except OSError:
                    pass
                check(time.perf_counter() < deadline, "server startup timed out")
                time.sleep(.025)
            result["startup_ms"] = (time.perf_counter() - start) * 1000
            start = time.perf_counter()
            arguments = [str(client), mode, "127.0.0.1", str(game_port), str(directory / "resume-token"),
                         *map(str, client_arguments)]
            if mode == "world":
                commands = []
                client_log = directory / f"{mode}-client.log"
                with client_log.open("wb") as client_output:
                    child = subprocess.Popen(arguments, cwd=client.parent, stdout=client_output, stderr=subprocess.STDOUT,
                                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    try:
                        while True:
                            requests = world_requests(client_log.read_text(encoding="utf-8"))
                            for hhmm in requests[len(commands):]:
                                check(hhmm == f"{1200 + len(commands) + 1:04d}" and len(commands) < 3, "unexpected world command request")
                                command_start = time.perf_counter()
                                response = admin(admin_port, directory, {"cmd": "world_time", "hhmm": hhmm})
                                check(response.get("ok"), "world_time rejected")
                                commands.append({"hhmm": hhmm, "ok": True, "elapsed_ms": (time.perf_counter() - command_start) * 1000,
                                                 "offset_ms": (command_start - start) * 1000})
                            if child.poll() is not None:
                                check(child.returncode == 0, "world client failed; see client log")
                                break
                            check(time.perf_counter() - start < client_timeout, "world client timed out")
                            time.sleep(.01)
                        check(len(commands) == 3, "world commands missing")
                        result["world_commands"] = commands
                    finally:
                        if child.poll() is None:
                            child.kill()
                            child.wait(timeout=5)
            else:
                completed = subprocess.run(arguments, cwd=client.parent, capture_output=True, timeout=client_timeout)
                (directory / f"{mode}-client.log").write_bytes(completed.stdout + completed.stderr)
                completed.check_returncode()
            result["client_ms"] = (time.perf_counter() - start) * 1000
            if mode in ("load", "movement", "reconnect", "world", "world-resume"):
                status = admin(admin_port, directory, "status")
                check(status.get("ok"), "server status rejected")
                result["server_status"] = status["data"]
            start = time.perf_counter()
            if stop_mode == "graceful":
                check(admin(admin_port, directory, "shutdown").get("ok"), "shutdown rejected")
                check(process.wait(timeout=5) == 0, "server graceful exit failed")
            else:
                check(process.poll() is None, "server exited before force termination")
                if mode == "seed":
                    wait_for_seed_state(directory)
                process.kill()
                process.wait(timeout=5)
            result["stop_ms"] = (time.perf_counter() - start) * 1000
            result["exit_code"] = process.returncode
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
    result["file_sha256"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in
                             (directory / f"{mode}-server.log", directory / f"{mode}-client.log", directory / "durable-state.json")
                             if persistence or path.name != "durable-state.json"}
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    server, client = args.server.resolve(strict=True), args.client.resolve(strict=True)
    binaries = [server, client]
    if platform.system() == "Windows":
        bridges = [path.parent / "commonwealth_online_gns_bridge.dll" for path in binaries]
        check(bridges[0].read_bytes() == bridges[1].read_bytes(), "server/client bridge mismatch")
        binaries += list(dict.fromkeys(bridges))
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve() / ("restart-" + uuid.uuid4().hex)
    if output.is_relative_to(root):
        check(subprocess.run(["git", "-C", str(root), "check-ignore", "--quiet", str(output)]).returncode == 0,
              "in-repository output must be ignored because it contains credentials")
    output.mkdir(parents=True, exist_ok=False)
    sources = [Path(__file__).resolve(), root / "CO-SERVER/server-cpp/tests/LivePersistenceGnsSmoke.cpp",
               root / "CO-SERVER/server-cpp/src/ServerRuntime.inc", root / "CO-SERVER/server-cpp/src/ServerMain.cpp",
               root / "CO-SERVER/server-cpp/src/PersistenceGate.cpp", root / "CO-SERVER/server-cpp/src/DurableStateFileStore.cpp"]
    report = {
        "source_head": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
        "working_tree_status": subprocess.check_output(["git", "-C", str(root), "status", "--short"], text=True),
        "platform": platform.platform(),
        "source_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
        "binary_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in binaries},
        "scope": "Localhost production GNS server and existing Protocol V2 persistence smoke client. "
                 "Two profiles in resume phase; no Fallout save association or multi-machine acceptance. "
                 "Force termination occurs after successful seed; no torn-write injection. "
                 "No representative load/capacity acceptance.",
        "cases": [],
    }
    for stop_mode in ("graceful", "force_after_seed"):
        directory = output / stop_mode
        directory.mkdir()
        seed = phase(server, client, directory, "seed", stop_mode)
        before = json.loads((directory / "durable-state.json").read_text())
        resumed = phase(server, client, directory, "resume", "graceful")
        after = json.loads((directory / "durable-state.json").read_text())
        check(before["serverInstanceId"] == after["serverInstanceId"], "server instance changed after restart")
        for state in (before, after):
            player = state["profiles"]["persistence-smoke-primary#0"]["playerState"]
            check(player["characterName"] == "Persistence Smoke Survivor" and "actionEvents" not in player,
                  "durable character state mismatch")
        check(before["profiles"]["persistence-smoke-primary"]["resume"]["tokenHash"] !=
              after["profiles"]["persistence-smoke-primary"]["resume"]["tokenHash"], "resume credential did not rotate")
        report["cases"].append({"seed": seed, "resume": resumed, "checks_passed": True,
                                "primary_bytes": (directory / "durable-state.json").stat().st_size,
                                "backup_bytes": (directory / "durable-state.json.bak").stat().st_size})
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(stop_mode + ": PASS", flush=True)
    print(output / "report.json")


if __name__ == "__main__":
    main()
