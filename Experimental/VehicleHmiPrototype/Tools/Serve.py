"""Serve the prototype without stale shader caches, with an optional Lobby landing page."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


class Handler(SimpleHTTPRequestHandler):
    def redirect_lobby(self):
        # 📝 A preview opens at '/', so offer the Lobby directly without changing the Proving Grounds URL.
        location = urlsplit(self.path)
        if not self.server.lobby_landing or location.path != "/":
            return False
        query = location.query
        if self.server.lobby_quality and "quality" not in parse_qs(query, keep_blank_values=True):
            query += ("&" if query else "") + "quality=" + self.server.lobby_quality
        destination = "/Lobby.html" + ("?" + query if query else "")
        self.send_response(302)
        self.send_header("Location", destination)
        self.send_header("Content-Length", "0")
        self.end_headers()
        return True

    def do_GET(self):
        if not self.redirect_lobby():
            super().do_GET()

    def do_HEAD(self):
        if not self.redirect_lobby():
            super().do_HEAD()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        super().end_headers()


def main():
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--port", type=int, default=8080)
    arguments.add_argument("--lobby", action="store_true", help="Open the Lobby at the preview root")
    arguments.add_argument("--quality", choices=("low", "medium", "high"), help="Default Lobby quality for the preview")
    options = arguments.parse_args()
    if options.quality and not options.lobby:
        arguments.error("--quality requires --lobby")
    root = Path(__file__).resolve().parent.parent
    server = ThreadingHTTPServer(("0.0.0.0", options.port), partial(Handler, directory=str(root)))
    server.lobby_landing = options.lobby
    server.lobby_quality = options.quality
    print(f"Serving {'Lobby' if options.lobby else 'Vehicle HMI'} on 0.0.0.0:{options.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
