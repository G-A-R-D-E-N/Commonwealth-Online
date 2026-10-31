from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def function_body(code, signature):
    start = code.index(signature)
    brace = code.index("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


lineage_raw = (PLUGIN / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8")
lineage = strip_comments(lineage_raw)
lineage_header = (PLUGIN / "include/F4TSaveLineageSession.h").read_text(encoding="utf-8")
session = strip_comments((PLUGIN / "src/F4TSessionLaunch.cpp").read_text(encoding="utf-8"))

check("#include <Windows.h>" in lineage_raw,
      "F4TSaveLineageSession.cpp must include <Windows.h> for HANDLE and CreateFileW")

is_settled = function_body(lineage, "bool IsSaveFileSettled(")
check(re.search(r'tmpFilePath \+= "\.tmp";', is_settled) is not None
      and re.search(r"exists\(tmpFilePath, ec\) \|\| ec\) \{\s*return false;", is_settled) is not None,
      "IsSaveFileSettled must return false while <path>.tmp exists")
check(re.search(r"CreateFileW\(\s*a_saveFilePath\.c_str\(\),\s*GENERIC_READ,\s*0,", is_settled) is not None,
      "IsSaveFileSettled must open the save with CreateFileW and share mode 0 (exclusive)")
check(is_settled.index("exists(tmpFilePath") < is_settled.index("CreateFileW("),
      "IsSaveFileSettled must check the .tmp file before opening the save")

read_name = function_body(lineage, "std::string ReadPostSaveGameName(")
check("kSaveNameFieldBytes = 0x104" in read_name and "strnlen(name, kSaveNameFieldBytes)" in read_name,
      "ReadPostSaveGameName must bound the post-save name to the 0x104-byte save name field")
check("std::string ReadPostSaveGameName(" not in session and "kSaveNameFieldBytes" not in session,
      "ReadPostSaveGameName must live in F4TSaveLineageSession.cpp, not F4TSessionLaunch.cpp")
check("F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg)" in session,
      "SessionLaunch must read the post-save name through F4TSaveLineageSession")

update = function_body(session, "void Update()")
check("TryCompletePendingRecord" not in update,
      "SessionLaunch::Update must not call TryCompletePendingRecord; the record settles on the F4SE task queue")

begin_calls = [match.start() for match in re.finditer(r"(?<!void )BeginPendingRecord\(", lineage)]
check(len(begin_calls) == 1,
      "BeginPendingRecord must have exactly one call site")
check(begin_calls and lineage[:begin_calls[0]].rstrip().endswith("if (!a_savedName.empty()) {"),
      "BeginPendingRecord must be reached for every post-save name")
check("kEnableSaveLineage" not in lineage and "kEnableSaveLineage" not in lineage_header,
      "the save lineage path must not be gated")

check(re.search(r"constexpr auto kPendingRecordTimeout = std::chrono::seconds\{ 30 \};", lineage) is not None,
      "kPendingRecordTimeout must be a 30 s bound")
check(re.search(r"steady_clock::now\(\) - g_pendingRecord->startedAt < kPendingRecordTimeout\) \{\s*return;",
                lineage) is not None,
      "PollPendingRecord must wait for the next frame while under the timeout")
check("AddTask" not in lineage and "QueuePendingPoll" not in lineage,
      "the pending record must not re-queue itself on the F4SE task queue; the drain runs re-added tasks in the same frame")
replication = strip_comments((PLUGIN / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8"))
check("F4T::SaveLineageSession::TickPendingRecord();" in function_body(replication, "void Tick()"),
      "ReplicationRuntime::Tick must poll the pending save record once per frame")
check(re.search(r"did not settle within \{\} s[\s\S]*?g_pendingRecord\.reset\(\);\s*return;", lineage) is not None,
      "PollPendingRecord must drop the pending record once the timeout passes")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TSaveLineageTimingContract: all checks passed.")
