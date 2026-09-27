#!/usr/bin/env python3
"""Illumo Project Creation Tool.

Generates a new Illumo application project with an Unreal Engine style structure:
  - Illumo/      : Full source code of the engine framework and IllumoRuntime
  - IllumoGuest/ : The guest SDK that WASM programs are built with
  - IllEd/       : The SceneGraph world editor (a WASM program too)
  - <Name>/      : The application, a WASM program from the starter template
                   (a spinning cube scene)
  - cmake/       : Shared CMake build utilities
  - tools/       : bootstrap-wasm.ps1, which fetches the pinned WASM toolchain
  - build.py     : Interactive front end for building, running, and testing
  - CMakeLists.txt : Root build file wiring the engine and programs together
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import subprocess
from typing import Sequence

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
TEMPLATES_DIR = REPOSITORY_ROOT / "Templates"


def source_provenance() -> dict[str, object]:
    """Describe source identity; a dirty copy is not an exact Git snapshot."""
    try:
        commit = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPOSITORY_ROOT,
            capture_output=True, text=True, check=True, timeout=10,
        ).stdout.strip()
        if not re.fullmatch(r"[0-9a-f]{40,64}", commit):
            raise ValueError("Invalid commit identity")
        status = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=normal"],
            cwd=REPOSITORY_ROOT, capture_output=True, text=True,
            check=True, timeout=10,
        ).stdout
        return {"source_commit": commit, "source_dirty": bool(status.strip()),
                "source_status": "available"}
    except (OSError, subprocess.SubprocessError, ValueError):
        return {"source_commit": None, "source_dirty": None,
                "source_status": "unavailable"}

EXCLUDED_DIR_NAMES = {
    ".git",
    "build",
    "build-workspace",
    "build-coverage",
    "build-tidy",
    "archive",
    "__pycache__",
    ".vs",
    ".agent",
    ".agents",
    "out",
}

EXCLUDED_EXTENSIONS = {
    ".pyc",
    ".obj",
    ".exe",
    ".lib",
    ".pdb",
    ".ilk",
    ".exp",
    ".suo",
    ".user",
    ".aux",
    ".log",
    ".out",
    ".toc",
    ".fls",
    ".fdb_latexmk",
    ".synctex.gz",
}


class ProjectCreationError(RuntimeError):
    """Failure during project creation."""


# Keep these aligned with the copied engine/editor and generated root targets.
# Case-insensitive rejection matches the supported Windows filesystem.
RESERVED_PROJECT_NAMES = {
    "illumo", "illed", "cmake", "build", "docs",
    "illumoplatformentry", "illumotestsupport", "illumotests",
    "illumotestsdiscover", "illumotestsshaderstage", "illumopublicheadersmoke", "illumocapture",
    "illumocapturegputests", "illumodocs", "illumoruntests",
    "illumocoverage", "illumotidy", "illumoruntimestage",
    "illumocaptureruntimestage", "illedcore", "illedtests",
    "illedtestsdiscover", "illedclosewindowtests", "illedruntimestage",
    "illed_envvars_json_stage",
    # WASM runtime and guest build infrastructure (cmake/IllumoWasm.cmake,
    # IllumoGuest/CMakeLists.txt).
    "illumoruntime", "illumowasmruntime", "illumowasmrendering",
    "illumowasmcompiler", "illumowasmtime", "illumoguestbuild", "illumopack",
    "illumoguest", "illumoguestoptions", "illumoguestrendering",
    "illumoguestengine", "illumoguestcontent", "paddleguest",
    "paddlepalettemod", "paddlefaultymod", "presentationguest",
    "jobcontrolguest", "sdkcontractguest", "filecontrolguest",
    "illedpackage", "illedwasmpackagetests",
    "glfw", "glm", "freetype", "glew_s", "uninstall",
    "all", "clean", "help", "install", "test", "package", "package_source",
    "edit_cache", "rebuild_cache", "all_build", "zero_check", "run_tests",
    "nightlymemorycheck", "con", "prn", "aux", "nul",
    *(f"com{index}" for index in range(1, 10)),
    *(f"lpt{index}" for index in range(1, 10)),
    *(mode + step for mode in ("nightly", "experimental", "continuous")
      for step in ("", "start", "update", "configure", "build", "test",
                   "coverage", "memcheck", "submit")),
}


def validate_project_name(name: str) -> str:
    """Validate that the project name is a valid C++ identifier."""
    if not name:
        raise ProjectCreationError("Project name cannot be empty.")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        raise ProjectCreationError(
            f"Invalid project name '{name}'. Must start with a letter or underscore "
            "and contain only alphanumeric characters and underscores."
        )
    generated_names = {
        name.casefold() + suffix
        for suffix in ("", "core", "tests", "testsdiscover", "runtimestage",
                       "testsruntimestage", "package")
    }
    if generated_names & RESERVED_PROJECT_NAMES:
        raise ProjectCreationError(
            f"Project name '{name}' is reserved by Windows or workspace build infrastructure."
        )
    return name


def is_excluded_dir(dir_name: str, rel_root: Path) -> bool:
    name_lower = dir_name.lower()
    if name_lower in {".git", ".vs", ".agent", ".agents", "__pycache__"}:
        return True
    # A retired-material archive at the top of a tree; deeper directories of
    # that name are content (the package reader's test fixture).
    if name_lower == "archive" and rel_root == Path("."):
        return True
    # Only treat 'build*' as build tree if not inside thirdparty
    parts = [p.lower() for p in rel_root.parts]
    if "thirdparty" not in parts:
        if name_lower == "build" or name_lower.startswith("build-") or name_lower.startswith("build_"):
            return True
    return False


def copy_directory_tree(
    source: Path,
    destination: Path,
    verbose: bool = False,
) -> int:
    """Recursively copy a directory tree, ignoring build artifacts and temporary files."""
    copied_count = 0
    destination.mkdir(parents=True, exist_ok=True)

    for root_dir, child_dirs, file_names in os.walk(source):
        rel_root = Path(root_dir).relative_to(source)

        # Filter out excluded child directories in-place
        child_dirs[:] = [
            d
            for d in child_dirs
            if not is_excluded_dir(d, rel_root)
        ]

        target_dir = destination / rel_root
        target_dir.mkdir(parents=True, exist_ok=True)

        for file_name in file_names:
            file_path = Path(root_dir) / file_name
            if file_path.suffix.lower() in EXCLUDED_EXTENSIONS:
                continue

            dest_file = target_dir / file_name
            shutil.copy2(file_path, dest_file)
            copied_count += 1
            if verbose:
                print(f"  Copied: {rel_root / file_name}")

    return copied_count


def package_id(project_name: str) -> str:
    """The application's package id: the name in lower case ([a-z0-9._-])."""
    return project_name.lower()


