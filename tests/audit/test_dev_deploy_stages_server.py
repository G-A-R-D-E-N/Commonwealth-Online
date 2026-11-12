import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEPLOY_ALL = ROOT / "CO-BETA" / "deploy-all.bat"
STAGE_MOD = ROOT / "CO-BETA" / "stage-mod.bat"
DEPLOY_SERVER = ROOT / "CO-BETA" / "tools" / "deploy-server.ps1"
SERVER_NAME = "CommonwealthOnline.Server.exe"
HOST_FILES = {
    "commonwealth-server.json": b'{"server_name": "Host"}\n',
    "workshop-zones.json": b'{"zones": ["host"]}\n',
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def powershell() -> str | None:
    return shutil.which("pwsh") or shutil.which("powershell")


def write_package(source: Path, config: bytes) -> None:
    source.mkdir(parents=True)
    (source / SERVER_NAME).write_bytes(b"server")
    (source / "commonwealth-server.json").write_bytes(config)
    (source / "workshop-zones.json").write_bytes(b'{"zones": ["package"]}\n')


def run_deploy(source: Path, target: Path, profile: Path) -> subprocess.CompletedProcess:
    shell = powershell()
    assert shell, "PowerShell is required to run the deploy script"
    env = dict(os.environ)
    env["USERPROFILE"] = str(profile)
    return subprocess.run(
        [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(DEPLOY_SERVER),
         "-DataDir", str(target), "-Package", str(source)],
        capture_output=True, text=True, timeout=120, env=env,
    )


def host_store(profile: Path) -> Path:
    return profile / "Documents" / "My Games" / "Fallout4" / "CommonwealthOnline" / "host-server-folder.json"


def open_exclusive(path: Path):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = ctypes.c_void_p
    handle = kernel32.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)
    if handle in (None, 0, 0xFFFFFFFFFFFFFFFF):
        raise OSError(ctypes.get_last_error(), "CreateFileW")
    return kernel32, handle


def close_exclusive(opened) -> None:
    kernel32, handle = opened
    kernel32.CloseHandle(ctypes.c_void_p(handle))


def test_the_dev_deploy_copies_a_server_package_after_the_staged_data() -> None:
    script = read(DEPLOY_ALL)
    staged = script.index("tools\\deploy-staged-data.ps1")
    server = script.index("tools\\deploy-server.ps1")
    assert staged < server, "the native server package deploys after the staged Data"
    assert '-DataDir "%DATA_DIR%"' in script[server:], (
        "the server deploy must target the game Data folder"
    )
    assert "server\\  (when a server package was found)" in script, (
        "the deploy summary must not claim a server that was not copied"
    )


def test_the_server_deploy_keeps_the_package_and_the_hosts_settings() -> None:
    script = read(DEPLOY_SERVER)
    assert SERVER_NAME in script, "the deploy must refuse a package folder that has no server executable"
    assert "CO_NATIVE_SERVER_PACKAGE" in script, "a dev must be able to point the deploy at a built package"
    assert "CO-SERVER\\out\\native-win-x64" in script, "the packaged native server build is the default source"
    assert "Expand-Archive" in script, "the launcher-style server zip is accepted as well as a folder"
    assert "} finally {" in script and "Remove-Item -LiteralPath $script:expandedPackage" in script, (
        "an expanded zip must be cleaned up even when the copy fails"
    )
    for name in HOST_FILES:
        assert name in script, "a redeploy must keep the host's own file: " + name
    absent = script[script.index("Write-Warning"):]
    assert "exit 0" in absent, "a missing package is a warning, so plugin-only deploys still work"


def test_the_host_files_are_never_copied_over() -> None:
    script = read(DEPLOY_SERVER)
    assert "$hostStateNames" in script, "the deploy must name every host-owned file it protects"
    for name in ("commonwealth-server.json", "workshop-zones.json", "blocked-words.txt", "bans.json", "durable-state.json"):
        assert name in script, "a redeploy must keep the host's own file: " + name
    assert "if (Test-Path -LiteralPath $destination) {" in script, (
        "the package copy must decide per file before it overwrites anything"
    )
    assert "$relative.StartsWith($name" in script, "a protected folder keeps everything beneath it"
    assert "Where-Object { $hostOwned -notcontains $_.Name }" not in script
    assert script.index("New-HostStatePlan $previousFolder $target") < script.index("Copy-Item -LiteralPath $_.FullName"), (
        "conflicts must be known before the destination changes"
    )
    assert script.index("Set-HostState $plan.Files $targetRoot") < script.index("Reset-HostServerFolder $target"), (
        "the host's state must land in the new folder before Server settings points there"
    )
    assert "WriteAllBytes" not in script, (
        "a failed copy must not depend on an in-memory restore that never runs"
    )


