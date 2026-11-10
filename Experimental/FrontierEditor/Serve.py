#!/usr/bin/env python3
"""Live interactive preview server for Experimental/FrontierEditor (commit c0aaab6) on 0.0.0.0:8091."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time

REPO_ROOT = Path(__file__).resolve().parents[2]
RUNTIME_DIR = REPO_ROOT / "_ClaudeScratch" / "FrontierEditorRuntime"
HOST_BIN = RUNTIME_DIR / "FrontierEditorInteractiveHost"

INTERACTIVE_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Frontier Editor UI (c0aaab6 — Live Interactive Host)</title>
<style>
  html, body {
    margin: 0;
    padding: 0;
    width: 100%;
    height: 100%;
    background: #0b100e;
    overflow: hidden;
    user-select: none;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Inter, sans-serif;
  }
  #stage {
    width: 100vw;
    height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  #screen {
    max-width: 100vw;
    max-height: 100vh;
    width: 100%;
    height: 100%;
    object-fit: contain;
    outline: none;
    cursor: default;
  }
  .hud-pill {
    position: fixed;
    right: 12px;
    bottom: 10px;
    display: flex;
    gap: 8px;
    align-items: center;
    padding: 5px 11px;
    border-radius: 999px;
    background: rgba(16, 20, 24, 0.82);
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #c7d0d9;
    font-size: 11px;
    z-index: 20;
    backdrop-filter: blur(8px);
  }
  .hud-pill a {
    color: #7cc4ff;
    text-decoration: none;
    font-weight: 500;
  }
  .hud-pill a:hover { text-decoration: underline; }
</style>
</head>
<body>
  <div id="stage">
    <canvas id="screen" width="1600" height="900" tabindex="0"></canvas>
  </div>
  <div class="hud-pill">
    <span>FrontierEditor C++ Live (c0aaab6)</span>
    <span>·</span>
    <a href="/Experimental/FrontierEditor/Evidence/index.html" target="_blank" rel="noopener">Evidence Gallery ↗</a>
  </div>
<script>
(() => {
  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d', { alpha: false });
  const W = 1600, H = 900;
  let queue = [];
  let inFlight = false;
  let dirty = true;
  let lastFrameTime = performance.now();

  function toEditorCoords(ev) {
    const rect = canvas.getBoundingClientRect();
    const scale = Math.min(rect.width / W, rect.height / H);
    const drawW = W * scale;
    const drawH = H * scale;
    const offX = rect.left + (rect.width - drawW) * 0.5;
    const offY = rect.top + (rect.height - drawH) * 0.5;
    const x = Math.max(0, Math.min(W - 1, (ev.clientX - offX) / scale));
    const y = Math.max(0, Math.min(H - 1, (ev.clientY - offY) / scale));
    return [x.toFixed(1), y.toFixed(1)];
  }

  canvas.addEventListener('pointermove', (ev) => {
    const [x, y] = toEditorCoords(ev);
    queue.push(`MOVE ${x} ${y}`);
    dirty = true;
  });
  canvas.addEventListener('pointerdown', (ev) => {
    canvas.focus();
    const [x, y] = toEditorCoords(ev);
    const btn = ev.button === 2 ? 1 : (ev.button === 1 ? 2 : 0);
    queue.push(`MOVE ${x} ${y}`);
    queue.push(`BTN ${btn} 1`);
    dirty = true;
  });
  window.addEventListener('pointerup', (ev) => {
    const btn = ev.button === 2 ? 1 : (ev.button === 1 ? 2 : 0);
    queue.push(`BTN ${btn} 0`);
    dirty = true;
  });
  canvas.addEventListener('contextmenu', (ev) => ev.preventDefault());
  canvas.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    const dy = (-ev.deltaY / 100.0).toFixed(2);
    const dx = (-ev.deltaX / 100.0).toFixed(2);
    queue.push(`WHEEL ${dx} ${dy}`);
    dirty = true;
  }, { passive: false });

  window.addEventListener('keydown', (ev) => {
    if (['F5','F6','F7','F8','F9','Tab'].includes(ev.code) || (ev.ctrlKey && ev.shiftKey && ev.code === 'KeyR')) {
      ev.preventDefault();
    }
    queue.push(`KEY ${ev.code} 1 ${ev.repeat ? 1 : 0} ${ev.ctrlKey ? 1 : 0} ${ev.shiftKey ? 1 : 0} ${ev.altKey ? 1 : 0} ${ev.metaKey ? 1 : 0}`);
    if (ev.key && ev.key.length === 1 && !ev.ctrlKey && !ev.metaKey && !ev.altKey) {
      queue.push(`TEXT ${ev.key}`);
    }
    dirty = true;
  });
  window.addEventListener('keyup', (ev) => {
    queue.push(`KEY ${ev.code} 0 0 ${ev.ctrlKey ? 1 : 0} ${ev.shiftKey ? 1 : 0} ${ev.altKey ? 1 : 0} ${ev.metaKey ? 1 : 0}`);
    dirty = true;
  });

  async function pump() {
    if (inFlight) return;
    const now = performance.now();
    if (!dirty && (now - lastFrameTime) < 180) return;
    inFlight = true;
    const dt = Math.min(0.08, Math.max(0.008, (now - lastFrameTime) / 1000.0)).toFixed(4);
    lastFrameTime = now;
    const batch = queue.splice(0, queue.length);
    dirty = false;
    try {
      const res = await fetch('./api/tick', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ events: batch, dt })
      });
      if (res.ok) {
        const blob = await res.blob();
        const bmp = await createImageBitmap(blob);
        ctx.drawImage(bmp, 0, 0, W, H);
        bmp.close();
      }
    } catch (_) {
    } finally {
      inFlight = false;
    }
  }

  setInterval(pump, 33);
  pump();
})();
</script>
</body>
</html>
"""


