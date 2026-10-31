#!/usr/bin/env python3
"""Extract a single file from a Fallout 4 BA2 (BTDX) archive."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

def read_name(data: bytes, offset: int) -> tuple[str, int]:
    length = struct.unpack_from("<H", data, offset)[0]
    start = offset + 2
    name = data[start : start + length].decode("utf-8", errors="replace")
    return name, start + length

def extract_file(archive: Path, target: str, output: Path) -> None:
    data = archive.read_bytes()
    if data[:4] != b"BTDX":
        raise SystemExit(f"Not a BA2 archive: {archive}")

    version = struct.unpack_from("<I", data, 4)[0]
    if version not in (1, 7, 8):
        raise SystemExit(f"Unsupported BA2 version {version}")

    archive_type = data[8:12].decode("ascii", errors="replace")
    if archive_type != "GNRL":
        raise SystemExit(f"Unsupported archive type {archive_type!r} (only GNRL supported)")

    num_files = struct.unpack_from("<I", data, 12)[0]
    name_table = struct.unpack_from("<Q", data, 16)[0]
    file_table = 24

    target_norm = target.replace("/", "\\").lower()
    name_cursor = name_table
    for i in range(num_files):
        name, name_cursor = read_name(data, name_cursor)
        entry_offset = file_table + i * 36
        (
            _name_hash,
            _ext,
            _dir_hash,
            _flags,
            file_offset,
            packed_size,
            unpacked_size,
            _unk,
        ) = struct.unpack_from("<I4sIIQIII", data, entry_offset)

        if name.lower() != target_norm:
            continue

        if packed_size and unpacked_size and packed_size != unpacked_size:
            payload = zlib.decompress(data[file_offset : file_offset + packed_size])
        else:
            size = unpacked_size or packed_size
            payload = data[file_offset : file_offset + size]

        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(payload)
        print(f"Extracted {name} ({len(payload)} bytes) -> {output}")
        return

    raise SystemExit(f"File not found in archive: {target}")

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("target", help="Archive path, e.g. Interface\\MainMenu.swf")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    extract_file(args.archive, args.target, args.output)

if __name__ == "__main__":
    main()
