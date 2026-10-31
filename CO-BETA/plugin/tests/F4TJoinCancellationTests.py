import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default=shutil.which("cmake"))
parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "src/F4TUIClient.cpp")
args = parser.parse_args()
if not args.cmake:
    parser.error("cmake is required")
source = args.source.read_text(encoding="utf-8")
body = source[source.index("\t\tstd::atomic_bool g_joinInProgress"):source.index("\n\t\tbool Callback_IsConnected()")]
body = body[:body.index("\t\tstd::string GetLogPrefix()")] + body[body.index("\t\tbool Callback_JoinSession("):]
join_errors = (Path(__file__).resolve().parents[1] / "src/F4TUIClientJoinErrors.cpp").read_text(encoding="utf-8")
body = "namespace JoinErrors" + join_errors[join_errors.index("{", join_errors.index("namespace F4T::UIClient::JoinErrors")):] + "\nusing namespace JoinErrors;\n" + body
include = Path(__file__).resolve().parents[1] / "include"
api = (include / "CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
request = api[api.index("\tstruct JoinRequest"):api.index("\n\tstruct AlertAction")]
completion = api[api.index("\tusing JoinCompleteFn"):api.index("\n\tusing AlertResponseFn")]
launch = (include / "F4TSessionLaunch.h").read_text(encoding="utf-8")
launch_types = launch[launch.index("\tstruct JoinRequest"):launch.index("\n\tLaunchOutcome LaunchValidatedSession")]
with tempfile.TemporaryDirectory(prefix="co-join-cancellation-") as temporary:
    root = Path(temporary)
    (root / "ui-source.inc").write_text(body, encoding="utf-8")
    (root / "api-types.inc").write_text("namespace COMMONWEALTH_ONLINE_UI_API {\n" + request + completion + "\n}\nnamespace F4T::SessionLaunch {\n" + launch_types + "\n}\n", encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    shutil.copyfile(include / "F4TConnectFailure.h", root / "F4TConnectFailure.h")
    shutil.copyfile(include / "F4TSteamJoinGate.h", root / "F4TSteamJoinGate.h")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(JoinCancellation LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(join-cancellation test.cpp)
target_compile_features(join-cancellation PRIVATE cxx_std_20)
target_link_libraries(join-cancellation PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(join-cancellation PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(join-cancellation PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=180)
    executable = next(path for path in (root / "build").rglob("join-cancellation*") if path.name in ("join-cancellation", "join-cancellation.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=10)
print("Join cancellation: order, gate state machine, before/after publication, different owner, success, transport loss and failure PASS")
