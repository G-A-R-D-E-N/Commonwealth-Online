import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--json-include", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / "CO-SERVER/server-cpp"
    generated = "\n".join([
        '#include "DirectoryRegistrar.h"', '#include <iostream>',
        'int main(int argc, char** argv) {',
        'if (argc != 2) return 2;',
        'const auto result = CommonwealthOnline::Server::CreatePlatformDirectorySender()("GET", argv[1], "fixture-secret", {});',
        'std::cout << nlohmann::json{{"status", result.status}, {"body", result.body}, {"error", result.transportError}}.dump();',
        '}',
    ])
    redirected = []

    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path == "/redirect":
                self.send_response(307)
                self.send_header("Location", "/target")
                self.end_headers()
                return
            if self.path == "/target":
                redirected.append(self.headers.get("Authorization"))
            body = b"x" * 65537 if self.path == "/large" else b'{"ok":true}'
            self.send_response(200)
            if self.path != "/unknown-length":
                self.send_header("Content-Length", "1000" if self.path == "/truncated" else str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self.wfile.flush()
            self.close_connection = True

    with tempfile.TemporaryDirectory(prefix="co-directory-http-") as temp:
        cpp = Path(temp) / "probe.cpp"
        binary = Path(temp) / "probe.exe"
        cpp.write_text(generated, encoding="utf-8")
        subprocess.run([
            args.compiler, "-std=c++23", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
            "-I" + str(root / "include"), "-I" + args.json_include,
            str(root / "src/DirectoryHttpSender.cpp"), str(cpp),
            "-lwinhttp" if os.name == "nt" else "-lcurl", "-o", str(binary),
        ], check=True)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            results = {}
            for path in ("valid", "unknown-length", "redirect", "truncated", "large"):
                output = subprocess.check_output([str(binary), f"http://127.0.0.1:{server.server_port}/{path}"], text=True, timeout=20)
                results[path] = json.loads(output)
            summary = {path: {"status": result["status"], "error": result["error"], "body_bytes": len(result["body"])} for path, result in results.items()}
            print(json.dumps({"responses": summary, "redirected_requests": len(redirected)}, indent=2))
            assert results["valid"]["status"] == 200 and not results["valid"]["error"]
            assert results["valid"]["body"] == '{"ok":true}'
            assert results["unknown-length"]["status"] == 200 and not results["unknown-length"]["error"]
            assert results["unknown-length"]["body"] == '{"ok":true}'
            assert results["redirect"]["status"] == 307 and not redirected
            assert results["truncated"]["error"]
            assert results["large"]["error"]
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
    print("Platform directory sender checks passed")


if __name__ == "__main__":
    main()
