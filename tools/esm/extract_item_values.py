import json
import sys
import zlib
from collections import Counter
from pathlib import Path

PLUGINS = (
    "Fallout4.esm",
    "DLCRobot.esm",
    "DLCworkshop01.esm",
    "DLCCoast.esm",
    "DLCworkshop02.esm",
    "DLCworkshop03.esm",
    "DLCNukaWorld.esm",
    "DLCUltraHighResolution.esm",
)
INDEX = {name.lower(): index for index, name in enumerate(PLUGINS)}
VALUE_FIELDS = {
    b"ALCH": (b"ENIT", (20,), 0, True),
    b"AMMO": (b"DATA", (4, 8), 0, False),
    b"ARMO": (b"DATA", (12,), 0, True),
    b"BOOK": (b"DATA", (8,), 0, False),
    b"KEYM": (b"DATA", (8,), 0, True),
    b"MISC": (b"DATA", (8,), 0, True),
    b"NOTE": (b"DATA", (8,), 0, False),
    b"WEAP": (b"DNAM", (132,), 63, False),
}
CAPS_ID = 0x0000000F
DELETED_FLAG = 0x00000020
LIGHT_FLAG = 0x00000200
COMPRESSED_FLAG = 0x00040000
NON_PLAYABLE_FLAG = 0x00000004
NON_PLAYABLE_TYPES = (b"ARMO", b"AMMO", b"WEAP")


def number(data, at, size, signed=False):
    return int.from_bytes(data[at:at + size], "little", signed=signed)


def subrecords(body):
    found = []
    at = 0
    wide = None
    while at + 6 <= len(body):
        kind = body[at:at + 4]
        size = number(body, at + 4, 2)
        at += 6
        if kind == b"XXXX":
            wide = number(body, at, 4)
            at += 4
            continue
        if wide is not None:
            size, wide = wide, None
        if at + size > len(body):
            raise ValueError("subrecord runs past the record body")
        found.append((kind, body[at:at + size]))
        at += size
    return found


def records(data, start, end):
    at = start
    while at < end:
        size = number(data, at + 4, 4)
        if data[at:at + 4] == b"GRUP":
            if size < 24 or at + size > end:
                raise ValueError("bad group size")
            yield from records(data, at + 24, at + size)
            at += size
        else:
            if at + 24 + size > end:
                raise ValueError("bad record size")
            yield at
            at += 24 + size


def read_record(data, at):
    size = number(data, at + 4, 4)
    flags = number(data, at + 8, 4)
    form_id = number(data, at + 12, 4)
    body = data[at + 24:at + 24 + size]
    if flags & COMPRESSED_FLAG:
        declared = number(body, 0, 4)
        body = zlib.decompress(body[4:])
        if len(body) != declared:
            raise ValueError("compressed record size mismatch")
    return flags, form_id, body


def runtime_id(form_id, masters, owner):
    master_number = form_id >> 24
    if master_number == len(masters):
        index = owner
    elif master_number < len(masters):
        index = INDEX.get(masters[master_number].lower())
    else:
        return None
    if index is None:
        return None
    return (index << 24) | (form_id & 0xFFFFFF)


def field_value(body, name, sizes, position, signed):
    matches = [data for kind, data in subrecords(body) if kind == name]
    if len(matches) != 1 or len(matches[0]) not in sizes:
        return None
    return max(0, number(matches[0], position, 4, signed))


def apply_plugin(values, path, owner, report):
    data = path.read_bytes()
    if data[:4] != b"TES4":
        raise ValueError(f"not a plugin: {path}")
    header = number(data, 4, 4)
    if number(data, 8, 4) & LIGHT_FLAG:
        report["light"] += 1
        return
    masters = [
        value.split(b"\0", 1)[0].decode("ascii")
        for kind, value in subrecords(data[24:24 + header])
        if kind == b"MAST"
    ]
    for at in records(data, 24 + header, len(data)):
        kind = data[at:at + 4]
        if kind not in VALUE_FIELDS:
            continue
        flags, form_id, body = read_record(data, at)
        rid = runtime_id(form_id, masters, owner)
        if rid is None:
            report["unresolved"] += 1
            continue
        if rid == CAPS_ID:
            continue
        if flags & DELETED_FLAG:
            values.pop(rid, None)
            report["deleted"] += 1
            continue
        if kind in NON_PLAYABLE_TYPES and flags & NON_PLAYABLE_FLAG:
            values[rid] = (kind, None)
            report["nonPlayable"] += 1
            continue
        name, sizes, position, signed = VALUE_FIELDS[kind]
        value = field_value(body, name, sizes, position, signed)
        if value is None:
            report["mismatch"] += 1
        values[rid] = (kind, value)
        report[kind.decode("ascii")] += 1


def merge(versions):
    seen = {}
    for values in versions:
        for rid, entry in values.items():
            seen.setdefault(rid, set()).add(entry)
    table = {}
    excluded = {}
    for rid, entries in seen.items():
        if len(entries) == 1:
            (entry,) = entries
            if entry[1] is not None:
                table[rid] = entry
                continue
        excluded[rid] = min(kind for kind, _ in entries)
    return table, excluded


def main(argv):
    output = Path(argv[1])
    versions = []
    for directory in map(Path, argv[2:]):
        if not (directory / "Fallout4.esm").is_file():
            raise SystemExit(f"Fallout4.esm missing from {directory}")
        values = {}
        report = Counter()
        for name in PLUGINS:
            path = directory / name
            if path.is_file():
                apply_plugin(values, path, INDEX[name.lower()], report)
            else:
                report["absent"] += 1
        print(directory, dict(report), file=sys.stderr)
        versions.append(values)
    table, excluded = merge(versions)
    if not table:
        raise SystemExit("no item values merged")
    ordered = {str(rid): entry[1] for rid, entry in sorted(table.items())}
    output.write_text(json.dumps(ordered, indent=1) + "\n", encoding="utf-8", newline="\n")
    print("entries", len(table), file=sys.stderr)
    for kind, count in sorted(Counter(entry[0] for entry in table.values()).items()):
        print(kind.decode("ascii"), count, file=sys.stderr)
    print("excluded", len(excluded), dict(Counter(kind.decode("ascii") for kind in excluded.values())), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
