from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

source = (ROOT / "CommonwealthOnlineUI/src/F4TSettingsApplyHandlers.cpp").read_text(encoding="utf-8")
body = source[source.index("bool QueuePipBoyColourCells()"):source.index("bool ApplyHUDSettings(")]

assert "*cell = std::clamp(setting->GetFloat(), 0.005F, 1.0F);" in body, "Pip-Boy colour must use the engine bounds; source:docs/code-notes/CommonwealthOnlineUI.md"
assert "*cell = setting->GetFloat();" not in body, "the raw Pip-Boy store must not come back"

values = (ROOT / "CommonwealthOnlineUI/src/F4TSettingsServiceValues.cpp").read_text(encoding="utf-8")
assert "std::clamp(a_channel / 255.0f, 0.005F, 1.0F)" in values, "the stored and persisted Pip-Boy colour must use the engine bounds"
for channel in "rgb":
    assert f"WriteFloat(*{channel}, toEngine(colour->{channel}));" in values, f"Pip-Boy {channel} store must go through the clamp"

print("test_pipboy_colour_clamp_contract: all checks passed.")
