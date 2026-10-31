import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.parse
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "ci" / "forgejo_release.py"
REPO = "GARDEN/CommonwealthOnline-Matrix"
API = f"/api/v1/repos/{REPO}"
TOKEN = uuid.uuid4().hex
WRONG_TOKEN = uuid.uuid4().hex
SHA = "0123456789abcdef0123456789abcdef01234567"


class FakeForgejo(http.server.ThreadingHTTPServer):
    def __init__(self):
        super().__init__(("127.0.0.1", 0), FakeHandler)
        self.reset()

    def reset(self):
        self.releases = {}
        self.assets = {}
        self.tags = set()
        self.requests = []
        self.failures = {}
        self.next_id = 100

    @property
    def base_url(self):
        return f"http://127.0.0.1:{self.server_address[1]}"

    def seed_release(self, release_id, tag, name, prerelease=False, assets=()):
        self.releases[tag] = {
            "id": release_id,
            "tag_name": tag,
            "name": name,
            "body": "old notes",
            "draft": False,
            "prerelease": prerelease,
            "html_url": f"{self.base_url}/{REPO}/releases/tag/{tag}",
        }
        self.assets[release_id] = [dict(asset) for asset in assets]

    def release_by_id(self, release_id):
        for release in self.releases.values():
            if release["id"] == release_id:
                return release
        return None

    def new_id(self):
        self.next_id += 1
        return self.next_id


class FakeHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.dispatch("GET")

    def do_POST(self):
        self.dispatch("POST")

    def do_PATCH(self):
        self.dispatch("PATCH")

    def do_DELETE(self):
        self.dispatch("DELETE")

    def reply(self, status, payload=None):
        body = b"" if payload is None else json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def dispatch(self, method):
        server = self.server
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        parsed = urllib.parse.urlsplit(self.path)
        path = urllib.parse.unquote(parsed.path)
        query = urllib.parse.parse_qs(parsed.query)
        server.requests.append((method, path, self.headers.get("Authorization"), self.headers.get("Content-Type"), body))

        if self.headers.get("Authorization") != f"token {TOKEN}":
            return self.reply(401, {"message": "bad token " + (self.headers.get("Authorization") or "")})
        if not path.startswith(API):
            return self.reply(404, {"message": "not found"})
        route = path[len(API):]
        if f"{method} {route}" in server.failures:
            return self.reply(server.failures[f"{method} {route}"], {"message": "injected failure"})

        if method == "GET" and route.startswith("/releases/tags/"):
            release = server.releases.get(route[len("/releases/tags/"):])
            return self.reply(200, release) if release else self.reply(404, {"message": "release not found"})
        if method == "POST" and route == "/releases":
            payload = json.loads(body)
            release_id = server.new_id()
            release = {
                "id": release_id,
                "tag_name": payload["tag_name"],
                "name": payload["name"],
                "body": payload["body"],
                "draft": payload["draft"],
                "prerelease": payload["prerelease"],
                "target_commitish": payload.get("target_commitish"),
                "html_url": f"{server.base_url}/{REPO}/releases/tag/{payload['tag_name']}",
            }
            server.releases[payload["tag_name"]] = release
            server.assets[release_id] = []
            return self.reply(201, release)
        if method == "PATCH" and route.startswith("/releases/"):
            release = server.release_by_id(int(route.split("/")[2]))
            if release is None:
                return self.reply(404, {"message": "release not found"})
            release.update(json.loads(body))
            return self.reply(200, release)
        if method == "DELETE" and route.startswith("/releases/") and route.count("/") == 2:
            release = server.release_by_id(int(route.split("/")[2]))
            if release is None:
                return self.reply(404, {"message": "release not found"})
            server.releases.pop(release["tag_name"])
            server.assets.pop(release["id"], None)
            return self.reply(204)
        if route.startswith("/releases/") and route.endswith("/assets") and method == "GET":
            listing = [{key: asset[key] for key in ("id", "name", "size")} for asset in server.assets.get(int(route.split("/")[2]), [])]
            return self.reply(200, listing)
        if method == "POST" and route.startswith("/releases/") and route.endswith("/assets"):
            release_id = int(route.split("/")[2])
            name = query["name"][0]
            content_type = self.headers.get("Content-Type", "")
            asset = {"id": server.new_id(), "name": name, "size": 0, "content": multipart_attachment(content_type, body)}
            asset["size"] = len(asset["content"])
            server.assets.setdefault(release_id, []).append(asset)
            return self.reply(201, {key: asset[key] for key in ("id", "name", "size")})
        if method == "DELETE" and route.startswith("/releases/") and "/assets/" in route:
            release_id, _, asset_id = route[len("/releases/"):].partition("/assets/")
            remaining = [asset for asset in server.assets.get(int(release_id), []) if asset["id"] != int(asset_id)]
            server.assets[int(release_id)] = remaining
            return self.reply(204)
        if method == "DELETE" and route.startswith("/tags/"):
            return self.reply(204)
        return self.reply(404, {"message": "unhandled route"})


