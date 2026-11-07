#!/usr/bin/env python3
"""Build the commit that public CI runs: one ref's tree minus private files, with no history.

Usage: export-public-ci.py <ref> <message>   (prints the new commit SHA)
"""
import os
import re
import subprocess
import tempfile
import sys
from fnmatch import fnmatchcase
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

DENY = (
    "docs/*",
    "wiki/*",
    "*/docs/*",
    "AGENTS.md",
    "*/AGENTS.md",
    "CLAUDE.md",
    "*/CLAUDE.md",
    ".claude/*",
    "*/.claude/*",
    ".codex/*",
    "*/.codex/*",
    "Commonwealth Online Roadmap - Backlog.tsv",
    ".github/workflows/*",
    ".github/public-ci/*",
)

PUBLIC_WORKFLOWS = ".github/public-ci/workflows/"

SHARED = ("co-core/", "scripts/", "tests/", "tools/", "scripting/", ".github/", ".gitmodules")
CODE = ("CO-BETA/", "CO-SERVER/", "CommonwealthOnlineUI/", "CO-Launcher/", "Website/", "supabase/")
SERVER = ("CO-SERVER/", "CO-BETA/")
BETA = ("CO-BETA/", "CommonwealthOnlineUI/")
UI = ("CommonwealthOnlineUI/",)
ALL = SERVER + UI
NATIVE_SERVER = (
    "CO-SERVER/",
    "co-core/",
    ".gitmodules",
    ".github/actions/",
    "CO-BETA/plugin/gns-bridge/",
    "CO-BETA/ThirdParty/GameNetworkingSockets",
    "CO-BETA/ci/toolchains/",
    "CO-BETA/ci/protocol-acceptance/",
    "CO-BETA/plugin/toolchains/",
    "scripts/ci-ccache-env.sh",
    "scripts/provision-matrix-ci-toolchain.py",
    "scripts/verify-native-sanitizers.py",
)
LAUNCHER = (
    "CO-Launcher/",
    "co-core/include/F4TServerConfiguration.h",
    "co-core/src/config/F4TServerConfiguration.cpp",
    "co-core/src/config/F4TServerConfigModpack.cpp",
    "co-core/src/config/F4TServerConfigJson.cpp",
    "CO-SERVER/host/Services/ServerConfig.cs",
    "CommonwealthOnlineUI/ui/fonts/futura-pt-demibold.otf",
    "CommonwealthOnlineUI/ui/views/CommonwealthOnline/shared/logo.svg",
)
EXACT_SCOPES = {
    "co-launcher-ci": LAUNCHER,
    "co-native-ci": NATIVE_SERVER + ("CommonwealthOnlineUI/xmake-parser-ci.lua", "scripts/test-client-host-guards.sh", "CO-BETA/ci/stage-content.sh", "CO-BETA/creation-kit/esp/", "CO-BETA/creation-kit/assets/", "CO-BETA/creation-kit/scripts/compiled/", "CO-BETA/creation-kit/scripts/source/"),
    "co-server-acceptance": NATIVE_SERVER,
    "co-server-host": NATIVE_SERVER,
    "co-server-package": NATIVE_SERVER,
    "co-server-release-acceptance": NATIVE_SERVER,
}
SCOPES = {
    "co-website-ci": ("Website/", "supabase/"),
    "co-ui-ci": ALL,
    "co-ui-prisma-contract": ALL,
    "co-windows-compile": BETA,
    "co-beta-ci": ALL,
    "co-beta-release-stage-contract": BETA,
    "co-production-transport": SERVER,
    "co-audit-regressions": CODE,
    "co-contracts": CODE,
}
LINUX_COMPILE_ALLOWED = {"co-native-ci", "co-server-acceptance", "co-server-host", "co-server-package", "co-server-release-acceptance", "co-launcher-ci"}
CORE_BUILD_WORKFLOWS = {"co-native-ci", "co-server-acceptance", "co-server-package", "co-server-release-acceptance"}
CLIENT_ADAPTER_SOURCES = ("F4TGnsProtocolAdapter.cpp", "F4TGnsProtocolAdapterInbound.cpp", "F4TGnsProtocolAdapterOutbound.cpp", "F4TGnsProtocolAdapterModpack.cpp", "F4TGnsProtocolAdapterSlots.cpp")
CLIENT_MODPACK_SOURCES = ("F4TModpackProbeBudget.cpp", "F4TModpackProbe.cpp", "F4TModpackProbeCache.cpp", "F4TModpackNeed.cpp", "F4TModpackInstall.cpp", "F4TModpackInstallFormat.cpp", "F4TModpackInstallRollback.cpp", "F4TModpackInstallNative.cpp", "F4TModpackAutoFix.cpp")
CLIENT_RUNTIME_SOURCES = ("F4TDebugSwitch.cpp", "F4TVtableGuard.cpp", "F4TInlineHookGuard.cpp", "F4TF4SECapabilityPolicy.cpp", "F4TMutationRing.cpp", "F4TProbeRecord.cpp")
CLIENT_POLICY_SOURCES = (("validation", "F4TReceivePolicy.cpp"), ("manifest", "F4TModpackMismatchReport.cpp"), ("world", "F4TInventoryOperations.cpp"), ("world", "F4TDropMirrorTable.cpp"), ("npc", "F4TNpcLootSync.cpp"), ("npc", "F4TNpcSyncPolicy.cpp"))
LINUX_COMPILE_RE = re.compile(r"(?:^[ \t]*|run: |- |&&[ \t]*|;[ \t]*|\|[ \t]*|sudo )(?:xmake|cmake|ninja|cargo)(?:[ \t]+\S|[ \t]*$)|\b(?:clang|g)\+\+|\bc\+\+ -std|\bdotnet (?:build|test|publish)(?![\w-])", re.M)
JOB_SPLIT = r"^(?=  (?:[A-Za-z0-9_-]+|\"[^\"]+\"|'[^']+'):(?:[ \t]+#.*)?$)"
NO_LINUX_COMPILE_STEP = """      - name: Block Linux compile
        working-directory: ${{ github.workspace }}
        run: |
          shim="$RUNNER_TEMP/no-compile/bin"
          mkdir -p "$shim"
          for tool in c++ g++ gcc cc clang clang++ cmake xmake make ninja cargo; do
            printf '#!/bin/sh\\necho "::error::Linux compile is blocked here; only the server and launcher compile on Linux"\\nexit 1\\n' > "$shim/$tool"
            chmod +x "$shim/$tool"
          done
          echo "$shim" >> "$GITHUB_PATH"
"""



