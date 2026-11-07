#!/usr/bin/env python3
"""test_misc_contracts_part4: consolidated contract sections.

Consolidated from 5 single-contract scripts so the group runs one process
instead of 5. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_misc_contracts_part4.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_017_test_forgejo_release():
    """Contract moved verbatim from tests/audit/test_forgejo_release.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_forgejo_release.py")
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


    if True:

        _merge_unittest_result = unittest.TextTestRunner(verbosity=1).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(ForgejoReleaseTests)
        )
        _merge_exit(0 if _merge_unittest_result.wasSuccessful() else 1)

def _section_018_test_repository_audit_regressions():
    """Contract moved verbatim from tests/audit/test_repository_audit_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_repository_audit_regressions.py")
    import json
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def read_workflow(name: str) -> str:
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


    def read_proxy_controller() -> str:
        parts = sorted((ROOT / "CO-BETA/plugin/src").glob("F4TProxyActorController*.cpp")) + [ROOT / "CO-BETA/plugin/include/F4TProxyActorControllerInternal.h"]
        return "\n".join(part.read_text(encoding="utf-8") for part in parts)


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []

        for shadow in (".forgejo", ".gitea"):
            require(not (ROOT / shadow / "workflows").exists(), f"CI-DIR: {shadow}/workflows shadows .github/workflows (Forgejo reads only the first existing workflow directory)", failures)

        manifest = read("CO-BETA/tools/stage-client-manifest.ps1")
        require('"CommonwealthOnline.dll"' in manifest, "AUD-04: client DLL must use the canonical client-binary exclusion policy", failures)

        stage_mod = read("CO-BETA/stage-mod.bat")
        require("ThirdParty\\CommonwealthOnlineUI" not in stage_mod, "AUD-02: release staging must not source the detached ThirdParty UI mirror", failures)
        require("..\\CommonwealthOnlineUI" in stage_mod, "AUD-02: release staging must source the reviewed top-level UI package", failures)
        require(":AssembleRuntimePackages" in stage_mod, "AUD-PKG: stage-mod.bat must assemble isolated runtime packages", failures)
        require('"%BUILD_DIR%\\AE\\Fallout 4\\Data\\"' in stage_mod, "AUD-PKG: stage-mod.bat must assemble AE package", failures)
        require('"%BUILD_DIR%\\NG\\Fallout 4\\Data\\"' in stage_mod, "AUD-PKG: stage-mod.bat must assemble NG package", failures)
        require('copy /Y "%CO_NG_DLL%" "%PLUGINS_DIR%\\CommonwealthOnline-NG.dll"' not in stage_mod, "AUD-PKG: default staged Data must not copy sibling NG DLL", failures)
        require('copy /Y "%CO_OG_DLL%" "%PLUGINS_DIR%\\CommonwealthOnline-OG.dll"' not in stage_mod, "AUD-PKG: default staged Data must not copy sibling OG DLL", failures)

        deploy_ui = read("CO-BETA/deploy-ui.bat")
        require("ThirdParty\\CommonwealthOnlineUI" not in deploy_ui, "AUD-02: UI deployment must not source the detached ThirdParty mirror", failures)
        require("..\\CommonwealthOnlineUI\\package" in deploy_ui, "AUD-02: UI deployment must source the reviewed top-level UI package", failures)

        for xmake_path in ("co-core/xmake.lua", "CO-BETA/plugin/xmake.lua", "CO-SERVER/server-cpp/xmake.lua"):
            xmake = read(xmake_path)
            if 'set_policy("build.intermediate_directory", false)' in xmake:
                require(
                    'set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)")' in xmake,
                    f"BUILD-PATH: {xmake_path} disables build.intermediate_directory (needed so the "
                    "shared-source test targets keep unique objectdirs), but that policy also relocates "
                    "target output off xmake's default build/<plat>/<arch>/<mode> layout. The project must "
                    "pin set_targetdir back to $(builddir)/$(plat)/$(arch)/$(mode) or workflow/staging "
                    "paths such as build/linux/x86_64/debug/CommonwealthOnlineServerCpp stop resolving",
                    failures,
                )

        browser = read("CommonwealthOnlineUI/src/F4TServerBrowserBridge.cpp")
        require("ProbeTcpConnectMs" not in browser, "AUD-06: browser refresh must not probe TCP on the UDP/GNS gameplay port", failures)
        snapshot_index = browser.find("const auto currentServerSnapshot = SnapshotCurrentServer();")
        thread_index = browser.find("std::jthread([")
        if thread_index < 0:
            thread_index = browser.find("OwnDirectoryWorker([")
        require(snapshot_index >= 0 and thread_index >= 0 and snapshot_index < thread_index, "AUD-05: current server state must be snapshotted before owned refresh work starts", failures)
        require("currentServerSnapshot" in browser[thread_index:] if thread_index >= 0 else False, "AUD-05: owned refresh must consume the immutable current-server snapshot", failures)

        browser_data = read("CommonwealthOnlineUI/src/F4TServerBrowserData.cpp")
        require("(a_entry.ping <= 0 || a_entry.ping >= 50)" in browser_data, "AUD-06: 50 ms filter must reject non-positive and out-of-bounds pings", failures)
        require("(a_entry.ping <= 0 || a_entry.ping >= 100)" in browser_data, "AUD-06: 100 ms filter must reject non-positive and out-of-bounds pings", failures)
        require("(a_entry.ping <= 0 || a_entry.ping >= 150)" in browser_data, "AUD-06: 150 ms filter must reject non-positive and out-of-bounds pings", failures)

        def passes_ping_filter(filter_name: str, ping: int) -> bool:
            if filter_name == "< 50 ms" and (ping <= 0 or ping >= 50):
                return False
            if filter_name == "< 100 ms" and (ping <= 0 or ping >= 100):
                return False
            if filter_name == "< 150 ms" and (ping <= 0 or ping >= 150):
                return False
            return True

        for tier, limit in (("< 50 ms", 50), ("< 100 ms", 100), ("< 150 ms", 150)):
            require(not passes_ping_filter(tier, -10), f"AUD-06: negative ping must fail {tier}", failures)
            require(not passes_ping_filter(tier, 0), f"AUD-06: unprobed zero ping must fail {tier}", failures)
            require(passes_ping_filter(tier, 1), f"AUD-06: valid positive ping must pass {tier}", failures)
            require(passes_ping_filter(tier, limit - 1), f"AUD-06: valid ping below limit must pass {tier}", failures)
            require(not passes_ping_filter(tier, limit), f"AUD-06: ping at threshold must fail {tier}", failures)
            require(not passes_ping_filter(tier, limit + 10), f"AUD-06: ping above threshold must fail {tier}", failures)

        session_players = int(re.search(r"MaximumSessionPlayers = (\d+);", read("co-core/include/F4TRemotePlayerCapacity.h")).group(1))
        config = json.loads(read("CO-SERVER/server-cpp/commonwealth-server-native.json"))
        require(int(config.get("max_players", 0)) == session_players, "AUD-07: shipped default capacity must equal MaximumSessionPlayers", failures)
        for key in ("enable_persistence", "enable_chat", "enable_factions"):
            require(config.get(key) is True, f"AUD-07: the shipped default config must ship the implemented {key} feature on", failures)
        require(f"kMaxRuntimeProxyActors = {session_players - 1};" in read_proxy_controller(), "AUD-07: client proxy cap must be MaximumSessionPlayers - 1 remote proxies", failures)
        host_config = read("CO-SERVER/host/Services/ServerConfig.cs")
        host_view_model = read("CO-SERVER/host/ViewModels/MainWindowViewModel.cs")
        for needle, text in ((f"MaxPlayers {{ get; set; }} = {session_players};", host_config), (f"?? {session_players},", host_config), (f"MaxPlayers is < 1 or > {session_players}", host_config), (f"_maxPlayers = {session_players};", host_view_model)):
            require(needle in text, f"AUD-07: host capacity must match MaximumSessionPlayers ({needle})", failures)

        xmake_core = read("co-core/xmake.lua") + read("co-core/xmake-tests.lua")
        xmake_plugin = read("CO-BETA/plugin/xmake.lua") + read("CO-BETA/plugin/xmake-quest-tests.lua")
        xmake_server = read("CO-SERVER/server-cpp/xmake.lua")
        require("function core_test(" in xmake_core, "AUD-03: co-core must define the core_test helper", failures)
        core_helper = xmake_core[xmake_core.find("function core_test("):]
        core_helper = core_helper[:core_helper.find("\nend")] if "\nend" in core_helper else core_helper
        for needle in ('set_kind("binary")', 'add_deps("CommonwealthOnlineCore")',
                       'add_forceincludes("F4TAssertionsEnabled.h")', 'add_undefines("NDEBUG")', 'add_tests("default")'):
            require(needle in core_helper, f"AUD-03: the core_test helper must keep {needle}", failures)

        for name, content in (("co-core", xmake_core), ("CO-BETA/plugin", xmake_plugin), ("CO-SERVER/server-cpp", xmake_server)):
            target_blocks = re.findall(r'target\("([^"]+)"\)(.*?)(?=\ntarget\(|\Z)', content, re.DOTALL)
            test_targets = [tname for tname, block in target_blocks if "add_tests(" in block]
            test_targets += re.findall(r'core_test\("([^"]+)"', content)
            require(len(test_targets) > 0, f"AUD-03: {name} must have at least one test target", failures)
            for tname, block in target_blocks:
                if "add_tests(" in block:
                    require('add_forceincludes("F4TAssertionsEnabled.h")' in block,
                            f"AUD-03: {name} target '{tname}' must include add_forceincludes(\"F4TAssertionsEnabled.h\")", failures)

        profile_store = read("CO-BETA/plugin/src/F4TServerProfileStore.cpp")
        rejection = read("CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp")
        require("ClearResumeCredential" in profile_store, "AUD-01: profile store needs explicit per-server resume credential removal", failures)
        require('"Reset Session"' in rejection, "AUD-01: invalid-resume recovery must be an explicit user action", failures)
        require("ShowRetryFailed" in rejection and "!F4T::ServerProfileStore::ClearResumeCredential(boundServerKey, resumeToken)" in rejection, "AUD-01: invalid-resume recovery must fail closed when credential clearing fails", failures)

        admin = read("CO-SERVER/server-cpp/src/AdminControlServer.cpp")
        admin_header = read("CO-SERVER/server-cpp/include/AdminControlServer.h")
        require("SO_SNDTIMEO" in admin, "AUD-08: admin clients need a bounded send timeout", failures)
        require("ShutdownSocket" in admin, "AUD-08: Stop must cancel accepted client sockets", failures)
        require("_clientSockets" in admin_header, "AUD-08: accepted admin sockets must be tracked for cancellation", failures)

        durable = read("co-core/src/persistence/F4TDurableState.cpp")
        durable_header = read("co-core/include/F4TDurableState.h")
        require("Unchanged" in durable and "Unchanged" in durable_header, "AUD-09: unchanged durable updates must be distinguishable so disk writes can be suppressed", failures)

        for tname in ("CommonwealthOnline",):
            target_block = re.search(rf'target\("{tname}"\)(.*?)(?=\ntarget\(|\Z)', xmake_plugin, re.DOTALL)
            require(target_block is not None, f"DEV-SCOPE: target {tname} must exist in CO-BETA/plugin/xmake.lua", failures)
            if target_block:
                require("co_test_features" not in target_block.group(1) and "F4T_ENABLE_NPC_SYNC" not in target_block.group(1),
                        f"DEV-SCOPE: {tname} must build NPC sync unconditionally", failures)

        for path in ("CO-BETA/plugin/include/F4TNpcSync.h", "CO-BETA/plugin/src/F4TNpcSync.cpp",
                     "CO-BETA/plugin/src/F4TNpcSyncGns.cpp", "CO-BETA/plugin/src/F4TReplicationRuntime.cpp"):
            require("F4T_ENABLE_NPC_SYNC" not in read(path), f"DEV-SCOPE: {path} must not gate NPC sync on F4T_ENABLE_NPC_SYNC", failures)

        remote_state = read("CO-BETA/plugin/src/F4TRemotePlayerState.cpp")
        require("g_incomingCombatHits.PurgeSender(a_playerId)" in remote_state, "PVP-DAMAGE: RemoveRemotePlayer must reset shared hit state", failures)
        require("g_incomingCombatHits.QueueHit(a_senderId, a_sequence, a_damage)" in remote_state, "PVP-DAMAGE: QueueIncomingCombatHit must use shared tracker", failures)

        capture_cpp = read("CO-BETA/plugin/src/F4TRangedCombatCapture.cpp")
        require("F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId" in capture_cpp, "PVP-DAMAGE: capture must target remote proxy mappings", failures)

        networking_cpp = read("CO-BETA/plugin/src/F4TNetworking.cpp")
        inbound_cpp = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
        require("F4T::PvPCombatContract::ValidateHitRouting(" in inbound_cpp, "PVP-DAMAGE: networking must use shared routing validation", failures)
        require("F4T::UIClient::NotifyTransportDisconnected();" in networking_cpp, "SESSION-UI: unexpected transport loss must notify the UI client", failures)
        require("if (g_receiveThreadRunning.load())" in networking_cpp, "SESSION-UI: intentional disconnect must not be reported as transport loss", failures)

        receive_start = networking_cpp.index("void ProcessReceiveBuffer(")
        receive_end = networking_cpp.index("\n\tvoid ReceiveLoop(", receive_start)
        process_receive = networking_cpp[receive_start:receive_end]
        require(process_receive.count("F4T::UIClient::NotifyTransportDisconnected(") >= 2,
                "SESSION-UI: every receive-boundary terminal close (frame-limit, inbound-budget, and "
                "oversized unterminated pending frame) must reconcile transport loss, otherwise the "
                "receive loop breaks silently on the invalidated socket and a pending onboarding join "
                "outlives the connection", failures)

        ui_client = read("CO-BETA/plugin/src/F4TUIClient.cpp")
        require("SessionLaunch::CancelPendingJoin(reasonCopy)" in ui_client, "SESSION-UI: transport loss must cancel pending onboarding", failures)
        require("g_joinGeneration.fetch_add(1)" in ui_client, "SESSION-UI: transport loss must invalidate stale join completions", failures)
        require("PushConnectionStatus();" in ui_client, "SESSION-UI: transport loss must publish disconnected UI state", failures)
        require("if (g_joinGeneration.load() != joinGeneration)" in ui_client, "SESSION-UI: launch completion must reject a disconnect that races the load decision", failures)

        persistence_smoke = read("CO-SERVER/server-cpp/tests/LivePersistenceGnsSmoke.cpp")
        seed = persistence_smoke.split("int Seed(", 1)[1].split("\n\tint Resume(", 1)[0]
        require("WaitForSeedCommit(" in seed and "milliseconds(300)" not in seed,
                "PERSISTENCE-SEED: seed must observe committed player state instead of sleeping before restart", failures)
        require("kPrimarySlotProfile" in seed, "PERSISTENCE-SEED: legacy-client seed must wait on the slot profile that holds player state", failures)
        linux_smoke = read("CO-SERVER/server-cpp/scripts/smoke-native-linux-release.sh")
        windows_smoke = read("CO-SERVER/server-cpp/scripts/smoke-native-windows-package.ps1")
        require('"$resume_token" "$scratch/durable-state.json"' in linux_smoke
                and '"$persistencePort", $resumeToken, $durableState' in windows_smoke,
                "PERSISTENCE-SEED: both package smokes must supply the committed state path", failures)


        def else_without_if(text: str) -> list[int]:
            stack: list[list] = []
            depth = 0
            orphaned: list[int] = []
            for number, line in enumerate(text.split("\n"), 1):
                code = re.sub(r"//.*$", "", line)
                if code.lstrip().startswith("#"):
                    continue
                code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
                rest = code.lstrip()
                lead = 0
                while rest.startswith("}"):
                    lead += 1
                    rest = rest[1:].lstrip()
                effective = depth - lead
                for token in re.finditer(r"\b(if|else|for|while|switch|do)\b", rest):
                    if token.group(1) != "else":
                        stack.append([token.group(1), effective])
                        continue
                    while stack and stack[-1][1] > effective:
                        stack.pop()
                    if stack and stack[-1][0] in ("if", "else"):
                        stack[-1][0] = "else"
                    else:
                        orphaned.append(number)
                depth += code.count("{") - code.count("}")
            return orphaned

        for source in sorted((ROOT / "CO-BETA/plugin/src").glob("*.cpp")) + sorted((ROOT / "CO-BETA/plugin/include").glob("*.h")):
            orphaned = else_without_if(source.read_text(encoding="utf-8"))
            require(not orphaned, f"STRUCTURE: {source.name} has an else with no matching if at line(s) {orphaned}", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Repository audit regression contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_019_test_ui_release_provenance():
    """Contract moved verbatim from tests/audit/test_ui_release_provenance.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_ui_release_provenance.py")
    import hashlib
    import json
    import os
    import re
    import shutil
    import subprocess
    import sys
    import tempfile
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    SCRIPT = ROOT / "CO-BETA" / "ci" / "package-release.sh"
    PROVENANCE_FIELDS = {"sourceCommit", "uiSourceDirty", "uiDllSha256", "uiViewsFileCount", "uiViewsSha256"}


    def function_source() -> str:
        text = SCRIPT.read_text(encoding="utf-8").replace("\r\n", "\n")
        match = re.search(r"^write_ui_provenance\(\) \{\n.*?^PY\n\}\n", text, re.S | re.M)
        assert match, "write_ui_provenance is missing from package-release.sh"
        return match.group(0)


    def sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()


    def git(*args: str) -> str:
        return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


    def test_write_ui_provenance_records_source_and_artifact_hashes():
        bash = shutil.which("bash")
        assert bash, "bash is required to run write_ui_provenance"
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            data = tmp_dir / "Data"
            plugins = data / "F4SE" / "Plugins"
            views = data / "PrismaUI_F4" / "views"
            (views / "CommonwealthOnline" / "MapMarkers").mkdir(parents=True)
            plugins.mkdir(parents=True, exist_ok=True)
            dll = tmp_dir / "CommonwealthOnline.dll"
            dll.write_bytes(b"ui-dll")
            index = views / "index.html"
            index.write_bytes(b"<html>")
            marker = views / "CommonwealthOnline" / "MapMarkers" / "marker.png"
            marker.write_bytes(b"png")

            shim_dir = tmp_dir / "bin"
            shim_dir.mkdir()
            shim = shim_dir / "python3"
            shim.write_text(f'#!/bin/sh\nexec "{Path(sys.executable).as_posix()}" "$@"\n', encoding="utf-8", newline="\n")
            shim.chmod(0o755)

            runner = tmp_dir / "run.sh"
            runner.write_text(
                "set -euo pipefail\n"
                + function_source()
                + f'REPO_DIR="{ROOT.as_posix()}"\n'
                + f'write_ui_provenance "{data.as_posix()}" "{dll.as_posix()}"\n',
                encoding="utf-8",
                newline="\n",
            )
            env = dict(os.environ, PATH=f"{shim_dir}{os.pathsep}{os.environ['PATH']}")
            subprocess.run([bash, str(runner)], check=True, capture_output=True, text=True, env=env)

            provenance = json.loads((plugins / "CommonwealthOnlineUI.provenance.json").read_text(encoding="utf-8"))
            assert set(provenance) == PROVENANCE_FIELDS, sorted(provenance)
            assert provenance["sourceCommit"] == git("rev-parse", "HEAD").strip()
            assert provenance["uiSourceDirty"] is (git("status", "--porcelain", "--", "CommonwealthOnlineUI").strip() != "")
            assert provenance["uiDllSha256"] == sha256(dll)
            views_lines = sorted(
                [
                    f"index.html\t{sha256(index)}\n",
                    f"CommonwealthOnline/MapMarkers/marker.png\t{sha256(marker)}\n",
                ]
            )
            assert provenance["uiViewsFileCount"] == 2
            assert provenance["uiViewsSha256"] == hashlib.sha256("".join(views_lines).encode("utf-8")).hexdigest()


    if True:
        test_write_ui_provenance_records_source_and_artifact_hashes()
        print("UI release provenance contract passed")

