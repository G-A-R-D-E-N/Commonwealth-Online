import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import uuid
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--samples", type=int, default=100)
    parser.add_argument("--interval-ms", type=int, default=50)
    args = parser.parse_args()
    if not 2 <= args.samples <= 10000 or not 0 <= args.interval_ms <= 1000:
        parser.error("samples must be 2..10000; interval-ms must be 0..1000")
    binary = args.binary.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve() / ("run-" + uuid.uuid4().hex)
    output.mkdir(parents=True, exist_ok=False)
    sources = [Path(__file__).resolve(), root / "CO-SERVER/server-cpp/benchmarks/PersistenceBenchmark.cpp",
               root / "CO-SERVER/server-cpp/benchmarks/ProcessResources.h",
               root / "CO-SERVER/server-cpp/src/DurableStateFileStore.cpp",
               root / "CO-SERVER/server-cpp/src/PersistenceGate.cpp", root / "CO-SERVER/server-cpp/src/DurableCommitWriter.cpp"]
    sources += sorted((root / "co-core/src/persistence").glob("*.cpp"))
    sources += sorted((root / "co-core/src/validation").glob("*.cpp"))
    sources += sorted((root / "co-core/include").glob("*.h"))
    sources += sorted((root / "CO-SERVER/server-cpp/include").glob("*.h"))
    sources += [root / "co-core/xmake.lua", root / "CO-SERVER/server-cpp/xmake.lua"]
    report = {
        "source_head": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
        "working_tree_status": subprocess.check_output(["git", "-C", str(root), "status", "--short"], text=True),
        "platform": platform.platform(), "machine": platform.machine(),
        "storage_path": str(output), "storage_free_bytes": shutil.disk_usage(output).free,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
        "scope": "Production persistence in-process; synthetic appearance revisions and credential rotation. "
                 "Batch timing excludes full server ticks, protocol authentication, transport and Fallout saves. "
                 "World writes are an unpaced burst; backup is the previous commit. "
                 "CPU is process user+kernel time. Windows I/O counts process requests/transfers; "
                 "Linux I/O counts getrusage input/output blocks. Neither is physical device telemetry. "
                 "This harness does not measure full server CPU or physical disks.",
        "cases": [],
    }
    for players in (1, 2, 4, 8, 16):
        command = [str(binary), str(output / f"players-{players}"), str(players), str(args.samples), str(args.interval_ms)]
        completed = subprocess.run(command, capture_output=True, text=True,
                                   timeout=60 + args.samples * (args.interval_ms / 500 + .1))
        (output / f"players-{players}.stdout.json").write_text(completed.stdout, encoding="utf-8")
        (output / f"players-{players}.stderr.txt").write_text(completed.stderr, encoding="utf-8")
        completed.check_returncode()
        report["cases"].append(json.loads(completed.stdout))
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{players} profiles: PASS", flush=True)
    print(output / "report.json")


if __name__ == "__main__":
    main()
