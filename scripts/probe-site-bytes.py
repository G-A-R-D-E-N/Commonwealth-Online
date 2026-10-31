#!/usr/bin/env python3
import argparse
import struct
import sys
from pathlib import Path

EXE_DIR = Path("E:/RE-work/Tools/steamless-work")
EXE_NAMES = {
    "163": "Fallout4.163.exe.unpacked.exe",
    "984": "Fallout4.984.exe.unpacked.exe",
    "240": "Fallout4.240.exe.unpacked.exe",
}
PE32_PLUS_MAGIC = 0x20B
IMAGE_SCN_MEM_EXECUTE = 0x20000000
SECTION_HEADER_SIZE = 40


class SiteError(Exception):
    pass


def read_sections(data):
    if data[:2] != b"MZ":
        raise SiteError("not an MZ image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SiteError("bad PE signature")
    section_count, = struct.unpack_from("<H", data, pe + 6)
    optional_size, = struct.unpack_from("<H", data, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from("<H", data, optional)
    if magic != PE32_PLUS_MAGIC:
        raise SiteError("not a PE32+ image")
    image_base, = struct.unpack_from("<Q", data, optional + 24)
    sections = []
    for index in range(section_count):
        offset = optional + optional_size + index * SECTION_HEADER_SIZE
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, offset + 8)
        characteristics, = struct.unpack_from("<I", data, offset + 36)
        sections.append((virtual_address, virtual_size, raw_pointer, raw_size, characteristics))
    return image_base, sections


def file_offset(sections, rva, length):
    for virtual_address, virtual_size, raw_pointer, raw_size, characteristics in sections:
        if not virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            continue
        relative = rva - virtual_address
        if relative + length > raw_size:
            raise SiteError(f"RVA 0x{rva:X} + {length} runs past the section's raw data")
        if not characteristics & IMAGE_SCN_MEM_EXECUTE:
            raise SiteError(f"RVA 0x{rva:X} is not in an executable section")
        return raw_pointer + relative
    raise SiteError(f"RVA 0x{rva:X} is not inside any section")


def site_bytes(runtime, rva, length):
    path = EXE_DIR / EXE_NAMES[runtime]
    data = path.read_bytes()
    image_base, sections = read_sections(data)
    if image_base != 0x140000000:
        raise SiteError(f"{path.name}: unexpected image base 0x{image_base:X}")
    offset = file_offset(sections, rva, length)
    return offset, data[offset:offset + length]


def parse_site(text):
    runtime, _, rva_text = text.partition(":")
    if runtime not in EXE_NAMES or not rva_text:
        raise argparse.ArgumentTypeError(f"site must be <163|984|240>:<RVA>, got {text!r}")
    return runtime, int(rva_text, 0)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Read expected instruction bytes at RVAs from the unpacked runtime executables.")
    parser.add_argument("sites", nargs="+", type=parse_site, help="<163|984|240>:<RVA>, e.g. 240:0xBE91A0")
    parser.add_argument("--length", type=int, default=16, help="bytes to read per site (default 16)")
    args = parser.parse_args(argv)

    status = 0
    for runtime, rva in args.sites:
        try:
            offset, observed = site_bytes(runtime, rva, args.length)
        except (SiteError, OSError) as error:
            print(f"{runtime} 0x{rva:X}: error: {error}", file=sys.stderr)
            status = 1
            continue
        text = ", ".join(f"0x{value:02X}" for value in observed)
        print(f"{runtime} 0x{rva:X} file_offset=0x{offset:X}: {text}")
    return status


if __name__ == "__main__":
    sys.exit(main())
