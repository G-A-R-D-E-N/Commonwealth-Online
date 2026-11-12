#!/usr/bin/env python3
import importlib.util
import re
from pathlib import Path

spec = importlib.util.spec_from_file_location("exporter", Path(__file__).with_name("export-public-ci.py"))
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)
affected = exporter.affected

assert affected("co-website-ci", [])
assert affected("co-unknown-workflow", ["Website/a.js"])
assert not affected("co-website-ci", ["CO-BETA/plugin/a.cpp"])
assert not affected("co-launcher-ci", ["CommonwealthOnlineUI/a.cpp"])
assert affected("co-launcher-ci", ["CO-Launcher/src/main.rs"])
assert not affected("co-launcher-ci", ["co-core/src/a.cpp"])
assert affected("co-launcher-ci", ["co-core/src/config/F4TServerConfiguration.cpp"])
assert affected("co-launcher-ci", ["CO-SERVER/host/Services/ServerConfig.cs"])
assert affected("co-launcher-ci", ["CommonwealthOnlineUI/ui/fonts/futura-pt-demibold.otf"])
assert not affected("co-launcher-ci", ["CO-SERVER/server-cpp/src/a.cpp"])
assert affected("co-launcher-ci", [".github/public-ci/workflows/co-launcher-ci.yml"])
assert not affected("co-launcher-ci", ["unlisted-dir/file"])
assert not affected("co-server-host", ["Website/a.js"])
for name in exporter.EXACT_SCOPES:
    if name == "co-launcher-ci":
        continue
    assert affected(name, [])
    assert affected(name, ["CO-SERVER/server-cpp/src/a.cpp"])
    assert affected(name, ["co-core/src/a.cpp"])
    assert affected(name, ["CO-BETA/plugin/gns-bridge/co_gns_auth_protocol.h"])
    assert affected(name, ["CO-BETA/ThirdParty/GameNetworkingSockets-src"])
    assert affected(name, [f".github/public-ci/workflows/{name}.yml"])
    assert not affected(name, ["CO-BETA/plugin/src/a.cpp"])
    assert not affected(name, ["CommonwealthOnlineUI/a.cpp"])
    assert not affected(name, ["CO-Launcher/src/main.rs"])
    assert not affected(name, ["tests/audit/test_a.py"])
    assert not affected(name, [".github/public-ci/workflows/co-ui-ci.yml"])
root = Path(__file__).resolve().parents[1]
workflows = root / ".github/public-ci/workflows"
if not workflows.is_dir():
    workflows = root / ".github/workflows"
for name in exporter.EXACT_SCOPES:
    text = (workflows / f"{name}.yml").read_text(encoding="utf-8")
    for script in re.findall(r"""(?:GITHUB_WORKSPACE/|\.\./\.\./|['"\s])(scripts/[\w.-]+)""", text):
        assert script.startswith(exporter.EXACT_SCOPES[name]), (name, script)
for source in (root / "CO-Launcher/launcher-gui/src").glob("*.rs"):
    text = source.read_text(encoding="utf-8")
    for outside in re.findall(r'repo\.join\("([^"]+)"\)|"\.\./\.\./\.\./([^"]+)"', text):
        assert (outside[0] or outside[1]).startswith(exporter.LAUNCHER), (source.name, outside)
