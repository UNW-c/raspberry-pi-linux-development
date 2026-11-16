"""Serves the detector page and forwards its WHEP offer to mediamtx.

The page has to run on the same origin as the WHEP endpoint, otherwise the
browser sends a CORS preflight that mediamtx does not answer. So this little
server does two jobs:

    GET  /...   -> the static files in this directory
    POST /whep  -> forwarded to http://127.0.0.1:8889/prts/whep (same origin)

    python serve.py [--port 8890] [--whep http://127.0.0.1:8889/prts/whep]
"""

from __future__ import annotations

import argparse
import http.server
import urllib.error
import urllib.request
from functools import partial
from pathlib import Path

MODELS_DIR = Path(__file__).resolve().parents[2] / "models"


class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = "prts-detect/1.0"

    # .wasm has to be served with its own type for Streaming compilation.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".wasm": "application/wasm",
        ".onnx": "application/octet-stream",
    }

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        # ONNX Runtime Web only gets SharedArrayBuffer - and therefore worker
        # threads - on a cross-origin isolated page.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def translate_path(self, path: str) -> str:
        # /models/... comes from the repository's models/ directory, which sits
        # outside this web root.
        clean = path.split("?")[0].split("#")[0]
        if clean.startswith("/models/"):
            name = clean[len("/models/"):]
            candidate = (MODELS_DIR / name).resolve()
            if MODELS_DIR.resolve() in candidate.parents or candidate.parent == MODELS_DIR.resolve():
                return str(candidate)
            return str(MODELS_DIR)
        return super().translate_path(path)

    def do_POST(self) -> None:  # noqa: N802 - http.server naming
        if self.path.split("?")[0] != "/whep":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length)

        request = urllib.request.Request(
            self.server.whep_url,
            data=body,
            method="POST",
            headers={"Content-Type": "application/sdp"},
        )
        try:
            with urllib.request.urlopen(request, timeout=10) as answer:
                payload = answer.read()
                status = answer.status
        except urllib.error.HTTPError as error:
            payload = error.read()
            status = error.code
        except (urllib.error.URLError, OSError) as error:
            message = f"whep unreachable: {error}".encode()
            self.send_response(502)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(message)))
            self.end_headers()
            self.wfile.write(message)
            return

        self.send_response(status)
        self.send_header("Content-Type", "application/sdp")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args) -> None:
        return


def main() -> int:
    parser = argparse.ArgumentParser(description="serve the PRTS detector page")
    parser.add_argument("--port", type=int, default=8890)
    parser.add_argument("--whep", default="http://127.0.0.1:8889/prts/whep")
    args = parser.parse_args()

    directory = str(Path(__file__).resolve().parent)
    handler = partial(Handler, directory=directory)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    server.daemon_threads = True
    server.whep_url = args.whep
    print(f"detector page: http://127.0.0.1:{args.port}/", flush=True)
    print(f"whep proxy:    /whep -> {args.whep}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
