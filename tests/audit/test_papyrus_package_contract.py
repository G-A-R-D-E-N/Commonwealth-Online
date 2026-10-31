from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CO_BETA = ROOT / "CO-BETA"
COMPILED = CO_BETA / "creation-kit" / "scripts" / "compiled"
WORKFLOWS = ROOT / ".github" / "workflows"


def test_checked_in_papyrus_outputs_are_the_package_contract():
    assert {path.name for path in COMPILED.glob("*.pex")} == {
        "COCharacterSetupQuestScript.pex",
        "COQuestSyncScript.pex",
        "CommonwealthOnlineDebug.pex",
        "CommonwealthOnlineQuest.pex",
    }
    assert not list(COMPILED.glob("CoSync*.pex"))
    assert all(path.stat().st_size > 0 for path in COMPILED.glob("*.pex"))
    stage = (CO_BETA / "ci" / "stage-content.sh").read_text(encoding="utf-8")
    assert "stage_papyrus \"$COMPILED_DIR\" Scripts '*.pex'" in stage
    assert "cosync*" in stage


def test_package_build_does_not_require_papyrus_compiler():
    build_all = (CO_BETA / "build-all.bat").read_text(encoding="utf-8")
    assert 'call "%SCRIPT_DIR%compile-papyrus.bat"' not in build_all
    for workflow in WORKFLOWS.glob("*.yml"):
        text = workflow.read_text(encoding="utf-8")
        assert "PapyrusCompiler.exe" not in text
        assert "compile-papyrus.bat" not in text


if __name__ == "__main__":
    test_checked_in_papyrus_outputs_are_the_package_contract()
    test_package_build_does_not_require_papyrus_compiler()
    print("Papyrus package contract passed")
