import hashlib
import struct
import sys

CONT_ID, NEXT_OBJECT_ID, FORM_VERSION = 0x01009984, 0x9985, 131
CONT_TOP = 0
TES4, GRUP, HEDR = b"TES4", b"GRUP", b"HEDR"
REC_HDR = GRP_HDR = 24

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

CONT_BODY = b"".join([
    sub(b"EDID", zs("CO_DeathBag")),
    sub(b"OBND", struct.pack("<6h", -48, -14, -1, 47, 14, 30)),
    sub(b"FULL", zs("Death Bag")),
    sub(b"MODL", zs("SetDressing" + chr(92) + "DuffleBag" + chr(92) + "DuffleBag.nif")),
    sub(b"DATA", struct.pack("<Bf", 1, 0.0)),
    sub(b"KSIZ", struct.pack("<I", 1)),
    sub(b"KWDA", struct.pack("<I", 0x00184462)),  # TODO-verify: vanilla keyword/sound ids unverified against xEdit/CK
    sub(b"SNAM", struct.pack("<I", 0x0003C602)),
    sub(b"QNAM", struct.pack("<I", 0x0003C603)),
    sub(b"TNAM", struct.pack("<I", 0x0003C604)),
])

def main(argv):
    assert len(argv) == 3, "usage: add_death_bag.py <in.esp> <out.esp>"
    data = open(argv[1], "rb").read()
    assert not u32(data, 8) & 0x80, "localized plugins are not supported: strings are not inline"
    groups, records = walk(data)
    assert CONT_ID not in {r[3] for r in records}, "the death bag form id is already used"
    assert "CO_DeathBag" not in {r[4] for r in records}, "the death bag record already exists"
    top = [g for g in groups if not g[4]]
    assert not [g for g in top if g[2] == b"CONT"], "a top-level CONT group already exists"
    npc = [g for g in top if g[2] == b"NPC_"]
    assert len(npc) == 1, "expected one top-level NPC_ group"
    npc = npc[0]
    hedr = [a for s, a, n in subs(data, REC_HDR, u32(data, 4)) if s == HEDR]
    assert len(hedr) == 1 and u16(data, hedr[0] + 4) == 12, "HEDR is wrong"
    hedr = hedr[0]
    count, next_id = u32(data, hedr + 10), u32(data, hedr + 14)
    assert next_id <= NEXT_OBJECT_ID, "next object id is already past the death bag id"

    blob = group(b"CONT", CONT_TOP, data[npc[0] + 16:npc[0] + 24], record(b"CONT", CONT_ID, CONT_BODY))
    buf = bytearray(data)
    struct.pack_into("<I", buf, hedr + 10, count + 2)
    struct.pack_into("<I", buf, hedr + 14, NEXT_OBJECT_ID)
    out = bytes(buf[:npc[0]]) + blob + bytes(buf[npc[0]:])

    groups2, records2 = walk(out)
    assert len(groups2) == len(groups) + 1 and len(records2) == len(records) + 1, "record or group count wrong"
    new = [r for r in records2 if r[3] == CONT_ID]
    assert len(new) == 1 and new[0][2] == b"CONT" and new[0][4] == "CO_DeathBag", "new CONT is missing"
    assert not [s for s, a, n in subs(out, new[0][0] + REC_HDR, new[0][1]) if s in (b"CNTO", b"COCT")], "death bag must have no default items"
    assert out[npc[0] + len(blob):npc[0] + len(blob) + 4] == GRUP and out[:hedr + 10] == data[:hedr + 10], "existing bytes moved"

    open(argv[2], "wb").write(out)
    print("added CONT %08x CO_DeathBag, records %d -> %d, next object id 0x%x -> 0x%x" % (CONT_ID, len(records), len(records2), next_id, NEXT_OBJECT_ID))
    print("sha256 %s -> %s" % (hashlib.sha256(data).hexdigest(), hashlib.sha256(out).hexdigest()))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
