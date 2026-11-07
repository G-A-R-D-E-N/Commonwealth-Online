#!/usr/bin/env python3
import base64
import os
import shutil
import subprocess
import tempfile
from contextlib import contextmanager

ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
PUBLIC_KEY_BYTES = 32
SIGNATURE_BYTES = 64


class SigningError(Exception):
    pass


def openssl(*args):
    binary = os.environ.get("OPENSSL") or "openssl"
    try:
        return subprocess.run([binary, *args], capture_output=True)
    except OSError as error:
        raise SigningError("cannot run %s: %s" % (binary, error.strerror)) from None


def checked(*args):
    result = openssl(*args)
    if result.returncode != 0:
        raise SigningError("openssl %s failed" % args[0])
    return result.stdout


@contextmanager
def secret_workspace(pem_text):
    directory = tempfile.mkdtemp(prefix="co-update-sign-")
    try:
        key = os.path.join(directory, "signing-key.pem")
        descriptor = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(pem_text)
        yield directory, key
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def raw_public_key_hex(der):
    if len(der) != len(ED25519_SPKI_PREFIX) + PUBLIC_KEY_BYTES or not der.startswith(ED25519_SPKI_PREFIX):
        raise SigningError("key is not an ed25519 key")
    return der[len(ED25519_SPKI_PREFIX):].hex()


def public_key_hex(private_key_path):
    return raw_public_key_hex(checked("pkey", "-in", private_key_path, "-pubout", "-outform", "DER"))


def sign_document(pem_text, data_path, sig_path, expected_public_hex=None):
    with secret_workspace(pem_text) as (work, key):
        public_hex = public_key_hex(key)
        if expected_public_hex is not None and public_hex != expected_public_hex.lower():
            raise SigningError("signing key does not match the expected public key")
        raw_path = os.path.join(work, "signature.bin")
        checked("pkeyutl", "-sign", "-rawin", "-inkey", key, "-in", data_path, "-out", raw_path)
        with open(raw_path, "rb") as handle:
            signature = handle.read()
    if len(signature) != SIGNATURE_BYTES:
        raise SigningError("openssl produced a %d-byte signature" % len(signature))
    with open(sig_path, "w", encoding="ascii", newline="\n") as handle:
        handle.write(base64.b64encode(signature).decode("ascii") + "\n")
    if not verify_document(public_hex, data_path, sig_path):
        os.remove(sig_path)
        raise SigningError("the new signature did not verify; no signature was kept")
    return public_hex


def verify_document(public_hex, data_path, sig_path):
    if not isinstance(public_hex, str) or len(public_hex) != 2 * PUBLIC_KEY_BYTES:
        raise SigningError("public key must be 64 hex characters")
    try:
        key_bytes = bytes.fromhex(public_hex)
    except ValueError:
        raise SigningError("public key must be 64 hex characters") from None
    with open(sig_path, encoding="ascii") as handle:
        text = handle.read().strip()
    try:
        signature = base64.b64decode(text, validate=True)
    except ValueError:
        return False
    if len(signature) != SIGNATURE_BYTES:
        return False
    with tempfile.TemporaryDirectory(prefix="co-update-verify-") as work:
        der_path = os.path.join(work, "public.der")
        raw_path = os.path.join(work, "signature.bin")
        with open(der_path, "wb") as handle:
            handle.write(ED25519_SPKI_PREFIX + key_bytes)
        with open(raw_path, "wb") as handle:
            handle.write(signature)
        result = openssl(
            "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey", der_path,
            "-rawin", "-in", data_path, "-sigfile", raw_path,
        )
    return result.returncode == 0
