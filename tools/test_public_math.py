"""Verify the CMake-managed public GLM include boundary across rebuilds."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parent.parent
CMAKE = os.environ.get("ILLUMO_TEST_CMAKE") or shutil.which("cmake")


@unittest.skipUnless(CMAKE and shutil.which("ninja"), "CMake and Ninja are required")
class TestPublicMath(unittest.TestCase):
    def test_header_updates_and_removals(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            vendor = root / "engine/thirdparty"
            glm = vendor / "glm"
            glm.mkdir(parents=True)
            (glm / "glm.hpp").write_text("// Original @UNCHANGED@\n")
            (glm / "detail").mkdir()
            (glm / "detail/old.inl").write_text("// Old\n")
            (glm / "CMakeLists.txt").write_text("not a public header")
            (vendor / "stb_image.h").write_text("must stay private")
            helper = (ROOT / "Illumo/cmake/IllumoPublicMath.cmake").as_posix()
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\n"
                "project(PublicMath NONE)\n"
                'set(ILLUMO_LIBRARY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engine")\n'
                f'include("{helper}")\n'
                "illumo_prepare_public_math(public_math)\n"
            )
            build = root / "build"

            def run(*arguments: str) -> None:
                result = subprocess.run([str(CMAKE), *arguments], text=True,
                                        capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            run("-S", str(root), "-B", str(build), "-G", "Ninja")
            public = build / "public-thirdparty"
            header = public / "glm/glm.hpp"
            self.assertEqual(header.read_text(), "// Original @UNCHANGED@\n")
            self.assertEqual({path.relative_to(public).as_posix() for path in public.rglob("*") if path.is_file()},
                             {"glm/glm.hpp", "glm/detail/old.inl"})
            timestamp = header.stat().st_mtime_ns
            run("-S", str(root), "-B", str(build))
            self.assertEqual(header.stat().st_mtime_ns, timestamp)
            time.sleep(1.1)
            (glm / "glm.hpp").write_text("// Changed\n")
            run("--build", str(build))
            self.assertEqual(header.read_text(), "// Changed\n")
            (glm / "detail/new.hpp").write_text("// New\n")
            (glm / "detail/old.inl").unlink()
            run("--build", str(build))
            self.assertTrue((public / "glm/detail/new.hpp").is_file())
            self.assertFalse((public / "glm/detail/old.inl").exists())
            self.assertFalse((public / "stb_image.h").exists())


if __name__ == "__main__":
    unittest.main()
