from pathlib import Path
import importlib.util
import os
import tempfile
from unittest.mock import patch


root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "provision", root / "scripts/provision-matrix-ci-toolchain.py"
)
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)

with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    provision.INSTALL_ROOT = base
    destination = base / "installed"
    destination.mkdir()
    (destination / "old").write_text("preserve")
    staged = base / "staged"
    staged.mkdir()
    try:
        provision.install_tree(staged, Path("/usr/local/xwin"), ("required",))
    except RuntimeError as error:
        assert "Unsafe installation destination" in str(error)
    else:
        raise AssertionError("an installation outside the allowed root must be rejected")
    link, outside = base / "link", base.parent / "outside-toolchain"
    real_realpath = os.path.realpath
    def realpath(path, *args, **kwargs):
        return str(outside) if Path(path) == link else real_realpath(path, *args, **kwargs)
    with patch.object(provision.os.path, "realpath", realpath):
        try:
            provision.install_tree(staged, link / "installed", ("required",))
        except RuntimeError as error:
            assert "Unsafe installation destination" in str(error)
        else:
            raise AssertionError("a parent that resolves outside the allowed root must be rejected")
    assert not link.exists()
    try:
        provision.install_tree(staged, destination, ("required",))
    except RuntimeError:
        pass
    else:
        raise AssertionError("an incomplete toolchain must not replace the installed tree")
    assert (destination / "old").read_text() == "preserve"
    (staged / "required").write_text("complete")
    provision.install_tree(staged, destination, ("required",))
    assert (destination / "required").read_text() == "complete"
    assert not (destination / "old").exists()

    (staged / "required").write_text("replacement")
    original_rename = Path.rename
    def rename(path, target):
        if path.name == "new":
            raise OSError("simulated replacement failure")
        return original_rename(path, target)

    with patch.object(Path, "rename", rename):
        try:
            provision.install_tree(staged, destination, ("required",))
        except OSError:
            pass
        else:
            raise AssertionError("the replacement failure must be reported")
    assert (destination / "required").read_text() == "complete"
    assert not list(base.glob(".installed-*"))

    sdk = base / "sdk"
    packages = base / "packages"
    for name in provision.SDK_PATHS:
        (sdk / name).mkdir(parents=True)
    for pattern in provision.STAGED_PACKAGES:
        library = packages / pattern.replace("*", "staged")
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_text("library")
    original_is_file = Path.is_file
    for available, version in ((False, "libprotoc 3.21.12"), (True, "libprotoc 3.20.0"), (True, "libprotoc 3.21.12")):
        def is_file(path):
            if path == Path("/usr/bin/protoc"):
                return available
            if path in (Path("/usr/include/openssl/ssl.h"), Path("/usr/include/openssl/rand.h")):
                return True
            return original_is_file(path)

        with patch.object(provision.sys, "platform", "linux"), patch.object(provision.shutil, "which", return_value="/bin/tool"), patch.object(Path, "is_file", is_file), patch.object(provision, "output", return_value=version):
            try:
                provision.verify_preflight(sdk, packages)
            except RuntimeError as error:
                assert "/usr/bin/protoc" in str(error)
                assert not available or version != "libprotoc 3.21.12"
            else:
                assert available and version == "libprotoc 3.21.12", "preflight must require the absolute pinned protoc"

    (staged / "required").unlink()
    (staged / "required").mkdir()
    assert not provision.complete(staged, ("required",)), "a required file cannot be a directory"
    assert provision.complete(staged, ("required",), directories=True)
    (staged / "required").rmdir()
    (staged / "required").write_text("file")
    assert not provision.complete(staged, ("required",), directories=True), "an SDK directory cannot be a file"

print("Runner toolchain replacement, rollback, required layout types and pinned protoc passed")