assert affected("co-beta-ci", ["CO-BETA/plugin/a.cpp"])
assert affected("co-windows-compile", ["CO-BETA/plugin/a.cpp"])
assert affected("co-audit-regressions", ["Website/a.js"])
assert affected("co-beta-ci", ["CO-SERVER/server-cpp/src/ServerFactions.cpp"])
assert affected("co-ui-ci", ["CO-BETA/plugin/src/a.cpp"])
assert affected("co-ui-prisma-contract", ["CO-BETA/stage-mod.bat"])
assert not affected("co-ui-ci", ["Website/a.js"])
assert exporter.LINUX_COMPILE_ALLOWED == {"co-native-ci", "co-server-acceptance", "co-server-host", "co-server-package", "co-server-release-acceptance", "co-launcher-ci"}
assert not {"co-beta-ci", "co-ui-ci", "co-windows-compile"} & exporter.LINUX_COMPILE_ALLOWED
assert affected("co-native-ci", ["CO-BETA/ci/stage-content.sh"])
assert affected("co-native-ci", ["CO-BETA/creation-kit/esp/x.esp"])
assert affected("co-native-ci", ["CO-BETA/creation-kit/scripts/source/User/a.psc"])
assert exporter.unguarded_linux_compile("jobs:\n  b:\n    runs-on: ubuntu-24.04\n    steps:\n      - run: xmake build -y X\n")
assert not exporter.unguarded_linux_compile("jobs:\n  b:\n    runs-on: ubuntu-24.04\n    steps:\n      - name: x\n        if: true # linux-compile\n        run: xmake build -y X\n")
assert not exporter.unguarded_linux_compile("jobs:\n  b:\n    if: true # linux-compile\n    runs-on: ubuntu-24.04\n    steps:\n      - run: xmake build -y X\n")
assert not exporter.unguarded_linux_compile("jobs:\n  b:\n    runs-on: windows-latest\n    steps:\n      - run: xmake build -y X\n")
ubuntu_job = "jobs:\n  b:\n    runs-on: ubuntu-24.04\n    steps:\n      - run: xmake build -y X\n"
windows_job = "jobs:\n  b:\n    runs-on: windows-latest\n    steps:\n      - run: xmake build -y X\n"
mixed_jobs = "jobs:\n  a:\n    runs-on: ubuntu-24.04\n    steps:\n      - run: x\n  b:\n    runs-on: windows-latest\n    steps:\n      - run: y\n"
assert "    steps:\n      - name: Block Linux compile\n" in exporter.block_linux_compile("co-ui-ci", ubuntu_job)
assert exporter.block_linux_compile("co-native-ci", ubuntu_job) == ubuntu_job
assert exporter.block_linux_compile("co-ui-ci", windows_job) == windows_job
shimmed = exporter.block_linux_compile("co-ui-ci", mixed_jobs)
assert shimmed.count("Block Linux compile") == 1 and shimmed.index("Block Linux compile") < shimmed.index("  b:")
defaulted = "jobs:\n  b:\n    runs-on: ubuntu-24.04\n    defaults:\n      run:\n        working-directory: X\n    steps:\n      - run: x\n"
assert "        working-directory: ${{ github.workspace }}\n" in exporter.block_linux_compile("co-ui-ci", defaulted)
assert "for tool in c++ g++ gcc cc clang clang++ cmake xmake make ninja cargo; do" in exporter.NO_LINUX_COMPILE_STEP
matrix_job = "jobs:\n  b:\n    runs-on: ${{ matrix.os }}\n    steps:\n      - run: cargo build\n"
assert exporter.unguarded_linux_compile(matrix_job) and "Block Linux compile" in exporter.block_linux_compile("co-ui-ci", matrix_job)
assert exporter.unguarded_linux_compile("jobs:\n  b:\n    runs-on:\n      - self-hosted\n    steps:\n      - run: cmake --build b\n")
assert not exporter.unguarded_linux_compile("jobs:\n  b:\n    runs-on:\n      - self-hosted\n      - windows\n    steps:\n      - run: cmake --build b\n")
quoted_job = "jobs:\n  \"b\":  # build\n    runs-on: ubuntu-24.04\n    steps:\n      - run: ninja\n"
assert exporter.unguarded_linux_compile(quoted_job) and "Block Linux compile" in exporter.block_linux_compile("co-ui-ci", quoted_job)
assert exporter.unguarded_linux_compile("jobs:\n  b:  # build\n    runs-on: ubuntu-24.04\n    steps:\n      - run: xmake build -y X\n")
old_native = 'jobs:\n  linux-tests:\n    runs-on: ubuntu-24.04\n    steps:\n      - name: Test plugin integration\n        working-directory: CO-BETA/plugin\n        run: xmake test -y\n'
server_native = 'jobs:\n  linux-tests:\n    runs-on: ubuntu-24.04\n    steps:\n      - name: Test server\n        working-directory: CO-SERVER/server-cpp\n        run: xmake test -y\n'
assert exporter.stale_linux_client('co-native-ci', old_native)
assert exporter.stale_linux_client('co-native-ci', old_native.replace('xmake test -y', './scripts/test-linux.sh'))
assert not exporter.stale_linux_client('co-native-ci', server_native)
core = (root / 'co-core/xmake.lua').read_text(encoding='utf-8')
assert exporter.guarded_client_adapter(core)
assert not exporter.guarded_client_adapter(core.replace('if is_host("windows") then', 'if is_plat("windows") then', 1))
assert not exporter.guarded_client_adapter(core + '\nadd_files("F4TGnsProtocolAdapter.cpp")\n')
assert not exporter.guarded_client_adapter(core.replace('F4TGnsProtocolAdapterSlots.cpp', 'F4TGnsProtocolAdapterSlots-old.cpp'))
assert not exporter.guarded_client_adapter(core.replace('        add_files(path.join(co_core_dir, "src/session/F4TSteamTunnelGuest.cpp"))', '        add_files(path.join(co_core_dir, "src/session/F4TSteamTunnelGuest-old.cpp"))'))
assert not exporter.guarded_client_adapter(core + '\nadd_files("F4TSteamTunnelGuest.cpp")\n')
assert not exporter.guarded_client_adapter(core.replace('F4TModpackProbe.cpp', 'F4TModpackProbe-old.cpp'))
assert not exporter.guarded_client_adapter(core + '\nadd_files("F4TModpackNeed.cpp")\n')
assert not exporter.guarded_client_adapter(core.replace('F4TInlineHookGuard.cpp', 'F4TInlineHookGuard-old.cpp'))
assert not exporter.guarded_client_adapter(core + '\nadd_files("F4TDebugSwitch.cpp")\n')
assert not exporter.guarded_client_adapter(core.replace('F4TNpcLootSync.cpp', 'F4TNpcLootSync-old.cpp'))
assert not exporter.guarded_client_adapter(core + '\nadd_files("F4TReceivePolicy.cpp")\n')
voice = (root / 'co-core/xmake-voice.lua').read_text(encoding='utf-8')
assert exporter.guarded_client_voice(voice)
assert not exporter.guarded_client_voice(voice.replace('if is_host("windows") then', 'if is_plat("windows") then'))
assert not exporter.guarded_client_voice(voice + '\nadd_files("F4TVoiceGate.cpp")\n')
assert not exporter.guarded_client_voice(voice.replace('target("CoreVoiceMixerTests")', 'end\ntarget("CoreVoiceMixerTests")', 1))
assert exporter.stale_linux_client('co-client-protocol-acceptance', server_native)
acceptance = 'co-client-protocol-acceptance'
assert not exporter.stale_linux_client(acceptance, windows_job)
assert not exporter.stale_linux_client(acceptance, windows_job.replace('windows-latest', 'windows-2022'))
for invalid in (
    mixed_jobs,
    windows_job.replace('windows-latest', 'ubuntu-24.04 # windows'),
    windows_job.replace('windows-latest', '${{ matrix.os }}'),
    windows_job.replace('windows-latest', '${{ "windows-latest" }}'),
    windows_job.replace('windows-latest', '[windows-latest]'),
    windows_job.replace('windows-latest', 'windows-latest # comment'),
    windows_job.replace('    runs-on: windows-latest\n', '    uses: owner/repo/.github/workflows/test.yml@main\n'),
    windows_job.replace('    steps:', '    uses: owner/repo/.github/workflows/test.yml@main\n    steps:'),
    windows_job + '  c: {runs-on: ubuntu-24.04}\n',
    windows_job.replace('    steps:', '    runs-on: ubuntu-24.04\n    steps:'),
    windows_job.replace('    steps:', '    "runs-on": ubuntu-24.04\n    steps:'),
    windows_job.replace('    steps:', '    <<: *linux\n    steps:'),
    windows_job + '"jobs": {}\n',
    'jobs: {}\n',
):
    assert exporter.stale_linux_client(acceptance, invalid), invalid
