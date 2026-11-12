import hashlib
import struct
import sys

PLUGIN = 0x01000000
MISC_ID, REFR_ID, QUST_ID = PLUGIN | 0x997F, PLUGIN | 0x9980, PLUGIN | 0x9981
CELL_ID, NEXT_OBJECT_ID, FORM_VERSION = 0x16D8, 0x9982, 131
GET_STAGE, TEMPORARY, MISC_TOP = 58, 9, 0
TES4, GRUP, QUST, HEDR = b"TES4", b"GRUP", b"QUST", b"HEDR"
REC_HDR = GRP_HDR = 24
CELL_LABEL = struct.pack("<I", CELL_ID)

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def zs(s): return s.encode("ascii") + b"\0"
def sub(sig, body): return sig + struct.pack("<H", len(body)) + body
def record(sig, form_id, body): return sig + struct.pack("<IIIIHH", len(body), 0, form_id, 0, FORM_VERSION, 0) + body
def group(label, kind, stamp, body): return GRUP + struct.pack("<I", GRP_HDR + len(body)) + label + struct.pack("<I", kind) + stamp + body

def subs(data, body, size):
    p = body
    while p < body + size:
        n = u16(data, p + 4)
        assert p + 6 + n <= body + size, "bad subrecord at %d" % p
        yield data[p:p + 4], p, n
        p += 6 + n
    assert p == body + size, "record body at %d does not end on a subrecord" % body

def walk(data):
    groups, records = [], []
    def scan(start, end, parents):
        o = start
        while o < end:
            sig, size = data[o:o + 4], u32(data, o + 4)
            if sig == GRUP:
                assert GRP_HDR <= size and o + size <= end, "bad GRUP size at %d" % o
                groups.append((o, size, data[o + 8:o + 12], u32(data, o + 12), parents))
                scan(o + GRP_HDR, o + size, parents + (o,))
                o += size
                continue
            assert o + REC_HDR + size <= end, "bad record size at %d" % o
            edid = None if u32(data, o + 8) & 0x40000 else next((data[a + 6:a + 6 + n].rstrip(b"\0").decode("ascii") for s, a, n in subs(data, o + REC_HDR, size) if s == b"EDID"), None)
            records.append((o, size, sig, u32(data, o + 12), edid, parents))
            o += REC_HDR + size
        assert o == end, "children do not fill their parent at %d" % start
    assert data[:4] == TES4, "not a TES4 plugin"
    scan(REC_HDR + u32(data, 4), len(data), ())
    return groups, records

def one(items, what):
    assert len(items) == 1, "expected one %s, found %d" % (what, len(items))
    return items[0]

MISC_BODY = b"".join([
    sub(b"EDID", zs("CO_CampKit")),
    sub(b"OBND", struct.pack("<6h", -46, -50, -1, 46, 50, 90)),
    sub(b"FULL", zs("Camp Kit")),
    sub(b"MODL", zs("CommonwealthOnline\\CAMP\\CO_CampPrinter.nif")),
    sub(b"DATA", struct.pack("<If", 2, 0.1)),
])
REFR_BODY = b"".join([
    sub(b"NAME", struct.pack("<I", MISC_ID)),
    sub(b"DATA", struct.pack("<6f", -1574.76, -845.97, 60.0, 0.0, 0.0, 0.072)),
])
QUST_BODY = b"".join([
    sub(b"EDID", zs("CO_WelcomeQuest")),
    sub(b"FULL", zs("Welcome to Commonwealth Online")),
    sub(b"DNAM", struct.pack("<HB9x", 0x11, 50)),
    sub(b"NEXT", b""),
    sub(b"INDX", struct.pack("<HBB", 10, 0, 0)),
    sub(b"QSDT", b"\0"),
    sub(b"CNAM", zs("Welcome to Commonwealth Online. Find your camp kit in the Vault.")),
    sub(b"INDX", struct.pack("<HBB", 20, 0, 0)),
    sub(b"QSDT", b"\1"),
    sub(b"CNAM", zs("You found your camp kit.")),
    sub(b"QOBJ", struct.pack("<H", 10)),
    sub(b"FNAM", struct.pack("<I", 0)),
    sub(b"NNAM", zs("Find your camp kit")),
    sub(b"QSTA", struct.pack("<iII", 0, 0, 0)),
    sub(b"CTDA", struct.pack("<B3xfH2xIIIII", 0, 10.0, GET_STAGE, QUST_ID, 0, 0, 0, 0)),
    sub(b"ANAM", struct.pack("<I", 1)),
    sub(b"ALST", struct.pack("<I", 0)),
    sub(b"ALID", zs("CampKit")),
    sub(b"FNAM", struct.pack("<I", 0)),
    sub(b"ALFR", struct.pack("<I", REFR_ID)),
    sub(b"ALED", b""),
])

