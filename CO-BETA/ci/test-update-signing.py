import base64
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import update_signing  # noqa: E402

SIGN = os.path.join(HERE, "sign-latest-json.py")
KEYGEN = os.path.join(HERE, "gen-update-key.py")
KEY_ENV = "CO_UPDATE_SIGNING_KEY"
LATEST = b'{"schema":2,"version":"0.0.0.7"}\n'
SIG_RE = re.compile(r"^[A-Za-z0-9+/]{86}==\n$")


def openssl_path():
    return os.environ.get("OPENSSL") or shutil.which("openssl")


@unittest.skipUnless(openssl_path(), "openssl is required (set OPENSSL to its path)")
class UpdateSigningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ.setdefault("OPENSSL", openssl_path())
        keygen = subprocess.run([sys.executable, KEYGEN], capture_output=True, text=True)
        assert keygen.returncode == 0, keygen.stderr
        lines = keygen.stdout.splitlines()
        start = lines.index("-----" + "BEGIN PRIVATE" + " KEY-----")
        end = lines.index("-----" + "END PRIVATE" + " KEY-----")
        cls.pem = "\n".join(lines[start:end + 1]) + "\n"
        cls.public_hex = lines[-1]

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.dir = directory.name
        self.latest = os.path.join(self.dir, "latest.json")
        self.sig = os.path.join(self.dir, "latest.json.sig")
        with open(self.latest, "wb") as handle:
            handle.write(LATEST)

    def sign(self, env_value=None, extra=()):
        env = dict(os.environ)
        env.pop(KEY_ENV, None)
        if env_value is not None:
            env[KEY_ENV] = env_value
        return subprocess.run(
            [sys.executable, SIGN, self.latest, *extra],
            capture_output=True,
            text=True,
            env=env,
        )

    def test_keygen_prints_pem_and_matching_public_hex(self):
        self.assertIn("BEGIN PRIVATE" + " KEY", self.pem)
        self.assertRegex(self.public_hex, r"^[0-9a-f]{64}$")
        with tempfile.NamedTemporaryFile("w", delete=False, suffix=".pem") as handle:
            handle.write(self.pem)
            key_path = handle.name
        try:
            self.assertEqual(update_signing.public_key_hex(key_path), self.public_hex)
        finally:
            os.remove(key_path)

    def test_sign_then_verify_round_trip(self):
        result = self.sign(self.pem, ["--expect-public-key-hex", self.public_hex])
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.sig, encoding="ascii") as handle:
            text = handle.read()
        self.assertRegex(text, SIG_RE)
        self.assertEqual(len(base64.b64decode(text.strip(), validate=True)), 64)
        self.assertTrue(update_signing.verify_document(self.public_hex, self.latest, self.sig))

    def test_one_byte_tamper_fails_verification(self):
        self.assertEqual(self.sign(self.pem).returncode, 0)
        tampered = bytearray(LATEST)
        tampered[5] ^= 0x01
        with open(self.latest, "wb") as handle:
            handle.write(bytes(tampered))
        self.assertFalse(update_signing.verify_document(self.public_hex, self.latest, self.sig))

    def test_wrong_public_key_fails_verification(self):
        self.assertEqual(self.sign(self.pem).returncode, 0)
        other = subprocess.run([sys.executable, KEYGEN], capture_output=True, text=True).stdout.splitlines()[-1]
        self.assertNotEqual(other, self.public_hex)
        self.assertFalse(update_signing.verify_document(other, self.latest, self.sig))

    def test_expected_key_mismatch_writes_no_signature(self):
        result = self.sign(self.pem, ["--expect-public-key-hex", "0" * 64])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)
        self.assertFalse(os.path.exists(self.sig))

    def test_missing_key_env_fails_without_signing(self):
        result = self.sign(None)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(KEY_ENV, result.stderr)
        self.assertFalse(os.path.exists(self.sig))

    def test_key_is_never_printed(self):
        result = self.sign(self.pem, ["--expect-public-key-hex", self.public_hex])
        self.assertEqual(result.returncode, 0, result.stderr)
        body = self.pem.splitlines()[1]
        output = result.stdout + result.stderr
        self.assertNotIn("PRIVATE KEY", output)
        self.assertNotIn(body, output)

    def test_bad_expected_key_format_fails(self):
        result = self.sign(self.pem, ["--expect-public-key-hex", "abc"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("64 hex", result.stderr)
        self.assertFalse(os.path.exists(self.sig))


if __name__ == "__main__":
    unittest.main()
