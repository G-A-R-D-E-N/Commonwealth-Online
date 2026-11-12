from pathlib import Path
import re


plugin = Path(__file__).resolve().parents[1]
runtime = (plugin / "include/F4TExecutingRuntime.h").read_text(encoding="utf-8")

EXPECTED = {
    "src/F4TNpcActorGuard.cpp": [
        ("handleHealthRva", (0xE01460, 0xC4CB10, 0xCD2AE0)),
        ("killImplRva", (0xDB27B0, 0xC1A0F0, 0xCA00C0)),
    ],
    "src/F4TProxyPuppet.cpp": [
        ("updateRva", (0xD7B020, 0xBDE8D0, 0xC648A0)),
    ],
    "src/F4TWorkshopCommitHooks.cpp": [
        (None, (0x247BD0, 0x36D280, 0x3C19C0)),
        (None, (0xB14F30, 0xA07CA0, 0xA5BAE0)),
    ],
}
CHECKS = {
    "src/F4TNpcActorGuard.cpp": [
        "const bool handleHealthMatches = VtableGuard::SlotMatchesExpectedRva(request.slotValue, moduleBase, handleHealthRva);",
        "if (!handleHealthMatches || !VtableGuard::SlotMatchesExpectedRva(killRequest.slotValue, moduleBase, killImplRva)) {",
    ],
    "src/F4TProxyPuppet.cpp": [
        "if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), updateRva)) {",
    ],
    "src/F4TWorkshopCommitHooks.cpp": [
        "if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), a_expectedRva)) {",
    ],
}


def branch_returns_false(source, opener, name):
    start = source.find(opener)
    assert start != -1, f"{name}: missing `{opener}`"
    line_start = source.rfind("\n", 0, start) + 1
    indent = source[line_start:start]
    assert indent.strip() == "", f"{name}: `{opener}` must start its line"
    close = re.compile(rf"\r?\n{re.escape(indent)}\}}\r?\n").search(source, start)
    assert close is not None, f"{name}: `{opener}` has no closing brace"
    assert re.search(r"\breturn false;", source[start:close.start()]), f"{name}: `{opener}` must refuse with return false"


assert "version.build() != 0" in runtime and "return std::nullopt;" in runtime, "an unknown runtime build must yield no target"
assert "a_target == Target::OG ? a_og : a_target == Target::NG ? a_ng : a_ae" in runtime

for name, rows in EXPECTED.items():
    source = (plugin / name).read_text(encoding="utf-8")
    assert '#include "F4TExecutingRuntime.h"' in source, name
    assert "ExecutingRuntime::Target()" in source, name
    branch_returns_false(source, "if (!target) {", name)
    for variable, (og, ng, ae) in rows:
        binding = rf"const auto {variable} = " if variable else ""
        call = rf"{binding}(?:F4T::)?ExecutingRuntime::RvaFor\((\*target|g_target), 0x{og:X}, 0x{ng:X}, 0x{ae:X}\)"
        match = re.search(call, source)
        assert match is not None, f"{name}: {call}"
    for check in CHECKS[name]:
        if check.startswith("if ("):
            branch_returns_false(source, check, name)
        else:
            assert check in source, f"{name}: missing `{check}`"
    checks = [m.start() for m in re.finditer(r"SlotMatchesExpectedRva\(", source)]
    writes = [m.start() for m in re.finditer(r"write_vfunc\(", source)]
    assert len(checks) == sum(c.count("SlotMatchesExpectedRva(") for c in CHECKS[name]), f"{name}: unexpected identity check count"
    assert writes and max(checks) < min(writes), f"{name}: every identity check must precede every vtable write"

workshop = (plugin / "src/F4TWorkshopCommitHooks.cpp").read_text(encoding="utf-8")
assert "std::uintptr_t a_expectedRva," in workshop
assert workshop.count("InstallVtableSlot(") == 3, "one definition and two callers, each passing an expected RVA"

print("hook identity contract OK")
