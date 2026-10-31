from __future__ import annotations

import struct
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ESP = ROOT / "CO-BETA" / "creation-kit" / "esp" / "CommonwealthOnline.esp"

COMPRESSED_FLAG = 0x00040000

REQUIRED_EDITOR_IDS = {
    "COPlayerProxy",
    "COPlayerProxyFemale",
    "CO_CharacterSetupQuest",
    "CO_GenderChoiceMessage",
    "CO_SexChangePending",
    "CO_XMarkerHeading",
    "CO_Markers",
}

REMOVED_ORPHAN_EDITOR_IDS = {
    "LargeRooms",
    "LrgHalls",
}

EXPECTED_RECORD_COUNT = 11


def _payload(data: bytes, start: int, size: int, flags: int) -> bytes:
    body = data[start:start + size]
    if flags & COMPRESSED_FLAG and len(body) >= 4:
        body = zlib.decompress(body[4:])
    return body


def _editor_id(payload: bytes) -> str:
    index = 0
    pending_size = None
    while index + 6 <= len(payload):
        signature = payload[index:index + 4]
        sub_size = struct.unpack_from("<H", payload, index + 4)[0]
        body_start = index + 6
        if signature == b"XXXX" and sub_size == 4:
            pending_size = struct.unpack_from("<I", payload, body_start)[0]
            index = body_start + 4
            continue
        if pending_size is not None:
            sub_size = pending_size
            pending_size = None
        if signature == b"EDID":
            return payload[body_start:body_start + sub_size].split(b"\x00")[0].decode("latin1")
        index = body_start + sub_size
    return ""


def _records(data: bytes, start: int, end: int):
    offset = start
    while offset + 24 <= end:
        signature = data[offset:offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        if signature == b"GRUP":
            if size < 24:
                size = 24
            yield from _records(data, offset + 24, min(offset + size, end))
            offset += size
            continue
        if offset + 24 + size > end:
            break
        flags = struct.unpack_from("<I", data, offset + 8)[0]
        yield signature.decode("latin1"), _editor_id(_payload(data, offset + 24, size, flags))
        offset += 24 + size


def read_records() -> list[tuple[str, str]]:
    data = ESP.read_bytes()
    return [record for record in _records(data, 0, len(data)) if record[0] != "TES4"]


def main() -> int:
    failures: list[str] = []

    if not ESP.exists() or ESP.stat().st_size == 0:
        print("FAIL: ESP-01: CommonwealthOnline.esp is missing or empty")
        return 1

    records = read_records()
    editors = {editor for _, editor in records if editor}

    for editor in sorted(REQUIRED_EDITOR_IDS):
        if editor not in editors:
            failures.append(f"ESP-02: required data form '{editor}' is missing from CommonwealthOnline.esp")

    for editor in sorted(REMOVED_ORPHAN_EDITOR_IDS):
        if editor in editors:
            failures.append(f"ESP-03: unreferenced orphan layer '{editor}' must not be reintroduced")

    if len(records) != EXPECTED_RECORD_COUNT:
        failures.append(
            f"ESP-04: expected {EXPECTED_RECORD_COUNT} authored forms after orphan-layer removal, "
            f"found {len(records)}"
        )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("CommonwealthOnline.esp data contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
