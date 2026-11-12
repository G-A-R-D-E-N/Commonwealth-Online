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
    compiler = shutil.which(args.compiler)
    if os.name != "nt" and not compiler:
        raise RuntimeError(f"Compiler unavailable: {args.compiler}")
    source = (Path(__file__).resolve().parents[1] / "src/F4TMenuInput.cpp").read_text(encoding="utf-8")
    start = source.index("\t\t[[nodiscard]] float NormalizeThumbAxis(")
    end = source.index("\n\t\tvoid NotifyGamepadActivity()", start)
    harness = "\n".join([
        "#include <algorithm>",
        "#include <cassert>",
        "#include <cmath>",
        "#include <cstdint>",
        "#include <limits>",
        "",
    ]) + source[start:end] + """
int main() {
    for (std::int16_t deadzone : {0, 7849, 8689}) {
        assert(NormalizeThumbAxis(std::numeric_limits<std::int16_t>::min(), deadzone) == -1.f);
        assert(NormalizeThumbAxis(std::numeric_limits<std::int16_t>::max(), deadzone) == 1.f);
        assert(NormalizeThumbAxis(0, deadzone) == 0.f);
        float previous = -1.f;
        for (int raw = -32768; raw <= 32767; ++raw) {
            const auto value = NormalizeThumbAxis(static_cast<std::int16_t>(raw), deadzone);
            assert(std::isfinite(value) && value >= -1.f && value <= 1.f);
            assert(value >= previous);
            if (std::abs(raw) <= deadzone) assert(value == 0.f);
            if (raw < -deadzone) assert(value < 0.f);
            if (raw > deadzone) assert(value > 0.f);
            previous = value;
        }
    }
}
"""
    with tempfile.TemporaryDirectory(prefix="co-menu-axis-") as temporary:
        directory = Path(temporary)
        cpp = directory / "axis.cpp"
        binary = directory / "axis.exe"
        cpp.write_text(harness, encoding="utf-8")
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
            subprocess.run([compiler, "-std=c++20", "-UNDEBUG", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Menu stick axis: full signed range, deadzones and monotonicity PASS")


if __name__ == "__main__":
    main()
