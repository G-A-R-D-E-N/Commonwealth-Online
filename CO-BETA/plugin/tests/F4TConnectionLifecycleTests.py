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
sections = [('\tbool EnsureWinsockInitialized()', '\n\tbool SetNonBlocking('),
            ('\tvoid StopReceiveThread()', '\n\tstd::string TrimAsciiWhitespace('),
            ('\tbool ConnectToServerInternal(', '\n}\n\nnamespace F4T::Networking::Detail'),
            ('\tbool ConnectAndValidateServer(', '\n\tvoid DisconnectFromLocalServer()'),
            ('\tvoid DisconnectFromLocalServer()', '\n\tbool IsConnectedToServer()')]
body = '\n'.join(source[source.index(start):source.index(end, source.index(start))] for start, end in sections)
body = body.replace('std::format', 'FixtureFormat')
with tempfile.TemporaryDirectory(prefix="co-connection-lifecycle-") as temporary:
    root = Path(temporary)
    (root / "transport-source.inc").write_text(body, encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    shutil.copyfile(
        Path(__file__).resolve().parents[3] / "co-core/include/F4TConnectionPolicy.h",
        root / "F4TConnectionPolicy.h",
    )
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(ConnectionLifecycle LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(connection-lifecycle test.cpp)
target_compile_features(connection-lifecycle PRIVATE cxx_std_20)
target_link_libraries(connection-lifecycle PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(connection-lifecycle PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(connection-lifecycle PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=120)
    executable = next(path for path in (root / "build").rglob("connection-lifecycle*") if path.name in ("connection-lifecycle", "connection-lifecycle.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=15)
print("Connection lifecycle: paired cleanup, connect/disconnect, prior receiver drain, terminal, prompt welcome cancellation, stale waiter and welcome timeout PASS")
