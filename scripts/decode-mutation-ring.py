#!/usr/bin/env python3
# source:CO-BETA/plugin/lib/commonlibf4/include/RE/B/BSCoreTypes.h
# source:co-core/include/F4TMutationRing.h CO-defined dump layout
import argparse
import struct
import sys
from collections import namedtuple
from pathlib import Path

MAGIC = b"CORG"
VERSION = 1
CAPACITY = 4096
FIXTURE_RECORDS = 8
HEADER = struct.Struct("<4sII")
ENTRY = struct.Struct("<QQHIQI")
TRAILER_MAGIC = b"CRFT"
TRAILER = struct.Struct("<4sQQ")
SITE_NAMES = {0: "kUnknown", 1: "kPositionCorrection", 2: "kTimeState", 3: "kWeatherState"}
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIXTURE = ROOT / "co-core/tests/fixtures/mutation-ring-v1.corg"

Entry = namedtuple("Entry", "seq tick site formId address aux")
Fault = namedtuple("Fault", "rip address")
FIXTURE_FAULT = Fault(0x00007FF6A1B2C3D4, 0x0000000000000008)


class DumpError(Exception):
    pass


def encode(entries):
    body = b"".join(ENTRY.pack(e.seq, e.tick, e.site, e.formId, e.address, e.aux) for e in entries)
    return HEADER.pack(MAGIC, VERSION, len(entries)) + body


def decode(data):
    if len(data) < HEADER.size or data[:4] != MAGIC:
        raise DumpError("bad magic: not a CORG mutation ring dump")
    _, version, count = HEADER.unpack_from(data, 0)
    if version != VERSION:
        raise DumpError(f"unknown dump version {version} (expected {VERSION})")
    if count > CAPACITY:
        raise DumpError(f"record count {count} exceeds ring capacity {CAPACITY}")
    end = HEADER.size + count * ENTRY.size
    if len(data) < end:
        raise DumpError(f"truncated dump: {count} records need {end} bytes, file has {len(data)}")
    entries = [Entry(*ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)) for i in range(count)]
    fault = None
    rest = data[end:]
    if len(rest) >= TRAILER.size and rest[:4] == TRAILER_MAGIC:
        _, rip, address = TRAILER.unpack_from(rest, 0)
        fault = Fault(rip, address)
    return entries, fault


def encode_with_fault(entries, fault):
    return encode(entries) + TRAILER.pack(TRAILER_MAGIC, fault.rip, fault.address)


def parse_site(text):
    lowered = text.lower()
    for number, name in SITE_NAMES.items():
        if lowered in (str(number), name.lower()):
            return number
    raise ValueError(f"unknown site {text!r}; use a number or one of {', '.join(SITE_NAMES.values())}")


def format_entry(entry):
    site = SITE_NAMES.get(entry.site, str(entry.site))
    return (f"seq={entry.seq} tick={entry.tick} site={site} "
            f"formId=0x{entry.formId:08X} address=0x{entry.address:016X} aux=0x{entry.aux:08X}")


def select(entries, args):
    selected = entries
    if args.site is not None:
        site = parse_site(args.site)
        selected = [e for e in selected if e.site == site]
    if args.form is not None:
        form = int(args.form, 16)
        selected = [e for e in selected if e.formId == form]
    if args.addr_min is not None:
        low = int(args.addr_min, 0)
        selected = [e for e in selected if e.address >= low]
    if args.addr_max is not None:
        high = int(args.addr_max, 0)
        selected = [e for e in selected if e.address <= high]
    if args.last is not None:
        selected = selected[-args.last:] if args.last > 0 else []
    return selected


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def self_test(fixture):
    entries = [
        Entry(1, 100, 1, 0x00000014, 0x7FF000000001, 5),
        Entry(2, 200, 2, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0),
        Entry(3, 300, 3, 0x0001ABCD, 0x0000000000001000, 0xDEADBEEF),
    ]
    fault = FIXTURE_FAULT
    dump = encode_with_fault(entries, fault)
    check(dump[:4] == MAGIC and dump[4:8] == struct.pack("<I", 1), "header layout")
    decoded, decoded_fault = decode(dump)
    check(decoded == entries, "in-memory round trip")
    check(decoded_fault == fault, "fault trailer round trip")
    check(decode(encode(entries))[1] is None, "dump without trailer has no fault")

    for mutate, label in (
        (lambda d: b"XXXX" + d[4:], "bad magic"),
        (lambda d: d[:4] + struct.pack("<I", 2) + d[8:], "unknown version"),
        (lambda d: d[:-(TRAILER.size + ENTRY.size + 1)], "truncated records"),
    ):
        try:
            decode(mutate(encode(entries)))
        except DumpError:
            pass
        else:
            raise AssertionError(f"{label} was accepted")

    fixture_path = Path(fixture) if fixture else DEFAULT_FIXTURE
    fixture_entries, fixture_fault = decode(fixture_path.read_bytes())
    check(len(fixture_entries) == FIXTURE_RECORDS, f"fixture holds {len(fixture_entries)} records, expected {FIXTURE_RECORDS}")
    check([e.seq for e in fixture_entries] == list(range(1, FIXTURE_RECORDS + 1)), "fixture seq is not 1..8")
    check(fixture_fault == FIXTURE_FAULT, "fixture fault trailer differs from the C++ writer")
    print(f"fixture {fixture_path}: {len(fixture_entries)} records, fault rip=0x{fixture_fault.rip:016X}")
    print("self-test PASS")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Decode a Commonwealth Online mutation ring .corg dump.")
    parser.add_argument("path", nargs="?", help="dump file to decode")
    parser.add_argument("--site", help="site number or name, e.g. kWeatherState")
    parser.add_argument("--form", help="hex form id, e.g. 14 or 0x14")
    parser.add_argument("--addr-min", help="lowest address (decimal or 0x hex)")
    parser.add_argument("--addr-max", help="highest address (decimal or 0x hex)")
    parser.add_argument("--last", type=int, help="only the newest N records after filtering")
    parser.add_argument("--self-test", action="store_true", help="run the built-in decoder checks")
    parser.add_argument("--fixture", help="with --self-test, decode this dump instead of the committed fixture")
    args = parser.parse_args(argv)

    try:
        if args.self_test:
            return self_test(args.fixture)
        if not args.path:
            parser.error("a dump path is required unless --self-test is given")
        entries, fault = decode(Path(args.path).read_bytes())
        for entry in select(entries, args):
            print(format_entry(entry))
        if fault is not None:
            print(f"fault rip=0x{fault.rip:016X} address=0x{fault.address:016X}")
        return 0
    except (DumpError, OSError, ValueError, AssertionError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
