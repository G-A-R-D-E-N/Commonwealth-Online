from pathlib import Path
import re
import subprocess
import tempfile


root = Path(__file__).resolve().parents[3]
source = root / "CO-BETA/plugin/lib/commonlibf4"
relative = "CO-BETA/plugin/lib/commonlibf4"


def git(directory, *args):
    return subprocess.run(
        ["git", "-c", "protocol.file.allow=always", "-C", str(directory), *args],
        check=True, text=True, capture_output=True,
    ).stdout.strip()


with tempfile.TemporaryDirectory(prefix="co-commonlib-checkout-") as temporary:
    checkout = Path(temporary)
    git(checkout, "init")
    (checkout / ".gitmodules").write_text(
        f'[submodule "{relative}"]\n\tpath = {relative}\n\turl = {source.as_posix()}\n',
        encoding="utf-8",
    )
    git(checkout, "add", ".gitmodules")
    revision = git(source, "rev-parse", "HEAD")
    git(checkout, "update-index", "--add", "--cacheinfo", f"160000,{revision},{relative}")
    git(checkout, "submodule", "update", "--init", "--", relative)
    library = checkout / relative
    git(library, "config", "submodule.lib/commonlib-shared.url",
        (source / "lib/commonlib-shared").as_posix())
    git(checkout, "submodule", "update", "--init", "--recursive", "--", relative)
    build_files = [library / "xmake.lua", library / "lib/commonlib-shared/xmake.lua"]
    for build_file in build_files:
        build_file.unlink()
    git(checkout, "submodule", "update", "--init", "--recursive", "--", relative)
    assert all(not build_file.exists() for build_file in build_files)
    print("Reproduced: same-revision submodule update leaves both build files missing")

    for name, count in (("co-beta-ci.yml", 6), ("co-beta-gns-client-bridge.yml", 4), ("co-native-ci.yml", 2)):
        workflow = (root / ".github/workflows" / name).read_text(encoding="utf-8")
        updates = re.findall(r"submodule update ([^\n|]+)", workflow)
        updates = [update for update in updates if relative in update]
        assert len(updates) == count
        for update in updates:
            git(checkout, "submodule", "update", *update.split())
            assert all(build_file.is_file() for build_file in build_files), name
            assert git(library, "rev-parse", "HEAD") == revision
            for build_file in build_files:
                build_file.unlink()
    print("Passed: all CI initialization paths restore the pinned build files")
