import argparse
import copy
import hashlib
import json
import platform
import subprocess
import time
import uuid
from pathlib import Path


def check(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    probe = args.probe.resolve(strict=True)
    output = args.output.resolve() / ("commit-crash-" + uuid.uuid4().hex)
    if output.is_relative_to(root):
        check(subprocess.run(["git", "-C", str(root), "check-ignore", "--quiet", str(output)]).returncode == 0,
              "in-repository output must be ignored")
    output.mkdir(parents=True, exist_ok=False)
    sources = [Path(__file__), root / "CO-SERVER/server-cpp/benchmarks/DurableCommitCrashProbe.cpp",
               root / "CO-SERVER/server-cpp/src/DurableStateFileStore.cpp",
               root / "CO-SERVER/server-cpp/include/DurableStateFileStore.h",
               root / "co-core/src/persistence/F4TDurableState.cpp", root / "CO-SERVER/server-cpp/xmake.lua"]
    report = {"source_head": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
              "working_tree_status": subprocess.check_output(["git", "-C", str(root), "status", "--short"], text=True),
              "platform": platform.platform(), "probe_path": str(probe), "probe_sha256": digest(probe),
              "source_sha256": {str(p.relative_to(root)): digest(p) for p in sources},
              "scope": "Owned native process termination at completed temp-sync, backup-replace and primary-replace boundaries; "
                       "actual durable store batch atomicity, server/profile/credential/world continuity and subsequent commit. "
                       "Synthetic store fixture, no game/server protocol, torn-write, replacement-failure or power-loss acceptance.",
              "cases": []}
    for count in (1, 2, 4, 8, 16):
        for stage in ("temp-synced", "backup-replaced", "primary-replaced"):
            directory = output / f"{count}-{stage}"
            directory.mkdir()
            primary = directory / "durable-state.json"
            backup = directory / "durable-state.json.bak"
            with (directory / "seed.log").open("wb") as log:
                subprocess.run([str(probe), "seed", str(primary), str(count), "seed"], stdout=log,
                               stderr=subprocess.STDOUT, timeout=10, check=True)
            seeded_primary, seeded_backup = primary.read_bytes(), backup.read_bytes()
            seeded = json.loads(seeded_primary)
            updated = copy.deepcopy(seeded)
            for index in range(count):
                updated["profiles"][f"crash-profile-{index}"]["playerState"]["characterName"] = f"new-{index}"
            expected = updated if stage == "primary-replaced" else seeded
            client_log = directory / "update.log"
            with client_log.open("wb") as log:
                process = subprocess.Popen([str(probe), "update", str(primary), str(count), stage], stdout=log,
                                           stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                try:
                    deadline = time.monotonic() + 10
                    marker = f"CO_DURABLE_COMMIT_STAGE {stage}\n"
                    while marker not in client_log.read_text(encoding="utf-8"):
                        check(process.poll() is None, "probe exited before selected boundary")
                        check(time.monotonic() < deadline, "selected boundary timed out")
                        time.sleep(.01)
                    check(process.poll() is None, "probe did not remain paused")
                    paused_primary, paused_backup = primary.read_bytes(), backup.read_bytes()
                    check(json.loads(paused_primary) == expected, "partial or unexpected primary batch")
                    check(paused_backup == (seeded_backup if stage == "temp-synced" else seeded_primary), "unexpected backup generation")
                    orphans = list(directory.glob("durable-state.json.tmp-*"))
                    check(len(orphans) == (0 if stage == "primary-replaced" else 1), "unexpected temporary files")
                    for path in orphans:
                        check(json.loads(path.read_bytes()) == updated, "temporary batch incomplete")
                    orphan_hashes = {p.name: digest(p) for p in orphans}
                    process.kill()
                    check(process.wait(timeout=5) != 0, "expected force termination")
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
            check(primary.read_bytes() == paused_primary and backup.read_bytes() == paused_backup, "files changed after paused termination")
            with (directory / "verify.log").open("wb") as log:
                subprocess.run([str(probe), "verify", str(primary), str(count),
                                "new" if stage == "primary-replaced" else "old"], stdout=log,
                               stderr=subprocess.STDOUT, timeout=10, check=True)
            continued = copy.deepcopy(expected)
            for index in range(count):
                continued["profiles"][f"crash-profile-{index}"]["playerState"]["characterName"] = f"continued-{index}"
            check(json.loads(primary.read_bytes()) == continued and backup.read_bytes() == paused_primary,
                  "subsequent commit or backup mismatch")
            check({p.name: digest(p) for p in directory.glob("durable-state.json.tmp-*")} == orphan_hashes,
                  "orphan temporary files changed or new temporary files leaked")
            report["cases"].append({"profiles": count, "stage": stage, "owned_pid": process.pid,
                                    "exit_code": process.returncode, "marker": marker.strip(),
                                    "complete_batch": "new" if stage == "primary-replaced" else "old",
                                    "identity_credentials_world_unchanged": True, "subsequent_commit_verified": True,
                                    "seeded_primary_sha256": hashlib.sha256(seeded_primary).hexdigest(),
                                    "seeded_backup_sha256": hashlib.sha256(seeded_backup).hexdigest(),
                                    "paused_primary_sha256": hashlib.sha256(paused_primary).hexdigest(),
                                    "paused_backup_sha256": hashlib.sha256(paused_backup).hexdigest(),
                                    "orphan_sha256": orphan_hashes,
                                    "file_sha256": {p.name: digest(p) for p in (primary, backup, directory / "seed.log", client_log, directory / "verify.log")}})
            (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(f"{count} profiles {stage}: PASS", flush=True)
    print(output / "report.json")


if __name__ == "__main__":
    main()
