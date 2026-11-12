import argparse
import json
import mmap
import os
import struct
import sys
import zlib

OFFICIAL_MASTERS = (
    "Fallout4.esm",
    "DLCRobot.esm",
    "DLCworkshop01.esm",
    "DLCCoast.esm",
    "DLCworkshop02.esm",
    "DLCworkshop03.esm",
    "DLCNukaWorld.esm",
    "DLCUltraHighResolution.esm",
)
FALLOUT4 = "fallout4.esm"
FOOD_KEYWORD = (FALLOUT4, 0x055ECC)
DRINK_KEYWORDS = {
    (FALLOUT4, 0x0F4AEC): "ObjectTypeDrink",
    (FALLOUT4, 0x0F4AED): "ObjectTypeWater",
    (FALLOUT4, 0x249F30): "ObjectTypeNukaCola",
    (FALLOUT4, 0x10C416): "ObjectTypeAlcohol",
    (FALLOUT4, 0x249F31): "ObjectTypeCaffeinated",
}
KEYWORD_EDIDS = {FOOD_KEYWORD: "ObjectTypeFood", **DRINK_KEYWORDS}
RECORD_HEADER = 24
COMPRESSED = 0x00040000
DELETED = 0x00000020
LIGHT = 0x00000200
HOSTILE = 0x00020000
LOCAL_MASK = 0x00FFFFFF
LIGHT_LOCAL_MASK = 0x00000FFF


def u32(data, pos):
    return struct.unpack_from("<I", data, pos)[0]


def subrecords(body):
    pos = 0
    oversize = None
    while pos + 6 <= len(body):
        kind = body[pos:pos + 4]
        size = struct.unpack_from("<H", body, pos + 4)[0]
        pos += 6
        if kind == b"XXXX":
            oversize = u32(body, pos)
            pos += 4
            continue
        if oversize is not None:
            size, oversize = oversize, None
        yield kind, body[pos:pos + size]
        pos += size


def top_groups(data):
    pos = RECORD_HEADER + u32(data, 4)
    while pos < len(data):
        if data[pos:pos + 4] != b"GRUP":
            raise ValueError("expected a top-level GRUP")
        size = u32(data, pos + 4)
        yield data[pos + 8:pos + 12], pos + RECORD_HEADER, pos + size
        pos += size


def group_records(data, start, end):
    pos = start
    while pos < end:
        if data[pos:pos + 4] == b"GRUP":
            pos += u32(data, pos + 4)
            continue
        size = u32(data, pos + 4)
        flags = u32(data, pos + 8)
        body = data[pos + RECORD_HEADER:pos + RECORD_HEADER + size]
        if flags & COMPRESSED:
            body = zlib.decompress(body[4:])
        yield u32(data, pos + 12), flags, body
        pos += RECORD_HEADER + size


def parse_plugin(data, name):
    tes4 = data[RECORD_HEADER:RECORD_HEADER + u32(data, 4)]
    masters = [value.rstrip(b"\0").decode("latin-1") for kind, value in subrecords(tes4) if kind == b"MAST"]
    light = bool(u32(data, 8) & LIGHT)
    own = len(masters)

    def identity(raw):
        top = raw >> 24
        if top < own:
            owner = masters[top].lower()
        elif top == own:
            owner = name.lower()
        else:
            raise ValueError(f"{name}: form {raw:08X} names a master past the list")
        local = raw & LOCAL_MASK
        if top == own and light and local > LIGHT_LOCAL_MASK:
            raise ValueError(f"{name}: light form {raw:08X} is past the light range")
        return (owner, local)

    keywords = {}
    alch = {}
    for label, start, end in top_groups(data):
        if label == b"KYWD":
            for fid, flags, body in group_records(data, start, end):
                edid = next((v.rstrip(b"\0").decode("latin-1") for k, v in subrecords(body) if k == b"EDID"), "")
                keywords[identity(fid)] = edid
        elif label == b"ALCH":
            for fid, flags, body in group_records(data, start, end):
                if flags & DELETED:
                    alch[identity(fid)] = None
                    continue
                edid = ""
                enit_flags = 0
                refs = set()
                for kind, value in subrecords(body):
                    if kind == b"EDID":
                        edid = value.rstrip(b"\0").decode("latin-1")
                    elif kind == b"ENIT" and len(value) >= 8:
                        enit_flags = u32(value, 4)
                    elif kind == b"KWDA":
                        refs = {identity(u32(value, i)) for i in range(0, len(value), 4)}
                alch[identity(fid)] = {"edid": edid, "hostile": bool(enit_flags & HOSTILE), "keywords": frozenset(refs)}
    return keywords, alch