def git(*args, env=None, input=None):
    data = input.encode("utf-8") if input is not None else None
    out = subprocess.run(["git", *args], cwd=ROOT, env=env, input=data, capture_output=True, check=True).stdout
    return out.decode("utf-8")


def exported(path):
    return not any(fnmatchcase(path, pattern) for pattern in DENY)


def affected(name, changed):
    exact = EXACT_SCOPES.get(name)
    if exact and changed:
        return any(path.startswith(exact) or path == f"{PUBLIC_WORKFLOWS}{name}.yml" for path in changed)
    scope = SCOPES.get(name)
    if scope is None or not changed:
        return True
    for path in changed:
        if path.startswith(scope + SHARED):
            return True
        if not exported(path) or ("/" not in path and path.endswith((".md", ".tsv"))):
            continue
        if not path.startswith(CODE):
            return True
    return False


def windows_job(job):
    runs_on = re.search(r"^    runs-on:.*(?:\n      .*)*", job, re.M)
    return runs_on is not None and "windows" in runs_on.group(0)


def linux_job(job):
    return "\n    steps:" in job and not windows_job(job)


def unguarded_linux_compile(text):
    for job in re.split(JOB_SPLIT, text, flags=re.M):
        if not linux_job(job) or re.search(r"^    if: true # linux-compile$", job, re.M):
            continue
        for step in re.split(r"^(?=      - )", job, flags=re.M):
            if LINUX_COMPILE_RE.search(step) and "        if: true # linux-compile" not in step:
                return True
    return False


def windows_only_acceptance(text):
    top_keys = set()
    jobs = []
    current = None
    in_jobs = False
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if "\t" in line:
            return False
        if not line.startswith(" "):
            match = re.fullmatch(r"(name|on|permissions|concurrency|defaults|env|run-name|jobs):(?: .*)?", line)
            if not match or match.group(1) in top_keys:
                return False
            key = match.group(1)
            top_keys.add(key)
            in_jobs = key == "jobs"
            if in_jobs and line != "jobs:":
                return False
            continue
        if not in_jobs:
            continue
        if line.startswith("  ") and not line.startswith("   "):
            if not re.fullmatch(r"  [A-Za-z0-9_-]+:", line):
                return False
            current = {}
            jobs.append(current)
        elif line.startswith("    ") and not line.startswith("     "):
            if current is None:
                return False
            match = re.fullmatch(r"    ([A-Za-z0-9_-]+):(?: (.*))?", line)
            if not match or match.group(1) in current:
                return False
            current[match.group(1)] = match.group(2)
        elif current is None or not line.startswith("      "):
            return False
    return bool(jobs) and all(
        job.get("runs-on") in {"windows-latest", "windows-2022", "windows-2025"}
        and "uses" not in job
        for job in jobs
    )