def main(argv):
    assert len(argv) == 3, "usage: add_camp_kit.py <in.esp> <out.esp>"
    src, dst = argv[1], argv[2]
    data = open(src, "rb").read()
    assert not u32(data, 8) & 0x80, "localized plugins are not supported: strings are not inline"
    groups, records = walk(data)
    assert not {r[3] for r in records} & {MISC_ID, REFR_ID, QUST_ID}, "a new local id is already used"
    assert not {r[4] for r in records} & {"CO_CampKit", "CO_WelcomeQuest"}, "camp kit records already exist"
    top = [g for g in groups if not g[4]]
    assert not [g for g in top if g[2] == b"MISC"], "a top-level MISC group already exists"
    npc = one([g for g in top if g[2] == b"NPC_"], "top-level NPC_ group")
    qust = one([g for g in top if g[2] == QUST], "top-level QUST group")
    cell = one([g for g in groups if g[3] == 6 and g[2] == CELL_LABEL], "cell children group for 0x16D8")
    assert not [g for g in groups if g[3] == TEMPORARY and g[2] == CELL_LABEL], "a temporary children group already exists"
    persistent = one([g for g in groups if g[3] == 8 and g[2] == CELL_LABEL], "persistent children group for 0x16D8")
    hedr = one([a for s, a, n in subs(data, REC_HDR, u32(data, 4)) if s == HEDR], "HEDR")
    assert u16(data, hedr + 4) == 12, "HEDR is not 12 bytes"
    count, next_id = u32(data, hedr + 10), u32(data, hedr + 14)
    new_next = max(next_id, NEXT_OBJECT_ID)

    misc_group = group(b"MISC", MISC_TOP, data[npc[0] + 16:npc[0] + 24], record(b"MISC", MISC_ID, MISC_BODY))
    temp_group = group(CELL_LABEL, TEMPORARY, data[persistent[0] + 16:persistent[0] + 24], record(b"REFR", REFR_ID, REFR_BODY))
    quest = record(QUST, QUST_ID, QUST_BODY)
    inserts = sorted([(npc[0], misc_group), (cell[0] + cell[1], temp_group), (qust[0] + qust[1], quest)])
    grow = {off: len(temp_group) for off in cell[4] + (cell[0],)}
    grow[qust[0]] = len(quest)

    buf = bytearray(data)
    for off, n in grow.items():
        struct.pack_into("<I", buf, off + 4, u32(data, off + 4) + n)
    struct.pack_into("<I", buf, hedr + 10, count + 3)
    struct.pack_into("<I", buf, hedr + 14, new_next)
    out, last = bytearray(), 0
    for off, blob in inserts:
        out += buf[last:off] + blob
        last = off
    out += buf[last:]
    out = bytes(out)

    def shifted(j): return j + sum(len(b) for off, b in inserts if off <= j)
    patched = {i for off in grow for i in range(off + 4, off + 8)} | set(range(hedr + 10, hedr + 18))
    assert len(out) == len(data) + sum(len(b) for _, b in inserts), "output length is wrong"
    for j, byte in enumerate(data):
        if j not in patched:
            assert out[shifted(j)] == byte, "byte 0x%x changed" % j
    for i in patched:
        assert out[shifted(i)] == buf[i], "patched byte 0x%x wrong" % i

    groups2, records2 = walk(out)
    assert len(groups2) == len(groups) + 2 and len(records2) == len(records) + 3, "record or group count wrong"
    for g in groups:
        assert u32(out, shifted(g[0]) + 4) == g[1] + grow.get(g[0], 0), "GRUP size wrong at %d" % g[0]
    assert u32(out, hedr + 10) == count + 3 and u32(out, hedr + 14) == new_next, "HEDR wrong"
    byoff = {g[0]: g for g in groups2}
    new = {r[3]: r for r in records2 if r[3] in (MISC_ID, REFR_ID, QUST_ID)}
    assert len(new) == 3 and new[MISC_ID][2] == b"MISC" and new[REFR_ID][2] == b"REFR" and new[QUST_ID][2] == QUST, "new records missing"
    assert byoff[new[MISC_ID][5][-1]][2:4] == (b"MISC", MISC_TOP), "MISC group is wrong"
    assert byoff[new[REFR_ID][5][-1]][2:4] == (CELL_LABEL, TEMPORARY), "REFR is not in the temporary group of 0x16D8"
    assert byoff[new[QUST_ID][5][-1]][2:4] == (QUST, 0), "QUST is not in the QUST group"
    refr = {s: a for s, a, n in subs(out, new[REFR_ID][0] + REC_HDR, new[REFR_ID][1])}
    assert u32(out, refr[b"NAME"] + 6) == MISC_ID, "REFR base is wrong"
    q = new[QUST_ID]
    field = {}
    for s, a, n in subs(out, q[0] + REC_HDR, q[1]):
        field.setdefault(s, []).append(a)
    alst = u32(out, field[b"ALST"][0] + 6)
    assert u32(out, field[b"ANAM"][0] + 6) == alst + 1, "ANAM is not next alias id"
    assert struct.unpack_from("<i", out, field[b"QSTA"][0] + 6)[0] == alst, "QSTA alias does not match ALST"
    assert u32(out, field[b"ALFR"][0] + 6) == REFR_ID, "ALFR is not the camp kit REFR"
    assert u16(out, field[b"CTDA"][0] + 14) == GET_STAGE and u32(out, field[b"CTDA"][0] + 18) == QUST_ID, "CTDA is not GetStage on the quest"
    assert max(field[b"QOBJ"]) < max(field[b"ANAM"]) < min(field[b"ALST"]), "QUST section order is wrong"

    open(dst, "wb").write(out)
    print("records %d -> %d, groups %d -> %d, HEDR count %d -> %d, nextObjectID 0x%x -> 0x%x" % (
        len(records), len(records2), len(groups), len(groups2), count, count + 3, next_id, new_next))
    print("added MISC %08x, REFR %08x (temporary group under CELL 0x16D8), QUST %08x" % (MISC_ID, REFR_ID, QUST_ID))
    print("inserted %d bytes, patched %d bytes, unchanged %d of %d original bytes" % (
        sum(len(b) for _, b in inserts), len(patched), len(data) - len(patched), len(data)))
    print("sha256 %s -> %s" % (hashlib.sha256(data).hexdigest(), hashlib.sha256(out).hexdigest()))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