def multipart_attachment(content_type, body):
    boundary = content_type.split("boundary=", 1)[1].encode("utf-8")
    for part in body.split(b"--" + boundary):
        head, separator, content = part.partition(b"\r\n\r\n")
        if separator and b'name="attachment"' in head:
            return content[:-2] if content.endswith(b"\r\n") else content
    raise AssertionError("multipart body has no attachment part")


class ForgejoReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = FakeForgejo()
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        self.server.reset()
        self.workdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.workdir.cleanup)

    def write(self, name, content):
        path = Path(self.workdir.name) / name
        path.write_bytes(content)
        return str(path)

    def run_script(self, *args, token=TOKEN, server_url=None, repository=REPO):
        env = dict(os.environ)
        env.pop("RELEASE_TOKEN", None)
        env["RELEASE_SERVER_URL"] = server_url or self.server.base_url
        env["RELEASE_REPOSITORY"] = repository
        if token is not None:
            env["RELEASE_TOKEN"] = token
        result = subprocess.run(
            [sys.executable, str(SCRIPT), *args],
            env=env,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertNotIn(TOKEN, result.stdout + result.stderr)
        return result

    def methods_and_paths(self):
        return [(method, path) for method, path, _, _, _ in self.server.requests]

    def test_publish_creates_release_and_uploads_assets(self):
        zip_path = self.write("CommonwealthOnline-AE-0.0.0.4.zip", b"zip-bytes")
        tar_path = self.write("CommonwealthOnline.Host-linux-x64-0.0.0.4.tar.gz", b"tar-bytes")
        notes = self.write("release-notes.md", "Changes:\n- one\n".encode("utf-8"))

        result = self.run_script(
            "publish", "0.0.0.4", zip_path, tar_path,
            "--title", "0.0.0.4 dev build", "--notes-file", notes,
            "--target", SHA, "--prerelease",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("release 0.0.0.4: created", result.stdout)
        release = self.server.releases["0.0.0.4"]
        self.assertEqual(release["name"], "0.0.0.4 dev build")
        self.assertEqual(release["body"], "Changes:\n- one\n")
        self.assertTrue(release["prerelease"])
        self.assertFalse(release["draft"])
        self.assertEqual(release["target_commitish"], SHA)
        self.assertEqual(
            self.methods_and_paths(),
            [
                ("GET", f"{API}/releases/tags/0.0.0.4"),
                ("POST", f"{API}/releases"),
                ("GET", f"{API}/releases/101/assets"),
                ("POST", f"{API}/releases/101/assets"),
                ("GET", f"{API}/releases/101/assets"),
                ("POST", f"{API}/releases/101/assets"),
            ],
        )
        for method, path, auth, _, _ in self.server.requests:
            self.assertEqual(auth, f"token {TOKEN}")
        uploaded = {asset["name"]: asset["content"] for asset in self.server.assets[101]}
        self.assertEqual(uploaded, {
            "CommonwealthOnline-AE-0.0.0.4.zip": b"zip-bytes",
            "CommonwealthOnline.Host-linux-x64-0.0.0.4.tar.gz": b"tar-bytes",
        })

    def test_publish_reuses_existing_release_and_updates_only_requested_flags(self):
        self.server.seed_release(7, "0.0.0.4", "old title", prerelease=True)
        asset = self.write("a.zip", b"new")
        notes = self.write("notes.md", b"fresh notes")

        result = self.run_script("publish", "0.0.0.4", asset, "--title", "new title", "--notes-file", notes)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("release 0.0.0.4: updated", result.stdout)
        self.assertNotIn(("POST", f"{API}/releases"), self.methods_and_paths())
        patch = [body for method, path, _, _, body in self.server.requests if method == "PATCH"]
        self.assertEqual(len(patch), 1)
        payload = json.loads(patch[0])
        self.assertEqual(payload, {"name": "new title", "body": "fresh notes"})
        release = self.server.releases["0.0.0.4"]
        self.assertEqual(release["name"], "new title")
        self.assertTrue(release["prerelease"])

    def test_publish_sets_draft_and_prerelease_when_requested_on_update(self):
        self.server.seed_release(7, "0.0.0.4", "old title")
        asset = self.write("a.zip", b"x")
        notes = self.write("notes.md", b"n")

        result = self.run_script(
            "publish", "0.0.0.4", asset, "--title", "t", "--notes-file", notes, "--draft", "--prerelease"
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        patch = [body for method, path, _, _, body in self.server.requests if method == "PATCH"]
        self.assertEqual(json.loads(patch[0]), {"name": "t", "body": "n", "draft": True, "prerelease": True})

    def test_publish_replaces_same_named_asset_and_keeps_others(self):
        self.server.seed_release(
            7, "0.0.0.4", "t",
            assets=[{"id": 40, "name": "a.zip", "size": 1}, {"id": 41, "name": "keep.txt", "size": 1}],
        )
        asset = self.write("a.zip", b"replacement")

        result = self.run_script("publish", "0.0.0.4", asset, "--title", "t", "--notes", "n")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(("DELETE", f"{API}/releases/7/assets/40"), self.methods_and_paths())
        self.assertNotIn(("DELETE", f"{API}/releases/7/assets/41"), self.methods_and_paths())
        names = sorted((asset["name"], asset["id"]) for asset in self.server.assets[7])
        self.assertEqual([name for name, _ in names], ["a.zip", "keep.txt"])
        new_asset = [asset for asset in self.server.assets[7] if asset["name"] == "a.zip"][0]
        self.assertNotEqual(new_asset["id"], 40)
        self.assertEqual(new_asset["content"], b"replacement")

    def test_delete_removes_release_and_tag_and_tolerates_missing(self):
        self.server.seed_release(7, "ci-winsmoke-1-1", "smoke")

        result = self.run_script("delete", "ci-winsmoke-1-1", "--cleanup-tag")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(("DELETE", f"{API}/releases/7"), self.methods_and_paths())
        self.assertIn(("DELETE", f"{API}/tags/ci-winsmoke-1-1"), self.methods_and_paths())
        self.assertNotIn("ci-winsmoke-1-1", self.server.releases)

        missing = self.run_script("delete", "ci-winsmoke-1-1", "--cleanup-tag")
        self.assertEqual(missing.returncode, 0, missing.stderr)

    def test_url_prints_html_url(self):
        self.server.seed_release(7, "0.0.0.4", "t")

        result = self.run_script("url", "0.0.0.4")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), f"{self.server.base_url}/{REPO}/releases/tag/0.0.0.4")

    def test_url_fails_when_no_release_exists(self):
        result = self.run_script("url", "0.0.0.4")

        self.assertEqual(result.returncode, 1)
        self.assertIn("no release for tag 0.0.0.4", result.stderr)

    def test_dry_run_sends_no_requests_and_needs_no_token(self):
        asset = self.write("a.zip", b"x")

        result = self.run_script(
            "--dry-run", "publish", "0.0.0.4", asset, "--title", "t", "--notes", "n", "--prerelease", token=None
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.server.requests, [])
        self.assertIn(f"dry-run: GET {self.server.base_url}{API}/releases/tags/0.0.0.4", result.stdout)
        self.assertIn(f"dry-run: POST {self.server.base_url}{API}/releases", result.stdout)
        self.assertIn("/assets?name=a.zip", result.stdout)

    def test_missing_token_fails_before_any_request(self):
        asset = self.write("a.zip", b"x")

        result = self.run_script("publish", "0.0.0.4", asset, "--title", "t", "--notes", "n", token=None)

        self.assertEqual(result.returncode, 1)
        self.assertIn("RELEASE_TOKEN is not set", result.stderr)
        self.assertEqual(self.server.requests, [])

    def test_wrong_token_reports_http_status_without_echoing_secret(self):
        asset = self.write("a.zip", b"x")

        result = self.run_script("publish", "0.0.0.4", asset, "--title", "t", "--notes", "n", token=WRONG_TOKEN)

        self.assertEqual(result.returncode, 1)
        self.assertIn("HTTP 401", result.stderr)
        self.assertNotIn(WRONG_TOKEN, result.stdout + result.stderr)

    def test_server_error_on_create_is_reported(self):
        self.server.failures["POST /releases"] = 500
        asset = self.write("a.zip", b"x")

        result = self.run_script("publish", "0.0.0.4", asset, "--title", "t", "--notes", "n")

        self.assertEqual(result.returncode, 1)
        self.assertIn("POST /releases failed: HTTP 500", result.stderr)
        self.assertNotIn(("POST", f"{API}/releases/101/assets"), self.methods_and_paths())

    def test_missing_asset_fails_before_any_request(self):
        result = self.run_script("publish", "0.0.0.4", str(Path(self.workdir.name) / "missing.zip"), "--title", "t", "--notes", "n")

        self.assertEqual(result.returncode, 1)
        self.assertIn("asset not found", result.stderr)
        self.assertEqual(self.server.requests, [])

    def test_unreachable_server_is_reported(self):
        asset = self.write("a.zip", b"x")

        result = self.run_script(
            "publish", "0.0.0.4", asset, "--title", "t", "--notes", "n", server_url="http://127.0.0.1:1"
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("failed:", result.stderr)

    def test_malformed_repository_is_rejected(self):
        result = self.run_script("url", "0.0.0.4", repository="no-slash-here")

        self.assertEqual(result.returncode, 1)
        self.assertIn("owner/name", result.stderr)
        self.assertEqual(self.server.requests, [])


if __name__ == "__main__":
    unittest.main()
