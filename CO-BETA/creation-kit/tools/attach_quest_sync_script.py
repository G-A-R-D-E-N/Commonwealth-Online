import hashlib
import struct
import sys

QUEST_EDID, NEW_SCRIPT = b"CO_CharacterSetupQuest", b"COQuestSyncScript"
QUST, VMAD, EDID, GRUP = b"QUST", b"VMAD", b"EDID", b"GRUP"
REC_HDR = GRP_HDR = 24

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]

def wstr(b, o):
    n = u16(b, o)
    s = b[o + 2:o + 2 + n]
    assert len(s) == n, "wstring ran past the buffer"
    return s, o + 2 + n

def subs(data, body, size):
    p = body
    while p < body + size:
        n = u16(data, p + 4)
        assert p + 6 + n <= body + size, "bad subrecord at %d" % p
        yield data[p:p + 4], p, n
        p += 6 + n
    assert p == body + size, "record body at %d does not end on a subrecord" % body

def skip_val(b, o, t):
    fixed = {0: 0, 1: 8, 3: 4, 4: 4, 5: 1, 6: 4}
    if t in fixed: return o + fixed[t]
    if t == 2: return wstr(b, o)[1]
    if t == 7:
        n, o, e = u32(b, o), o + 4, 7
    else:
        assert 11 <= t <= 17, "unknown VMAD property type %d" % t
        n, o, e = u32(b, o), o + 4, t - 10
    for _ in range(n):
        if e != 7:
            o = skip_val(b, o, e)
            continue
        m, o = u32(b, o), o + 4
        for _ in range(m):
            o = skip_prop(b, o)
    return o

def skip_prop(b, o):
    _, o = wstr(b, o)
    return skip_val(b, o + 2, b[o])

def parse_vmad(b):
    fmt, count = struct.unpack_from("<HH", b, 2)
    assert fmt == 2, "objectFormat %d is not Fallout 4" % fmt
    o, scripts = 6, []
    while len(scripts) < count:
        name, o = wstr(b, o)
        status, props = b[o], u16(b, o + 1)
        start = o = o + 3
        for _ in range(props):
            o = skip_prop(b, o)
        scripts.append((name.decode("ascii"), status, b[start:o]))
    return scripts, o

def parse_fragments(b, o):
    assert b[o] <= 3, "unexpected QUST VMAD prefix %d" % b[o]
    count, o = u16(b, o + 1), o + 3
    _, o = wstr(b, o)
    n, o = u16(b, o + 1), o + 3
    for _ in range(n):
        o = skip_prop(b, o)
    for _ in range(count):
        o += 9
        _, o = wstr(b, o)
        _, o = wstr(b, o)
    aliases, o = u16(b, o), o + 2
    for _ in range(aliases):
        n, o = u16(b, o + 12), o + 14
        for _ in range(n):
            _, o = wstr(b, o)
            p, o = u16(b, o + 1), o + 3
            for _ in range(p):
                o = skip_prop(b, o)
    assert o == len(b), "VMAD has %d undecoded bytes" % (len(b) - o)
    return count

def walk(data):
    found = []
    def scan(start, end, path):
        o = start
        while o < end:
            sig, size = data[o:o + 4], u32(data, o + 4)
            if sig == GRUP:
                assert GRP_HDR <= size and o + size <= end, "bad GRUP size at %d" % o
                scan(o + GRP_HDR, o + size, path + (o,))
                o += size
                continue
            assert o + REC_HDR + size <= end, "bad record size at %d" % o
            if sig == QUST:
                assert not u32(data, o + 8) & 0x40000, "QUST at %d is compressed" % o
                edid = next((data[a + 6:a + 6 + n].rstrip(b"\0") for s, a, n in subs(data, o + REC_HDR, size) if s == EDID), None)
                if edid == QUEST_EDID:
                    found.append((o, size, path))
            o += REC_HDR + size
        assert o == end, "children do not fill their parent at %d" % start
    assert data[:4] == b"TES4", "not a TES4 plugin"
    scan(REC_HDR + u32(data, 4), len(data), ())
    return found

def main(argv):
    assert len(argv) == 3, "usage: attach_quest_sync_script.py <in.esp> <out.esp>"
    src, dst = argv[1], argv[2]; data = open(src, "rb").read()
    assert len(quests := walk(data)) == 1, "expected one %r, found %d" % (QUEST_EDID, len(quests))
    rec, rec_size, groups = quests[0]
    assert (sub := next((a for s, a, n in subs(data, rec + REC_HDR, rec_size) if s == VMAD), None)) is not None, "QUST has no VMAD"
    payload = data[sub + 6:sub + 6 + (vmad_size := u16(data, sub + 4))]
    scripts, frag_start = parse_vmad(payload)
    names = [s[0] for s in scripts]
    frags = parse_fragments(payload, frag_start)
    assert NEW_SCRIPT.decode() not in names, "%s is already attached" % NEW_SCRIPT.decode()
    entry = struct.pack("<H", len(NEW_SCRIPT)) + NEW_SCRIPT + b"\x00\x00\x00"
    print("before:", names, "fragments:", frags, "vmad bytes:", vmad_size)
    insert = sub + 6 + frag_start
    out = bytearray(data[:insert] + entry + data[insert:])
    struct.pack_into("<H", out, sub + 4, vmad_size + len(entry))
    struct.pack_into("<I", out, rec + 4, rec_size + len(entry))
    for g in groups:
        struct.pack_into("<I", out, g + 4, u32(data, g + 4) + len(entry))
    struct.pack_into("<H", out, sub + 10, len(scripts) + 1)
    out = bytes(out)

    assert [(q[0], q[1]) for q in walk(out)] == [(rec, rec_size + len(entry))], "record moved or resized"
    scripts2, frag2 = parse_vmad(out[sub + 6:sub + 6 + vmad_size + len(entry)])
    assert [s[0] for s in scripts2] == names + [NEW_SCRIPT.decode()], "script list wrong"
    assert scripts2[:-1] == scripts, "existing script or its properties changed"
    assert scripts2[-1][1:] == (0, b""), "new entry is not status 0 with no properties"
    assert parse_fragments(out[sub + 6:sub + 6 + vmad_size + len(entry)], frag2) == frags, "fragment section changed"
    assert out[sub + 12:sub + 6 + frag_start + len(entry)] == payload[6:frag_start] + entry, "insert point wrong"
    touched = set(range(insert, insert + len(entry))) | {i for off, n in [(sub + 4, 2), (rec + 4, 4), (sub + 10, 2)] + [(g + 4, 4) for g in groups] for i in range(off, off + n)}
    for i, byte in enumerate(out):
        assert i in touched or byte == data[i if i < insert else i - len(entry)], "byte 0x%x changed" % i
    open(dst, "wb").write(out)

    print("after: ", [s[0] for s in scripts2])
    print("inserted %d bytes at 0x%x, VMAD %d -> %d, scriptCount %d -> %d" % (len(entry), insert, vmad_size, vmad_size + len(entry), len(scripts), len(scripts2)))
    print("QUST %08x dataSize %d -> %d, GRUP %s size %d -> %d, changed %d of %d bytes, sha256 %s -> %s" % (
        u32(data, rec + 12), rec_size, rec_size + len(entry), data[groups[0] + 8:groups[0] + 12].decode("ascii"),
        u32(data, groups[0] + 4), u32(out, groups[0] + 4), len(touched), len(out),
        hashlib.sha256(data).hexdigest(), hashlib.sha256(out).hexdigest()))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