for command in ("xmake build -y X", "  cmake --build b", "cargo build", "ninja -C build", "g++ x.cpp", "dotnet publish -c Release"):
    assert exporter.LINUX_COMPILE_RE.search(command), command
for prose in ("- name: Make sure the cache is warm", 'echo "run cmake later"', "python CO-BETA/ci/test-make-latest-json.py"):
    assert not exporter.LINUX_COMPILE_RE.search(prose), prose
for workflow in sorted(workflows.glob("*.yml")):
    if workflow.stem in exporter.LINUX_COMPILE_ALLOWED:
        continue
    assert not exporter.unguarded_linux_compile(workflow.read_text(encoding="utf-8")), workflow.name
text = 'on:\n  push:\n    branches: ["ci/**"]\n'
written = []
exporter.git = lambda *args, **kw: text if args[0] == "cat-file" else (written.append(kw.get("input")), "0" * 40 + "\n")[1]
out = exporter.disabled("100644 " + "0" * 40 + "\t.github/workflows/co-ui-ci.yml")
assert out.startswith("100644 ") and out.endswith("\t.github/workflows/co-ui-ci.yml")
assert 'branches: ["ci-skipped/**"]' in written[0] and "ci/**" not in written[0]
assert not exporter.exported(".github/public-ci/sync/public-ci-sync.yml")
assert not exporter.exported(".github/public-ci/release/co-beta-publish-release.yml")
text = "steps:\n  - name: x\n    if: true # linux-compile\n"
written = []
out = exporter.compile_gated("100644 " + "0" * 40 + "\t.github/workflows/co-beta-ci.yml")
assert out.startswith("100644 ") and out.endswith("\t.github/workflows/co-beta-ci.yml")
assert "if: false # linux-compile" in written[0] and "if: true # linux-compile" not in written[0]
text = "steps:\n  - name: x\n    if: true\n"
written = []
entry = "100644 " + "0" * 40 + "\t.github/workflows/co-beta-ci.yml"
assert exporter.compile_gated(entry) == entry and written == []
for name in ("co-beta-ci.yml", "co-ui-ci.yml"):
    text = (workflows / name).read_text(encoding="utf-8")
    assert "linux-compile" not in text, name
    assert not exporter.unguarded_linux_compile(text), name
