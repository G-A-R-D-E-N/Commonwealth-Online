#!/usr/bin/env python3
import argparse
import struct
import sys
from collections import namedtuple
from pathlib import Path

FILE_MAGIC = b"PRB1"
BLOCK_MAGIC = b"BLCK"
FILE_VERSION = 1
RECORD_VERSION = 1
RECORD_SIZE = 82
MAX_ARGS = 6
KIND_NAMES = {0: "Enter", 1: "Return", 2: "Snapshot"}
FILE_HEADER = struct.Struct("<4sI")
BLOCK_HEADER = struct.Struct("<4sI")
RECORD = struct.Struct("<BHBIQQ6QQBB")
BLOCK_TAIL = struct.Struct("<Q")

Record = namedtuple(
    "Record",
    "version probe_id kind thread_id tick_micros caller_rva args retval arg_count",
)

# TODO-verify(#407)
PROBE_FIELDS = {
    1: ("selftest", ()),
    2: ("res408-unload-insert", ("siteRva", "rcx", "key", "value", "returnRva")),
    3: ("res408-command-execute-dispatch", ("siteRva", "bridgeRdi", "dispatchRax", "dispatchTargetRva")),
    4: ("res408-console-compile", ("siteRva", "rcx", "rdx", "r8", "returnRva")),
    5: ("res364-vslot1-owner", ("siteRva", "objectRcx", "callVtable", "callVtableRva")),
    6: ("res361-ng-entry", ("siteRva", "objectRcx", "returnRva")),
    7: ("res349-taskunpack-case4a", ("siteRva", "taskPtr", "taskType", "taskRef8")),
    8: ("res410-ae-global-call-arg", ("siteRva", "globalRcx", "globalArgRva")),
    9: ("res410-handler-a-og", ("siteRva", "objectRdi", "handlerAFlag")),
    10: ("res410-handler-a-ng", ("siteRva", "objectR14", "handlerAFlag")),
    11: ("res385-setvolume-entry", ("siteRva", "handleRcx", "volumeXmm1", "returnRva")),
    12: ("res407-pitch-channel-copy", ("siteRva", "channelR9", "channelDword")),
    13: ("res553-vmbridge-read", ("siteRva", "bridgeRdi")),
}


class DumpError(Exception):
    pass


def encode_record(record):
    if record.kind not in KIND_NAMES:
        raise DumpError(f"unknown record kind {record.kind}")
    if not 0 <= record.arg_count <= MAX_ARGS:
        raise DumpError(f"argument count {record.arg_count} exceeds {MAX_ARGS}")
    args = tuple(record.args) + (0,) * (MAX_ARGS - len(record.args))
    return RECORD.pack(
        RECORD_VERSION,
        record.probe_id,
        record.kind,
        record.thread_id,
        record.tick_micros,
        record.caller_rva,
        *args,
        record.retval,
        record.arg_count,
        0,
    )


def make_file(records, overwritten=0):
    body = b"".join(encode_record(record) for record in records)
    return (
        FILE_HEADER.pack(FILE_MAGIC, FILE_VERSION)
        + BLOCK_HEADER.pack(BLOCK_MAGIC, len(records))
        + body
        + BLOCK_TAIL.pack(overwritten)
    )


def decode(data):
    if len(data) < FILE_HEADER.size or data[:4] != FILE_MAGIC:
        raise DumpError("bad magic: not a Commonwealth Online probe recording")
    _, version = FILE_HEADER.unpack_from(data, 0)
    if version != FILE_VERSION:
        raise DumpError(f"unknown file version {version} (expected {FILE_VERSION})")

    offset = FILE_HEADER.size
    records = []
    blocks = 0
    overwritten = 0
    while offset < len(data):
        if offset + BLOCK_HEADER.size > len(data):
            raise DumpError("truncated block header")
        magic, count = BLOCK_HEADER.unpack_from(data, offset)
        offset += BLOCK_HEADER.size
        if magic != BLOCK_MAGIC:
            raise DumpError(f"bad block magic at offset {offset - BLOCK_HEADER.size}")
        end = offset + count * RECORD_SIZE
        if end + BLOCK_TAIL.size > len(data):
            raise DumpError(f"truncated block: {count} records need {end} bytes, file has {len(data)}")
        for _ in range(count):
            fields = RECORD.unpack_from(data, offset)
            offset += RECORD_SIZE
            if fields[0] != RECORD_VERSION:
                raise DumpError(f"unknown record version {fields[0]}")
            if fields[2] not in KIND_NAMES:
                raise DumpError(f"unknown record kind {fields[2]}")
            if fields[13] > MAX_ARGS:
                raise DumpError(f"record argument count {fields[13]} exceeds {MAX_ARGS}")
            records.append(
                Record(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6:12], fields[12], fields[13])
            )
        overwritten, = BLOCK_TAIL.unpack_from(data, offset)
        offset += BLOCK_TAIL.size
        blocks += 1
    return records, blocks, overwritten


def format_record(record):
    name, labels = PROBE_FIELDS.get(record.probe_id, (f"unknown-{record.probe_id}", ()))
    fields = " ".join(
        f"{labels[index] if index < len(labels) else f'arg{index}'}=0x{value:016X}"
        for index, value in enumerate(record.args[: record.arg_count])
    )
    return (
        f"v{record.version} probe={record.probe_id} name={name} kind={KIND_NAMES[record.kind]} "
        f"tid={record.thread_id} time={record.tick_micros}us "
        f"callerRva=0x{record.caller_rva:016X} {fields} retval=0x{record.retval:016X}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description="Decode a Commonwealth Online probe recording.")
    parser.add_argument("path", nargs="?", help="recording to decode")
    args = parser.parse_args(argv)

    if not args.path:
        parser.error("a recording path is required")
    try:
        records, blocks, overwritten = decode(Path(args.path).read_bytes())
    except (DumpError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    for record in records:
        print(format_record(record))
    print(f"records={len(records)} blocks={blocks} overwritten={overwritten}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
