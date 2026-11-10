"""Preview server: never mix cached JS layouts with newer WGSL bindings."""
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


Port = int(os.environ.get("PORT", "8082"))
ThreadingHTTPServer(("0.0.0.0", Port), Handler).serve_forever()
