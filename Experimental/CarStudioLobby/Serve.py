#!/usr/bin/env python3
"""Static HTTP server for the Frontier Car Studio Configurator Lobby & Proving Grounds exhibit."""

import http.server
import os
import socketserver
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "application/javascript; charset=utf-8",
        ".mjs": "application/javascript; charset=utf-8",
        ".wgsl": "text/plain; charset=utf-8",
        ".wasm": "application/wasm",
        ".facet": "application/octet-stream",
        ".woff2": "font/woff2",
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def log_message(self, format, *args):
        pass


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with ThreadedHTTPServer(("0.0.0.0", PORT), QuietHandler) as httpd:
        print(f"Serving CarStudioLobby on http://0.0.0.0:{PORT}/", flush=True)
        httpd.serve_forever()
