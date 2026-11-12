#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys
import zipfile

ENTRY = "Data/CommonwealthOnline/release.json"
SCHEMA = 1
VERSION_RE = re.compile(r"[0-9]{1,9}(\.[0-9]{1,9})*")
MAX_VERSION = 32


def fail(message):
    sys.stderr.write("make-release-info: error: %s\n" % message)
    sys.exit(1)


def info_bytes(version):
    return (json.dumps({"schema": SCHEMA, "version": version}, indent=2) + "\n").encode("utf-8")


def inject(path, data):
    name = os.path.basename(path)
    if not zipfile.is_zipfile(path):
        fail("%s is not a valid zip" % name)
    with zipfile.ZipFile(path, "a") as archive:
        if any(item.filename.lower() == ENTRY.lower() for item in archive.infolist()):
            fail("%s already has %s" % (name, ENTRY))
        item = zipfile.ZipInfo(ENTRY, date_time=(1980, 1, 1, 0, 0, 0))
        item.compress_type = zipfile.ZIP_DEFLATED
        item.external_attr = 0o644 << 16
        archive.writestr(item, data)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the release.json that tells the launcher which build a game folder holds.")
    parser.add_argument("--version", required=True, help="release version and tag, e.g. 0.0.0.7")
    parser.add_argument("--out", help="write release.json to this path")
    parser.add_argument("--inject", help="add %s to this zip" % ENTRY)
    args = parser.parse_args(argv)

    if len(args.version) > MAX_VERSION or not VERSION_RE.fullmatch(args.version):
        fail("version must be dotted digits (1 to 9 digits each, at most %d characters), got %r" % (MAX_VERSION, args.version))
    if not args.out and not args.inject:
        fail("give --out, --inject or both")
    data = info_bytes(args.version)
    if args.inject:
        inject(args.inject, data)
    if args.out:
        with open(args.out, "wb") as handle:
            handle.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