def test_a_failed_copy_leaves_the_hosts_configuration_untouched() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')
        (source / "z-locked.dll").write_bytes(b"package payload")
        server = target / "server"
        server.mkdir(parents=True)
        for name, content in HOST_FILES.items():
            (server / name).write_bytes(content)
        locked = server / "z-locked.dll"
        locked.write_bytes(b"host payload")

        profile = Path(root) / "profile"
        opened = open_exclusive(locked)
        try:
            result = run_deploy(source, target, profile)
        finally:
            close_exclusive(opened)

        assert result.returncode != 0, "a locked destination must fail the deploy: " + result.stdout + result.stderr
        for name, content in HOST_FILES.items():
            assert (server / name).read_bytes() == content, (
                "a failed redeploy replaced the host's " + name
            )
        assert not host_store(profile).is_file(), (
            "a failed redeploy must leave the folder Server settings saved alone"
        )


def test_a_first_deploy_uses_the_package_configuration() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        profile = Path(root) / "profile"
        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        server = target / "server"
        assert (server / SERVER_NAME).is_file(), "the package executable must land in Data\\server"
        assert (server / "commonwealth-server.json").read_bytes() == b'{"server_name": "Package"}\n'

        store = json.loads(host_store(profile).read_text(encoding="utf-8"))
        assert os.path.normcase(store["serverDirectory"]) == os.path.normcase(str(server)), (
            "a fresh deploy must point Server settings at the folder hosting will start"
        )
        assert store.get("movedByDeploy") is True, (
            "the panel must know a deploy moved the folder, not the player"
        )


def test_a_successful_deploy_resets_the_saved_host_folder() -> None:
    script = read(DEPLOY_SERVER)
    assert "host-server-folder.json" in script, (
        "a fresh package must reset the folder the panel saved, or the old choice keeps hosting"
    )
    assert "Documents\\My Games\\Fallout4\\CommonwealthOnline" in script, (
        "the reset must write the store the panel reads"
    )
    assert script.index("Copy-Item -LiteralPath $_.FullName") < script.index("Reset-HostServerFolder $target"), (
        "the saved folder is reset only after the package landed"
    )


def test_the_release_staging_does_not_gain_the_server_folder() -> None:
    stage_mod = read(STAGE_MOD)
    assert "deploy-server" not in stage_mod, (
        "only the dev deploy adds Data\\server; the shipped mod archive layout stays as it is"
    )
    assert SERVER_NAME not in stage_mod


def test_a_deploy_carries_the_hosts_settings_into_the_new_folder() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        previous = Path(root) / "OldServer"
        previous.mkdir()
        (previous / SERVER_NAME).write_bytes(b"server")
        host_config = b'{"server_name": "Host", "serverPassword": "hunter2", "maxPlayers": 6}\n'
        (previous / "commonwealth-server.json").write_bytes(host_config)
        (previous / "workshop-zones.json").write_bytes(b'{"zones": ["host"]}\n')

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")

        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        server = target / "server"
        assert (server / "commonwealth-server.json").read_bytes() == host_config, (
            "a deploy must move the host's settings, not start the package's defaults"
        )
        assert (server / "workshop-zones.json").read_bytes() == b'{"zones": ["host"]}\n', (
            "a deploy must move the host's workshop zones too"
        )
        saved = json.loads(store.read_text(encoding="utf-8"))
        assert os.path.normcase(saved["serverDirectory"]) == os.path.normcase(str(server)), (
            "after moving the settings, Server settings must point at the folder hosting starts"
        )
        assert saved.get("movedByDeploy") is True


def test_a_deploy_that_cannot_move_the_settings_keeps_the_saved_folder() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        previous = Path(root) / "OldServer"
        previous.mkdir()
        (previous / SERVER_NAME).write_bytes(b"server")
        config = previous / "commonwealth-server.json"
        config.write_bytes(b'{"server_name": "Host"}\n')

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")

        opened = open_exclusive(config)
        try:
            result = run_deploy(source, target, profile)
        finally:
            close_exclusive(opened)

        assert result.returncode == 0, result.stdout + result.stderr
        saved = json.loads(store.read_text(encoding="utf-8"))
        assert os.path.normcase(saved["serverDirectory"]) == os.path.normcase(str(previous)), (
            "switching folders without the host's settings would silently reset them"
        )
        assert saved.get("movedByDeploy") is not True


