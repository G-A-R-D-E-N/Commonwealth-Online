import argparse
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

CLASS_KEYS = ("story", "companion", "vendor", "town", "settler", "enemy", "animal")
DOORS_KEY = "doors"
SCHEMA = "co.world-policy/1"
LOCAL_ID_DIGITS = 6
IN_GAME_ID_DIGITS = 8
MAXIMUM_LOCAL_ID = 0xFFFFFF
MAXIMUM_PLUGIN_NAME_BYTES = 260
IMPLICIT_PLUGIN_LOAD_ORDER_BYTES = ("00",)

RADICLE_CLASS_TO_CO_CLASS = {
    "story_unique": "story",
    "companion": "companion",
    "vendor": "vendor",
    "town_npc": "town",
    "enemy": "enemy",
    "animal": "animal",
}

VERIFIER = Path(__file__).resolve().parent / "verify-world-policy-packaging.py"


def load_verifier():
    spec = importlib.util.spec_from_file_location("verify_world_policy_packaging", VERIFIER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def valid_plugin_name(name):
    return (
        bool(name)
        and len(name.encode("utf-8")) <= MAXIMUM_PLUGIN_NAME_BYTES
        and all(" " <= character <= "~" and character not in ':"\\' for character in name)
    )


def is_absent_form_key(text):
    return text is None or text == "" or text == "<unresolved>"


def split_form_key(text, default_plugin):
    if not isinstance(text, str) or not text:
        return None, "empty form key"
    digits, separator, plugin = text.partition(":")
    if not separator:
        plugin = default_plugin
        if len(digits) == IN_GAME_ID_DIGITS and digits[:2] not in IMPLICIT_PLUGIN_LOAD_ORDER_BYTES:
            return None, f"{text}: load-order byte {digits[:2]} needs an explicit :plugin"
    if not valid_plugin_name(plugin):
        return None, f"{text}: unusable plugin name"
    if not all(character in "0123456789abcdefABCDEF" for character in digits):
        return None, f"{text}: form id is not hex"
    if len(digits) not in (LOCAL_ID_DIGITS, IN_GAME_ID_DIGITS):
        return None, (
            f"{text}: form id must be {LOCAL_ID_DIGITS} hex digits (local id) "
            f"or {IN_GAME_ID_DIGITS} (in-game form id with its load-order byte)"
        )
    value = int(digits, 16)
    if len(digits) == IN_GAME_ID_DIGITS:
        value &= MAXIMUM_LOCAL_ID
    if value == 0:
        return None, f"{text}: local id 0 is not a record"
    return (plugin, value), None


def append_unique(entries, seen, key):
    if key in seen:
        return
    seen.add(key)
    entries.append(f"{key[1]:06X}:{key[0]}")


def convert(policy, plugin, version):
    problems = []
    classes = {name: [] for name in CLASS_KEYS}
    doors = []
    seen = {name: set() for name in CLASS_KEYS}
    seen_doors = set()
    unmapped = {}
    skipped = 0

    for npc in policy.get("npcs") or []:
        if not isinstance(npc, dict):
            problems.append(f"npcs: row is not an object: {npc!r}")
            continue
        co_class = RADICLE_CLASS_TO_CO_CLASS.get(npc.get("class"))
        if co_class is None:
            unmapped[npc.get("class")] = unmapped.get(npc.get("class"), 0) + 1
            continue
        key, problem = split_form_key(npc.get("formkey"), plugin)
        if key is None:
            if is_absent_form_key(npc.get("formkey")):
                skipped += 1
                continue
            problems.append(problem)
            continue
        append_unique(classes[co_class], seen[co_class], key)

    for door in policy.get("doors") or []:
        if not isinstance(door, dict):
            problems.append(f"doors: row is not an object: {door!r}")
            continue
        key, problem = split_form_key(door.get("formkey"), plugin)
        if key is None:
            if is_absent_form_key(door.get("formkey")):
                skipped += 1
                continue
            problems.append(problem)
            continue
        append_unique(doors, seen_doors, key)

    if problems:
        return None, problems, unmapped, skipped

    game = policy.get("plugin") or plugin
    return {
        "version": version,
        "game": game,
        "classes": classes,
        DOORS_KEY: doors,
    }, [], unmapped, skipped


def run(args):
    try:
        policy = json.loads(Path(args.source).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"{args.source}: not readable JSON ({error})", file=sys.stderr)
        return 1
    if not isinstance(policy, dict):
        print(f"{args.source}: root must be an object", file=sys.stderr)
        return 1
    if policy.get("schema") != SCHEMA:
        print(f"{args.source}: schema must be {SCHEMA}", file=sys.stderr)
        return 1

    data, problems, unmapped, skipped = convert(policy, args.plugin, args.version)
    for problem in problems:
        print(problem, file=sys.stderr)
    if data is None:
        return 1

    if unmapped:
        counts = ", ".join(f"{name or '<none>'}={count}" for name, count in sorted(unmapped.items(), key=lambda item: str(item[0])))
        print(f"{args.source}: no Commonwealth Online class for {counts}", file=sys.stderr)
    if skipped:
        print(f"{args.source}: skipped {skipped} rows with no usable form key", file=sys.stderr)

    counts = {name: len(entries) for name, entries in data["classes"].items()}
    print(json.dumps({"entries": counts, "doors": len(data[DOORS_KEY])}, indent=1))
    if not counts["story"] and not counts["companion"]:
        print("story and companion are both empty; free-roam cannot be served from this file", file=sys.stderr)

    out = Path(args.out)
    verifier = load_verifier()
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=out.parent) as staging:
            staged = Path(staging) / verifier.DATA_FILE_NAME
            staged.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")
            reported = verifier.check_data_file(Path(staging))
            if reported:
                for problem in reported:
                    print(problem, file=sys.stderr)
                return 1
            staged.replace(out)
    except OSError as error:
        print(f"{out}: cannot write ({error})", file=sys.stderr)
        return 1
    return 0


