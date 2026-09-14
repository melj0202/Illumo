#!/usr/bin/env python3
"""Tests for tools/create_project.py."""

from __future__ import annotations

import os
import json
import subprocess
from pathlib import Path
import shutil
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from unittest.mock import patch

from create_project import (
    ProjectCreationError,
    create_project,
    generate_readme,
    generate_root_cmake,
    instantiate_template,
    validate_project_name,
    source_provenance,
)

ROOT_DIR = Path(__file__).resolve().parent.parent


class TestProjectCreation(unittest.TestCase):
    @unittest.skipUnless(
        os.environ.get("ILLUMO_TEST_GENERATED_COVERAGE") == "1",
        "set ILLUMO_TEST_GENERATED_COVERAGE=1 for LLVM coverage configuration",
    )
    def test_generated_coverage_inventory(self) -> None:
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        for with_editor in (False, True):
            with self.subTest(editor=with_editor):
                dest = self.temp_dir / f"Coverage{with_editor}"
                create_project(dest, project_name="CoverageApp", include_debug_tools=with_editor)
                build = dest / "build"
                configured = subprocess.run(
                    [cmake, "-S", str(dest), "-B", str(build), "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_COMPILER=clang",
                     "-DCMAKE_CXX_COMPILER=clang++", "-DILLUMO_ENABLE_COVERAGE=ON",
                     "-DILLUMO_ENABLE_CLANG_TIDY=OFF"],
                    text=True, capture_output=True, timeout=120,
                )
                self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                binaries = (build / "coverage-binaries-Debug.txt").read_text().splitlines()
                expected = {"IllumoTests", "CoverageAppTests"}
                if with_editor:
                    expected.add("IllEdTests")
                self.assertEqual({Path(binary).stem for binary in binaries}, expected)
                dependencies = subprocess.run(
                    ["ninja", "-C", str(build), "-t", "query", "IllumoCoverage"],
                    text=True, capture_output=True, timeout=30,
                )
                self.assertEqual(dependencies.returncode, 0, dependencies.stderr)
                for runner in expected:
                    self.assertIn(f"{runner}Discover", dependencies.stdout)
                disabled = subprocess.run(
                    [cmake, "-S", str(dest), "-B", str(build), "-DBUILD_TESTING=OFF"],
                    text=True, capture_output=True, timeout=120,
                )
                self.assertNotEqual(disabled.returncode, 0)
                self.assertIn("requires BUILD_TESTING=ON", disabled.stderr)

    @unittest.skipUnless(shutil.which("cmake"), "CMake required for seed concurrency test")
    def test_concurrent_runtime_seeds(self) -> None:
        sources = [self.temp_dir / "a.txt", self.temp_dir / "b.txt"]
        contents = ["a" * 65536, "b" * 65536]
        for source, content in zip(sources, contents):
            source.write_text(content, encoding="utf-8")
        destination = self.temp_dir / "runtime" / "settings.txt"
        lock = self.temp_dir / "locks" / "seed.lock"

        def seed(index: int) -> subprocess.CompletedProcess[str]:
            return subprocess.run(
                [shutil.which("cmake"), f"-DSOURCE={sources[index % 2]}",
                 f"-DDESTINATION={destination}", f"-DLOCK_FILE={lock}",
                 "-P", str(ROOT_DIR / "Illumo" / "cmake" / "CopyIfMissing.cmake")],
                text=True, capture_output=True, timeout=90,
            )

        with ThreadPoolExecutor(max_workers=8) as workers:
            for result in workers.map(seed, range(8)):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(destination.read_text(), contents)
        destination.write_text("user settings", encoding="utf-8")
        with ThreadPoolExecutor(max_workers=8) as workers:
            for result in workers.map(seed, range(8)):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(destination.read_text(), "user settings")

    @unittest.skipUnless(
        os.environ.get("ILLUMO_TEST_GENERATED_BUILD") == "1",
        "set ILLUMO_TEST_GENERATED_BUILD=1 for the full generated build",
    )
    def test_generated_default_build(self) -> None:
        self.assertIsNotNone(shutil.which("latexmk"), "LaTeX tools reproduce the docs regression")
        self.assertTrue(shutil.which("powershell") or shutil.which("pwsh"))
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        dest = self.temp_dir / "GeneratedBuild"
        create_project(dest, project_name="GeneratedApp")
        self.assertFalse((dest / "docs").exists())
        build = dest / "build"
        for command in (
            [cmake, "-S", str(dest), "-B", str(build)],
            [cmake, "--build", str(build), "--config", "Release", "--parallel", "4"],
        ):
            result = subprocess.run(command, text=True, capture_output=True, timeout=1200)
            self.assertEqual(result.returncode, 0, result.stdout[-16000:] + result.stderr[-16000:])
        self.assertIn("100% tests passed", result.stdout)
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
        self.assertIn("ILLUMO_BUILD_DOCUMENTATION:BOOL=OFF", cache)
        forced = subprocess.run(
            [cmake, "-S", str(dest), "-B", str(build), "-DILLUMO_BUILD_DOCUMENTATION=ON"],
            text=True, capture_output=True, timeout=120,
        )
        self.assertNotEqual(forced.returncode, 0)
        self.assertIn("requires the engine docs source tree", forced.stderr)
        for name in ("IllumoGame", "Game_123"):
            with self.subTest(project=name):
                representative = self.temp_dir / name
                create_project(representative, project_name=name, include_debug_tools=False)
                configured = subprocess.run(
                    [cmake, "-S", str(representative), "-B", str(representative / "build")],
                    text=True, capture_output=True, timeout=120,
                )
                self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)

    def test_source_provenance(self) -> None:
        commit = "a" * 40
        for status, dirty in [("", False), (" M file.cpp\n", True)]:
            with patch("create_project.subprocess.run", side_effect=[
                subprocess.CompletedProcess([], 0, commit + "\n"),
                subprocess.CompletedProcess([], 0, status),
            ]):
                self.assertEqual(source_provenance(), {
                    "source_commit": commit, "source_dirty": dirty,
                    "source_status": "available",
                })
        with patch("create_project.subprocess.run", side_effect=FileNotFoundError):
            self.assertEqual(source_provenance(), {
                "source_commit": None, "source_dirty": None,
                "source_status": "unavailable",
            })

    def setUp(self) -> None:
        self.temp_dir = Path(tempfile.mkdtemp(prefix="illumo_test_project_"))

    def tearDown(self) -> None:
        if self.temp_dir.is_dir():
            shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_validate_project_name(self) -> None:
        self.assertEqual(validate_project_name("MyGame"), "MyGame")
        self.assertEqual(validate_project_name("IllumoGame"), "IllumoGame")
        self.assertEqual(validate_project_name("Game_123"), "Game_123")

        with self.assertRaises(ProjectCreationError):
            validate_project_name("")

        with self.assertRaises(ProjectCreationError):
            validate_project_name("123Game")

        with self.assertRaises(ProjectCreationError):
            validate_project_name("My Game")

        with self.assertRaises(ProjectCreationError):
            validate_project_name("My-Game")
        with self.assertRaises(ProjectCreationError):
            validate_project_name("MyGame\n")

    def test_reserved_names_preserve_destination(self) -> None:
        names = ("Illumo", "ILLUMO", "illed", "IlLeD", "cmake", "CMAKE",
                 "Build", "IllumoTests", "IllEdCore", "GLFW", "glm",
                 "IllumoTestsShaderStage", "IllEd_envvars_json_Stage",
                 "IllumoCapture", "docs", "all", "INSTALL", "test", "NightlyBuild",
                 "CON", "nul", "COM1", "lpt9")
        destination = self.temp_dir / "existing"
        destination.mkdir()
        sentinel = destination / "CMakeLists.txt"
        sentinel.write_text("user content", encoding="utf-8")
        for name in names:
            with self.subTest(name=name):
                with patch("create_project.copy_directory_tree") as copy, patch(
                    "create_project.source_provenance"
                ) as provenance:
                    with self.assertRaisesRegex(ProjectCreationError, "reserved"):
                        create_project(destination, project_name=name, force=True)
                    copy.assert_not_called()
                    provenance.assert_not_called()
                self.assertEqual(sentinel.read_text(), "user content")
                self.assertEqual(list(destination.iterdir()), [sentinel])
                absent = self.temp_dir / "absent"
                with self.assertRaises(ProjectCreationError):
                    create_project(absent, project_name=name, in_workspace=True)
                self.assertFalse(absent.exists())
        with self.assertRaises(ProjectCreationError):
            instantiate_template(ROOT_DIR / "Templates" / "SpinningCube",
                                 self.temp_dir / "template", "Illumo")
        self.assertFalse((self.temp_dir / "template").exists())
        for name in ("IllumoGame", "MyGame", "Game_123", "COM10", "ConsoleGame"):
            self.assertEqual(validate_project_name(name), name)

    def test_instantiate_template(self) -> None:
        template_dir = ROOT_DIR / "Templates" / "SpinningCube"
        target_dir = self.temp_dir / "InstantiatedGame"

        count = instantiate_template(
            template_dir, target_dir, "CustomCubeGame"
        )
        self.assertGreater(count, 0)
        self.assertTrue((target_dir / "CMakeLists.txt").is_file())
        self.assertTrue(
            (target_dir / "Source" / "SpinningCubeModule.cpp").is_file()
        )

        cmake_content = (target_dir / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("CustomCubeGame", cmake_content)
        self.assertNotIn("@PROJECT_NAME@", cmake_content)
        self.assertIn("cmake_minimum_required(VERSION 3.25)", cmake_content)

    def test_create_standalone_project(self) -> None:
        dest = self.temp_dir / "NewProject"
        created = create_project(
            destination=dest,
            project_name="SpinningApp",
            template_name="spinning-cube",
            include_debug_tools=True,
            force=True,
        )

        self.assertEqual(created, dest)
        provenance = json.loads((dest / "engine-provenance.json").read_text())
        self.assertEqual(provenance["schema_version"], 1)
        self.assertEqual(provenance["project_name"], "SpinningApp")
        self.assertEqual(provenance["creation_mode"], "standalone-source-copy")
        self.assertTrue(provenance["include_debug_tools"])
        self.assertTrue((dest / "CMakeLists.txt").is_file())
        self.assertTrue((dest / "README.md").is_file())
        self.assertTrue((dest / "build.py").is_file())
        self.assertTrue((dest / "THIRD_PARTY_NOTICES.md").is_file())
        self.assertTrue((dest / "cmake" / "IllumoBuild.cmake").is_file())
        self.assertTrue((dest / "cmake" / "RunWorkspaceTidy.py").is_file())

        # Engine framework files
        self.assertTrue((dest / "Illumo" / "CMakeLists.txt").is_file())
        self.assertTrue(
            (dest / "Illumo" / "Include" / "Illumo" / "Engine" / "Application.h").is_file()
        )
        self.assertTrue((dest / "Illumo" / "Source" / "Engine" / "Application.cpp").is_file())
        self.assertTrue((dest / "Illumo" / "thirdparty" / "glfw-3.4").is_dir())

        # Debug tools
        self.assertTrue((dest / "IllEd" / "CMakeLists.txt").is_file())
        self.assertTrue((dest / "IllEd" / "Source" / "EditorModule.cpp").is_file())

        # App folder with spinning cube starter
        app_dir = dest / "SpinningApp"
        self.assertTrue(app_dir.is_dir())
        self.assertTrue((app_dir / "CMakeLists.txt").is_file())
        self.assertTrue((app_dir / "envvars.json").is_file())
        self.assertTrue((app_dir / "Source" / "SpinningCubeApplication.cpp").is_file())
        self.assertTrue((app_dir / "Source" / "SpinningCubeModule.cpp").is_file())
        self.assertTrue((app_dir / "Tests" / "TestSpinningCubeModule.cpp").is_file())

        # Check root CMake
        root_cmake = (dest / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("SpinningAppWorkspace", root_cmake)
        self.assertIn("cmake_minimum_required(VERSION 3.25)", root_cmake)
        self.assertIn("add_subdirectory(SpinningApp)", root_cmake)
        self.assertIn("add_subdirectory(IllEd)", root_cmake)

    def test_create_without_debug_tools(self) -> None:
        dest = self.temp_dir / "MinimalProject"
        create_project(
            destination=dest,
            project_name="MinimalApp",
            include_debug_tools=False,
            force=True,
        )
        self.assertFalse((dest / "IllEd").exists())
        root_cmake = (dest / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("add_subdirectory(IllEd)", root_cmake)


if __name__ == "__main__":
    unittest.main()