def instantiate_template(
    template_dir: Path,
    destination: Path,
    project_name: str,
    verbose: bool = False,
) -> int:
    """Instantiate a template directory into destination.

    @PROJECT_NAME@ becomes the project name and @PROJECT_ID@ its package id.
    """
    project_name = validate_project_name(project_name)
    if not template_dir.is_dir():
        raise ProjectCreationError(
            f"Template directory not found at: {template_dir}"
        )

    file_count = 0
    destination.mkdir(parents=True, exist_ok=True)

    text_extensions = {
        ".cpp",
        ".h",
        ".hpp",
        ".txt",
        ".json",
        ".cmake",
        ".md",
    }

    for root_dir, child_dirs, file_names in os.walk(template_dir):
        rel_root = Path(root_dir).relative_to(template_dir)
        target_dir = destination / rel_root
        target_dir.mkdir(parents=True, exist_ok=True)

        for file_name in file_names:
            src_file = Path(root_dir) / file_name
            dest_file = target_dir / file_name

            if src_file.suffix.lower() in text_extensions:
                try:
                    content = src_file.read_text(encoding="utf-8")
                    replaced = content.replace(
                        "@PROJECT_NAME@", project_name
                    ).replace("@PROJECT_ID@", package_id(project_name))
                    dest_file.write_text(replaced, encoding="utf-8")
                except OSError as err:
                    raise ProjectCreationError(
                        f"Failed to process template file {src_file}: {err}"
                    ) from err
            else:
                shutil.copy2(src_file, dest_file)

            file_count += 1
            if verbose:
                print(f"  Template file: {rel_root / file_name}")

    return file_count


