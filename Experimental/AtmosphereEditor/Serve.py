"""Serve the atmosphere prototype and its optional Frontier typefaces."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import os

Repository = Path(__file__).resolve().parents[2]
os.chdir(Repository)
Routes = {
    '/': '/Experimental/AtmosphereEditor/index.html',
    '/AtmosphereEditor.css': '/Experimental/AtmosphereEditor/AtmosphereEditor.css',
    '/AtmosphereEditor.js': '/Experimental/AtmosphereEditor/AtmosphereEditor.js',
    '/GeometryDetail.js': '/Experimental/AtmosphereEditor/GeometryDetail.js',
    '/VisualControls.js': '/Experimental/AtmosphereEditor/VisualControls.js',
    '/Experimental/AtmosphereEditor/index.html': '/Experimental/AtmosphereEditor/index.html',
    '/Experimental/AtmosphereEditor/AtmosphereEditor.css': '/Experimental/AtmosphereEditor/AtmosphereEditor.css',
    '/Experimental/AtmosphereEditor/AtmosphereEditor.js': '/Experimental/AtmosphereEditor/AtmosphereEditor.js',
    '/Experimental/AtmosphereEditor/GeometryDetail.js': '/Experimental/AtmosphereEditor/GeometryDetail.js',
    '/Experimental/AtmosphereEditor/VisualControls.js': '/Experimental/AtmosphereEditor/VisualControls.js',
    '/EngineContent/FontArchives/DMSans/DMSans-Regular.ttf': '/EngineContent/FontArchives/DMSans/DMSans-Regular.ttf',
    '/EngineContent/FontArchives/DMSans/DMSans-Light.ttf': '/EngineContent/FontArchives/DMSans/DMSans-Light.ttf',
}

class AtmosphereRequest(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, max-age=0')
        super().end_headers()

    def do_GET(self):
        Route = urlsplit(self.path).path
        if Route not in Routes:
            self.send_error(404)
            return
        self.path = Routes[Route]
        super().do_GET()

    def do_HEAD(self):
        Route = urlsplit(self.path).path
        if Route not in Routes:
            self.send_error(404)
            return
        self.path = Routes[Route]
        super().do_HEAD()

if __name__ == '__main__':
    Port = int(os.environ.get('PORT', '4173'))
    ThreadingHTTPServer(('0.0.0.0', Port), AtmosphereRequest).serve_forever()
