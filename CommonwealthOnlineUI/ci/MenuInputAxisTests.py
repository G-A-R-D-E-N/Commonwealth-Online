import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
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
        subprocess.run([compiler, "-std=c++20", "-UNDEBUG", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Menu stick axis: full signed range, deadzones and monotonicity PASS")


if __name__ == "__main__":
    main()
