import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

Spec = importlib.util.spec_from_file_location("PreviewServer", Path(__file__).with_name("Serve.py"))
Server = importlib.util.module_from_spec(Spec)
Spec.loader.exec_module(Server)


class PreviewDependencies(unittest.TestCase):
    def test_present_module_skips_install(self):
        with tempfile.TemporaryDirectory() as Folder:
            Root = Path(Folder)
            Module = Root / "node_modules/jolt-physics/dist/jolt-physics.wasm-compat.js"
            Module.parent.mkdir(parents=True)
            Module.write_text("export default {}")
            with patch.object(Server.subprocess, "run") as Run:
                Server.RestoreDependencies(Root)
                Run.assert_not_called()

    def test_missing_module_restores_locked_dependencies(self):
        with tempfile.TemporaryDirectory() as Folder:
            Root = Path(Folder)
            def Restore(*Args, **Keywords):
                Module = Root / "node_modules/jolt-physics/dist/jolt-physics.wasm-compat.js"
                Module.parent.mkdir(parents=True)
                Module.write_text("export default {}")
            with patch.object(Server.shutil, "which", return_value="npm"), patch.object(Server.subprocess, "run", side_effect=Restore) as Run:
                Server.RestoreDependencies(Root)
                Run.assert_called_once_with(["npm", "ci", "--ignore-scripts"], cwd=Root, check=True)

    def test_browser_application_rebuilt_before_serving(self):
        with tempfile.TemporaryDirectory() as Folder:
            Root = Path(Folder)
            (Root / "Tools/node_modules/esbuild").mkdir(parents=True)
            (Root / "Tools/node_modules/esbuild/package.json").write_text("{}")
            with patch.object(Server, "RestoreDependencies") as Restore, patch.object(Server.shutil, "which", return_value="node"), patch.object(Server.subprocess, "run") as Run:
                Server.PrepareBrowserApplication(Root)
                Restore.assert_called_once_with(Root)
                Run.assert_called_once_with(["node", str((Root / "Tools/BuildBrowser.mjs").resolve())], check=True)

    def test_incomplete_restore_fails_before_serving(self):
        with tempfile.TemporaryDirectory() as Folder:
            with patch.object(Server.shutil, "which", return_value="npm"), patch.object(Server.subprocess, "run"):
                with self.assertRaisesRegex(RuntimeError, "did not produce"):
                    Server.RestoreDependencies(Path(Folder))


if __name__ == "__main__":
    unittest.main()
