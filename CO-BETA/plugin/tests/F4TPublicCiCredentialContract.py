import importlib.util
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path


root = Path(__file__).resolve().parents[3]
SECRET_REFERENCE = re.compile(r"(?i)(?<![\w.-])secrets(?![\w-])")
TOKEN_KEY = re.compile(r"(?<![\w-])token:")
REPORTED = (
    "co-audit-regressions", "co-beta-ci", "co-beta-release-stage-contract", "co-client-protocol-acceptance",
    "co-contracts", "co-launcher-ci", "co-native-ci", "co-production-transport", "co-server-acceptance",
    "co-server-host", "co-server-package", "co-server-release-acceptance", "co-ui-ci", "co-ui-prisma-contract",
    "co-website-ci", "co-windows-compile",
)
BETA = (
    "co-audit-regressions", "co-beta-ci", "co-beta-release-stage-contract", "co-client-protocol-acceptance",
    "co-contracts", "co-launcher-ci", "co-native-ci", "co-production-transport", "co-ui-ci",
    "co-ui-prisma-contract", "co-windows-compile",
)
SERVER = (
    "co-contracts", "co-native-ci", "co-production-transport", "co-server-acceptance", "co-server-host",
    "co-server-package", "co-server-release-acceptance",
)
failures = []


def workflow_folder():
    for folder in (".github/public-ci/workflows", ".github/workflows"):
        if (root / folder).is_dir():
            return root / folder
    raise FileNotFoundError("no public CI workflow folder")


def step_script(relative, step):
    lines = (root / relative).read_text(encoding="utf-8").split("\n")
    run = lines.index("        run: |", lines.index(f"      - name: {step}"))
    body = []
    for line in lines[run + 1:]:
        if line.strip() and not line.startswith(" " * 10):
            break
        body.append(line[10:])
    return "\n".join(body)


GATE_GH = """#!/bin/sh
case "$*" in
  *compare*) echo ahead ;;
  *"--jq .sha"*) echo "$SHA" ;;
  *) printf '%s\\n' "$STATUSES" ;;
esac
"""
REPORT_GH = """#!/bin/sh
case "$*" in
  *commits*) printf 'Commonwealth Online\\n\\nSource-Commit: %s\\n' "$SOURCE" ;;
  *) echo "$*" >> "$LOG" ;;
esac
"""


def artifact(state, context="public-ci/co-contracts"):
    return json.dumps({"context": context, "state": state, "export": "b" * 40})


def run_step(script, env, fake_gh, result=None):
    with tempfile.TemporaryDirectory() as tmp:
        gh = Path(tmp, "gh")
        gh.write_text(fake_gh, encoding="utf-8")
        gh.chmod(0o755)
        log = Path(tmp, "log")
        if result is not None:
            Path(tmp, "result.json").write_text(result, encoding="utf-8")
        values = {"PATH": f"{tmp}{os.pathsep}{os.environ['PATH']}", "LOG": str(log), "RESULT": str(Path(tmp, "result.json")), "GITHUB_OUTPUT": str(Path(tmp, "output")), **env}
        done = subprocess.run(["bash", "-c", script], env=values, capture_output=True, text=True)
        posted = log.read_text(encoding="utf-8") if log.exists() else ""
    return done.returncode, done.stdout + done.stderr, posted


folder = workflow_folder()
reporting = set()
for path in sorted(folder.glob("*.yml")):
    text = path.read_text(encoding="utf-8")
    if SECRET_REFERENCE.search(text):
        failures.append(f"{path.name} references the secrets context")
    if re.search(r"^\s*environment:", text, re.M):
        failures.append(f"{path.name} declares an environment")
    if TOKEN_KEY.search(text) or "CO_PUBLIC_CI" in text:
        failures.append(f"{path.name} names a token")
    if "./.github/actions/co-report" in text:
        reporting.add(path.stem)
        if f"context: public-ci/{path.stem}" not in text:
            failures.append(f"{path.name} does not report public-ci/{path.stem}")
if reporting != set(REPORTED):
    failures.append(f"reporting workflows are {sorted(reporting)}")

action = (root / ".github/actions/co-report/action.yml").read_text(encoding="utf-8")
if TOKEN_KEY.search(action) or SECRET_REFERENCE.search(action) or "name: public-ci-result" not in action or "actions/upload-artifact@" not in action:
    failures.append("co-report must upload public-ci-result and take no token")
exporter = (root / "scripts/export-public-ci.py").read_text(encoding="utf-8")
if "SECRET_REFERENCE_RE.search(" not in exporter:
    failures.append("the exporter must refuse exported secret references")
