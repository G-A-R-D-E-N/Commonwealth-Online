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
SCHEMA = 2
MIN_LAUNCHER_DEFAULT = "0.0.0.0"
VERSION_RE = re.compile(r"^[0-9]+(\.[0-9]+)*$")
MAX_COMPONENT = 0xFFFFFFFF
MAX_PART = 512 << 20
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


def check_launcher_zip(path, expected_name):
    if os.path.basename(path) != expected_name:
        fail("launcher zip must be named %s, got %s" % (expected_name, os.path.basename(path)))
    if not os.path.isfile(path):
        fail("launcher zip not found: %s" % path)
    if not zipfile.is_zipfile(path):
        fail("%s is not a valid zip" % expected_name)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the schema 2 latest.json for one release.")
    parser.add_argument("--dir", required=True, help="directory holding the release zips")
    parser.add_argument("--version", required=True, help="release version and tag, e.g. 0.0.0.7")
    parser.add_argument("--base-url", required=True, help="https prefix of the public release download path")
    parser.add_argument(
        "--min-launcher",
        default=MIN_LAUNCHER_DEFAULT,
        help="oldest launcher allowed to install this release (default %s)" % MIN_LAUNCHER_DEFAULT,
    )
    parser.add_argument("--launcher-zip", help="optional CommonwealthOnline-Launcher-<version>.zip; adds the launcher entry")
    parser.add_argument("--out", help="output path (default: <dir>/latest.json)")
    args = parser.parse_args(argv)

    for label, value in (("version", args.version), ("min-launcher", args.min_launcher)):
        if not VERSION_RE.match(value) or any(int(part) > MAX_COMPONENT for part in value.split(".")):
            fail("%s must be dotted digits, each at most %d, got %r" % (label, MAX_COMPONENT, value))
    base = args.base_url.rstrip("/")
    parts = urlsplit(base)
    if parts.scheme != "https" or not parts.netloc:
        fail("base url must be an https URL, got %r" % args.base_url)
    out = args.out or os.path.join(args.dir, "latest.json")

    unified_name = "CommonwealthOnline-%s.zip" % args.version
    if os.path.isfile(os.path.join(args.dir, unified_name)):
        runtime_names = {key: unified_name for key, _ in RUNTIMES}
        ui_name = unified_name
    else:
        runtime_names = {key: "CommonwealthOnline-%s-%s.zip" % (tag, args.version) for key, tag in RUNTIMES}
        ui_name = "CommonwealthOnlineUI-%s.zip" % args.version
    launcher_name = "CommonwealthOnline-Launcher-%s.zip" % args.version
    required = list(runtime_names.values()) + [ui_name]
    missing = [name for name in required if not os.path.isfile(os.path.join(args.dir, name))]
    if missing:
        fail("missing release asset(s) in %s: %s" % (args.dir, ", ".join(missing)))

    fingerprints = {}
    for key, tag in RUNTIMES:
        fingerprints[tag] = fingerprint_of(os.path.join(args.dir, runtime_names[key]))
    if len(set(fingerprints.values())) != 1:
        fail("runtime archives disagree on contentManifestFingerprint (AE, NG and OG must match)")

    def asset(path, name):
        size = os.path.getsize(path)
        if not 0 < size <= MAX_PART:
            fail("%s is %d bytes; the launcher accepts 1 to %d" % (name, size, MAX_PART))
        return {"url": base + "/" + name, "sha256": sha256_of(path), "size": os.path.getsize(path)}

    latest = {
        "schema": SCHEMA,
        "version": args.version,
        "minLauncher": args.min_launcher,
        "runtimes": {key: asset(os.path.join(args.dir, runtime_names[key]), runtime_names[key]) for key, _ in RUNTIMES},
        "ui": asset(os.path.join(args.dir, ui_name), ui_name),
    }
    if args.launcher_zip is not None:
        check_launcher_zip(args.launcher_zip, launcher_name)
        latest["launcher"] = asset(args.launcher_zip, launcher_name)
    latest["contentFingerprint"] = next(iter(fingerprints.values()))

    with open(out, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(latest, handle, indent=2)
        handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
