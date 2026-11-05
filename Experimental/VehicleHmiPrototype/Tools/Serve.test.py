"""Checks the preview landing page without needing a fixed port."""
import importlib.util
import threading
import unittest
from functools import partial
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("prototype_server", HERE / "Serve.py")
SERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SERVER)


class PreviewServerTest(unittest.TestCase):
    def setUp(self):
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), partial(SERVER.Handler, directory=str(HERE.parent)))
        self.server.lobby_landing = False
        self.server.lobby_quality = None
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, path, method="GET"):
        connection = HTTPConnection("127.0.0.1", self.server.server_port)
        try:
            connection.request(method, path)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_default_root_remains_proving_grounds(self):
        status, headers, body = self.request("/")
        self.assertEqual(status, 200)
        self.assertIn(b"id=\"viewport\"", body)
        self.assertIn("no-store", headers["Cache-Control"])

    def test_lobby_redirect_preserves_query_and_get_head_agree(self):
        self.server.lobby_landing = True
        self.server.lobby_quality = "low"
        for method in ("GET", "HEAD"):
            for path, target in (
                ("/", "/Lobby.html?quality=low"),
                ("/?renderer=sdf&quality=high", "/Lobby.html?renderer=sdf&quality=high"),
                ("/?capture=1", "/Lobby.html?capture=1&quality=low"),
            ):
                with self.subTest(method=method, path=path):
                    status, headers, body = self.request(path, method)
                    self.assertEqual(status, 302)
                    self.assertEqual(headers["Location"], target)
                    self.assertEqual(body, b"")
        self.assertEqual(self.request("/index.html")[0], 200)
        self.assertEqual(self.request("/Lobby.html")[0], 200)

    def test_lobby_does_not_force_quality_when_not_requested(self):
        self.server.lobby_landing = True
        self.assertEqual(self.request("/?renderer=bvh")[1]["Location"], "/Lobby.html?renderer=bvh")


if __name__ == "__main__":
    unittest.main()