if "plugin/tests/F4TPublicCiCredentialContract.py" not in (folder / "co-contracts.yml").read_text(encoding="utf-8"):
    failures.append("co-contracts.yml must run this contract")
if "python3 tools/tests/test_engine_ids.py" not in (folder / "co-contracts.yml").read_text(encoding="utf-8"):
    failures.append("co-contracts.yml must check engine ids without a token")
if "gh release download" in (folder / "co-contracts.yml").read_text(encoding="utf-8"):
    failures.append("co-contracts.yml must not download address bins")
spec = importlib.util.spec_from_file_location("exporter", root / "scripts/export-public-ci.py")
exporter_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter_module)
HEADER = "permissions:\n  contents: read\n"
for text in (
    "T: ${{ secrets.CO_PUBLIC_CI_TOKEN }}",
    "    secrets: inherit",
    r'T: "${{ \x73ecrets.CO_PUBLIC_CI_TOKEN }}"',
    'T: "\\u0073ecrets"',
    r'T: "\U00000073ecrets"',
    r"run: echo \x41",
    "    uses: G-A-R-D-E-N/Other-Repo/.github/workflows/publish.yml@main",
    '    uses: "other/repo/.github/workflows/x.yml@v1"',
    "    USES: other/repo/.github/workflows/x.yml@v1",
    'T: "${{ sec\\\n  rets.CO_PUBLIC_CI_TOKEN }}"',
    '    uses: "owner/repo/.github/\\\n  workflows/x.yml@v1"',
    "    environment: public-ci-status",
    "    Environment: public-ci-status",
    "run: &anchor echo",
    "run: *anchor",
    "run: !!str echo",
    "<<: *base",
    "permissions:\n  Contents: Write",
    "permissions:\n  contents: write",
    "permissions: write-all",
    "permissions: {statuses: write}",
    "permissions:\n  actions: write",
    "permissions:\n  actions:\n    write",
    "permissions: {actions:\n  write}",
):
    if exporter_module.workflow_refusal(".github/workflows/x.yml", HEADER + text) is None:
        failures.append(f"the exporter must refuse {text!r}")
if exporter_module.workflow_refusal(".github/workflows/x.yml", "on: push\njobs: {}\n") is None:
    failures.append("the exporter must refuse a workflow without a top-level permissions key")
for text in (
    "    uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262",
    "    uses: ./.github/workflows/local.yml",
    "    uses: G-A-R-D-E-N/Commonwealth-Online/.github/actions/co-xmake@fb68442309ddaab8d1bbeae0aae93c69daac19c2",
    "      - name: Write the result",
    "      - name: Environment setup",
):
    if exporter_module.workflow_refusal(".github/workflows/x.yml", HEADER + text) is not None:
        failures.append(f"the exporter must allow {text!r}")
if exporter_module.workflow_refusal(".github/actions/x/action.yml", "runs:\n  using: composite\n") is not None:
    failures.append("the exporter must allow an action without permissions")
if exporter_module.workflow_refusal(".github/workflows/public-ci-cleanup.yml", HEADER + "permissions:\n  actions: write") is None:
    failures.append("a cleanup-style actions write must be refused")
for path in folder.glob("*.yml"):
    if exporter_module.workflow_refusal(f".github/workflows/{path.name}", path.read_text(encoding="utf-8")):
        failures.append(f"the exporter would refuse {path.name}")
