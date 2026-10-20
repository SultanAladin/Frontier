"""No-store preview server. Bind all interfaces for Arena's HTTPS preview proxy."""
import argparse
import subprocess
import shutil
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from functools import partial

class PreviewResponses(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        super().end_headers()

def RestoreDependencies(Directory):
    Module = Directory / "node_modules/jolt-physics/dist/jolt-physics.wasm-compat.js"
    if Module.is_file():
        return
    Npm = shutil.which("npm")
    if not Npm:
        raise RuntimeError("Jolt is missing. Install Node/npm and run npm ci in " + str(Directory))
    print("Restoring locked app dependencies (Jolt Physics)...", flush=True)
    subprocess.run([Npm, "ci", "--ignore-scripts"], cwd=Directory, check=True)
    if not Module.is_file():
        raise RuntimeError("Dependency restore did not produce the required Jolt module: " + str(Module))


def PrepareBrowserApplication(Directory):
    RestoreDependencies(Directory)
    Tools = Directory / "Tools"
    Node = shutil.which("node")
    if not Node:
        raise RuntimeError("Node.js is required to build the standalone browser application")
    if not (Tools / "node_modules/esbuild/package.json").is_file():
        Npm = shutil.which("npm")
        if not Npm:
            raise RuntimeError("npm is required to restore the locked browser build tools")
        subprocess.run([Npm, "ci", "--ignore-scripts"], cwd=Tools, check=True)
    subprocess.run([Node, str((Tools / "BuildBrowser.mjs").resolve())], check=True)


if __name__ == "__main__":
    Arguments = argparse.ArgumentParser(description=__doc__)
    Arguments.add_argument("--port", type=int, default=8080)
    Options = Arguments.parse_args()
    Directory = Path(__file__).resolve().parents[1]
    PrepareBrowserApplication(Directory)
    print(f"Combined app: http://0.0.0.0:{Options.port}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", Options.port), partial(PreviewResponses, directory=str(Directory))).serve_forever()
