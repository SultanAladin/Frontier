#!/usr/bin/env python3
"""Live HTTP preview server for Frontier Markdown Studio (Plans & Documentation browser).
Binds to 0.0.0.0 so Arena Live Preview proxies it directly into the browser."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import urllib.parse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import BuildMarkdownViewer as builder  # noqa: E402


class MarkdownStudioHandler(BaseHTTPRequestHandler):
    def _send_bytes(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status: int, data: object) -> None:
        raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self._send_bytes(status, "application/json; charset=utf-8", raw)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        route = parsed.path

        if route in ("/", "/index.html", "/MarkdownStudioViewer.html"):
            html_bytes = builder.render_viewer_html().encode("utf-8")
            self._send_bytes(HTTPStatus.OK, "text/html; charset=utf-8", html_bytes)
            return

        if route == "/api/tree":
            files = builder.collect_markdown_documents(include_content=True)
            self._send_json(HTTPStatus.OK, {"root": "Frontier", "files": files})
            return

        if route == "/api/file":
            qs = urllib.parse.parse_qs(parsed.query)
            rel_path = (qs.get("path") or [""])[0].strip().lstrip("/")
            if not rel_path:
                self._send_json(HTTPStatus.BAD_REQUEST, {"error": "Missing path query parameter"})
                return

            candidate = (REPO_ROOT / rel_path).resolve()
            try:
                candidate.relative_to(REPO_ROOT.resolve())
            except ValueError:
                self._send_json(HTTPStatus.FORBIDDEN, {"error": "Path outside repository"})
                return

            if not candidate.is_file() or candidate.suffix.lower() not in (".md", ".markdown", ".txt"):
                self._send_json(HTTPStatus.NOT_FOUND, {"error": "Markdown file not found"})
                return

            stat = candidate.stat()
            self._send_json(
                HTTPStatus.OK,
                {
                    "path": candidate.relative_to(REPO_ROOT).as_posix(),
                    "name": candidate.name,
                    "size": stat.st_size,
                    "mtime": round(stat.st_mtime, 3),
                    "content": candidate.read_text(encoding="utf-8"),
                },
            )
            return

        if route.startswith("/fonts/"):
            rel_font = route[len("/fonts/"):].lstrip("/")
            fonts_root = (REPO_ROOT / "EngineContent" / "FontsArchives").resolve()
            font_file = (fonts_root / rel_font).resolve()
            try:
                font_file.relative_to(fonts_root)
            except ValueError:
                self._send_json(HTTPStatus.FORBIDDEN, {"error": "Invalid font path"})
                return
            if font_file.is_file() and font_file.suffix.lower() in (".ttf", ".otf"):
                mime = "font/ttf" if font_file.suffix.lower() == ".ttf" else "font/otf"
                self._send_bytes(HTTPStatus.OK, mime, font_file.read_bytes())
                return
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "Font not found"})
            return

        if route == "/favicon.ico":
            self._send_bytes(HTTPStatus.NO_CONTENT, "image/x-icon", b"")
            return

        self._send_json(HTTPStatus.NOT_FOUND, {"error": f"Unknown route: {route}"})

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stdout.write("[MarkdownStudio] " + (fmt % args) + "\n")
        sys.stdout.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve Frontier Markdown Studio")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), MarkdownStudioHandler)
    print(f"[MarkdownStudio] Serving live .md browser on http://{args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
