import sys
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

if sys.platform != "win32":
    raise SystemExit("Game client tests can only run on a Windows host")

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default=shutil.which("cmake"))
parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "src/F4TReconnect.cpp")
args = parser.parse_args()
if not args.cmake:
    parser.error("cmake is required")
source = args.source.read_text(encoding="utf-8")
start = source.index("\t\tbool CommitResume(")
end = source.index("\t\tvoid OnRejoinComplete(")
stub = "\t\tAttempt TryResumeOnce(Target& a_target, std::optional<std::uint32_t> a_previousPlayerId, Clock::time_point a_deadline);\n\n"
body = source[:start] + stub + source[end:]
body = "\n".join(line for line in body.splitlines() if not line.startswith('#include "') or line == '#include "F4TReconnect.h"')
with tempfile.TemporaryDirectory(prefix="co-reconnect-window-") as temporary:
    root = Path(temporary)
    (root / "reconnect-source.inc").write_text(body, encoding="utf-8")
    shutil.copyfile(args.source.parents[1] / "include/F4TReconnect.h", root / "F4TReconnect.h")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(ReconnectWindow LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(reconnect-window test.cpp)
target_compile_features(reconnect-window PRIVATE cxx_std_20)
target_link_libraries(reconnect-window PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(reconnect-window PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(reconnect-window PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=180)
    executable = next(path for path in (root / "build").rglob("reconnect-window*") if path.name in ("reconnect-window", "reconnect-window.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=30)
print("Reconnect window: deadline from lost time, cancel vs rejoin race, transient refusal retry PASS")
