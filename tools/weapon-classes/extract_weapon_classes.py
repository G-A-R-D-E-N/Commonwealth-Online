import argparse
import collections
import json
import os
import struct
import sys
import zlib

COMPRESSED = 0x00040000
DELETED = 0x00000020
LIGHT_MASTER = 0x00000200
ANIMATION_TYPE_INDEX = 54
ANIMATION_CLASS = {
    0: "Melee",
    1: "Melee",
    2: "Melee",
    3: "Melee",
    4: "Melee",
    5: "Melee",
    6: "Melee",
    7: "None",
    8: "None",
    9: "Gun",
    10: "None",
    11: "None",
}
RIFLE_KEYWORD = ("fallout4.esm", 0x0004A0A1)
HEAVY_KEYWORD = ("fallout4.esm", 0x0004A0A3)
RIFLE_KEYWORD_EDID = "WeaponTypeRifle"
HEAVY_KEYWORD_EDID = "WeaponTypeHeavyGun"
OFFICIAL_MASTERS = (
    "fallout4.esm",
    "dlcrobot.esm",
    "dlcworkshop01.esm",
    "dlccoast.esm",
    "dlcworkshop02.esm",
    "dlcworkshop03.esm",
    "dlcnukaworld.esm",
)


def subrecords(data):
    i = 0
    pending = None
    out = []
    while i + 6 <= len(data):
        tag = data[i:i + 4].decode("latin1")
        size = struct.unpack_from("<H", data, i + 4)[0]
        i += 6
        if tag == "XXXX":
            pending = struct.unpack_from("<I", data, i)[0]
            i += size
            continue
        if pending is not None:
            size = pending
            pending = None
        out.append((tag, data[i:i + size]))
        i += size
    return out


def walk(buf, pos, end, wanted, out):
    while pos < end:
        tag = buf[pos:pos + 4]
        size = struct.unpack_from("<I", buf, pos + 4)[0]
        if tag == b"GRUP":
            label = buf[pos + 8:pos + 12].decode("latin1")
            group_type = struct.unpack_from("<I", buf, pos + 12)[0]
            if group_type == 0 and label not in wanted:
                pos += size
                continue
            walk(buf, pos + 24, pos + size, wanted, out)
            pos += size
        else:
            flags, form_id = struct.unpack_from("<II", buf, pos + 8)
            name = tag.decode("latin1")
            if name in wanted:
                body = buf[pos + 24:pos + 24 + size]
                if flags & COMPRESSED:
                    body = zlib.decompress(body[4:])
                out.append((name, form_id, flags, subrecords(body)))
            pos += 24 + size


class Plugin:
    def __init__(self, path):
        self.name = os.path.basename(path).lower()
        with open(path, "rb") as f:
            buf = f.read()
        if struct.unpack_from("<I", buf, 8)[0] & LIGHT_MASTER:
            raise SystemExit(f"{self.name}: light master plugins are not supported by this extractor")
        header_size = struct.unpack_from("<I", buf, 4)[0]
        header = subrecords(buf[24:24 + header_size])
        self.masters = [d.split(b"\0")[0].decode("latin1").lower() for t, d in header if t == "MAST"]
        self.records = []
        walk(buf, 24 + header_size, len(buf), {"KYWD", "WEAP"}, self.records)

    def key(self, form_id):
        index = form_id >> 24
        plugin = self.masters[index] if index < len(self.masters) else self.name
        return (plugin, form_id & 0xFFFFFF)


def first(subs, tag):
    for t, d in subs:
        if t == tag:
            return d
    return None


def edid(subs):
    d = first(subs, "EDID")
    return d.split(b"\0")[0].decode("latin1") if d else ""


def extract(paths):
    plugins = [Plugin(p) for p in paths]
    keywords = {}
    weapons = {}
    stats = collections.Counter()
    for p in plugins:
        for rec, form_id, flags, subs in p.records:
            if rec == "KYWD":
                keywords[p.key(form_id)] = edid(subs)
    if keywords.get(RIFLE_KEYWORD) != RIFLE_KEYWORD_EDID or keywords.get(HEAVY_KEYWORD) != HEAVY_KEYWORD_EDID:
        raise SystemExit(
            f"keyword ids differ: {RIFLE_KEYWORD} is {keywords.get(RIFLE_KEYWORD)!r}, "
            f"{HEAVY_KEYWORD} is {keywords.get(HEAVY_KEYWORD)!r}"
        )
    for p in plugins:
        for rec, form_id, flags, subs in p.records:
            if rec != "WEAP":
                continue
            key = p.key(form_id)
            if flags & DELETED:
                weapons.pop(key, None)
                stats["deleted"] += 1
                continue
            dnam = first(subs, "DNAM") or b""
            if len(dnam) <= ANIMATION_TYPE_INDEX:
                stats["no_dnam"] += 1
                if first(subs, "TNAM") is not None:
                    stats["template"] += 1
                continue
            animation = dnam[ANIMATION_TYPE_INDEX]
            if animation not in ANIMATION_CLASS:
                stats["unknown_animation"] += 1
                continue
            kwda = first(subs, "KWDA") or b""
            keyword_keys = {p.key(struct.unpack_from("<I", kwda, j)[0]) for j in range(0, len(kwda) - 3, 4)}
            weapon_class = ANIMATION_CLASS[animation]
            if weapon_class == "Gun":
                weapon_class = "Heavy" if HEAVY_KEYWORD in keyword_keys else "Rifle" if RIFLE_KEYWORD in keyword_keys else "Light"
            if key in weapons:
                stats["overridden"] += 1
            weapons[key] = (weapon_class, edid(subs))
            stats[f"animation_{animation}"] += 1
            stats[weapon_class] += 1
    return weapons, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--data", required=True)
    args = ap.parse_args()
    weapons, stats = extract([os.path.join(args.data, name) for name in OFFICIAL_MASTERS])
    lines = ",\n".join(
        json.dumps({"plugin": plugin, "localFormId": local, "class": weapon_class, "editorId": editor_id}, separators=(",", ":"))
        for (plugin, local), (weapon_class, editor_id) in sorted(weapons.items())
    )
    with open(args.out, "w", newline="\n", encoding="utf-8") as f:
        f.write('{"format":1,"weapons":[\n' + lines + "\n]}\n")
    print(dict(stats), len(weapons), file=sys.stderr)


if __name__ == "__main__":
    main()
