import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default=shutil.which("cmake"))
parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "src/F4TNetworking.cpp")
args = parser.parse_args()
if not args.cmake:
    parser.error("cmake is required")
source = args.source.read_text(encoding="utf-8")
body = source[source.index("\tvoid TrimPacketLine("):source.index("\tstd::string TrimAsciiWhitespace(")]
core = Path(__file__).resolve().parents[3] / "co-core"
with tempfile.TemporaryDirectory(prefix="co-receive-notification-") as temporary:
    root = Path(temporary)
    (root / "receive-source.inc").write_text(body, encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(ReceiveNotification LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(receive-notification test.cpp)
target_compile_features(receive-notification PRIVATE cxx_std_20)
target_link_libraries(receive-notification PRIVATE Threads::Threads)
target_include_directories(receive-notification PRIVATE "''' + core.as_posix() + '''/include")
''' + "\n".join('target_sources(receive-notification PRIVATE "' + (core / "src/validation" / name).as_posix() + '")' for name in ("F4TReceivePolicy.cpp", "F4TReceiveFrame.cpp", "F4TPacketRateLimit.cpp")) + '''
if(MSVC)
  target_compile_options(receive-notification PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(receive-notification PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=120)
    executable = next(path for path in (root / "build").rglob("receive-notification*") if path.name in ("receive-notification", "receive-notification.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=10)
print("Actual receiver/parser: seven terminal-notification cases PASS")
