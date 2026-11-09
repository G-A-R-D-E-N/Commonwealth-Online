#!/usr/bin/env python3
import os
import shutil
import sys
import tempfile

from update_signing import SigningError, checked, public_key_hex


def main(argv=None):
    work = tempfile.mkdtemp(prefix="co-update-keygen-")
    try:
        key = os.path.join(work, "signing-key.pem")
        checked("genpkey", "-algorithm", "ed25519", "-out", key)
        with open(key, encoding="utf-8") as handle:
            pem = handle.read()
        public_hex = public_key_hex(key)
    except SigningError as error:
        sys.stderr.write("gen-update-key: error: %s\n" % error)
        return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    sys.stdout.write("# (a) Private key PEM. Store as repository secret CO_UPDATE_SIGNING_KEY. Never commit it.\n")
    sys.stdout.write(pem if pem.endswith("\n") else pem + "\n")
    sys.stdout.write("# (b) Public key, 64 hex. Paste as the launcher constant UPDATE_PUBLIC_KEY.\n")
    sys.stdout.write(public_hex + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
