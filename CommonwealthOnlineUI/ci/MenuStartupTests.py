import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[1] / "src/F4TPrismaUI_Startup.cpp").read_text()
    source = source.replace('#include "F4TPrismaUIInternal.h"', "")
    harness = Path(__file__).with_suffix(".cpp").read_text()
    harness = harness.replace('#include "F4TPrismaUI_Startup.cpp"', source)
    with tempfile.TemporaryDirectory(prefix="co-menu-startup-") as temporary:
        directory = Path(temporary)
        cpp, binary = directory / "startup.cpp", directory / "startup.exe"
        cpp.write_text(harness)
        subprocess.run([args.compiler, "-std=c++20", "-UNDEBUG", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Menu startup: production readiness gate, hidden warmup, both callback orders, bind-once and legacy fallback PASS")


if __name__ == "__main__":
    main()
