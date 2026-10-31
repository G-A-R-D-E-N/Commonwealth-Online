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
body = source[source.index("\tvoid ReceiveLoop()"):source.index("\tstd::string TrimAsciiWhitespace(")]
with tempfile.TemporaryDirectory(prefix="co-receive-ownership-") as temporary:
    root = Path(temporary)
    (root / "receive-thread-source.inc").write_text(body, encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(ReceiveThreadOwnership LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(receive-ownership test.cpp)
target_compile_features(receive-ownership PRIVATE cxx_std_20)
target_link_libraries(receive-ownership PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(receive-ownership PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(receive-ownership PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=120)
    executable = next(path for path in (root / "build").rglob("receive-ownership*") if path.name in ("receive-ownership", "receive-ownership.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=10)
print("Receive thread ownership: self-stop during external join and 50 paired external stops PASS")
