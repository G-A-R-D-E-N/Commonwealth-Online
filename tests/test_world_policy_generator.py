import inspect
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts/generate-world-policy.py"
VERIFIER = ROOT / "scripts/verify-world-policy-packaging.py"

RADICLE_POLICY = {
    "schema": "co.world-policy/1",
    "plugin": "Fallout4.esm",
    "npcs": [
        {"formkey": "0101CA6E:Fallout4.esm", "class": "story_unique"},
        {"formkey": "0101D162:Fallout4.esm", "class": "companion"},
        {"formkey": "0001CA70:Fallout4.esm", "class": "vendor"},
        {"formkey": "0101CA71:Fallout4.esm", "class": "town_npc"},
        {"formkey": "0101CA73:Fallout4.esm", "class": "enemy"},
        {"formkey": "0101CA74:Fallout4.esm", "class": "animal"},
    ],
    "doors": [{"formkey": "0101CA75:Fallout4.esm"}],
}


def run(script, *arguments):
    return subprocess.run(
        [sys.executable, str(script), *arguments],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )


def test_generator_self_test_passes():
    result = run(GENERATOR, "--self-test")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "self-test PASS" in result.stdout


def test_generator_output_satisfies_packaging_verifier(tmp_path):
    source = tmp_path / "radicle.json"
    source.write_text(json.dumps(RADICLE_POLICY), encoding="utf-8")
    out = tmp_path / "world-policy.json"

    generated = run(GENERATOR, "--source", str(source), "--out", str(out))
    assert generated.returncode == 0, generated.stdout + generated.stderr

    data = json.loads(out.read_text(encoding="utf-8"))
    assert data["game"] == "Fallout4.esm"
    assert data["version"] == 1
    assert "sample" not in data
    assert data["classes"]["story"] == ["01CA6E:Fallout4.esm"]
    assert data["classes"]["companion"] == ["01D162:Fallout4.esm"]
    assert data["classes"]["vendor"] == ["01CA70:Fallout4.esm"]
    assert data["classes"]["town"] == ["01CA71:Fallout4.esm"]
    assert data["classes"]["enemy"] == ["01CA73:Fallout4.esm"]
    assert data["classes"]["animal"] == ["01CA74:Fallout4.esm"]
    assert data["classes"]["settler"] == []
    assert data["doors"] == ["01CA75:Fallout4.esm"]

    verified = run(VERIFIER, str(tmp_path))
    assert verified.returncode == 0, verified.stdout + verified.stderr


def test_generator_rejects_a_foreign_schema(tmp_path):
    source = tmp_path / "radicle.json"
    source.write_text(json.dumps({"schema": "co.something-else/1"}), encoding="utf-8")

    result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
    assert result.returncode == 1
    assert "schema" in result.stderr
    assert not (tmp_path / "world-policy.json").exists()


def test_generator_rejects_a_malformed_form_key(tmp_path):
    policy = json.loads(json.dumps(RADICLE_POLICY))
    policy["npcs"].append({"formkey": "001CA6E:Fallout4.esm", "class": "enemy"})
    source = tmp_path / "radicle.json"
    source.write_text(json.dumps(policy), encoding="utf-8")

    result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
    assert result.returncode == 1
    assert "001CA6E" in result.stderr


def test_generator_rejects_a_load_order_byte_without_a_plugin(tmp_path):
    policy = json.loads(json.dumps(RADICLE_POLICY))
    policy["npcs"].append({"formkey": "051CA6E0", "class": "enemy"})
    source = tmp_path / "radicle.json"
    source.write_text(json.dumps(policy), encoding="utf-8")

    result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
    assert result.returncode == 1
    assert "051CA6E0" in result.stderr
    assert not (tmp_path / "world-policy.json").exists()


def test_generator_rejects_non_object_rows(tmp_path):
    policy = json.loads(json.dumps(RADICLE_POLICY))
    policy["npcs"].append("0101CA77:Fallout4.esm")
    policy["doors"].append(7)
    source = tmp_path / "radicle.json"
    source.write_text(json.dumps(policy), encoding="utf-8")

    result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
    assert result.returncode == 1
    assert "Traceback" not in result.stderr
    assert "npcs: row is not an object" in result.stderr
    assert "doors: row is not an object" in result.stderr


def test_generator_keeps_the_previous_output_when_the_verifier_fails(tmp_path):
    policy = {"schema": "co.world-policy/1", "plugin": "Fallout4.esm", "npcs": [], "doors": []}
    source = tmp_path / "radicle.json"
    source.write_text(json.dumps(policy), encoding="utf-8")
    out = tmp_path / "world-policy.json"
    out.write_text("previous", encoding="utf-8")

    result = run(GENERATOR, "--source", str(source), "--out", str(out))
    assert result.returncode == 1
    assert out.read_text(encoding="utf-8") == "previous"
    assert sorted(path.name for path in tmp_path.iterdir()) == ["radicle.json", "world-policy.json"]


def main() -> int:
    failures: list[str] = []
    tests = [(name, value) for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for name, func in tests:
        try:
            if "tmp_path" in inspect.signature(func).parameters:
                with tempfile.TemporaryDirectory() as directory:
                    func(Path(directory))
            else:
                func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
