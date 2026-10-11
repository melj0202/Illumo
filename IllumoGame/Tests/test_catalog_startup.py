"""Opt-in startup integration using an isolated Release IllumoGameTests binary."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("ILLUMO_CATALOG_TEST_BINARY"),
                     "set ILLUMO_CATALOG_TEST_BINARY to the Release test executable")
class TestCatalogStartup(unittest.TestCase):
    def test_menu_and_direct_startup(self) -> None:
        binary = Path(os.environ["ILLUMO_CATALOG_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable_dir = root / "executable"
            working_dir = root / "working"
            executable_dir.mkdir()
            working_dir.mkdir()
            executable = executable_dir / binary.name
            shutil.copy2(binary, executable)
            catalog = '[{"id":"STARTUP_CUSTOM","rule":"B2/S"}]'
            for direct in (False, True):
                for fallback in (False, True):
                    with self.subTest(direct=direct, fallback=fallback):
                        (executable_dir / "rulesets.json").write_text("invalid" if fallback else catalog)
                        (working_dir / "rulesets.json").write_text(catalog if fallback else "[]")
                        environment = dict(os.environ)
                        environment["ILLUMO_TEST_CATALOG_RULE"] = "STARTUP_CUSTOM"
                        environment.pop("ILLUMO_TEST_CATALOG_DIRECT", None)
                        if direct:
                            environment["ILLUMO_TEST_CATALOG_DIRECT"] = "1"
                        result = subprocess.run(
                            [str(executable), "--run", "IllumoGame.Catalog.StartupComposition"],
                            cwd=working_dir, env=environment, text=True,
                            capture_output=True, timeout=30,
                        )
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
