"""Preview server: no-store headers, cross-origin isolation for SharedArrayBuffer, and the MIME types the module graph needs."""
import mimetypes
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

mimetypes.add_type("text/javascript", ".js")
mimetypes.add_type("text/javascript", ".mjs")
mimetypes.add_type("text/css", ".css")


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # The audio pipeline prefers an AudioWorklet fed by a SharedArrayBuffer ring. Isolation is what lets the browser expose it; the
        # fallback paths (posted blocks, ScriptProcessor) are exercised when a host refuses the headers.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        super().end_headers()


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
