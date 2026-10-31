#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import sys
import zipfile
from urllib.parse import urlsplit

CLIENT_MANIFEST = "Data/F4SE/Plugins/CommonwealthOnlineClient.json"
RUNTIMES = (("og", "OG"), ("ng", "NG"), ("ae", "AE"))
VERSION_RE = re.compile(r"^[0-9]+(\.[0-9]+)*$")
FINGERPRINT_RE = re.compile(r"^[0-9a-f]{64}$")


def fail(message):
    sys.stderr.write("make-latest-json: error: %s\n" % message)
    sys.exit(1)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fingerprint_of(path):
    name = os.path.basename(path)
    try:
        with zipfile.ZipFile(path) as archive:
            raw = archive.read(CLIENT_MANIFEST)
    except KeyError:
        fail("%s has no %s" % (name, CLIENT_MANIFEST))
    except zipfile.BadZipFile:
        fail("%s is not a valid zip" % name)
    try:
        value = json.loads(raw.decode("utf-8")).get("contentManifestFingerprint")
    except (ValueError, AttributeError):
        fail("%s has an unreadable %s" % (name, CLIENT_MANIFEST))
    if not isinstance(value, str) or not FINGERPRINT_RE.match(value):
        fail("%s has no 64-hex contentManifestFingerprint" % name)
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the launcher latest.json for one release.")
    parser.add_argument("--dir", required=True, help="directory holding the release zips")
    parser.add_argument("--version", required=True, help="release version, e.g. 0.0.0.7")
    parser.add_argument("--base-url", required=True, help="https prefix the asset urls are built from")
    parser.add_argument("--out", help="output path (default: <dir>/latest.json)")
    args = parser.parse_args(argv)

    if not VERSION_RE.match(args.version):
        fail("version must be dotted digits, got %r" % args.version)
    base = args.base_url.rstrip("/")
    parts = urlsplit(base)
    if parts.scheme != "https" or not parts.netloc:
        fail("base url must be an https URL, got %r" % args.base_url)
    out = args.out or os.path.join(args.dir, "latest.json")

    runtime_names = {key: "CommonwealthOnline-%s-%s.zip" % (tag, args.version) for key, tag in RUNTIMES}
    ui_name = "CommonwealthOnlineUI-%s.zip" % args.version
    required = list(runtime_names.values()) + [ui_name]
    missing = [name for name in required if not os.path.isfile(os.path.join(args.dir, name))]
    if missing:
        fail("missing release asset(s) in %s: %s" % (args.dir, ", ".join(missing)))

    fingerprints = {}
    for key, tag in RUNTIMES:
        fingerprints[tag] = fingerprint_of(os.path.join(args.dir, runtime_names[key]))
    if len(set(fingerprints.values())) != 1:
        fail("runtime archives disagree on contentManifestFingerprint (AE, NG and OG must match)")

    def asset(name):
        return {"url": base + "/" + name, "sha256": sha256_of(os.path.join(args.dir, name))}

    latest = {
        "version": args.version,
        "runtimes": {key: asset(runtime_names[key]) for key, _ in RUNTIMES},
        "ui": asset(ui_name),
        "contentFingerprint": next(iter(fingerprints.values())),
    }
    with open(out, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(latest, handle, indent=2)
        handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
