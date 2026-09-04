#!/usr/bin/env python3
"""Tests for tools/create_project.py."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import tempfile
import unittest

from create_project import (
    ProjectCreationError,
    create_project,
    generate_readme,
    generate_root_cmake,
    instantiate_template,
    validate_project_name,
)

ROOT_DIR = Path(__file__).resolve().parent.parent


class TestProjectCreation(unittest.TestCase):
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
        self.assertTrue((dest / "CMakeLists.txt").is_file())
        self.assertTrue((dest / "README.md").is_file())
        self.assertTrue((dest / "build.py").is_file())
        self.assertTrue((dest / "THIRD_PARTY_NOTICES.md").is_file())
        self.assertTrue((dest / "cmake" / "IllumoBuild.cmake").is_file())

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
