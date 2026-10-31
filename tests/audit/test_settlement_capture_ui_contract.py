from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FACTION = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/"


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def lines(text):
    return text.count("\n") + (0 if text.endswith("\n") else 1)


faction_view = read("CommonwealthOnlineUI/src/F4TFactionView.cpp")
faction_js = read(FACTION + "faction.js")
ownership_js = read(FACTION + "faction.ownership.js")
ownership_test = read(FACTION + "faction.ownership.test.js")
index_html = read(FACTION + "index.html")
preview_html = read(FACTION + "preview.html")
harness = read(FACTION + "faction.harness.js")
ui_workflow = read(".github/workflows/co-ui-ci.yml")
workflow = read(".github/workflows/co-audit-regressions.yml")

is_known = faction_view.split("bool IsActionKnown(std::string_view a_op)")[1].split("void PushState()")[0]
for op in ("capture", "cancelCapture", "refreshCapture"):
    assert '"' + op + '"' in is_known, "the native faction bridge must let " + op + " through IsActionKnown"

for op in ('"capture"', '"cancelCapture"', '"refreshCapture"'):
    assert "case " + op + ":" in faction_js, "faction.js actionFor must build " + op
assert "buildZoneId: Number(data.buildZone)" in faction_js, "capture must send buildZoneId as a number"
assert "window.coFactionAction" in faction_js and "coFactionAction" in faction_view, \
    "capture ops must travel the shared coFactionAction bridge"

assert "ownershipSection(s, esc)" in faction_js, "renderState must include the Ownership section"
assert '<script src="faction.ownership.js"></script>' in index_html, "the Social window must load faction.ownership.js"
assert '<script src="faction.ownership.js"></script>' in preview_html, "the preview must load faction.ownership.js"
assert "faction.ownership.js" in harness, "the shared harness must load faction.ownership.js"
assert "faction.ownership.test.js" in ui_workflow, "CI must run the Ownership panel tests"
assert "faction.ownership.js" in ui_workflow, "CI must node --check the Ownership module"

for key in ("settlementCapture", "settlements", "nearBuildZoneId", "canCapture", "localCapturing", "buildZoneId"):
    assert key in ownership_js, "faction.ownership.js must read " + key
for name in ("workbenchRefId", "nearWorkbenchRefId"):
    assert name not in ownership_js and name not in faction_js, \
        name + " was renamed to buildZoneId in the contract deviations"
assert 'data-op="capture"' in ownership_js and 'data-op="cancelCapture"' in ownership_js \
    and 'data-op="refreshCapture"' in ownership_js, "the panel must render all three capture controls"
assert "Ownership data is not available yet." in ownership_js, "a missing state must show a placeholder"
assert "Number.isSafeInteger" in ownership_js, "a zone id must be a safe integer before it renders into an action"
assert '"Settlement " + row.buildZoneId' in ownership_js, \
    "a zone is labelled by its id, the plugin sends no zone names"
assert "coFactionOwnership" in ownership_test or "faction.ownership.js" in ownership_test, \
    "the ownership test must load the module under test"

assert lines(faction_js) < 750, "faction.js must stay under 750 lines"
assert lines(ownership_js) < 750, "faction.ownership.js must stay under 750 lines"
assert lines(faction_view) < 750, "F4TFactionView.cpp must stay under 750 lines"

assert "run: python3 tests/audit/test_settlement_capture_ui_contract.py" in workflow, \
    "CI must run this contract"

print("test_settlement_capture_ui_contract: all checks passed.")
