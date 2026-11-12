import argparse
import collections
import json
import os
import struct
import sys
import zlib

COMPRESSED = 0x00040000
FILTER_PREFIX = "WorkshopRecipeFilter"


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
                out.append((name, form_id, subrecords(body)))
            pos += 24 + size


class Plugin:
    def __init__(self, path):
        self.name = os.path.basename(path).lower()
        buf = open(path, "rb").read()
        header_size = struct.unpack_from("<I", buf, 4)[0]
        header = subrecords(buf[24:24 + header_size])
        self.masters = [d.split(b"\0")[0].decode("latin1").lower() for t, d in header if t == "MAST"]
        self.records = []
        walk(buf, 24 + header_size, len(buf), {"COBJ", "FLST", "CMPO", "KYWD"}, self.records)

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


def expand(created, flst, seen=()):
    if created in seen:
        return []
    if created in flst:
        out = []
        for member in flst[created]:
            out.extend(expand(member, flst, seen + (created,)))
        return out
    return [created]


def extract(paths):
    plugins = [Plugin(p) for p in paths]
    keywords = {}
    flst = {}
    scrap = {}
    for p in plugins:
        for rec, form_id, subs in p.records:
            key = p.key(form_id)
            if rec == "KYWD":
                keywords[key] = edid(subs)
            elif rec == "FLST":
                flst[key] = [p.key(struct.unpack("<I", d)[0]) for t, d in subs if t == "LNAM" and len(d) == 4]
            elif rec == "CMPO":
                mnam = first(subs, "MNAM")
                if mnam and len(mnam) == 4:
                    scrap[key] = p.key(struct.unpack("<I", mnam)[0])
    costs = {}
    stats = collections.Counter()
    for p in plugins:
        for rec, form_id, subs in p.records:
            if rec != "COBJ":
                continue
            fnam = first(subs, "FNAM") or b""
            categories = [keywords.get(p.key(struct.unpack_from("<I", fnam, j)[0]), "") for j in range(0, len(fnam) - 3, 4)]
            if not any(c.startswith(FILTER_PREFIX) for c in categories):
                stats["not_workshop"] += 1
                continue
            cnam = first(subs, "CNAM")
            fvpa = first(subs, "FVPA")
            if not cnam or len(cnam) != 4 or not fvpa:
                stats["incomplete"] += 1
                continue
            components = collections.OrderedDict()
            for j in range(0, len(fvpa) - 7, 8):
                ref = p.key(struct.unpack_from("<I", fvpa, j)[0])
                count = struct.unpack_from("<I", fvpa, j + 4)[0]
                if ref in scrap:
                    ref = scrap[ref]
                    stats["via_component"] += 1
                else:
                    stats["direct_item"] += 1
                components[ref] = components.get(ref, 0) + count
            for target in expand(p.key(struct.unpack("<I", cnam)[0]), flst):
                if target in costs:
                    stats["duplicate_kept_first"] += 1
                    continue
                costs[target] = components
                stats["entries"] += 1
    return costs, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("plugins", nargs="+")
    args = ap.parse_args()
    costs, stats = extract(args.plugins)
    entries = []
    for (plugin, local), components in sorted(costs.items()):
        entries.append({
            "plugin": plugin,
            "localFormId": local,
            "components": [{"plugin": c[0], "localFormId": c[1], "count": n} for c, n in sorted(components.items())],
        })
    lines = ",\n".join(json.dumps(e, separators=(",", ":")) for e in entries)
    text = '{"format":1,"costs":[\n' + lines + "\n]}\n"
    with open(args.out, "w", newline="\n") as f:
        f.write(text)
    print(dict(stats), len(entries), file=sys.stderr)


if __name__ == "__main__":
    main()