def stale_linux_client(name, text):
    if name == "co-client-protocol-acceptance":
        return not windows_only_acceptance(text)
    if name != "co-native-ci":
        return False
    for job in re.split(JOB_SPLIT, text, flags=re.M):
        if not linux_job(job):
            continue
        for step in re.split(r"^(?=      - )", job, flags=re.M):
            if re.search(r"^        working-directory:\s*['\"]?CO-BETA/plugin(?:[/\s'\"]|$)", step, re.M) or ("CO-BETA/plugin" in step and LINUX_COMPILE_RE.search(step)):
                return True
    return False


def guarded_client_adapter(text):
    block = re.search(r'(?m)^    if is_host\("windows"\) then\n((?:        add_files\(path.join\(co_core_dir, "src/protocol/F4TGnsProtocolAdapter\w*\.cpp"\)\)\n)+)    end$', text)
    guest = '    if is_host("windows") then\n        add_files(path.join(co_core_dir, "src/session/F4TSteamTunnelGuest.cpp"))\n    end'
    modpack = '    if is_host("windows") then\n' + ''.join(f'        add_files(path.join(co_core_dir, "src/manifest/{source}"))\n' for source in CLIENT_MODPACK_SOURCES) + '    end'
    runtime = '    if is_host("windows") then\n' + ''.join(f'        add_files(path.join(co_core_dir, "src/runtime/{source}"))\n' for source in CLIENT_RUNTIME_SOURCES) + '    end'
    policy = '    if is_host("windows") then\n' + ''.join(f'        add_files(path.join(co_core_dir, "src/{directory}/{source}"))\n' for directory, source in CLIENT_POLICY_SOURCES) + '    end'
    return (bool(block) and all(text.count(source) == 1 and source in block.group(1) for source in CLIENT_ADAPTER_SOURCES)
            and guest in text and text.count("F4TSteamTunnelGuest.cpp") == 1
            and modpack in text and all(text.count(source) == 1 for source in CLIENT_MODPACK_SOURCES)
            and runtime in text and all(text.count(source) == 1 for source in CLIENT_RUNTIME_SOURCES)
            and policy in text and all(text.count(source) == 1 for _, source in CLIENT_POLICY_SOURCES))


def guarded_client_voice(text):
    start = 'if is_host("windows") then\ntarget("CoreLib")\n    add_headerfiles(path.join(os.scriptdir(), "include/F4TVoiceMixer.h"))'
    client = ("F4TVoiceMixer.cpp", "F4TVoiceJitter.cpp", "F4TVoiceGate.cpp")
    if text.count(start) != 1 or not all(text.count(source) == 1 for source in client):
        return False
    block = text.split(start, 1)[1]
    if not all(source in block for source in client):
        return False
    depth = 1
    lines = block.strip().splitlines()
    for index, line in enumerate(lines):
        line = line.strip()
        if re.fullmatch(r'if .+ then', line):
            depth += 1
        elif line == 'end':
            depth -= 1
            if depth == 0 and index != len(lines) - 1:
                return False
        elif line == 'else' or line.startswith('elseif '):
            return False
        if depth < 0:
            return False
    return depth == 0


def block_linux_compile(name, text):
    if name in LINUX_COMPILE_ALLOWED:
        return text
    return "".join(
        re.sub(r"^    steps:.*\n", lambda match: match.group(0) + NO_LINUX_COMPILE_STEP, chunk, count=1, flags=re.M) if linux_job(chunk) else chunk
        for chunk in re.split(JOB_SPLIT, text, flags=re.M)
    )


def disabled(entry):
    meta, path = entry.split("\t", 1)
    mode, sha = meta.split()
    text = git("cat-file", "blob", sha)
    trigger = 'branches: ["ci/**"]'
    if trigger not in text:
        raise SystemExit(f"{path} has no ci/** trigger to disable")
    text = block_linux_compile(Path(path).stem, text.replace(trigger, 'branches: ["ci-skipped/**"]'))
    return f"{mode} {git('hash-object', '-w', '--stdin', input=text).strip()}\t{path}"