def test_a_deploy_carries_the_servers_state_and_relative_files() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        previous = Path(root) / "OldServer"
        (previous / "mods").mkdir(parents=True)
        (previous / SERVER_NAME).write_bytes(b"server")
        (previous / "commonwealth-server.json").write_bytes(
            b'{"server_name": "Host", "required_modpack_path": "mods/pack.zip", '
            b'"modpack_download_dir": "downloads"}\n'
        )
        (previous / "workshop-zones.json").write_bytes(b'{"zones": ["host"]}\n')
        (previous / "durable-state.json").write_bytes(b'{"players": {"ann": {}}}\n')
        (previous / "durable-state.json.bak").write_bytes(b'{"players": {"old": {}}}\n')
        (previous / "bans.json").write_bytes(b'{"banned_ips": ["203.0.113.9"]}\n')
        (previous / "world-policy.json").write_bytes(b'{"stories": ["intro"]}\n')
        (previous / "mods" / "pack.zip").write_bytes(b"modpack")
        (previous / "downloads").mkdir()
        (previous / "downloads" / "client-file.zip").write_bytes(b"downloadable")

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")

        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        server = target / "server"
        assert (server / "durable-state.json").read_bytes() == b'{"players": {"ann": {}}}\n', (
            "switching folders must not start the next launch with empty player state"
        )
        assert (server / "durable-state.json.bak").read_bytes() == b'{"players": {"old": {}}}\n'
        assert (server / "bans.json").read_bytes() == b'{"banned_ips": ["203.0.113.9"]}\n', (
            "the host's bans must move with the server"
        )
        assert (server / "world-policy.json").read_bytes() == b'{"stories": ["intro"]}\n', (
            "a relative world policy must move or free-roam refuses to start"
        )
        assert (server / "mods" / "pack.zip").read_bytes() == b"modpack", (
            "a relative path configured in the file must move with it"
        )
        assert (server / "downloads" / "client-file.zip").read_bytes() == b"downloadable", (
            "a relative modpack download folder must move or downloads break"
        )
        saved = json.loads(store.read_text(encoding="utf-8"))
        assert os.path.normcase(saved["serverDirectory"]) == os.path.normcase(str(server))
        assert saved.get("movedByDeploy") is True


def test_a_deploy_does_not_overwrite_another_servers_state() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        server = target / "server"
        server.mkdir(parents=True)
        target_config = b'{"server_name": "Existing", "maxPlayers": 4}\n'
        target_state = b'{"players": {"bob": {}}}\n'
        (server / "commonwealth-server.json").write_bytes(target_config)
        (server / "durable-state.json").write_bytes(target_state)

        previous = Path(root) / "OldServer"
        previous.mkdir()
        (previous / SERVER_NAME).write_bytes(b"server")
        (previous / "commonwealth-server.json").write_bytes(b'{"server_name": "Saved", "maxPlayers": 12}\n')

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")

        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        assert (server / "commonwealth-server.json").read_bytes() == target_config, (
            "a deploy must not replace the target server's own configuration"
        )
        assert (server / "durable-state.json").read_bytes() == target_state, (
            "a deploy must not replace the target server's own saved state"
        )
        saved = json.loads(store.read_text(encoding="utf-8"))
        assert os.path.normcase(saved["serverDirectory"]) == os.path.normcase(str(previous)), (
            "the host stays on the folder whose state could not be moved"
        )
        assert saved.get("movedByDeploy") is not True


def test_a_relative_path_without_an_extension_is_moved() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        previous = Path(root) / "OldServer"
        previous.mkdir()
        (previous / SERVER_NAME).write_bytes(b"server")
        (previous / "commonwealth-server.json").write_bytes(
            b'{"server_name": "Host", "durable_state_path": "state.dat"}\n'
        )
        (previous / "state.dat").write_bytes(b"players")
        (previous / "state.dat.bak").write_bytes(b"backup")
        (previous / "state.dat.save-recovery").write_bytes(b"approvals")
        (previous / "state.dat.save-recovery.bak").write_bytes(b"backup approvals")

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")

        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        assert (target / "server" / "state.dat").read_bytes() == b"players", (
            "a configured relative path must move whatever its name looks like"
        )
        for name, content in (("state.dat.bak", b"backup"), ("state.dat.save-recovery", b"approvals"),
                              ("state.dat.save-recovery.bak", b"backup approvals")):
            assert (target / "server" / name).read_bytes() == content, "recovery data must follow " + name


