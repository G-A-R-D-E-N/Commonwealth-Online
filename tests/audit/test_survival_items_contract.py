import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ITEMS = ROOT / "CO-SERVER/server-cpp/data/survival-items.json"
KNOWN = {
    "fallout4.esm:000366C0": "drink",
    "fallout4.esm:0004835D": "drink",
    "fallout4.esm:0004695A": "food",
}
HEX_DIGITS = set("0123456789ABCDEF")


def load_items():
    data = json.loads(ITEMS.read_text(encoding="utf-8"))
    assert data["format"] == 1, "survival items format must be 1"
    return data["items"]


def test_shipped_survival_items_are_populated():
    items = load_items()
    assert len(items) >= 200, f"survival items file has {len(items)} entries; an empty list turns relief off"


def test_shipped_survival_items_are_sorted_unique_and_wire_formatted():
    items = load_items()
    keys = [item["itemKey"] for item in items]
    assert keys == sorted(keys), "survival items must be sorted by itemKey"
    assert len(set(keys)) == len(keys), "survival items must not repeat an itemKey"
    for item in items:
        assert set(item) == {"itemKey", "kind"}, item
        assert item["kind"] in ("food", "drink"), item
        plugin, separator, local = item["itemKey"].rpartition(":")
        assert separator and plugin and plugin == plugin.lower(), item["itemKey"]
        assert len(local) == 8 and set(local) <= HEX_DIGITS, item["itemKey"]


def test_shipped_survival_items_match_the_game_data():
    kinds = {item["itemKey"]: item["kind"] for item in load_items()}
    for key, kind in KNOWN.items():
        assert kinds.get(key) == kind, f"{key} must be {kind}"


def main():
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"survival items contract: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
