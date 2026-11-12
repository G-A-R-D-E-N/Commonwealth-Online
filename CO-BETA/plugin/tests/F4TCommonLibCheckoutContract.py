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


def read_workflow(name):
    for folder in (".github/public-ci/workflows", ".github/workflows"):
        if (root / folder / name).is_file():
            return (root / folder / name).read_text(encoding="utf-8")
    raise FileNotFoundError(f"workflow not found: {name}")


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

    action = (root / ".github/actions/co-commonlib/action.yml").read_text(encoding="utf-8")
    fallbacks = re.findall(r"submodule update ([^\n|]+)", action)
    assert len(fallbacks) == 2, (
        "the shared CommonLib action must keep the primary and mirror submodule update, found "
        + str(len(fallbacks))
    )
    assert "$SUBMODULE" in action, "the shared CommonLib action must take the submodule path as an input"
    for name, count in (("co-beta-ci.yml", 2), ("co-native-ci.yml", 1), ("co-windows-compile.yml", 1)):
        workflow = read_workflow(name)
        uses = workflow.count("./.github/actions/co-commonlib")
        assert uses == count, (
            f"{name} must initialize CommonLib through the shared action {count} time(s), found {uses}"
        )
        assert workflow.count(f"path: {relative}") >= count, (
            f"{name} must point the shared CommonLib action at {relative}"
        )
    for _ in fallbacks:
        git(checkout, "submodule", "update", "--init", "--recursive", "--force", "--checkout", "--", relative)
        assert all(build_file.is_file() for build_file in build_files), "shared action update path"
        assert git(library, "rev-parse", "HEAD") == revision
        for build_file in build_files:
            build_file.unlink()
    print("Passed: all CI initialization paths restore the pinned build files")