def generate_root_cmake(
    project_name: str,
    app_folder_name: str,
    include_debug_tools: bool = True,
) -> str:
    """Generate the root CMakeLists.txt for the standalone project."""
    debug_tools_sub = "add_subdirectory(IllEd)\n" if include_debug_tools else ""
    debug_tools_test = "IllEdTestsDiscover " if include_debug_tools else ""
    programs = f"{app_folder_name} IllEd" if include_debug_tools else app_folder_name

    return f"""cmake_minimum_required(VERSION 3.25)

project({project_name}Workspace LANGUAGES C CXX)

include("${{CMAKE_CURRENT_SOURCE_DIR}}/cmake/IllumoBuild.cmake")
include(CTest)

set(ILLUMO_WORKSPACE_BUILD ON)
add_subdirectory(Illumo)
add_subdirectory({app_folder_name})
{debug_tools_sub}
if(BUILD_TESTING)
  add_custom_target(IllumoRunTests ALL
    COMMAND ${{CMAKE_CTEST_COMMAND}} -C $<CONFIG>
      -L IllumoWorkspace --output-on-failure
    DEPENDS IllumoTestsDiscover {project_name}TestsDiscover
      {debug_tools_test}IllumoPublicHeaderSmoke
    WORKING_DIRECTORY "${{CMAKE_BINARY_DIR}}"
    USES_TERMINAL
    COMMENT "Running the {project_name} workspace test suite"
  )
endif()

# The WASM programs IllumoRuntime plays: each directory has a
# GuestTargets.cmake and a PackageTargets.cmake (cmake/IllumoWasm.cmake).
# Run tools/bootstrap-wasm.ps1 once first, or configure with
# -DILLUMO_BUILD_WASM_RUNTIME=OFF to build only the native libraries and tests.
set(ILLUMO_PROGRAMS {programs})
include("${{CMAKE_CURRENT_SOURCE_DIR}}/cmake/IllumoWasm.cmake")

if(ILLUMO_ENABLE_COVERAGE)
  illumo_add_workspace_coverage()
endif()

if(ILLUMO_ENABLE_CLANG_TIDY)
  find_package(Python3 COMPONENTS Interpreter)
  get_filename_component(_illumo_clang_tidy_dir
    "${{ILLUMO_CLANG_TIDY_EXECUTABLE}}" DIRECTORY)
  find_program(ILLUMO_RUN_CLANG_TIDY_EXECUTABLE
    NAMES run-clang-tidy run-clang-tidy.py
    HINTS "${{_illumo_clang_tidy_dir}}")
  unset(_illumo_clang_tidy_dir)
  if(Python3_Interpreter_FOUND AND ILLUMO_RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(IllumoTidy
      COMMAND ${{CMAKE_COMMAND}}
        "-DBINARY_DIR=${{CMAKE_BINARY_DIR}}"
        "-DSOURCE_DIR=${{CMAKE_SOURCE_DIR}}"
        "-DCLANG_TIDY=${{ILLUMO_CLANG_TIDY_EXECUTABLE}}"
        "-DRUN_CLANG_TIDY=${{ILLUMO_RUN_CLANG_TIDY_EXECUTABLE}}"
        "-DPYTHON=${{Python3_EXECUTABLE}}"
        -P "${{CMAKE_CURRENT_SOURCE_DIR}}/cmake/RunWorkspaceTidy.cmake"
      USES_TERMINAL
      COMMENT "Running clang-tidy on first-party workspace sources"
    )
  endif()
endif()
"""


def generate_readme(project_name: str, app_folder_name: str) -> str:
    """Generate README.md for the new project."""
    app_id = package_id(project_name)
    return f"""# {project_name}

An interactive application built on the **Illumo** engine framework. It runs
as a WASM program under IllumoRuntime: the program owns a SceneDirector, and
each screen is a scene (`{app_folder_name}/Source/SpinningCubeScene.*`).

Requires CMake 3.25 or newer and a C++23-capable compiler. The WASM runtime and
guest build need Windows x64 and the pinned toolchain from
`tools/bootstrap-wasm.ps1` (or `python build.py wasm-tools`).

Illumo exposes its public headers and GLM math headers. Other vendor APIs require
an explicit dependency in your application's CMake configuration.

Engine PDF sources are not included; PDF builds default off in this workspace.

## Project Structure (Unreal Engine Style)

```text
{project_name}/
  CMakeLists.txt         # Root workspace CMake configuration (ILLUMO_PROGRAMS)
  build.py               # Interactive front end for building and testing
  Illumo/                # Engine framework and IllumoRuntime, shaders, assets
    Include/Illumo/      # Public engine API headers
    Source/              # Core runtime, rendering, and scene subsystems
    thirdparty/          # Vendored dependencies (GLFW, GLEW, GLM, Tracy, etc.)
  IllumoGuest/           # Guest SDK: GuestProgram and the WASI guest build
  IllEd/                 # SceneGraph world editor (a WASM program too)
  {app_folder_name}/             # Your application
    Source/              # The cube scene and its settings
    Source/Wasm/         # The program: GuestProgram and createScenes
    Tests/               # Headless tests of the scene through a director
    GuestTargets.cmake   # Builds {project_name}.wasm in the guest build
    PackageTargets.cmake # Stages apps/{app_id} beside IllumoRuntime
    illumo.json          # Package manifest
    envvars.json         # First-run settings
  tools/                 # bootstrap-wasm.ps1
```

## Quick Start

### Build and Run

Launch the interactive build menu:

```bash
python build.py
```

Or fetch the toolchain once, then build and play the application directly:

```bash
python build.py wasm-tools
python build.py play --app {app_id}
```

The staged package runs directly too: `IllumoRuntime --app {app_id}`.

### Run Tests

Run all unit and headless smoke tests:

```bash
python build.py test
```

## Controls (Spinning Cube Starter)

- **Space**: Pause / resume cube rotation
- **R**: Reset cube rotation angle to 0
- **G**: Toggle 3D spatial reference grid
- **Up / Down**: Increase / decrease rotation speed
- **Console** (`cube_speed [value]`): show or set the rotation speed; the
  command exists only while the cube scene is active

## Adding scenes

A program owns a `SceneDirector`. Add each screen as a `ProgramScene` in
`createScenes` (`{app_folder_name}/Source/Wasm/SpinningCubeProgram.cpp`)
and switch between them with `switchTo`. A scene the program leaves is kept
and resumes where it was.
"""


