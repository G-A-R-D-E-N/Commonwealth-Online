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
include_dir = Path(__file__).resolve().parents[1] / "include"
with tempfile.TemporaryDirectory(prefix="co-admin-item-queue-") as temporary:
    root = Path(temporary)
    for header in ("F4TAdminItemQueue.h", "F4TAdminItems.h"):
        shutil.copyfile(include_dir / header, root / header)
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(AdminItemQueue LANGUAGES CXX)
add_executable(admin-item-queue test.cpp)
target_compile_features(admin-item-queue PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(admin-item-queue PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(admin-item-queue PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=120)
    executable = next(path for path in (root / "build").rglob("admin-item-queue*") if path.name in ("admin-item-queue", "admin-item-queue.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=15)
print("Admin item queue: stale-generation drop, not-connected drop, 64-entry busy cap and FIFO drain PASS")