def self_test():
    sample = {
        "schema": SCHEMA,
        "plugin": "Fallout4.esm",
        "npcs": [
            {"formkey": "0101CA6E:Fallout4.esm", "class": "story_unique"},
            {"formkey": "0101D162:Fallout4.esm", "class": "companion"},
            {"formkey": "0101CA6E:Fallout4.esm", "class": "story_unique"},
            {"formkey": "0001CA70:Fallout4.esm", "class": "vendor"},
            {"formkey": "01CA71", "class": "town_npc"},
            {"formkey": "0101CA73:Fallout4.esm", "class": "enemy"},
            {"formkey": "0101CA74:Fallout4.esm", "class": "animal"},
            {"formkey": "0101CA75:Fallout4.esm", "class": "settlement_creature"},
            {"formkey": "", "class": "enemy"},
        ],
        "doors": [{"formkey": "0101CA76:Fallout4.esm"}, {"formkey": "<unresolved>"}],
    }
    data, problems, unmapped, skipped = convert(sample, "Fallout4.esm", 1)
    assert problems == [], problems
    assert unmapped == {"settlement_creature": 1}, unmapped
    assert skipped == 2, skipped
    assert list(data["classes"]) == list(CLASS_KEYS), list(data["classes"])
    assert data["game"] == "Fallout4.esm"
    assert data["classes"]["story"] == ["01CA6E:Fallout4.esm"], data["classes"]["story"]
    assert data["classes"]["companion"] == ["01D162:Fallout4.esm"]
    assert data["classes"]["vendor"] == ["01CA70:Fallout4.esm"]
    assert data["classes"]["town"] == ["01CA71:Fallout4.esm"]
    assert data["classes"]["settler"] == []
    assert data["classes"]["enemy"] == ["01CA73:Fallout4.esm"]
    assert data["classes"]["animal"] == ["01CA74:Fallout4.esm"]
    assert data["doors"] == ["01CA76:Fallout4.esm"], data["doors"]
    assert "sample" not in data

    assert split_form_key("0101CA6E:Fallout4.esm", "p.esm") == (("Fallout4.esm", 0x1CA6E), None)
    assert split_form_key("01CA6E:Fallout4.esm", "p.esm") == (("Fallout4.esm", 0x1CA6E), None)
    assert split_form_key("01CA6E", "p.esm") == (("p.esm", 0x1CA6E), None)
    assert split_form_key("0101CA6E", "p.esm")[0] is None
    assert split_form_key("0001CA6E", "p.esm")[0] == ("p.esm", 0x1CA6E)
    assert split_form_key("051CA6E0", "p.esm")[0] is None
    assert split_form_key("051CA6E0:Fallout4.esm", "p.esm")[0] == ("Fallout4.esm", 0x1CA6E0)

    _, rows, _, _ = convert({"npcs": ["x"], "doors": [1]}, "p.esm", 1)
    assert len(rows) == 2, rows
    assert split_form_key("0000CA6E:Fallout4.esm", "p.esm")[0] == ("Fallout4.esm", 0xCA6E)
    assert split_form_key("00000000:Fallout4.esm", "p.esm")[0] is None
    assert split_form_key("001CA6E:Fallout4.esm", "p.esm")[0] is None
    assert split_form_key("0101CA6E:bad:name", "p.esm")[0] is None
    assert split_form_key('0101CA6E:bad"name', "p.esm")[0] is None
    assert split_form_key("0101CA6E:bad\\name", "p.esm")[0] is None
    assert split_form_key("0101CA6E:café.esm", "p.esm")[0] is None
    assert split_form_key("0101CA6E:" + "a" * 261, "p.esm")[0] is None
    assert split_form_key("0101CA6E:" + "a" * 260, "p.esm")[0] is not None
    assert split_form_key("ZZZZZZ:Fallout4.esm", "p.esm")[0] is None
    assert split_form_key("", "p.esm")[0] is None

    verifier = load_verifier()
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory) / "world-policy.json"
        out.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")
        assert verifier.check_data_file(Path(directory)) == [], verifier.check_data_file(Path(directory))

        placeholder = dict(data)
        placeholder["classes"] = dict(data["classes"], story=[], companion=[])
        out.write_text(json.dumps(placeholder, indent=1) + "\n", encoding="utf-8")
        assert verifier.check_data_file(Path(directory)) != []

    print("self-test PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Convert a Radicle world-policy dump into the Commonwealth Online world-policy.json."
    )
    parser.add_argument("--source", help=f"Radicle policy JSON with schema {SCHEMA}")
    parser.add_argument("--out", help="path to write world-policy.json to")
    parser.add_argument("--plugin", default="Fallout4.esm", help="plugin name for form keys without one")
    parser.add_argument("--version", type=int, default=1, help="world-policy.json version to write")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.source or not args.out:
        parser.error("--source and --out are required")
    if args.version < 1 or args.version > 0xFFFFFF:
        parser.error("--version must be 1 through 0x00FFFFFF")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
