#!/usr/bin/env python3
import argparse
import os
import re
import sys

from update_signing import SigningError, sign_document

KEY_ENV = "CO_UPDATE_SIGNING_KEY"
HEX64_RE = re.compile(r"^[0-9a-f]{64}$")


def fail(message):
    sys.stderr.write("sign-latest-json: error: %s\n" % message)
    sys.exit(1)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Sign latest.json with the ed25519 key in $CO_UPDATE_SIGNING_KEY.")
    parser.add_argument("latest", help="path to latest.json")
    parser.add_argument("--sig", help="signature output path (default: <latest>.sig)")
    parser.add_argument(
        "--expect-public-key-hex",
        help="fail unless the key derives this 64-hex ed25519 public key (the launcher's UPDATE_PUBLIC_KEY)",
    )
    args = parser.parse_args(argv)

    pem = os.environ.get(KEY_ENV, "")
    if not pem.strip():
        fail("%s is not set; nothing was signed" % KEY_ENV)
    if not os.path.isfile(args.latest):
        fail("latest.json not found: %s" % args.latest)
    expected = args.expect_public_key_hex
    if expected is not None:
        expected = expected.strip().lower()
        if not HEX64_RE.match(expected):
            fail("--expect-public-key-hex must be 64 hex characters")
    sig_path = args.sig or args.latest + ".sig"

    try:
        public_hex = sign_document(pem, args.latest, sig_path, expected)
    except SigningError as error:
        fail(str(error))
    print("signed %s and verified it against public key %s" % (os.path.basename(args.latest), public_hex))
    return 0


if __name__ == "__main__":
    sys.exit(main())