def read_plugin(path):
    with open(path, "rb") as handle:
        data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            return parse_plugin(data, os.path.basename(path))
        finally:
            data.close()


def load_order(data_dir):
    order = []
    seen = set()
    for name in OFFICIAL_MASTERS:
        if os.path.isfile(os.path.join(data_dir, name)) and name.lower() not in seen:
            order.append(name)
            seen.add(name.lower())
    game_dir = os.path.dirname(os.path.normpath(data_dir))
    with open(os.path.join(game_dir, "Fallout4.ccc"), encoding="latin-1") as handle:
        listed = [line.strip() for line in handle]
    for name in listed:
        if name.lower().endswith((".esm", ".esl")) and name.lower() not in seen and os.path.isfile(os.path.join(data_dir, name)):
            order.append(name)
            seen.add(name.lower())
    return order


def install_entries(data_dir):
    entries = {}
    excluded = []
    winners = {}
    keywords = {}
    order = load_order(data_dir)
    for name in order:
        path = os.path.join(data_dir, name)
        plugin_keywords, plugin_alch = read_plugin(path)
        keywords.update(plugin_keywords)
        for ident, record in plugin_alch.items():
            if record is None:
                winners.pop(ident, None)
            else:
                winners[ident] = record
        size = os.path.getsize(path)
        print(f"plugin {name} size {size} mtime {os.path.getmtime(path):.0f}")
    for keyword, expected in KEYWORD_EDIDS.items():
        found = keywords.get(keyword)
        if found != expected:
            raise SystemExit(f"{data_dir}: keyword {keyword[1]:06X} is {found!r}, expected {expected!r}")
    for ident, record in winners.items():
        kind = None
        if record["keywords"] & set(DRINK_KEYWORDS):
            kind = "drink"
        elif FOOD_KEYWORD in record["keywords"]:
            kind = "food"
        if kind is None:
            continue
        key = f"{ident[0]}:{ident[1]:08X}"
        if record["hostile"]:
            excluded.append((kind, key, record["edid"]))
            continue
        entries[key] = (kind, record["edid"])
    return entries, excluded, len(winners)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("data_dirs", nargs="+")
    args = parser.parse_args()
    merged = {}
    for data_dir in args.data_dirs:
        entries, excluded, alch_count = install_entries(data_dir)
        food = sum(1 for kind, _ in entries.values() if kind == "food")
        print(f"{data_dir}: alch {alch_count} food {food} drink {len(entries) - food} hostile_excluded {len(excluded)}")
        for kind, key, edid in excluded:
            print(f"  excluded hostile {kind} {key} {edid}")
        for key, (kind, edid) in entries.items():
            prior = merged.get(key)
            if prior is not None and prior[0] != kind:
                raise SystemExit(f"{key} is {prior[0]} in one install and {kind} in another")
            merged[key] = (kind, edid)
    items = [{"itemKey": key, "kind": merged[key][0]} for key in sorted(merged)]
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        json.dump({"format": 1, "items": items}, handle, indent=2)
        handle.write("\n")
    food = sum(1 for kind, _ in merged.values() if kind == "food")
    print(f"written {args.out}: items {len(items)} food {food} drink {len(items) - food}")
    for key in sorted(merged):
        print(f"{merged[key][0]} {key} {merged[key][1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
