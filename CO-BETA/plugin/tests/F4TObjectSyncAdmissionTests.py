import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default=shutil.which("cmake"))
parser.add_argument("--json", required=True)
args = parser.parse_args()
if not args.cmake:
    parser.error("cmake is required")
plugin = Path(__file__).resolve().parents[1]
repository = Path(__file__).resolve().parents[3]
source = (plugin / "src/F4TObjectSync.cpp").read_text(encoding="utf-8")
header = (plugin / "include/F4TObjectSync.h").read_text(encoding="utf-8")


def block_after(text, anchor):
    start = text.index(anchor)
    begin = text.index("{", start)
    depth = 0
    for end in range(begin, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return start, end + 1
    raise ValueError(anchor)


gate_start, gate_end = block_after(source, "a_runGameplay([&] {")
drain = source.index("g_inbound.pop_front();")
if drain > gate_start:
    raise ValueError("inbound drain must run before the gameplay gate")
sends = [match.start() for match in re.finditer(r"SendObject(?:Activate|Lock)Request\(", source)]
if len(sends) != 2 or not all(gate_start <= position < gate_end for position in sends):
    raise ValueError("SendObject*Request must appear only inside the runGameplay lambda")

constants = re.findall(r"inline constexpr std::(?:size_t|uint32_t) kMaximum\w+ = [^;]+;", header)
if len(constants) != 3:
    raise ValueError("object sync limits not found")

with tempfile.TemporaryDirectory(prefix="co-object-sync-admission-") as temporary:
    root = Path(temporary)
    (root / "object-sync-constants.h").write_text(
        "namespace F4T::ObjectSync\n{\n" + "\n".join(constants) + "\n}\n", encoding="utf-8")
    (root / "object-sync-body.h").write_text(source[source.index("namespace F4T::ObjectSync"):], encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "admission-test.cpp")
    (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(ObjectSyncAdmission LANGUAGES CXX)
add_executable(object-sync-admission admission-test.cpp "{(repository / 'co-core/src/world/F4TObjectState.cpp').as_posix()}")
target_include_directories(object-sync-admission PRIVATE "{(repository / 'co-core/include').as_posix()}" "{Path(args.json).as_posix()}")
target_compile_features(object-sync-admission PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(object-sync-admission PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(object-sync-admission PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=300)
    executable = next(path for path in (root / "build").rglob("object-sync-admission*")
                      if path.name in ("object-sync-admission", "object-sync-admission.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=60)
print("Object sync admission gate: denial, collapse, saturation, release, capture stamp, poll and reset cases PASS")