class InteractiveBridge:
    def __init__(self):
        self.lock = threading.Lock()
        self.proc = None
        self.last_jpeg = b""
        if HOST_BIN.is_file():
            self._spawn()

    def _spawn(self):
        self.proc = subprocess.Popen(
            [str(HOST_BIN), str(RUNTIME_DIR)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            bufsize=0,
        )
        self.last_jpeg = self._request_frame(["STEP 0.016"], "0.016")

    def _read_exact(self, count: int) -> bytes:
        chunks = []
        remaining = count
        while remaining > 0:
            part = self.proc.stdout.read(remaining)
            if not part:
                raise EOFError("Interactive host closed stdout")
            chunks.append(part)
            remaining -= len(part)
        return b"".join(chunks)

    def _request_frame(self, events, dt_str: str) -> bytes:
        lines = [ev.replace("\r", "").replace("\n", "") for ev in events]
        lines.append(f"FRAME {dt_str}")
        payload = ("\n".join(lines) + "\n").encode("utf-8")
        self.proc.stdin.write(payload)
        self.proc.stdin.flush()
        header = self._read_exact(4)
        (size,) = struct.unpack(">I", header)
        return self._read_exact(size)

    def tick(self, events, dt_str: str) -> bytes:
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                if not HOST_BIN.is_file():
                    return b""
                self._spawn()
            try:
                self.last_jpeg = self._request_frame(events, dt_str)
            except Exception:
                self._spawn()
            return self.last_jpeg


BRIDGE = InteractiveBridge()


class FrontierEditorHandler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        path_only = self.path.split("?", 1)[0]
        if path_only in ("/", "/index.html", "/Experimental/FrontierEditor/", "/Experimental/FrontierEditor/index.html"):
            if BRIDGE.proc is not None:
                body = INTERACTIVE_HTML.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            self.send_response(302)
            self.send_header("Location", "/Experimental/FrontierEditor/Evidence/index.html")
            self.end_headers()
            return
        super().do_GET()

    def do_POST(self):
        path_only = self.path.split("?", 1)[0]
        if path_only.endswith("/api/tick"):
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length > 0 else b"{}"
            data = json.loads(raw.decode("utf-8") or "{}")
            events = data.get("events") or []
            dt = str(float(data.get("dt") or 0.016))
            jpeg = BRIDGE.tick(events, dt)
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(jpeg)))
            self.end_headers()
            self.wfile.write(jpeg)
            return
        self.send_response(404)
        self.end_headers()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8091
    handler = partial(FrontierEditorHandler, directory=str(REPO_ROOT))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"FrontierEditor UI (c0aaab6) serving on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