def _section_020_test_papyrus_package_contract():
    """Contract moved verbatim from tests/audit/test_papyrus_package_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_papyrus_package_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    CO_BETA = ROOT / "CO-BETA"
    COMPILED = CO_BETA / "creation-kit" / "scripts" / "compiled"
    WORKFLOWS = ROOT / ".github" / "workflows"


    def test_checked_in_papyrus_outputs_are_the_package_contract():
        assert {path.name for path in COMPILED.glob("*.pex")} == {
            "COCharacterSetupQuestScript.pex",
            "COQuestSyncScript.pex",
            "CommonwealthOnlineDebug.pex",
            "CommonwealthOnlineNeeds.pex",
            "CommonwealthOnlineQuest.pex",
        }
        assert not list(COMPILED.glob("CoSync*.pex"))
        assert all(path.stat().st_size > 0 for path in COMPILED.glob("*.pex"))
        stage = (CO_BETA / "ci" / "stage-content.sh").read_text(encoding="utf-8")
        assert "stage_papyrus \"$COMPILED_DIR\" Scripts '*.pex'" in stage
        assert "cosync*" in stage


    def test_package_build_does_not_require_papyrus_compiler():
        build_all = (CO_BETA / "build-all.bat").read_text(encoding="utf-8")
        assert 'call "%SCRIPT_DIR%compile-papyrus.bat"' not in build_all
        for workflow in WORKFLOWS.glob("*.yml"):
            text = workflow.read_text(encoding="utf-8")
            assert "PapyrusCompiler.exe" not in text
            assert "compile-papyrus.bat" not in text


    if True:
        test_checked_in_papyrus_outputs_are_the_package_contract()
        test_package_build_does_not_require_papyrus_compiler()
        print("Papyrus package contract passed")

def _section_021_test_runner_toolchain_provisioning():
    """Contract moved verbatim from tests/audit/test_runner_toolchain_provisioning.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_runner_toolchain_provisioning.py")
    from pathlib import Path
    import importlib.util
    import os
    import tempfile
    from unittest.mock import patch


    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location(
        "provision", root / "scripts/provision-matrix-ci-toolchain.py"
    )
    provision = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(provision)

    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        provision.INSTALL_ROOT = base
        destination = base / "installed"
        destination.mkdir()
        (destination / "old").write_text("preserve")
        staged = base / "staged"
        staged.mkdir()
        try:
            provision.install_tree(staged, Path("/usr/local/xwin"), ("required",))
        except RuntimeError as error:
            assert "Unsafe installation destination" in str(error)
        else:
            raise AssertionError("an installation outside the allowed root must be rejected")
        link, outside = base / "link", base.parent / "outside-toolchain"
        real_realpath = os.path.realpath
        def realpath(path, *args, **kwargs):
            return str(outside) if Path(path) == link else real_realpath(path, *args, **kwargs)
        with patch.object(provision.os.path, "realpath", realpath):
            try:
                provision.install_tree(staged, link / "installed", ("required",))
            except RuntimeError as error:
                assert "Unsafe installation destination" in str(error)
            else:
                raise AssertionError("a parent that resolves outside the allowed root must be rejected")
        assert not link.exists()
        try:
            provision.install_tree(staged, destination, ("required",))
        except RuntimeError:
            pass
        else:
            raise AssertionError("an incomplete toolchain must not replace the installed tree")
        assert (destination / "old").read_text() == "preserve"
        (staged / "required").write_text("complete")
        provision.install_tree(staged, destination, ("required",))
        assert (destination / "required").read_text() == "complete"
        assert not (destination / "old").exists()

        (staged / "required").write_text("replacement")
        original_rename = Path.rename
        def rename(path, target):
            if path.name == "new":
                raise OSError("simulated replacement failure")
            return original_rename(path, target)

        with patch.object(Path, "rename", rename):
            try:
                provision.install_tree(staged, destination, ("required",))
            except OSError:
                pass
            else:
                raise AssertionError("the replacement failure must be reported")
        assert (destination / "required").read_text() == "complete"
        assert not list(base.glob(".installed-*"))

        sdk = base / "sdk"
        packages = base / "packages"
        for name in provision.SDK_PATHS:
            (sdk / name).mkdir(parents=True)
        for pattern in provision.STAGED_PACKAGES:
            library = packages / pattern.replace("*", "staged")
            library.parent.mkdir(parents=True, exist_ok=True)
            library.write_text("library")
        original_is_file = Path.is_file
        for available, version in ((False, "libprotoc 3.21.12"), (True, "libprotoc 3.20.0"), (True, "libprotoc 3.21.12")):
            def is_file(path):
                if path == Path("/usr/bin/protoc"):
                    return available
                if path in (Path("/usr/include/openssl/ssl.h"), Path("/usr/include/openssl/rand.h")):
                    return True
                return original_is_file(path)

            with patch.object(provision.sys, "platform", "linux"), patch.object(provision.shutil, "which", return_value="/bin/tool"), patch.object(Path, "is_file", is_file), patch.object(provision, "output", return_value=version):
                try:
                    provision.verify_preflight(sdk, packages)
                except RuntimeError as error:
                    assert "/usr/bin/protoc" in str(error)
                    assert not available or version != "libprotoc 3.21.12"
                else:
                    assert available and version == "libprotoc 3.21.12", "preflight must require the absolute pinned protoc"

        (staged / "required").unlink()
        (staged / "required").mkdir()
        assert not provision.complete(staged, ("required",)), "a required file cannot be a directory"
        assert provision.complete(staged, ("required",), directories=True)
        (staged / "required").rmdir()
        (staged / "required").write_text("file")
        assert not provision.complete(staged, ("required",), directories=True), "an SDK directory cannot be a file"

    print("Runner toolchain replacement, rollback, required layout types and pinned protoc passed")
_SECTIONS = [
    ("tests/audit/test_forgejo_release.py", _section_017_test_forgejo_release),
    ("tests/audit/test_repository_audit_regressions.py", _section_018_test_repository_audit_regressions),
    ("tests/audit/test_ui_release_provenance.py", _section_019_test_ui_release_provenance),
    ("tests/audit/test_papyrus_package_contract.py", _section_020_test_papyrus_package_contract),
    ("tests/audit/test_runner_toolchain_provisioning.py", _section_021_test_runner_toolchain_provisioning),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_misc_contracts_part4: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