def compile_gated(entry):
    meta, path = entry.split("\t", 1)
    mode, sha = meta.split()
    text = git("cat-file", "blob", sha)
    gated = block_linux_compile(Path(path).stem, text.replace("if: true # linux-compile", "if: false # linux-compile"))
    if gated == text:
        return entry
    return f"{mode} {git('hash-object', '-w', '--stdin', input=gated).strip()}\t{path}"


def main():
    ref, message = sys.argv[1], sys.argv[2]
    entries = []
    for line in git("ls-tree", "-r", "--full-tree", ref).splitlines():
        meta, path = line.split("\t", 1)
        mode, kind, sha = meta.split()
        if path.startswith(PUBLIC_WORKFLOWS) and path.endswith(".yml"):
            entries.append(f"100644 {sha}\t.github/workflows/{path[len(PUBLIC_WORKFLOWS):]}")
        elif exported(path):
            entries.append(f"{mode} {sha}\t{path}")
    paths = [entry.split("\t", 1)[1] for entry in entries]
    changed_file = os.environ.get("PUBLIC_CI_CHANGED_FILE")
    changed = Path(changed_file).read_text(encoding="utf-8").split("\n") if changed_file and Path(changed_file).exists() else []
    changed = [path for path in changed if path]
    skipped = [Path(p).stem for p in paths if p.startswith(".github/workflows/") and not affected(Path(p).stem, changed)]
    for required in ("co-core/", "CO-SERVER/", "CO-BETA/plugin/", ".github/workflows/co-native-ci.yml"):
        if not any(path.startswith(required) for path in paths):
            raise SystemExit(f"export is missing {required}")
    leaked = [path for path in paths if path.startswith(("docs/", "wiki/")) or "/docs/" in path or path.endswith("AGENTS.md")
              or "code-notes" in path or "Roadmap" in path]
    if leaked:
        raise SystemExit(f"export would publish internal files: {leaked[:5]}")

    blocked = []
    core_guarded = guarded_client_adapter(git("show", f"{ref}:co-core/xmake.lua")) and guarded_client_voice(git("show", f"{ref}:co-core/xmake-voice.lua"))
    for entry in entries:
        path = entry.split("\t", 1)[1]
        stem = Path(path).stem
        if not path.startswith(".github/workflows/") or stem in skipped:
            continue
        workflow = git("cat-file", "blob", entry.split()[1])
        client = stale_linux_client(stem, workflow) or (stem in CORE_BUILD_WORKFLOWS and not core_guarded)
        if client or (stem not in LINUX_COMPILE_ALLOWED and unguarded_linux_compile(workflow)):
            reason = "contains a Linux client lane" if client else "runs a Linux compile without the required marker"
            print(f"::warning::{path} {reason}; disabled", file=sys.stderr)
            skipped.append(stem)
            blocked.append(stem)
    if os.environ.get("PUBLIC_CI_SKIPPED_FILE"):
        Path(os.environ["PUBLIC_CI_SKIPPED_FILE"]).write_text("\n".join(skipped) + "\n", encoding="utf-8")
    if os.environ.get("PUBLIC_CI_BLOCKED_FILE"):
        Path(os.environ["PUBLIC_CI_BLOCKED_FILE"]).write_text("\n".join(blocked) + "\n", encoding="utf-8")
    if os.environ.get("PUBLIC_CI_WINDOWS_ONLY_FILE"):
        Path(os.environ["PUBLIC_CI_WINDOWS_ONLY_FILE"]).write_text("", encoding="utf-8")
    rewritten = []
    for entry in entries:
        path = entry.split("\t", 1)[1]
        stem = Path(path).stem
        if path.startswith(".github/workflows/") and stem in skipped:
            rewritten.append(disabled(entry))
        elif path.startswith(".github/workflows/") and stem not in LINUX_COMPILE_ALLOWED:
            rewritten.append(compile_gated(entry))
        else:
            rewritten.append(entry)
    entries = rewritten

    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(tmp) / "index"))
        git("update-index", "--add", "--index-info", env=env, input="\n".join(entries) + "\n")
        tree = git("write-tree", env=env).strip()
    print(git("commit-tree", tree, "-m", message).strip())


if __name__ == "__main__":
    main()
