import sys
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

if sys.platform != "win32":
    raise SystemExit("Game client tests can only run on a Windows host")

CLANG = shutil.which("clang++") or "C:/Program Files/LLVM/bin/clang++.exe"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default=CLANG)
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[1] / "src/F4TPrismaUI_Startup.cpp").read_text()
    source = source.replace('#include "F4TPrismaUIInternal.h"', "")
    harness = Path(__file__).with_suffix(".cpp").read_text()
    harness = harness.replace('#include "F4TPrismaUI_Startup.cpp"', source)
    with tempfile.TemporaryDirectory(prefix="co-menu-startup-") as temporary:
        directory = Path(temporary)
        cpp, binary = directory / "startup.cpp", directory / "startup.exe"
        cpp.write_text(harness)
        if os.name == "nt":
            (directory / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\n'
                'project(UiHarness LANGUAGES CXX)\n'
                f'add_executable(ui-harness {cpp.name})\n'
                'target_compile_features(ui-harness PRIVATE cxx_std_20)\n'
                'target_compile_options(ui-harness PRIVATE /UNDEBUG)\n', encoding="utf-8")
            subprocess.run(["cmake", "-S", str(directory), "-B", str(directory / "build"), "-A", "x64"], check=True)
            subprocess.run(["cmake", "--build", str(directory / "build"), "--config", "Release"], check=True)
            binary = directory / "build" / "Release" / "ui-harness.exe"
        else:
            subprocess.run([args.compiler, "-std=c++20", "-UNDEBUG", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Menu startup: production readiness gate, hidden warmup, both callback orders, bind-once and legacy fallback PASS")


if __name__ == "__main__":
    main()
