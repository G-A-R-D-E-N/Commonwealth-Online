import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default=shutil.which("cmake"))
args = parser.parse_args()
if not args.cmake:
    parser.error("cmake is required")
root = Path(__file__).resolve().parents[1]
source = (root / "src/F4TAlertSystem.cpp").read_text(encoding="utf-8")
sections = [
    ("\t\tvoid FlushPending()", "\n\t\tvoid FinishDomReady("),
    ("\tvoid Shutdown()", "\n\tvoid SetMenuInteractionActive("),
]
body = "\n".join(source[source.index(start):source.index(end, source.index(start))] for start, end in sections)
with tempfile.TemporaryDirectory(prefix="co-alert-lifecycle-") as temporary:
    build = Path(temporary)
    (build / "alert-source.inc").write_text(body, encoding="utf-8")
    shutil.copyfile(root / "scripts/alert-lifecycle-tests.cpp", build / "test.cpp")
    (build / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(AlertLifecycle LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(alert-lifecycle test.cpp)
target_compile_features(alert-lifecycle PRIVATE cxx_std_20)
target_link_libraries(alert-lifecycle PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(alert-lifecycle PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(alert-lifecycle PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    subprocess.run([args.cmake, "-S", str(build), "-B", str(build / "build")], check=True)
    subprocess.run([args.cmake, "--build", str(build / "build"), "--config", "Debug"], check=True)
    exe = next(p for p in (build / "build").rglob("alert-lifecycle*") if p.name in ("alert-lifecycle", "alert-lifecycle.exe") and p.is_file())
    subprocess.run([str(exe)], check=True, timeout=15)
print("Alert lifecycle: pending dismissal, replacement and concurrent flush passed")