def create_project(
    destination: Path,
    project_name: str = "IllumoGame",
    template_name: str = "spinning-cube",
    include_debug_tools: bool = True,
    in_workspace: bool = False,
    force: bool = False,
    verbose: bool = False,
) -> Path:
    """Create a new Illumo application project."""
    project_name = validate_project_name(project_name)
    destination = destination.resolve()

    template_dir = TEMPLATES_DIR / "SpinningCube"
    if template_name != "spinning-cube":
        custom_dir = TEMPLATES_DIR / template_name
        if custom_dir.is_dir():
            template_dir = custom_dir
        else:
            raise ProjectCreationError(
                f"Unknown template '{template_name}'. Available: spinning-cube"
            )

    if in_workspace:
        # In-workspace creation: instantiate template inside current repository
        target_dir = destination
        if target_dir.exists() and any(target_dir.iterdir()) and not force:
            raise ProjectCreationError(
                f"Target directory '{target_dir}' is not empty. Use --force to proceed."
            )

        print(f"Creating in-workspace application '{project_name}' in: {target_dir}")
        instantiate_template(template_dir, target_dir, project_name, verbose)

        root_cmake = REPOSITORY_ROOT / "CMakeLists.txt"
        sub_line = f"add_subdirectory({target_dir.name})"
        if root_cmake.is_file():
            cmake_content = root_cmake.read_text(encoding="utf-8")
            if sub_line not in cmake_content:
                print(
                    f"\nNOTE: To enable your application in the workspace build, add:"
                    f"\n  {sub_line}\nto {root_cmake}, and add {target_dir.name} to"
                    f" its set(ILLUMO_PROGRAMS ...) list."
                )

        print(f"\nSuccessfully created '{project_name}' application at: {target_dir}")
        return target_dir

    # Standalone mode: generate complete Unreal-style engine + tools + game project
    provenance = {
        "schema_version": 1,
        **source_provenance(),
        "template": template_name,
        "project_name": project_name,
        "include_debug_tools": include_debug_tools,
        "creation_mode": "standalone-source-copy",
    }
    if destination.exists() and any(destination.iterdir()) and not force:
        raise ProjectCreationError(
            f"Destination directory '{destination}' already exists and is not empty. "
            "Use --force to overwrite."
        )

    destination.mkdir(parents=True, exist_ok=True)
    print(f"Creating new Illumo project '{project_name}' at: {destination}")

    # 1. Copy engine framework (Illumo/) and the guest SDK (IllumoGuest/)
    engine_src = REPOSITORY_ROOT / "Illumo"
    engine_dest = destination / "Illumo"
    print("  [1/6] Copying the engine framework and guest SDK (Illumo/, IllumoGuest/)...")
    copy_directory_tree(engine_src, engine_dest, verbose)
    copy_directory_tree(
        REPOSITORY_ROOT / "IllumoGuest", destination / "IllumoGuest", verbose
    )

    # 2. Copy debug tools (IllEd/)
    if include_debug_tools:
        editor_src = REPOSITORY_ROOT / "IllEd"
        editor_dest = destination / "IllEd"
        print("  [2/6] Copying debug tools and SceneGraph editor (IllEd/)...")
        copy_directory_tree(editor_src, editor_dest, verbose)
    else:
        print("  [2/6] Skipping debug tools (--no-debug-tools requested).")

    # 3. Copy cmake helper scripts
    cmake_src = REPOSITORY_ROOT / "cmake"
    cmake_dest = destination / "cmake"
    print("  [3/6] Copying CMake build utilities (cmake/)...")
    copy_directory_tree(cmake_src, cmake_dest, verbose)

    # 4. Copy build and formatting tools and licensing notices
    print("  [4/6] Copying build.py front end, notices, and configuration files...")
    shutil.copy2(REPOSITORY_ROOT / "build.py", destination / "build.py")
    # The pinned WASM toolchain download (build.py wasm-tools runs it).
    (destination / "tools").mkdir(exist_ok=True)
    shutil.copy2(
        REPOSITORY_ROOT / "tools" / "bootstrap-wasm.ps1",
        destination / "tools" / "bootstrap-wasm.ps1",
    )
    # The engine release; cmake/IllumoVersion.cmake stamps BuildInfo from it.
    shutil.copy2(REPOSITORY_ROOT / "VERSION.txt", destination / "VERSION.txt")
    if (REPOSITORY_ROOT / "THIRD_PARTY_NOTICES.md").is_file():
        shutil.copy2(
            REPOSITORY_ROOT / "THIRD_PARTY_NOTICES.md",
            destination / "THIRD_PARTY_NOTICES.md",
        )
    if (REPOSITORY_ROOT / ".clang-format").is_file():
        shutil.copy2(
            REPOSITORY_ROOT / ".clang-format", destination / ".clang-format"
        )
    if (REPOSITORY_ROOT / ".clang-tidy").is_file():
        shutil.copy2(
            REPOSITORY_ROOT / ".clang-tidy", destination / ".clang-tidy"
        )

    # 5. Instantiate the game template
    app_folder_name = project_name
    app_dest = destination / app_folder_name
    print(
        f"  [5/6] Instantiating '{template_name}' starter template into {app_folder_name}/..."
    )
    instantiate_template(template_dir, app_dest, project_name, verbose)

    # 6. Generate root CMakeLists.txt and README.md
    print("  [6/6] Generating root CMakeLists.txt and README.md...")
    root_cmake_content = generate_root_cmake(
        project_name=project_name,
        app_folder_name=app_folder_name,
        include_debug_tools=include_debug_tools,
    )
    (destination / "CMakeLists.txt").write_text(
        root_cmake_content, encoding="utf-8"
    )

    readme_content = generate_readme(project_name, app_folder_name)
    (destination / "README.md").write_text(readme_content, encoding="utf-8")
    (destination / "engine-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8"
    )

    print("\nProject creation completed successfully!")
    print(f"\nTo build and run your new application:")
    print(f"  cd {destination}")
    print(f"  python build.py wasm-tools")
    print(f"  python build.py play --app {package_id(project_name)}")
    print(f"  python build.py test\n")

    return destination