for private in ("log.md", "AUDIT-2026-11-08.md", "sub/AUDIT-x.md", "feat-PAUSE-POINT.md", "a/b/Bench.blend1"):
    assert not exporter.exported(private), private
assert exporter.exported("CHANGELOG.md")
for workflow in workflows.glob("*.yml"):
    assert "WINDOWS_RUNTIME_SMOKE" not in workflow.read_text(encoding="utf-8"), workflow.name
report = (root / ".github/actions/co-report/action.yml").read_text(encoding="utf-8")
assert "allow-skipped" in report and "ALLOW_SKIPPED" in report
assert 'and .value.result != "skipped")' not in report
for workflow in workflows.glob("*.yml"):
    text = workflow.read_text(encoding="utf-8")
    if "co-report" not in text:
        continue
    conditions = re.findall(r"^    if: (.*)$", text, re.M)
    assert all(c.strip() == "always()" for c in conditions) or "allow-skipped" in text, workflow.name
assert "WINDOWS_ONLY" not in (root / ".github/public-ci/sync/public-ci-sync.yml").read_text(encoding="utf-8")
assert "WINDOWS_ONLY" not in (root / "scripts/export-public-ci.py").read_text(encoding="utf-8")
for gate in ("co-beta-publish-release.yml", "co-server-publish-host.yml"):
    path = root / ".github/public-ci/release" / gate
    if path.exists():
        text = path.read_text(encoding="utf-8")
        assert r'\(.state) \(.context) \(.description)' in text, gate
        assert 'grep -qx "success $context passed"' in text, gate
        assert 'grep -qx "success $context"' not in text, gate
scoped_workflow = "jobs:\n  ui:\n    runs-on: windows-latest\n  addictol-msvc:\n    runs-on: windows-latest\n  report:\n    needs: [ui, addictol-msvc, tests]\n"
plugin_only = ["CO-BETA/plugin/src/F4TPipboyUnpause.cpp"]
assert "addictol-msvc" not in exporter.drop_unaffected_jobs("co-windows-compile", scoped_workflow, plugin_only)
assert "needs: [ui, tests]" in exporter.drop_unaffected_jobs("co-windows-compile", scoped_workflow, plugin_only)
assert exporter.drop_unaffected_jobs("co-windows-compile", scoped_workflow, ["CO-BETA/ThirdParty/Addictol"]) == scoped_workflow
assert exporter.drop_unaffected_jobs("co-windows-compile", scoped_workflow, []) == scoped_workflow
last_workflow = scoped_workflow.replace("[ui, addictol-msvc, tests]", "[ui, tests, addictol-msvc]")
assert "needs: [ui, tests]" in exporter.drop_unaffected_jobs("co-windows-compile", last_workflow, plugin_only)
print("export-public-ci scope ok")