for path in (root / ".github/actions").rglob("*.yml"):
    if exporter_module.workflow_refusal(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8")):
        failures.append(f"the exporter would refuse {path.name}")

reporter_path = root / ".github/public-ci/sync/public-ci-report.yml"
releases = {name: root / ".github/public-ci/release" / name for name in ("co-beta-publish-release.yml", "co-server-publish-host.yml")}
if reporter_path.is_file() and all(path.is_file() for path in releases.values()):
    reporter = reporter_path.read_text(encoding="utf-8")
    if not re.search(r"^on:\n  workflow_run:\n", reporter, re.M) or "types: [completed]" not in reporter:
        failures.append("the reporter must run on workflow_run completed")
    if reporter.count("secrets.CO_PUBLIC_CI_STATUS_TOKEN") != 1 or "CO_PUBLIC_CI_TOKEN" in reporter.replace("CO_PUBLIC_CI_STATUS_TOKEN", ""):
        failures.append("the reporter must use only CO_PUBLIC_CI_STATUS_TOKEN")
    if "actions/checkout" in reporter or "environment: public-ci-status" not in reporter:
        failures.append("the reporter must not check out code and must use environment public-ci-status")
    for stem in REPORTED:
        name = re.search(r"^name: (.+)$", (folder / f"{stem}.yml").read_text(encoding="utf-8"), re.M).group(1)
        pattern = re.sub(r"([*+?!\[\]])", r"\\\1", name)
        if f"      - {pattern}\n" not in reporter or stem not in reporter:
            failures.append(f"the reporter does not listen for {name}")
    for release, contexts in (("co-beta-publish-release.yml", BETA), ("co-server-publish-host.yml", SERVER)):
        if " ".join(f"public-ci/{name}" for name in contexts) not in releases[release].read_text(encoding="utf-8"):
            failures.append(f"{release} must require exactly {contexts}")
    if "SKIP_GREEN_CHECK" not in releases["co-beta-publish-release.yml"].read_text(encoding="utf-8"):
        failures.append("the beta release must keep skip_green_check")
    for name in ("public-ci-sync.yml", "public-ci-cache-prune.yml"):
        if "    environment: public-ci-sync\n" not in (root / ".github/public-ci/sync" / name).read_text(encoding="utf-8"):
            failures.append(f"{name} must run in environment public-ci-sync")
    for release in releases.values():
        text = release.read_text(encoding="utf-8")
        if "run: python3 tools/tests/test_engine_ids.py" not in text or "CO_ADDRESS_BINS: " not in text or "test -s" not in text:
            failures.append(f"{release.name} must check engine ids against all three address bins")
    if os.name == "posix":
        for release, contexts in (("co-beta-publish-release.yml", BETA), ("co-server-publish-host.yml", SERVER)):
            green = [f"success public-ci/{name}" for name in contexts]
            cases = {
                "all green": (green, "false", True),
                "missing context": (green[:-1], "false", False),
                "pending context": (green[:-1] + [f"pending public-ci/{contexts[-1]}"], "false", False),
                "failed context": (green[:-1] + [f"failure public-ci/{contexts[-1]}"], "false", False),
                "no results": ([], "false", False),
                "owner skip": (green[:-1], "true", True),
            }
            gate = step_script(f".github/public-ci/release/{release}", "Require green CI on the commit being released")
            for case, (statuses, skip, passes) in cases.items():
                code, output, _ = run_step(gate, {"GH_TOKEN": "t", "PRIVATE_REPO": "G-A-R-D-E-N/Commonwealth-Online-Private", "SHA": "a" * 40, "SKIP_GREEN_CHECK": skip, "CO_INPUT_TARGET": "nomad-dev", "STATUSES": "\n".join(statuses)}, GATE_GH)
                if (code == 0) != passes:
                    failures.append(f"{release} release gate, {case}: exit {code} {output}")
        workflow = ".github/workflows/co-contracts.yml@refs/heads/nomad-dev"
        report = step_script(".github/public-ci/sync/public-ci-report.yml", "Post the status with the status-only token")
        cases = [
            ("valid success posts success", workflow, "success", artifact("success"), 0, "state=success"),
            ("valid failure posts failure", workflow, "success", artifact("failure"), 0, "state=failure"),
            ("failed run with success artifact posts failure", workflow, "failure", artifact("success"), 0, "state=failure"),
            ("missing artifact posts nothing", workflow, "success", None, 0, None),
            ("invalid artifact posts nothing", workflow, "success", "not json", 0, None),
            ("cancelled artifact posts nothing", workflow, "success", artifact("cancelled"), 0, None),
            ("other context posts nothing", workflow, "success", artifact("success", "public-ci/co-beta-ci"), 0, None),
            ("unreported workflow is refused", ".github/workflows/evil.yml@refs/heads/main", "success", artifact("success"), 1, None),
        ]
        for case, path, conclusion, result, code, expected in cases:
            got, output, posted = run_step(report, {"STATUS_TOKEN": "t", "GH_TOKEN": "g", "PUBLIC_REPO": "G-A-R-D-E-N/Commonwealth-Online", "PRIVATE_REPO": "G-A-R-D-E-N/Commonwealth-Online-Private", "RUN_ID": "1", "HEAD_BRANCH": "ci/pr-12", "HEAD_SHA": "b" * 40, "WORKFLOW_PATH": path, "CONCLUSION": conclusion, "SOURCE": "c" * 40}, REPORT_GH, result)
            if got != code or (expected is None and posted) or (expected and expected not in posted):
                failures.append(f"the reporter {case}: exit {got}, posted {posted!r} {output}")
    else:
        print("SKIP: the release gate and reporter runs need posix bash")
else:
    print("SKIP: the reporter and release gates are private-only; checked where the private tree is present")
assert not failures, "\n".join(failures)
print("public CI credential contracts passed")
