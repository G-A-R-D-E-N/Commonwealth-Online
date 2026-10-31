import argparse
import json
import os
import shutil
import subprocess
import tempfile
import time
import urllib.request
import uuid

DEFAULT_DIRECTORY = "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory"

parser = argparse.ArgumentParser(description="Supabase directory registration -> live Browse listing -> GNS connect -> Protocol V2 sessionReady")
parser.add_argument("--bin", required=True, help="folder with CommonwealthOnlineServerCpp and CommonwealthOnlineServerLiveGnsSmoke")
parser.add_argument("--bridge", required=True, help="path to the built GNS bridge library")
parser.add_argument("--directory-url", default=DEFAULT_DIRECTORY, help="server-directory Edge Function URL")
parser.add_argument("--connect-host", help="address the GNS client dials instead of the listed one (LAN or loopback when UDP 7777 is not forwarded)")
parser.add_argument("--port", type=int, default=7777)
args = parser.parse_args()
EXE = ".exe" if os.name == "nt" else ""
BIN = os.path.abspath(args.bin)
WORK = tempfile.mkdtemp(prefix="co-directory-e2e-")
NAME = "E2E " + uuid.uuid4().hex[:8]

shutil.copy(os.path.abspath(args.bridge), BIN)


def listing():
    with urllib.request.urlopen(urllib.request.Request(args.directory_url, headers={"Cache-Control": "no-cache"}), timeout=10) as response:
        return next((s for s in json.load(response)["servers"] if s.get("live") and s["name"] == NAME), None)


server = subprocess.Popen([BIN + "/CommonwealthOnlineServerCpp" + EXE, "--port", str(args.port), "--admin-port", "7789",
                           "--no-lan-discovery", "--list-publicly", "--directory-url", args.directory_url, "--name", NAME],
                          cwd=WORK, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    listed = None
    for _ in range(40):
        listed = listing()
        if listed:
            break
        time.sleep(0.5)
    print("listing:", json.dumps(listed))
    assert listed and listed["port"] == args.port and listed["players"] == 0 and listed["address"] == f"{listed['host']}:{args.port}"

    host = args.connect_host or listed["host"]
    smoke = subprocess.run([BIN + "/CommonwealthOnlineServerLiveGnsSmoke" + EXE, host, str(listed["port"])], capture_output=True, text=True, timeout=60)
    print("smoke ->", host, smoke.returncode, smoke.stdout.strip(), smoke.stderr.strip())
    assert smoke.returncode == 0
    print("E2E PASSED")
finally:
    server.terminate()
    try:
        out, _ = server.communicate(timeout=15)
        print("server log:\n" + out[-1500:])
    except Exception:
        server.kill()
    time.sleep(1)
    try:
        print("listing after shutdown:", json.dumps(listing()))
    except Exception as error:
        print("listing check failed:", error)
