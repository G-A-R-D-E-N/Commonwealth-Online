from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"
ATTRIBUTION = "// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission."

header = (PLUGIN_DIR / "include/F4TCrashReport.h").read_text(encoding="utf-8")
source = (PLUGIN_DIR / "src/F4TCrashReport.cpp").read_text(encoding="utf-8")
main_source = (PLUGIN_DIR / "src/main.cpp").read_text(encoding="utf-8")
runtime_source = (PLUGIN_DIR / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")

for name in ("FormatFirstChance(", "KeepPreviousLog(", "Reassert(", "Tick("):
    assert name in header, f"F4TCrashReport.h must declare or define {name}"

assert "AddVectoredExceptionHandler(1, OnFirstChance);" in source, "the first-chance handler must be registered"
assert source.index("IsSeriousException(a_info->ExceptionRecord->ExceptionCode)") < source.index("FormatFirstChance("), (
    "first-chance lines must only be written for serious exceptions"
)
assert "EXCEPTION_STACK_OVERFLOW" not in source, "stack overflow must never reach the first-chance formatter or file I/O"
assert "g_firstChanceCount.fetch_add(1) < kMaxFirstChanceLines" in source, "first-chance lines must be capped per attempt"
assert "g_lastFirstChanceAddress.load()" in source, "repeated first-chance faults at one address must be deduplicated"
assert source.index("InterlockedExchange(std::addressof(g_firstChanceBusy), 1) != 0") < source.index("FormatFirstChance("), (
    "the first-chance line must be formatted under the busy lock"
)
assert source.index("g_firstChanceCount.fetch_add(1) < kMaxFirstChanceLines") < source.index("AppendToLog(g_firstChanceLine, length)"), (
    "the first-chance slot must be claimed before the write, so a log that keeps failing is capped too"
)
assert "g_firstChanceCount.fetch_add(1);" not in source, "a first-chance slot must be claimed once per attempt, not after a write"
assert source.index("g_lastFirstChanceAddress.store(address);") < source.index("AppendToLog(g_firstChanceLine, length)"), (
    "the last first-chance address must be recorded before the write"
)
assert source.index("AppendToLog(g_firstChanceLine, length)") < source.index("InterlockedExchange(std::addressof(g_firstChanceBusy), 0)"), (
    "the busy lock must be released after the write"
)

assert "RecordDisplaced(previous);" in source and "RecordDisplaced(current);" in source, (
    "every displaced filter must be recorded at install and at re-assert"
)
assert "g_previousFilter" not in source, "displaced filters must live in the chain array, not one previous filter"
assert "std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_displacedFilters[kMaxDisplacedFilters]" in source, (
    "the chain must be an array of atomic filter pointers"
)
assert "if (a_filter == nullptr || a_filter == OnCrash) {" in source, "OnCrash and empty filters must never be recorded"
assert "if (g_displacedFilters[index].load() == a_filter) {" in source, "a displaced filter must be recorded once"
assert "g_displacedFilters[index - 1].load()" in source, "the chain must be walked newest first"
assert source.index("for (auto index = g_displacedCount.load(); index > 0") < source.index("result = filter(a_info);"), (
    "the chain loop must run before the filter is called"
)

assert "const auto self = static_cast<LONG>(::GetCurrentThreadId());" in source, "the report owner must be keyed on the thread"
assert "thread_local bool t_inOnCrash{ false };" in source and "if (t_inOnCrash) {" in source and source.index("if (t_inOnCrash) {") < source.index("t_inOnCrash = true;") < source.index("result = filter(a_info);") < source.index("t_inOnCrash = false;"), (
    "OnCrash must refuse re-entry on any thread, so a displaced filter that chains back cannot recurse"
)
assert "const auto owner = ::InterlockedCompareExchange(std::addressof(g_crashOwner), self, 0);" in source, (
    "the crash owner must be claimed with InterlockedCompareExchange"
)
assert "if (owner == 0 && a_info != nullptr) {" in source, "only the thread that claimed the owner may write the report"
assert source.index("result = filter(a_info);") < source.index("InterlockedCompareExchange(std::addressof(g_crashOwner), 0, self);"), (
    "the crash owner must stay claimed across the chained filters"
)

assert "\"-previous.log\"" in source, "the previous session log must be named <plugin>-previous.log"
keep_body = source.split("void KeepPreviousLog(")[1].split("void Reassert()", 1)[0]
assert "PreInitPluginName()" in keep_body, "rotation must use the pre-init plugin name, not the post-init one"
assert "SupportsSaveFolderName(" in keep_body and "\"Fallout4\"" in keep_body, (
    "rotation must resolve the save folder the same way CommonLibF4 opens the log"
)
assert "std::filesystem::rename(current, folder / (name + \"-previous.log\"), error);" in keep_body, (
    "the rotation must replace an older previous log through the non-throwing std::filesystem::rename"
)
assert "MoveFileExW(" not in keep_body, "the rotation must not call MoveFileExW directly"
assert keep_body.index("file_size(current, error)") < keep_body.index("std::filesystem::rename("), (
    "the rename must come after the current log is checked"
)
assert "if (error || size == 0) {" in keep_body, "a missing or zero-byte current log must never replace the previous session"

reassert_body = source.split("void Reassert()")[1].split("void Tick()", 1)[0]
assert "if (current == OnCrash) {" in reassert_body, "Reassert must be a no-op when the handler is already in front"
assert reassert_body.index("if (current == OnCrash)") < reassert_body.index("RecordDisplaced(current);"), (
    "Reassert must never record OnCrash as its own displaced filter"
)
assert reassert_body.index("if (!g_installed) {") < reassert_body.index("SetUnhandledExceptionFilter(OnCrash)"), (
    "Reassert must do nothing when Install bailed out early"
)
tick_body = source.split("void Tick()")[1]
assert tick_body.index("if (!g_installed) {") < tick_body.index("steady_clock::now()"), "Tick must do nothing when Install bailed out early"
install_body = source.split("void Install() noexcept")[1].split("void KeepPreviousLog(", 1)[0]
assert install_body.index("if (!ResolveLogPath())") < install_body.index("g_installed = true;"), (
    "Install must mark itself installed only after the log path resolves"
)

assert main_source.count("F4T::CrashReport::Reassert();") == 2, (
    "Reassert must run on kGameDataReady and kPostLoadGame"
)
assert main_source.index("F4T::CrashReport::KeepPreviousLog(a_f4se);") < main_source.index("F4SE::Init(a_f4se);"), (
    "the previous log must be rotated before CommonLibF4 opens and truncates the plugin log"
)
assert runtime_source.count("F4T::CrashReport::Tick();") == 1, "the crash report must be re-asserted from the runtime tick"

for path in (
    PLUGIN_DIR / "include/F4TCrashReport.h",
    PLUGIN_DIR / "src/F4TCrashReport.cpp",
    PLUGIN_DIR / "tests/F4TCrashReportTests.cpp",
    PLUGIN_DIR / "src/main.cpp",
    PLUGIN_DIR / "src/F4TReplicationRuntime.cpp",
):
    raw = path.read_bytes()
    assert b"\x00" not in raw, f"{path} must not contain NUL bytes"
    assert raw.count(b"\n") == raw.count(b"\r\n"), f"{path} must use CRLF line endings"
    if path.suffix == ".cpp":
        assert path.read_text(encoding="utf-8").splitlines()[0] == ATTRIBUTION, f"{path} must carry the FathersFallout attribution"

for path in (PLUGIN_DIR / "src/F4TCrashReport.cpp", PLUGIN_DIR / "include/F4TCrashReport.h", PLUGIN_DIR / "tests/F4TCrashReportTests.cpp"):
    text = path.read_text(encoding="utf-8")
    assert text.splitlines()[0] == ATTRIBUTION, f"{path} must carry the FathersFallout attribution as its first line"
    assert "FathersFallout" not in text.split("\n", 1)[1], f"{path} must not name FathersFallout outside the attribution line"
