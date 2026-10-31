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
lifecycle_header = args.source.with_name("F4TNetworkingSocketLifecycle.h").read_text(encoding="utf-8")
lifecycle_source = args.source.with_name("F4TNetworkingSocketLifecycle.cpp").read_text(encoding="utf-8")
struct_start = lifecycle_header.index("\tstruct DetachedSocket")
struct_end = lifecycle_header.index("\n\t};\n", struct_start) + len("\n\t};\n")
lifecycle = lifecycle_header[struct_start:struct_end] + lifecycle_source[lifecycle_source.index("	DetachedSocket DetachSocketLocked()"):lifecycle_source.rindex("\n}\n")]
send_start = source.index("\tbool SendPacketBytes(")
send_end = source.index("\n}\n\nnamespace F4T::Networking\n", send_start)
send_text = source[send_start:send_end]
with tempfile.TemporaryDirectory(prefix="co-send-cleanup-race-") as temporary:
    root = Path(temporary)
    (root / "socket-lifecycle.inc").write_text(lifecycle, encoding="utf-8")
    (root / "send-bytes.inc").write_text(send_text, encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(SendCleanupRace LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(send-cleanup-race test.cpp)
target_compile_features(send-cleanup-race PRIVATE cxx_std_20)
target_link_libraries(send-cleanup-race PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(send-cleanup-race PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(send-cleanup-race PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=180)
    executable = next(path for path in (root / "build").rglob("send-cleanup-race*") if path.name in ("send-cleanup-race", "send-cleanup-race.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=10)
print("Send cleanup race: failed send closes its own socket and a reconnect keeps the new socket PASS")
