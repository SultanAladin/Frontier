"""No-cache preview, with a confined route to the separately preserved reference apps."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

REFERENCE_ROOT = Path(__file__).resolve().parents[3] / 'ReferenceApps'

class Handler(SimpleHTTPRequestHandler):
    def translate_path(self, path):
        route = unquote(urlsplit(path).path)
        if route.startswith('/reference-apps/'):
            target = (REFERENCE_ROOT / route[len('/reference-apps/'):]).resolve()
            if target.is_relative_to(REFERENCE_ROOT):
                return str(target)
            return str(REFERENCE_ROOT / '__invalid_path__')
        return super().translate_path(path)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()

if __name__ == '__main__':
    ThreadingHTTPServer(('0.0.0.0', 8080), Handler).serve_forever()