def test_the_package_cannot_overwrite_an_existing_host_file() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')
        (source / "blocked-words.txt").write_bytes(b"package words\n")

        server = target / "server"
        server.mkdir(parents=True)
        (server / "blocked-words.txt").write_bytes(b"host words\n")

        profile = Path(root) / "profile"
        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        assert (server / "blocked-words.txt").read_bytes() == b"host words\n", (
            "a customized host file must survive the package copy"
        )


def test_an_unreadable_saved_configuration_stops_the_switch() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')

        previous = Path(root) / "OldServer"
        previous.mkdir()
        (previous / SERVER_NAME).write_bytes(b"server")
        (previous / "commonwealth-server.json").write_bytes(b'{"server_name": "Host"')

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")

        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        saved = json.loads(store.read_text(encoding="utf-8"))
        assert os.path.normcase(saved["serverDirectory"]) == os.path.normcase(str(previous)), (
            "an unreadable configuration must not be abandoned"
        )
        assert saved.get("movedByDeploy") is not True
        assert not (target / "server" / SERVER_NAME).is_file(), (
            "without knowing the host's dependencies, the deploy must not change the target"
        )


def test_a_redeploy_into_the_same_folder_keeps_configured_state() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{"server_name": "Package"}\n')
        (source / "workshop-pieces.json").write_bytes(b'{"pieces": ["package"]}\n')

        server = target / "server"
        server.mkdir(parents=True)
        (server / SERVER_NAME).write_bytes(b"server")
        (server / "commonwealth-server.json").write_bytes(
            b'{"server_name": "Host", "durable_state_path": "./workshop-pieces.json"}\n'
        )
        state = b'{"pieces": ["host"]}\n'
        (server / "workshop-pieces.json").write_bytes(state)

        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(server)}), encoding="utf-8")

        result = run_deploy(source, target, profile)

        assert result.returncode == 0, result.stdout + result.stderr
        assert (server / "workshop-pieces.json").read_bytes() == state, (
            "a redeploy into the host's own folder must not replace its configured state"
        )
        assert (server / SERVER_NAME).read_bytes() == b"server"


def test_a_redeploy_without_a_saved_folder_keeps_configured_state() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{}\n')
        (source / "workshop-pieces.json").write_bytes(b"package")
        server = target / "server"
        server.mkdir(parents=True)
        (server / "commonwealth-server.json").write_bytes(b'{"durable_state_path":"workshop-pieces.json"}')
        (server / "workshop-pieces.json").write_bytes(b"host state")
        result = run_deploy(source, target, Path(root) / "profile")
        assert result.returncode == 0, result.stdout + result.stderr
        assert (server / "workshop-pieces.json").read_bytes() == b"host state", "existing host state must survive without a saved folder"


def test_relative_state_outside_the_server_folder_is_not_migrated() -> None:
    if sys.platform != "win32" or not powershell():
        return
    with tempfile.TemporaryDirectory() as root:
        source = Path(root) / "package"
        target = Path(root) / "Data"
        write_package(source, b'{}\n')
        previous = Path(root) / "OldServer"
        previous.mkdir()
        (previous / SERVER_NAME).write_bytes(b"server")
        (previous / "commonwealth-server.json").write_bytes(b'{"durable_state_path":"../state.dat"}')
        (Path(root) / "state.dat").write_bytes(b"host state")
        profile = Path(root) / "profile"
        store = host_store(profile)
        store.parent.mkdir(parents=True)
        store.write_text(json.dumps({"serverDirectory": str(previous)}), encoding="utf-8")
        result = run_deploy(source, target, profile)
        assert result.returncode == 0, result.stdout + result.stderr
        assert not (target / "state.dat").exists(), "migration must not write outside the server folder"
        assert json.loads(store.read_text(encoding="utf-8"))["serverDirectory"] == str(previous), "an outside dependency must keep the previous folder"


def main() -> int:
    failures: list[str] = []
    tests = [(name, value) for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for name, func in tests:
        try:
            func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
