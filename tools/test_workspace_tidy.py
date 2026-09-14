"""Regression tests for batch tidy source selection and driver forwarding."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parent.parent / "cmake" / "RunWorkspaceTidy.py"
SPEC = importlib.util.spec_from_file_location("workspace_tidy", SCRIPT)
TIDY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIDY)


class TestWorkspaceTidy(unittest.TestCase):
    def test_inventory_and_compilation_variants(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "workspace with spaces"
            build = root / "build"
            paths = ["Illumo/Source/Engine/Application.cpp", "MyGame/Source/Game.cpp",
                     "MyGame/Tests/TestGame.cpp", "Illumo/tools/CaptureMain.cpp",
                     "Illumo/tools/CaptureGpuTests.cpp", "IllEd/tools/CloseWindowTests.cpp",
                     "Illumo/thirdparty/library.cpp", "build/generated.cpp"]
            entries = [{"directory": str(build), "file": str(root / path),
                        "command": f"compiler -DVARIANT={index}"}
                       for index, path in enumerate(paths)]
            entries.append({**entries[1], "command": "compiler -DVARIANT=other"})
            entries.append({"directory": str(build), "file": str(root.parent / "external.cpp"),
                            "command": "compiler"})
            # A relative compilation path must resolve against its entry's directory.
            entries.append({"directory": str(root / "MyGame"), "file": "Source/Relative.cpp",
                            "arguments": ["compiler", "-DSPACE=a b"]})
            selected = TIDY.select_entries(entries, root, build)
            self.assertEqual(len(selected), 8)
            self.assertEqual([entry["command"] for entry in selected[:7]],
                             [entry["command"] for entry in entries[:6]] + [entries[8]["command"]])
            self.assertEqual(selected[-1]["arguments"], entries[-1]["arguments"])
            self.assertEqual(Path(selected[-1]["file"]), root / "MyGame/Source/Relative.cpp")

    def test_driver_uses_selected_database_and_propagates_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            entries = [{"directory": str(build), "file": str(root / path), "command": "compiler"}
                       for path in ("Custom/Source/App.cpp", "Illumo/tools/CaptureMain.cpp",
                                    "Illumo/thirdparty/vendor.cpp", "build/generated.cpp")]
            (build / "compile_commands.json").write_text(json.dumps(entries))
            driver = build / "driver.py"
            driver.write_text(
                "import json, pathlib, sys\n"
                "database = pathlib.Path(sys.argv[sys.argv.index('-p') + 1]) / 'compile_commands.json'\n"
                "entries = json.loads(database.read_text())\n"
                "assert len(entries) == 2\n"
                "assert {pathlib.Path(e['file']).name for e in entries} == {'App.cpp', 'CaptureMain.cpp'}\n"
                "assert sys.argv[sys.argv.index('-j') + 1] == '2'\n"
                "raise SystemExit(19)\n"
            )
            command = [sys.executable, "-B", str(SCRIPT), "--source-dir", str(root),
                       "--binary-dir", str(build), "--clang-tidy", "fake-tidy",
                       "--run-clang-tidy", str(driver), "--python", sys.executable, "--jobs", "2"]
            result = subprocess.run(command, text=True, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 19, result.stdout + result.stderr)
            self.assertIn("2 first-party source files", result.stdout)
            (build / "compile_commands.json").write_text("[]")
            result = subprocess.run(command, text=True, capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("No first-party compilation entries", result.stderr)


if __name__ == "__main__":
    unittest.main()
