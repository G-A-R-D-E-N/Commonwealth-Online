from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PLUGIN_CRASH = ROOT / "CO-BETA/plugin/src/F4TCrashReport.cpp"
UI_SHARE = ROOT / "CommonwealthOnlineUI/src/F4TShareLog.cpp"
CORE_REDACTION = ROOT / "co-core/src/runtime/F4TLogRedaction.cpp"
ATTRIBUTION = "// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission."

plugin = PLUGIN_CRASH.read_text(encoding="utf-8")
share = UI_SHARE.read_text(encoding="utf-8")
core = CORE_REDACTION.read_text(encoding="utf-8")

assert "\"-previous.log\"" in plugin, "the plugin must rotate the previous session to <plugin>-previous.log"
assert "kClientLogStems[] = { \"CommonwealthOnline\", \"CommonwealthOnlineNG\", \"CommonwealthOnlineOG\" }" in share, (
    "the UI must try the AE, NG and OG plugin log names"
)
assert "name = \"CommonwealthOnlineNG\"" in (ROOT / "CO-BETA/plugin/xmake.lua").read_text(encoding="utf-8"), (
    "the NG plugin must log under CommonwealthOnlineNG"
)
assert "name = \"CommonwealthOnlineOG\"" in (ROOT / "CO-BETA/plugin/xmake.lua").read_text(encoding="utf-8"), (
    "the OG plugin must log under CommonwealthOnlineOG"
)
assert "kPreviousLogSuffix = \"-previous.log\"" in share, (
    "the UI must read the rotated previous-session log written by the plugin"
)

share_body = share.split("Result Share()")[1]
previous_read = share_body.index("ReadLogTail(previousLog, previousRaw)")
composed = share_body.index("ComposeSharedLog(previousRaw, raw)")
assert previous_read < composed, "the previous session must be read before the logs are composed"
assert "RedactLog(" not in share_body, "redaction must happen inside ComposeSharedLog so both sessions are covered"
assert "ReadLogTail(previousLog, previousRaw)" in share_body, "the previous session must be read through the capped tail reader"
assert "for (const auto stem : kClientLogStems) {" in share_body and "clientLog.empty()" in share_body, (
    "the UI must pick a current log name that exists"
)
client_loop = share_body.split("for (const auto stem : kClientLogStems) {", 1)[1].split("if (clientLog.empty())", 1)[0]
assert "break;" not in client_loop, "the UI must not stop at the first current log name that exists"
assert "std::error_code error;" in client_loop and "std::filesystem::last_write_time(candidate, error)" in client_loop, (
    "the current log lookup must use the non-throwing last_write_time overload"
)
assert "written > newest" in client_loop, "the UI must share the current log with the newest last-write time"
assert "clientLog.stem().string() + std::string{ kPreviousLogSuffix }" in share_body, (
    "the previous log must share the stem of the chosen current log"
)
assert "Could not read {}.\", previousLog.string()" not in share_body, "an unreadable previous log must not fail the share"
assert "previousUnreadable = true;" in share_body and "Could not read the previous session log" in share_body, (
    "an unreadable previous log must be shared as a note on the current log"
)
assert share_body.index("ComposeSharedLog(previousRaw, raw)") < share_body.index("Could not read the previous session log"), (
    "the unreadable-previous note must be appended after the logs are composed"
)

compose_body = core.split("std::string ComposeSharedLog(")[1]
assert compose_body.index("RedactLog(a_previousRaw)") < compose_body.index("composed += current;"), (
    "the previous session must come first, redacted"
)
assert "===== previous session =====" in compose_body, "the previous session needs a separator"
assert "===== current session =====" in compose_body, "the current session needs a separator"
assert "if (a_previousRaw.empty()) {" in compose_body, "without a previous session the output must be the current log only"
assert "return current;" in compose_body, "without a previous session the output must be the current log only"

for path in (UI_SHARE, CORE_REDACTION):
    raw = path.read_bytes()
    assert raw.decode("utf-8").splitlines()[0] == ATTRIBUTION, f"{path} must carry the FathersFallout attribution"
    assert b"\x00" not in raw, f"{path} must not contain NUL bytes"
    assert raw.count(b"\n") == raw.count(b"\r\n"), f"{path} must use CRLF line endings"

assert not re.search(r"FathersFallout", share.split("\n", 1)[1]), "FathersFallout must not appear in UI strings"
assert not re.search(r"FathersFallout", core.split("\n", 1)[1]), "FathersFallout must not appear in co-core strings"

print("test_share_log_previous_session: all checks passed.")
