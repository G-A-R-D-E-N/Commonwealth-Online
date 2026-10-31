import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
INSTALL_ROOT = Path("/opt")
XWIN_VERSION = "0.10.0"
XWIN_SHA256 = "d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88"
SPDLOG_VERSION = "v1.16.0"
SPDLOG_COMMIT = "486b55554f11c9cccc913e11a87085b2a91f706f"
SAFETYHOOK_VERSION = "v0.7.0"
SAFETYHOOK_COMMIT = "19223663fb8a573253ffb2e82da87cc354bf5c16"
NLOHMANN_JSON_VERSION = "v3.12.0"
NLOHMANN_JSON_COMMIT = "55f93686c01528224f448c19128836e7df245f72"
SAFETYHOOK_LAYOUT = ("lib/safetyhook.lib", "lib/Zydis.lib", "lib/Zycore.lib", "include/safetyhook.hpp")
STAGED_PACKAGES = ("s/spdlog/*/*/lib/spdlog.lib", "s/safetyhook/*/*/lib/safetyhook.lib", "z/zydis/*/*/lib/Zydis.lib",
                   "z/zycore-c/*/*/lib/Zycore.lib", "n/nlohmann_json/*/*/include/nlohmann/json.hpp")
CROSS_TOOLS = ("clang-cl", "lld-link", "llvm-ar", "llvm-lib", "llvm-rc", "llvm-mt", "llvm-dlltool", "llvm-readobj", "llvm-nm")
SDK_PATHS = ("crt/include", "crt/lib/x86_64", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared", "sdk/include/winrt", "sdk/lib/ucrt/x86_64", "sdk/lib/um/x86_64")


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def output(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True).strip()


def complete(tree, required, directories=False):
    return all((tree / name).is_dir() if directories else (tree / name).is_file() for name in required)


def install_tree(staged, destination, required, directories=False):
    destination = Path(os.path.abspath(destination))
    real_root = Path(os.path.realpath(INSTALL_ROOT))
    if len(destination.parents) < 2 or destination.is_symlink() or not destination.is_relative_to(INSTALL_ROOT) or not Path(os.path.realpath(destination.parent)).is_relative_to(real_root):
        raise RuntimeError(f"Unsafe installation destination: {destination}")
    if not complete(staged, required, directories):
        raise RuntimeError(f"Incomplete staged toolchain: {staged}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix=f".{destination.name}-", dir=destination.parent))
    cleanup = True
    try:
        replacement, backup = scratch / "new", scratch / "old"
        shutil.copytree(staged, replacement, symlinks=True)
        if destination.exists():
            destination.rename(backup)
            cleanup = False
        try:
            replacement.rename(destination)
        except OSError:
            if backup.exists():
                backup.rename(destination)
            cleanup = True
            raise
        cleanup = True
    finally:
        if cleanup:
            shutil.rmtree(scratch)


def pick_llvm_major():
    configured = os.environ.get("LLVM_MAJOR")
    if configured:
        major = int(configured)
        if major < 19:
            raise RuntimeError("LLVM_MAJOR must be at least 19")
        return major
    for major in (22, 21, 20, 19):
        packages = (f"clang-{major}", f"clang-tools-{major}", f"lld-{major}", f"llvm-{major}")
        if all(re.search(r"Candidate:\s+(?!\(none\))\S+", output("apt-cache", "policy", package)) for package in packages):
            return major
    raise RuntimeError("No LLVM 19+ apt candidate; configure the distro LLVM repository first")


def link_llvm_tools(major):
    for tool in (*CROSS_TOOLS, "clang", "clang++"):
        if tool in ("clang", "clang++") and shutil.which(tool):
            continue
        source = Path(f"/usr/lib/llvm-{major}/bin") / tool
        if not source.is_file():
            raise RuntimeError(f"Missing LLVM tool: {source}")
        destination = Path("/usr/local/bin") / tool
        if destination.is_symlink():
            destination.unlink()
        elif destination.exists():
            raise RuntimeError(f"Refusing to replace a non-symlink tool: {destination}")
        destination.symlink_to(source)


def install_xwin(work):
    if shutil.which("xwin") and output("xwin", "--version") == f"xwin {XWIN_VERSION}":
        return
    asset = f"xwin-{XWIN_VERSION}-x86_64-unknown-linux-musl"
    archive = work / "xwin.tar.gz"
    urllib.request.urlretrieve(f"https://github.com/Jake-Shadle/xwin/releases/download/{XWIN_VERSION}/{asset}.tar.gz", archive)
    with archive.open("rb") as downloaded:
        digest = hashlib.file_digest(downloaded, "sha256").hexdigest()
    if digest != XWIN_SHA256:
        raise RuntimeError("xwin download checksum mismatch")
    with tarfile.open(archive) as compressed:
        compressed.extractall(work, filter="data")
    run("install", "-m", "0755", work / asset / "xwin", "/usr/local/bin/xwin")


def splat_xwin(work, sdk):
    if complete(sdk, SDK_PATHS, directories=True) and os.environ.get("FORCE_XWIN") != "1":
        return
    args = ["xwin", "--accept-license", "--manifest-version", os.environ.get("XWIN_MANIFEST_VERSION", "17"), "--cache-dir", work / "xwin-cache"]
    for variable, flag in (("XWIN_CRT_VERSION", "--crt-version"), ("XWIN_SDK_VERSION", "--sdk-version")):
        if os.environ.get(variable):
            args.extend((flag, os.environ[variable]))
    run(*args, "splat", "--output", work / "splat")
    install_tree(work / "splat", sdk, SDK_PATHS, directories=True)
    run("chmod", "-R", "a+rX", sdk)


def check_stl_clang_floor(sdk):
    header = next((path for path in (sdk / "crt/include").iterdir() if path.name.lower() == "yvals_core.h"), None)
    if header is None:
        raise RuntimeError("Missing MSVC yvals_core.h")
    required = max((int(value) for value in re.findall(r"expected Clang (\d+)", header.read_text(errors="replace"))), default=0)
    match = re.search(r"clang version (\d+)", output("clang-cl", "--version"))
    if match is None or int(match[1]) < required:
        raise RuntimeError(f"The MSVC STL requires Clang {required}+")


def clone(url, tag, commit, destination):
    run("git", "clone", "--depth", "1", "--branch", tag, url, destination)
    if output("git", "-C", destination, "rev-parse", "HEAD") != commit:
        raise RuntimeError(f"{url} {tag} does not resolve to its pinned commit")


def cross_configure(source, build, sdk):
    return ("cmake", "-S", source, "-B", build, "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'CO-BETA/ci/toolchains/xwin-clang-cl.cmake'}",
            f"-DXWIN_ROOT={sdk}", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")


def stage_spdlog(work, sdk, packages):
    destination = packages / "s/spdlog" / SPDLOG_VERSION / "xwin-md"
    required = ("lib/spdlog.lib", "include/spdlog/spdlog.h")
    if complete(destination, required) and os.environ.get("FORCE_SPDLOG") != "1":
        return
    source, build, staged = work / "spdlog", work / "spdlog-build", work / "spdlog-install"
    clone("https://github.com/gabime/spdlog.git", SPDLOG_VERSION, SPDLOG_COMMIT, source)
    run(*cross_configure(source, build, sdk), "-DSPDLOG_BUILD_SHARED=OFF",
        "-DSPDLOG_BUILD_EXAMPLE=OFF", "-DSPDLOG_BUILD_TESTS=OFF", "-DSPDLOG_USE_STD_FORMAT=ON", "-DSPDLOG_WCHAR_SUPPORT=ON")
    run("cmake", "--build", build, "--parallel")
    run("cmake", "--install", build, "--prefix", staged)
    install_tree(staged, destination, required)
    run("chmod", "-R", "a+rX", destination)


def stage_safetyhook(work, sdk, packages):
    destinations = (packages / "s/safetyhook" / SAFETYHOOK_VERSION / "xwin-md", packages / "z/zydis/v4.1.0/xwin-md",
                    packages / "z/zycore-c/v1.5.0/xwin-md")
    if all(complete(destination, SAFETYHOOK_LAYOUT) for destination in destinations):
        return
    source, build, staged = work / "safetyhook", work / "safetyhook-build", work / "safetyhook-install"
    clone("https://github.com/cursey/safetyhook.git", SAFETYHOOK_VERSION, SAFETYHOOK_COMMIT, source)
    run(*cross_configure(source, build, sdk), "-DSAFETYHOOK_FETCH_ZYDIS=ON", "-DCMKR_SKIP_GENERATION=ON")
    run("cmake", "--build", build, "--parallel")
    run("cmake", "--install", build, "--prefix", staged)
    # Zydis is fetched EXCLUDE_FROM_ALL, so safetyhook's install leaves its libraries in the build tree.
    for library in ("Zydis.lib", "Zycore.lib"):
        found = sorted(build.rglob(library))
        if not found:
            raise RuntimeError(f"safetyhook build produced no {library}")
        shutil.copy2(found[0], staged / "lib" / library)
    for destination in destinations:
        install_tree(staged, destination, SAFETYHOOK_LAYOUT)
        run("chmod", "-R", "a+rX", destination)


def stage_nlohmann_json(work, packages):
    destination = packages / "n/nlohmann_json" / NLOHMANN_JSON_VERSION / "headers"
    required = ("include/nlohmann/json.hpp",)
    if complete(destination, required):
        return
    source, staged = work / "nlohmann_json", work / "nlohmann_json-install"
    clone("https://github.com/nlohmann/json.git", NLOHMANN_JSON_VERSION, NLOHMANN_JSON_COMMIT, source)
    shutil.copytree(source / "include", staged / "include")
    install_tree(staged, destination, required)
    run("chmod", "-R", "a+rX", destination)


def verify_preflight(sdk, packages):
    if sys.platform == "win32":
        required = ("git", "python", "node", "xmake", "cmake", "ninja", "clang-cl", "lld-link")
        missing = [tool for tool in required if shutil.which(tool) is None]
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if not vswhere.is_file() or not output(vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"):
            missing.append("Visual Studio C++ x64 tools")
    else:
        required = ("git", "cmake", "ninja", "protoc", "clang", "c++", "readelf", "ldd", "ss", "file", "node", "python3", "gh", "xwin", *CROSS_TOOLS)
        missing = [tool for tool in required if shutil.which(tool) is None]
        missing += [str(path) for path in (Path("/usr/include/openssl/ssl.h"), Path("/usr/include/openssl/rand.h")) if not path.is_file()]
        if not complete(sdk, SDK_PATHS, directories=True):
            missing.append(f"CRT/SDK under {sdk}")
        missing += [f"{pattern} under {packages}" for pattern in STAGED_PACKAGES if not list(packages.glob(pattern))]
        if not Path("/usr/bin/protoc").is_file() or output("/usr/bin/protoc", "--version") != "libprotoc 3.21.12":
            missing.append("/usr/bin/protoc version libprotoc 3.21.12")
    if missing:
        raise RuntimeError("Missing prerequisites: " + ", ".join(missing))
    print("Toolchain preflight passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--provision", action="store_true", help="Install the Linux cross-build toolchain")
    parser.add_argument("--accept-microsoft-license", action="store_true")
    args = parser.parse_args()
    if sys.version_info < (3, 12):
        parser.error("Python 3.12+ required")
    sdk =Path(os.environ.get("XWIN_ROOT", "/opt/xwin"))
    packages = Path(os.environ.get("PRISMA_WIN_PACKAGES", "/opt/prisma-toolchain/win-packages"))
    if args.provision:
        if sys.platform != "linux" or os.geteuid() != 0 or shutil.which("apt-get") is None:
            parser.error("Provisioning requires a Debian/Ubuntu Linux host running as root")
        if not args.accept_microsoft_license:
            parser.error("Provisioning requires --accept-microsoft-license for the CRT/SDK download")
        run("apt-get", "update", "-qq")
        major = pick_llvm_major()
        env = os.environ.copy()
        env["DEBIAN_FRONTEND"] = "noninteractive"
        subprocess.run(["apt-get", "install", "-y", "--no-install-recommends", "ca-certificates", "curl", "git", "file", "tar", "gzip", "cmake", "ninja-build", "pkg-config", "g++", "binutils", "libc-bin", "iproute2", "protobuf-compiler", "libprotobuf-dev", "libssl-dev", "nodejs", "python3", "gh", f"clang-{major}", f"clang-tools-{major}", f"lld-{major}", f"llvm-{major}"], check=True, env=env)
        link_llvm_tools(major)
        with tempfile.TemporaryDirectory(prefix="matrix-toolchain-") as temporary:
            work = Path(temporary)
            install_xwin(work)
            splat_xwin(work, sdk)
            check_stl_clang_floor(sdk)
            stage_spdlog(work, sdk, packages)
            stage_safetyhook(work, sdk, packages)
            stage_nlohmann_json(work, packages)
    verify_preflight(sdk, packages)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
