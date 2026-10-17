"""Serve the web UI over HTTP backed by the real Api (dry-run) so it can be previewed in any browser.

    python tools/dev_server.py [port]
"""
import json
import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ.setdefault("APPDATA", str(ROOT / "build" / "dev-appdata"))  # keep dev settings out of the real profile
Path(os.environ["APPDATA"]).mkdir(parents=True, exist_ok=True)

from wandlike.app.api import Api  # noqa: E402

api = Api(dry_run=True)
WEB = ROOT / "wandlike" / "web"


class H(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=str(WEB), **k)

    def do_POST(self):
        name = self.path.rsplit("/", 1)[-1]
        args = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"[]")
        if name.startswith("_") or name in ("win", "open_url", "open_plugins_folder", "install_update"):
            out = None
        else:
            out = getattr(api, name)(*args)
        body = json.dumps(out).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print(f"http://127.0.0.1:{port}/index.html")
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