def create_parser() -> argparse.ArgumentParser:
    """Create the command line argument parser."""
    parser = argparse.ArgumentParser(
        prog="create_project.py",
        description=(
            "Generate a new Illumo application project with an Unreal Engine style "
            "layout, including the engine framework source, debug tools, and a "
            "starter spinning-cube game template."
        ),
    )
    parser.add_argument(
        "destination",
        type=Path,
        help="Path where the new project workspace or application will be created.",
    )
    parser.add_argument(
        "-n",
        "--name",
        default="IllumoGame",
        help="Name of the game application (default: %(default)s).",
    )
    parser.add_argument(
        "-t",
        "--template",
        default="spinning-cube",
        choices=["spinning-cube"],
        help="Starter template to instantiate (default: %(default)s).",
    )
    parser.add_argument(
        "--no-debug-tools",
        action="store_true",
        help="Do not include the IllEd debug editor in the generated workspace.",
    )
    parser.add_argument(
        "--in-workspace",
        action="store_true",
        help="Create as an application folder inside the current workspace.",
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite or create within an existing non-empty directory.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable detailed logging of files copied.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point."""
    parser = create_parser()
    args = parser.parse_args(argv)

    try:
        create_project(
            destination=args.destination,
            project_name=args.name,
            template_name=args.template,
            include_debug_tools=not args.no_debug_tools,
            in_workspace=args.in_workspace,
            force=args.force,
            verbose=args.verbose,
        )
        return 0
    except ProjectCreationError as err:
        print(f"Error: {err}", file=sys.stderr)
        return 1
    except Exception as err:
        print(f"Unexpected error: {err}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
