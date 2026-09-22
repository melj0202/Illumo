#!/usr/bin/env python3
"""Front end for the Illumo library and workspace applications."""

from __future__ import annotations

import argparse
import codecs
import contextlib
from collections import deque
from dataclasses import dataclass, field
import json
import io
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import textwrap
from typing import Sequence
import unicodedata
import xml.etree.ElementTree as ET


REPOSITORY_ROOT = Path(__file__).resolve().parent
SOURCE_DIRECTORY = REPOSITORY_ROOT
DEFAULT_BUILD_DIRECTORY = Path("build-workspace")
DEFAULT_COVERAGE_DIRECTORY = Path("build-workspace-coverage")
DEFAULT_TIDY_DIRECTORY = Path("build-workspace-tidy")
PUBLIC_HEADER_SMOKE_TEST = "Illumo.PublicHeaders.ConsumerSmoke"
DEFAULT_PROFILES_FILE = REPOSITORY_ROOT / "build-profiles.local.json"
BUILTIN_PROFILES = {
    "debug": {"config": "Debug", "build_dir": "build-workspace-debug"},
    "release": {"config": "Release", "build_dir": "build-workspace-release"},
}
PROFILE_STRINGS = ("config", "build_dir", "generator", "architecture")
PROFILE_FLAGS = ("tracy", "no_tests", "no_docs", "no_tidy", "no_wasm")

# IllumoGame ships only as a WASM package played by IllumoRuntime. The pinned
# Wasmtime/WASI SDK pair (cmake/IllumoWasm.cmake) is Windows x64 only.
WASM_HOST_SUPPORTED = os.name == "nt" and platform.machine().lower() in ("amd64", "x86_64")
WASM_CMAKE_MODULE = REPOSITORY_ROOT / "cmake" / "IllumoWasm.cmake"
WASM_BOOTSTRAP_SCRIPT = REPOSITORY_ROOT / "tools" / "bootstrap-wasm.ps1"
DEFAULT_WASM_TOOLS_DIRECTORY = REPOSITORY_ROOT / "build-wasm-tools"
WASM_RUNTIME_APPLICATION = "IllumoRuntime"
# Installed applications live in <runtime dir>/apps/<name>/ with app.json.
APPS_DIRECTORY = "apps"
DEFAULT_APP = "game"
APP_LABELS = {"game": "IllumoGame", "illed": "IllEd", "meshviewer": "Mesh Viewer"}
# Native programs from before the WASM cutover; nothing builds or launches
# them any more. The pre-apps game package directory is stale too.
RETIRED_EXECUTABLES = ("IllumoGame", "IllEd", "IllMeshViewer", "IllumoCapture")
RETIRED_DIRECTORIES = ("game",)


@dataclass(frozen=True)
class AppPackage:
    name: str
    module: str

    @property
    def label(self) -> str:
        return APP_LABELS.get(self.name, self.name)


def installed_apps(root: Path | None = None) -> tuple[AppPackage, ...]:
    """Applications the runtime build stages, in cmake/IllumoWasm.cmake order."""
    module_file = (root or REPOSITORY_ROOT) / "cmake" / "IllumoWasm.cmake"
    try:
        text = module_file.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ()
    apps: list[AppPackage] = []
    for match in re.finditer(
        r"illumo_stage_app\(\s*\w+\s+([a-z0-9._-]+)\s+MODULE\s+([A-Za-z0-9._-]+)", text
    ):
        apps.append(AppPackage(match[1], match[2]))
    return tuple(apps)


def app_names(root: Path | None = None) -> tuple[str, ...]:
    names = tuple(app.name for app in installed_apps(root))
    return names if names else (DEFAULT_APP,)


def dashboard_applications(root: Path | None = None) -> tuple[str, ...]:
    """Installed packages, or native executables in workspaces without any."""
    packages = tuple(app.name for app in installed_apps(root))
    if packages:
        return packages
    native = discover_workspace_projects(root or REPOSITORY_ROOT).applications
    return native if native else (DEFAULT_APP,)

ANSI_RESET = "\x1b[0m"
ANSI_BOLD = "\x1b[1m"
ANSI_DIM = "\x1b[2m"
ANSI_CYAN = "\x1b[38;5;45m"
ANSI_GREEN = "\x1b[38;5;82m"
ANSI_YELLOW = "\x1b[38;5;220m"
ANSI_BLUE = "\x1b[38;5;111m"
ANSI_RED = "\x1b[38;5;203m"
ANSI_REVERSE = "\x1b[7m"
ANSI_CLEAR = "\x1b[2J\x1b[H"
ANSI_ENTER_SCREEN = "\x1b[?1049h"
ANSI_LEAVE_SCREEN = "\x1b[?1049l"
ANSI_HIDE_CURSOR = "\x1b[?25l"
ANSI_SHOW_CURSOR = "\x1b[?25h"
ANSI_DISABLE_WRAP = "\x1b[?7l"
ANSI_ENABLE_WRAP = "\x1b[?7h"
_ANSI_SEQUENCE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

DASHBOARD_CONFIGURATIONS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")
DASHBOARD_PARALLEL_OPTIONS = (
    ("Auto", 0),
    ("Off", None),
    ("2 jobs", 2),
    ("4 jobs", 4),
    ("8 jobs", 8),
    ("16 jobs", 16),
)
DASHBOARD_ITEMS = (
    ("setting", "Configuration", "configuration"),
    ("setting", "Application", "application"),
    ("setting", "Testing", "testing"),
    ("setting", "Documentation", "documentation"),
    ("setting", "Tracy profiling", "tracy"),
    ("setting", "WASM runtime + apps", "wasm"),
    ("setting", "Parallel build", "parallel"),
    ("action", "Play", "play"),
    ("action", "Build everything", "build"),
    ("action", "Build runtime and apps", "build_app"),
    ("action", "Run headless tests", "test"),
    ("action", "Run existing build", "launch"),
    ("action", "Repository statistics", "stats"),
    ("action", "Development Tools", "tools"),
    ("action", "Build documentation", "docs"),
    ("action", "Run LLVM coverage", "coverage"),
    ("action", "Run clang-tidy", "tidy"),
    ("action", "Exit", "quit"),
)
DASHBOARD_DESCRIPTIONS = {
    "play": "build, then run the selected app",
    "build": "applications, tests, and optional PDFs",
    "build_app": "IllumoRuntime with every app package",
    "test": "all discovered test runners",
    "launch": "skip configure and build",
    "stats": "Git state, files, and first-party LOC",
    "file_stats": "first-party source files sorted by LOC",
    "tools": "tests, diagnostics, profiles, and artifacts",
    "docs": "illumo.pdf and architecture-map.pdf",
    "coverage": "Ninja, Clang, and the 85% gate",
    "tidy": "Ninja, Clang, and first-party clang-tidy",
    "quit": "return to the shell",
}


class BuildError(RuntimeError):
    """A user-facing build orchestration failure."""

    def __init__(self, message: str, exit_code: int = 1) -> None:
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class ProjectInfo:
    name: str
    directory: Path
    applications: tuple[str, ...] = ()
    test_runners: tuple[str, ...] = ()
    discovery_targets: tuple[str, ...] = ()
    smoke_targets: tuple[str, ...] = ()
    test_prefixes: tuple[str, ...] = ()


@dataclass(frozen=True)
class WorkspaceProjects:
    root: Path
    projects: tuple[ProjectInfo, ...]

    @property
    def applications(self) -> tuple[str, ...]:
        apps: list[str] = []
        for project in self.projects:
            apps.extend(project.applications)
        return tuple(apps)

    @property
    def test_runners(self) -> tuple[str, ...]:
        runners: list[str] = []
        for project in self.projects:
            runners.extend(project.test_runners)
        return tuple(runners)

    @property
    def discovery_targets(self) -> tuple[str, ...]:
        targets: list[str] = []
        for project in self.projects:
            targets.extend(project.discovery_targets)
        return tuple(targets)

    @property
    def smoke_targets(self) -> tuple[str, ...]:
        smoke: list[str] = []
        for project in self.projects:
            smoke.extend(project.smoke_targets)
        return tuple(smoke)

    @property
    def primary_application(self) -> str:
        apps = self.applications
        if "IllumoRuntime" in apps:
            return "IllumoRuntime"
        return apps[0] if apps else "IllumoRuntime"

    def resolve_test_target(self, test_name: str) -> str:
        if test_name == PUBLIC_HEADER_SMOKE_TEST:
            return "IllumoPublicHeaderSmoke"
        for project in self.projects:
            for prefix in project.test_prefixes:
                if test_name.startswith(prefix):
                    if project.test_runners:
                        return project.test_runners[0]
            if test_name.startswith(f"{project.name}."):
                if project.test_runners:
                    return project.test_runners[0]
        for runner in self.test_runners:
            prefix = runner.removesuffix("Tests") + "."
            if test_name.startswith(prefix):
                return runner
        valid_prefixes = sorted(
            {f"{p.name}." for p in self.projects if p.test_runners}
            | {f"{runner.removesuffix('Tests')}." for runner in self.test_runners}
            | {PUBLIC_HEADER_SMOKE_TEST}
        )
        rendered = ", ".join(f"'{p}'" for p in valid_prefixes)
        raise BuildError(
            f"Test '{test_name}' could not be matched to any test runner. "
            f"Exact tests must start with one of: {rendered}"
        )


def discover_workspace_projects(root: Path = REPOSITORY_ROOT) -> WorkspaceProjects:
    root_cmake = root / "CMakeLists.txt"
    ordered_names: list[str] = []
    if root_cmake.is_file():
        try:
            content = root_cmake.read_text(encoding="utf-8", errors="replace")
            for match in re.finditer(
                r"add_subdirectory\s*\(\s*([A-Za-z0-9_\-]+)\s*\)", content
            ):
                sub = match.group(1)
                if (root / sub / "CMakeLists.txt").is_file() and sub not in ordered_names:
                    ordered_names.append(sub)
        except OSError:
            pass

    for item in sorted(root.iterdir(), key=lambda p: p.name):
        if item.is_dir() and (item / "CMakeLists.txt").is_file():
            if item.name not in ordered_names and not is_excluded_repository_path(
                item.relative_to(root)
            ):
                name_lower = item.name.lower()
                if not (
                    name_lower in ("cmake", "thirdparty", "docs", "archive")
                    or name_lower.startswith("build")
                ):
                    ordered_names.append(item.name)

    project_list: list[ProjectInfo] = []
    for name in ordered_names:
        project_dir = root / name
        cmake_file = project_dir / "CMakeLists.txt"
        if not cmake_file.is_file():
            continue
        try:
            cmake_text = cmake_file.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        # WASI guest trees (IllumoGuest) are built by the host tree through
        # ExternalProject; their .wasm outputs are not launchable applications.
        if re.search(r'CMAKE_SYSTEM_NAME\s+STREQUAL\s+"WASI"', cmake_text):
            continue

        executables = re.findall(r"add_executable\s*\(\s*([A-Za-z0-9_]+)", cmake_text)
        discover_matches = re.findall(
            r"illumo_discover_test_runner\s*\(\s*([A-Za-z0-9_]+)(?:\s+([A-Za-z0-9_]+))?\)",
            cmake_text,
        )
        test_runners: list[str] = []
        discovery_targets: list[str] = []
        test_prefixes: list[str] = []
        for runner_target, label in discover_matches:
            if runner_target not in test_runners:
                test_runners.append(runner_target)
                discovery_targets.append(f"{runner_target}Discover")
                if label:
                    test_prefixes.append(f"{label}.")
                else:
                    test_prefixes.append(f"{name}.")

        for exe in executables:
            if exe.endswith("Tests") and exe not in test_runners:
                test_runners.append(exe)
                test_prefixes.append(f"{exe.removesuffix('Tests')}.")

        smoke_targets: list[str] = []
        for exe in executables:
            if "Smoke" in exe and exe not in test_runners:
                smoke_targets.append(exe)

        applications: list[str] = []
        for exe in executables:
            if exe not in test_runners and exe not in smoke_targets:
                applications.append(exe)

        # IllumoGame ships as a WASM package; IllumoRuntime (defined by the
        # root WASM CMake module) is the executable that plays it.
        wasm_cmake = root / "cmake" / "IllumoWasm.cmake"
        if name == "IllumoGame" and wasm_cmake.is_file():
            try:
                wasm_text = wasm_cmake.read_text(encoding="utf-8", errors="replace")
            except OSError:
                wasm_text = ""
            if re.search(rf"add_executable\s*\(\s*{WASM_RUNTIME_APPLICATION}\b", wasm_text):
                applications.insert(0, WASM_RUNTIME_APPLICATION)

        project_list.append(
            ProjectInfo(
                name=name,
                directory=project_dir,
                applications=tuple(applications),
                test_runners=tuple(test_runners),
                discovery_targets=tuple(discovery_targets),
                smoke_targets=tuple(smoke_targets),
                test_prefixes=tuple(test_prefixes),
            )
        )

    return WorkspaceProjects(root=root, projects=tuple(project_list))


@dataclass(frozen=True)
class LineStatistics:
    label: str
    files: int = 0
    physical_lines: int = 0
    loc: int = 0


@dataclass(frozen=True)
class SourceFileStatistics:
    path: Path
    category: str
    physical_lines: int = 0
    loc: int = 0

    @property
    def blank_lines(self) -> int:
        return max(0, self.physical_lines - self.loc)


@dataclass(frozen=True)
class WorktreeStatistics:
    staged: int = 0
    modified: int = 0
    untracked: int = 0
    conflicted: int = 0


@dataclass(frozen=True)
class RepositoryStatistics:
    root: Path
    branch: str | None
    commit: str | None
    subject: str | None
    worktree: WorktreeStatistics | None
    repository_files: int
    repository_files_source: str
    categories: tuple[LineStatistics, ...]
    projects: tuple[ProjectInfo, ...] = ()
    files: tuple[SourceFileStatistics, ...] = ()

    @property
    def first_party_files(self) -> int:
        return sum(category.files for category in self.categories)

    @property
    def first_party_physical_lines(self) -> int:
        return sum(category.physical_lines for category in self.categories)

    @property
    def first_party_loc(self) -> int:
        return sum(category.loc for category in self.categories)


@dataclass
class DashboardState:
    selected: int = 0
    configuration_index: int = 0
    application_index: int = 0
    testing_enabled: bool = True
    documentation_enabled: bool = True
    tracy_enabled: bool = False
    wasm_enabled: bool = WASM_HOST_SUPPORTED
    parallel_index: int = 0
    status: str = "Ready"
    status_kind: str = "normal"
    applications: tuple[str, ...] = field(default_factory=tuple)
    profile_name: str | None = None
    profile_settings: dict = field(default_factory=dict)
    overrides: dict = field(default_factory=dict)
    profiles_file: Path = DEFAULT_PROFILES_FILE

    def __post_init__(self) -> None:
        # Applications are the runtime's installed packages (game, illed, ...),
        # or native executables in workspaces that stage no packages.
        if not self.applications:
            self.applications = dashboard_applications()
        if self.application_index == 0 and DEFAULT_APP in self.applications:
            self.application_index = self.applications.index(DEFAULT_APP)

    @property
    def configuration(self) -> str:
        return DASHBOARD_CONFIGURATIONS[self.configuration_index]

    @property
    def application(self) -> str:
        apps = self.applications if self.applications else (DEFAULT_APP,)
        return apps[self.application_index % len(apps)]

    @property
    def application_label(self) -> str:
        return APP_LABELS.get(self.application, self.application)

    @property
    def parallel_value(self) -> int | None:
        if "parallel" in self.overrides:
            return self.overrides["parallel"]
        if "parallel" in self.profile_settings:
            return self.profile_settings["parallel"]
        return DASHBOARD_PARALLEL_OPTIONS[self.parallel_index][1]


class DashboardTerminal:
    """Own the alternate screen and raw keyboard mode while the menu is open."""

    def __init__(self) -> None:
        self.input_fd: int | None = None
        self.original_attributes: object | None = None
        self.windows_input: WindowsDashboardInput | None = None

    def enter(self) -> None:
        enable_virtual_terminal_processing()
        if os.name == "nt":
            try:
                self.windows_input = WindowsDashboardInput()
            except OSError:
                # Redirected/emulated consoles may expose only keyboard input.
                self.windows_input = None
        else:
            import termios
            import tty

            self.input_fd = sys.stdin.fileno()
            self.original_attributes = termios.tcgetattr(self.input_fd)
            # cbreak, not raw: raw clears OPOST so LF does not return to
            # column 0 and the boxed menu walks off the right edge.
            tty.setcbreak(self.input_fd)
            termios.tcflush(self.input_fd, termios.TCIFLUSH)
        sys.stdout.write(
            ANSI_ENTER_SCREEN + ANSI_HIDE_CURSOR + ANSI_DISABLE_WRAP
        )
        sys.stdout.flush()

    def leave(self) -> None:
        try:
            if self.windows_input is not None:
                reader = self.windows_input
                self.windows_input = None
                reader.close()
            if os.name != "nt" and self.original_attributes is not None:
                import termios

                termios.tcsetattr(
                    self.input_fd, termios.TCSADRAIN, self.original_attributes
                )
                self.original_attributes = None
                self.input_fd = None
        finally:
            sys.stdout.write(
                ANSI_RESET + ANSI_ENABLE_WRAP + ANSI_SHOW_CURSOR + ANSI_LEAVE_SCREEN
            )
            sys.stdout.flush()

    def read_event(self, text_mode: bool = False) -> str | DashboardMouseEvent | DashboardTextEvent:
        if self.windows_input is not None:
            return self.windows_input.read_event(text_mode=text_mode)
        return read_dashboard_key(text_mode=text_mode)


@dataclass(frozen=True)
class DashboardTextEvent:
    text: str


@dataclass(frozen=True)
class DashboardMouseEvent:
    x: int
    y: int
    kind: str


@dataclass(frozen=True)
class DashboardHitRegion:
    index: int
    row: int
    right: int
    previous_x: int | None = None


class WindowsDashboardInput:
    """Read native console records; preserve the caller's input mode."""

    def __init__(self) -> None:
        import ctypes
        from ctypes import wintypes

        class Coord(ctypes.Structure):
            _fields_ = [("x", wintypes.SHORT), ("y", wintypes.SHORT)]

        class Rect(ctypes.Structure):
            _fields_ = [(name, wintypes.SHORT) for name in ("left", "top", "right", "bottom")]

        class KeyRecord(ctypes.Structure):
            _fields_ = [
                ("down", wintypes.BOOL), ("repeat", wintypes.WORD),
                ("key", wintypes.WORD), ("scan", wintypes.WORD),
                ("character", wintypes.WCHAR), ("modifiers", wintypes.DWORD),
            ]

        class MouseRecord(ctypes.Structure):
            _fields_ = [
                ("position", Coord), ("buttons", wintypes.DWORD),
                ("modifiers", wintypes.DWORD), ("flags", wintypes.DWORD),
            ]

        class Event(ctypes.Union):
            _fields_ = [("key", KeyRecord), ("mouse", MouseRecord)]

        class InputRecord(ctypes.Structure):
            _fields_ = [("kind", wintypes.WORD), ("event", Event)]

        class ScreenInfo(ctypes.Structure):
            _fields_ = [
                ("size", Coord), ("cursor", Coord), ("attributes", wintypes.WORD),
                ("window", Rect), ("maximum", Coord),
            ]

        self.ctypes = ctypes
        self.record_type = InputRecord
        self.info_type = ScreenInfo
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.GetStdHandle.argtypes = [wintypes.DWORD]
        self.api.GetStdHandle.restype = wintypes.HANDLE
        self.api.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetConsoleMode.restype = wintypes.BOOL
        self.api.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.SetConsoleMode.restype = wintypes.BOOL
        self.api.ReadConsoleInputW.argtypes = [
            wintypes.HANDLE, ctypes.POINTER(InputRecord), wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
        ]
        self.api.ReadConsoleInputW.restype = wintypes.BOOL
        self.api.GetConsoleScreenBufferInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(ScreenInfo)]
        self.api.GetConsoleScreenBufferInfo.restype = wintypes.BOOL
        self.handle = self.api.GetStdHandle(-10)
        self.output_handle = self.api.GetStdHandle(-11)
        mode = wintypes.DWORD()
        if not self.api.GetConsoleMode(self.handle, ctypes.byref(mode)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.original_mode = mode.value
        self.buttons = 0
        # Enable mouse/window records; disable Quick Edit, line/echo/processed
        # input and VT-input translation while this reader owns the console.
        new_mode = (mode.value | 0x0098) & ~0x0247
        if not self.api.SetConsoleMode(self.handle, new_mode):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self) -> None:
        if not self.api.SetConsoleMode(self.handle, self.original_mode):
            raise BuildError("Could not restore the console input mode.")

    def read_event(self, text_mode: bool = False) -> str | DashboardMouseEvent | DashboardTextEvent:
        from ctypes import wintypes

        record = self.record_type()
        count = wintypes.DWORD()
        while True:
            if not self.api.ReadConsoleInputW(
                self.handle, self.ctypes.byref(record), 1, self.ctypes.byref(count)
            ):
                raise BuildError("Could not read console input.")
            if not count.value:
                continue
            if record.kind == 1 and record.event.key.down:
                key = record.event.key
                if key.character == "\x03":
                    raise KeyboardInterrupt
                if text_mode and key.character.isprintable():
                    return DashboardTextEvent(key.character * max(1, key.repeat))
                return {
                    0x26: "up", 0x28: "down", 0x25: "left", 0x27: "right",
                    0x0D: "enter", 0x1B: "escape" if text_mode else "quit",
                    0x08: "backspace", 0x09: "tab", 0x21: "page_up", 0x22: "page_down",
                    0x24: "home", 0x23: "end",
                }.get(key.key, {
                    "q": "quit", "Q": "quit", "j": "down", "k": "up",
                    "h": "left", "l": "right",
                    "/": "search",
                }.get(key.character, "unknown"))
            if record.kind == 4:
                return "resize"
            if record.kind == 2:
                mouse = record.event.mouse
                kind, self.buttons = dashboard_mouse_transition(
                    mouse.buttons, mouse.flags, self.buttons
                )
                if kind is None:
                    continue
                info = self.info_type()
                if not self.api.GetConsoleScreenBufferInfo(
                    self.output_handle, self.ctypes.byref(info)
                ):
                    continue
                return DashboardMouseEvent(
                    mouse.position.x - info.window.left + 1,
                    mouse.position.y - info.window.top + 1, kind,
                )


def dashboard_mouse_transition(
    buttons: int, flags: int, previous: int,
) -> tuple[str | None, int]:
    current = buttons & 0xFFFF
    if flags == 1 and current == 0:
        return "hover", current
    if flags == 4:
        delta = (buttons >> 16) & 0xFFFF
        if delta:
            return ("wheel_down" if delta & 0x8000 else "wheel_up"), current
    # Ignore dragging, release and the second press of a double-click.
    if flags == 0:
        pressed = current & ~previous
        if pressed & 1:
            return "left", current
        if pressed & 2:
            return "right", current
    return None, current


def dashboard_mouse_key(
    state: DashboardState, event: DashboardMouseEvent,
    regions: Sequence[DashboardHitRegion],
) -> str:
    for region in regions:
        if event.y != region.row or not 2 <= event.x < region.right:
            continue
        if event.kind in ("wheel_up", "wheel_down"):
            return "up" if event.kind == "wheel_up" else "down"
        kind = DASHBOARD_ITEMS[region.index][0]
        if event.kind == "right" and kind != "setting":
            return "unknown"
        state.selected = region.index
        if event.kind == "hover":
            return "unknown"  # Highlight only, including over a setting arrow.
        if kind == "setting" and (event.kind == "right" or event.x == region.previous_x):
            return "left"
        return "enter" if event.kind == "left" else "unknown"
    return "unknown"


def enable_virtual_terminal_processing() -> None:
    if os.name != "nt":
        return
    try:
        import ctypes

        kernel32 = ctypes.windll.kernel32
        output_handle = kernel32.GetStdHandle(-11)
        mode = ctypes.c_ulong()
        if kernel32.GetConsoleMode(output_handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(output_handle, mode.value | 0x0004)
    except (AttributeError, OSError):
        return


def dashboard_terminal_size() -> os.terminal_size:
    """Prefer a live TTY ioctl. Zero-sized answers fall back instead of
    rendering a 94-column menu into a narrow Ubuntu/Alacritty window."""
    for stream in (sys.stdout, sys.stdin, sys.stderr):
        try:
            size = os.get_terminal_size(stream.fileno())
        except (AttributeError, OSError, ValueError):
            continue
        if size.columns > 0 and size.lines > 0:
            return size
    return shutil.get_terminal_size((80, 24))


def dashboard_visible_width(text: str) -> int:
    """Terminal cells used by text. ANSI is ignored. Wide and ambiguous
    glyphs count as two cells so a boxed row cannot wrap on Ubuntu."""
    width = 0
    for char in _ANSI_SEQUENCE.sub("", text):
        if unicodedata.combining(char):
            continue
        if unicodedata.east_asian_width(char) in ("W", "F"):
            width += 2
        else:
            width += 1
    return width


def dashboard_pad(text: str, width: int, align: str = "left") -> str:
    if width < 1:
        return ""
    ellipsis = "..."
    if dashboard_visible_width(text) > width:
        trimmed: list[str] = []
        used = 0
        limit = width - dashboard_visible_width(ellipsis)
        if limit < 1:
            text = ellipsis[:width]
        else:
            for char in text:
                cell = dashboard_visible_width(char)
                if used + cell > limit:
                    break
                trimmed.append(char)
                used += cell
            text = "".join(trimmed) + ellipsis
    pad = max(0, width - dashboard_visible_width(text))
    if align == "center":
        left = pad // 2
        return (" " * left) + text + (" " * (pad - left))
    return text + (" " * pad)


def tui_newlines(text: str) -> str:
    """Keep each TUI row at column 0 even if the tty is in raw/cbreak."""
    return text.replace("\r\n", "\n").replace("\n", "\r\n")


def dashboard_style(text: str, style: str, ansi: bool) -> str:
    if not ansi or not style:
        return text
    return f"{style}{text}{ANSI_RESET}"


def dashboard_value(state: DashboardState, key: str) -> str:
    if key == "configuration":
        return state.configuration
    if key == "application":
        return state.application_label
    if key == "testing":
        return "On" if state.testing_enabled else "Off"
    if key == "documentation":
        return "On" if state.documentation_enabled else "Off"
    if key == "tracy":
        return "On" if state.tracy_enabled else "Off"
    if key == "wasm":
        if not WASM_HOST_SUPPORTED:
            return "Windows x64 only"
        return "On" if state.wasm_enabled else "Off"
    if key == "parallel":
        return dashboard_parallel_label(state)
    return ""


def dashboard_workspace_status(state: DashboardState, ok: str, bad: str) -> tuple[str, str]:
    """One header line saying whether the selected tree can play its apps."""
    if not WASM_HOST_SUPPORTED:
        return "Apps: IllumoRuntime and its packages are Windows x64 only", ANSI_DIM
    if not state.wasm_enabled:
        return "Apps: WASM runtime off; no applications will be built", ANSI_DIM
    settings = dashboard_settings(state)
    build_directory = resolve_build_directory(Path(settings["build_dir"]))
    try:
        missing = missing_wasm_tools(
            wasm_tools_directory(settings.get("cmake_arg", []), read_cmake_cache(build_directory))
        )
        outputs = runtime_outputs(build_directory, settings["config"])
    except (BuildError, OSError):
        return "Apps: build tree unreadable", ANSI_YELLOW
    if missing:
        return f"{bad} WASM toolchain missing: run 'python build.py wasm-tools'", ANSI_YELLOW
    stale = f" | {len(outputs.retired)} stale pre-WASM outputs" if outputs.retired else ""
    staged = [app.name for app in outputs.apps if app.staged]
    if outputs.runtime is not None and staged and outputs.complete:
        return (
            f"{ok} {WASM_RUNTIME_APPLICATION} ready ({settings['config']}): "
            f"{', '.join(staged)}{stale}",
            ANSI_GREEN,
        )
    return (f"{bad} Apps not built for {settings['config']}: choose Play{stale}",
            ANSI_YELLOW)


def render_dashboard(
    state: DashboardState, terminal_width: int, ansi: bool = True,
    hit_regions: list[DashboardHitRegion] | None = None,
    mouse_enabled: bool = False,
) -> str:
    # Never emit a row as wide as the TTY: the cursor wrap-around on the last
    # column splits labels and descriptions into overlapping columns.
    usable = max(20, terminal_width - 1)
    width = min(94, usable)
    inner_width = max(1, width - 2)
    lines: list[str] = []
    if hit_regions is not None:
        hit_regions.clear()
    encoding = sys.stdout.encoding or "utf-8"
    try:
        "╭─╮│├┤╰╯>‹›↑↓←→✔✘".encode(encoding)
        glyphs = {
            "ok": "✔",
            "bad": "✘",
            "top_left": "╭",
            "top_right": "╮",
            "middle_left": "├",
            "middle_right": "┤",
            "bottom_left": "╰",
            "bottom_right": "╯",
            "horizontal": "─",
            "vertical": "│",
            "marker": ">",
            "left": "‹",
            "right": "›",
            "help": "Up/Down navigate   Left/Right change   Enter select   q quit",
        }
    except UnicodeEncodeError:
        glyphs = {
            "ok": "+",
            "bad": "x",
            "top_left": "+",
            "top_right": "+",
            "middle_left": "+",
            "middle_right": "+",
            "bottom_left": "+",
            "bottom_right": "+",
            "horizontal": "-",
            "vertical": "|",
            "marker": ">",
            "left": "<",
            "right": ">",
            "help": "Up/Down navigate   Left/Right change   Enter select   q quit",
        }

    def border(left: str, fill: str, right: str, title: str = "") -> None:
        fill_width = dashboard_visible_width(fill) or 1
        # Section titles sit in the rule itself, so the whole console still
        # fits a default 30-row terminal (taller output disables the mouse).
        label = f" {title} " if title and dashboard_visible_width(title) + 4 <= inner_width else ""
        lead = fill if label else ""
        remaining = inner_width - dashboard_visible_width(lead + label)
        count = max(0, remaining // fill_width)
        lines.append(
            left + lead + dashboard_style(label, ANSI_BOLD + ANSI_BLUE, ansi)
            + dashboard_pad(fill * count, remaining) + right
        )

    def content(
        value: str = "", style: str = "", align: str = "left"
    ) -> None:
        if align == "center":
            padded = dashboard_pad(value, inner_width, "center")
        else:
            padded = dashboard_pad(" " + value, inner_width)
        lines.append(
            glyphs["vertical"]
            + dashboard_style(padded, style, ansi)
            + glyphs["vertical"]
        )

    border(glyphs["top_left"], glyphs["horizontal"], glyphs["top_right"])
    content("ILLUMO WORKSPACE BUILD CONSOLE", ANSI_BOLD + ANSI_CYAN, "center")
    content(
        f"Profile: {state.profile_name or 'Default'}" + (" | session overrides" if state.overrides else ""),
        ANSI_DIM,
        "center",
    )
    workspace_status, workspace_style = dashboard_workspace_status(
        state, glyphs["ok"], glyphs["bad"]
    )
    content(workspace_status, workspace_style, "center")
    last_kind = None
    for index, (kind, label, key) in enumerate(DASHBOARD_ITEMS):
        if kind != last_kind:
            border(
                glyphs["middle_left"],
                glyphs["horizontal"],
                glyphs["middle_right"],
                "Settings" if kind == "setting" else "Actions",
            )
            last_kind = kind

        marker = glyphs["marker"] if index == state.selected else " "
        prefix = f" {marker} {label}"
        if kind == "setting":
            value = dashboard_value(state, key)
            suffix = f"{glyphs['left']} {value} {glyphs['right']} "
            gap = inner_width - dashboard_visible_width(prefix) - dashboard_visible_width(suffix)
            if gap < 1:
                raw = prefix
            else:
                raw = prefix + (" " * gap) + suffix
        else:
            if key in ("play", "launch", "build_app") and not state.wasm_enabled:
                description = "needs the WASM runtime setting"
            elif key == "play":
                description = f"build, then run {state.application_label}"
            elif key == "launch":
                description = f"run the built {state.application_label}"
            else:
                description = DASHBOARD_DESCRIPTIONS[key]
            suffix = f"{description} "
            gap = inner_width - dashboard_visible_width(prefix) - dashboard_visible_width(
                suffix
            )
            if gap >= 2:
                raw = prefix + (" " * gap) + suffix
            else:
                raw = prefix
        raw = dashboard_pad(raw, inner_width)
        if hit_regions is not None:
            previous_x = raw.rfind(glyphs["left"]) + 2 if kind == "setting" else None
            hit_regions.append(DashboardHitRegion(index, len(lines) + 1, width, previous_x))
        style = ANSI_REVERSE if index == state.selected else ""
        lines.append(
            glyphs["vertical"]
            + dashboard_style(raw, style, ansi)
            + glyphs["vertical"]
        )

    border(
        glyphs["middle_left"],
        glyphs["horizontal"],
        glyphs["middle_right"],
    )
    combined_help = (
        "Up/Down move  Left/Right change  Enter select  q quit | "
        "Mouse: click, right-click, wheel"
    )
    if mouse_enabled and dashboard_visible_width(combined_help) + 1 <= inner_width:
        content(combined_help, ANSI_DIM)
    else:
        content(glyphs["help"], ANSI_DIM)
        if mouse_enabled:
            content("Click select | Right-click previous | Wheel navigate", ANSI_DIM)
    status_style = ""
    if state.status_kind == "success":
        status_style = ANSI_GREEN
    elif state.status_kind == "failure":
        status_style = ANSI_YELLOW
    content(f"Status: {state.status}", status_style)
    border(
        glyphs["bottom_left"],
        glyphs["horizontal"],
        glyphs["bottom_right"],
    )
    return "\n".join(lines)


def posix_escape_to_key(sequence: str, text_mode: bool) -> str:
    """Map bytes after ESC. Alternate-screen terminals send OA/OB for arrows
    instead of [A/[B; unknown CSI must not quit the dashboard."""
    if not sequence:
        return "escape" if text_mode else "quit"
    final = sequence[-1]
    if final in "ABCD":
        return {"A": "up", "B": "down", "C": "right", "D": "left"}[final]
    return "escape" if text_mode else "unknown"


def _posix_read_byte(file_descriptor: int, timeout: float | None) -> bytes | None:
    import select

    if timeout is not None and not select.select([file_descriptor], [], [], timeout)[0]:
        return None
    try:
        data = os.read(file_descriptor, 1)
    except OSError:
        return None
    return data or None


def read_dashboard_key(text_mode: bool = False) -> str | DashboardTextEvent:
    if os.name == "nt":
        import msvcrt

        character = msvcrt.getwch()
        if character in ("\x00", "\xe0"):
            return {
                "H": "up",
                "P": "down",
                "K": "left",
                "M": "right",
            }.get(msvcrt.getwch(), "unknown")
        if character == "\x03":
            raise KeyboardInterrupt
        if text_mode and character.isprintable():
            return DashboardTextEvent(character)
        return {
            "\r": "enter",
            "q": "quit",
            "Q": "quit",
            "j": "down",
            "k": "up",
            "h": "left",
            "l": "right",
            "/": "search", "\x08": "backspace", "\t": "tab",
            "\x1b": "escape" if text_mode else "quit",
        }.get(character, "unknown")

    file_descriptor = sys.stdin.fileno()
    first = _posix_read_byte(file_descriptor, 0.25)
    if first is None:
        return "resize"
    code = first[0]
    if code == 3:
        raise KeyboardInterrupt
    if text_mode and 32 <= code < 127:
        return DashboardTextEvent(chr(code))
    if code == 27:
        sequence = b""
        nxt = _posix_read_byte(file_descriptor, 0.05)
        if nxt is None:
            return posix_escape_to_key("", text_mode)
        sequence += nxt
        if nxt in (b"[", b"O"):
            while len(sequence) < 16:
                nxt = _posix_read_byte(file_descriptor, 0.05)
                if nxt is None:
                    break
                sequence += nxt
                if 64 <= nxt[0] <= 126:
                    break
        return posix_escape_to_key(sequence.decode("latin1"), text_mode)
    character = chr(code) if 32 <= code < 127 else first.decode("latin1")
    return {
        "\r": "enter",
        "\n": "enter",
        "q": "quit",
        "Q": "quit",
        "j": "down",
        "k": "up",
        "h": "left",
        "l": "right",
        "/": "search", "\x7f": "backspace", "\x08": "backspace", "\t": "tab",
    }.get(character, "unknown")


def adjust_dashboard_setting(state: DashboardState, direction: int) -> None:
    key = DASHBOARD_ITEMS[state.selected][2]
    if key == "configuration":
        state.configuration_index = (
            state.configuration_index + direction
        ) % len(DASHBOARD_CONFIGURATIONS)
    elif key == "application":
        apps = state.applications if state.applications else (DEFAULT_APP,)
        state.application_index = (state.application_index + direction) % len(apps)
    elif key == "testing":
        state.testing_enabled = not state.testing_enabled
    elif key == "documentation":
        state.documentation_enabled = not state.documentation_enabled
    elif key == "tracy":
        state.tracy_enabled = not state.tracy_enabled
    elif key == "wasm" and WASM_HOST_SUPPORTED:
        state.wasm_enabled = not state.wasm_enabled
    elif key == "parallel":
        state.parallel_index = (state.parallel_index + direction) % len(
            DASHBOARD_PARALLEL_OPTIONS
        )
    mapped = {
        "configuration": ("config", state.configuration),
        "testing": ("no_tests", not state.testing_enabled),
        "documentation": ("no_docs", not state.documentation_enabled),
        "tracy": ("tracy", state.tracy_enabled),
        "wasm": ("no_wasm", not state.wasm_enabled),
        "parallel": ("parallel", DASHBOARD_PARALLEL_OPTIONS[state.parallel_index][1]),
    }
    if key in mapped:
        name, value = mapped[key]
        state.overrides[name] = value


def dashboard_parallel_label(state: DashboardState) -> str:
    value = state.parallel_value
    return "Auto" if value == 0 else "Off" if value is None else f"{value} jobs"


def dashboard_settings(state: DashboardState) -> dict:
    settings = {
        "config": state.configuration, "build_dir": str(DEFAULT_BUILD_DIRECTORY),
        "tracy": state.tracy_enabled, "no_tests": not state.testing_enabled,
        "no_docs": not state.documentation_enabled, "no_tidy": False,
        "no_wasm": not state.wasm_enabled,
        "parallel": state.parallel_value, "cmake_arg": [],
    }
    settings.update(state.profile_settings)
    settings.update(state.overrides)
    return settings


def apply_dashboard_profile(state: DashboardState, name: str | None, settings: dict) -> None:
    validate_profile(name or "default", settings)
    state.profile_name = name
    state.profile_settings = dict(settings)
    state.overrides.clear()
    state.configuration_index = DASHBOARD_CONFIGURATIONS.index(settings.get("config", "Release"))
    state.testing_enabled = not settings.get("no_tests", False)
    state.documentation_enabled = not settings.get("no_docs", False)
    state.tracy_enabled = settings.get("tracy", False)
    state.wasm_enabled = not settings.get("no_wasm", not WASM_HOST_SUPPORTED)
    state.parallel_index = next((i for i, item in enumerate(DASHBOARD_PARALLEL_OPTIONS)
                                 if item[1] == settings.get("parallel", 0)), 0)


def dashboard_parallel_arguments(state: DashboardState) -> list[str]:
    parallel = state.parallel_value
    if parallel is None:
        return []
    if parallel == 0:
        return ["--parallel"]
    return ["--parallel", str(parallel)]


def dashboard_common_arguments(state: DashboardState) -> list[str]:
    return profile_arguments(dashboard_settings(state))


def dashboard_action_arguments(
    state: DashboardState, action: str
) -> list[str]:
    if action == "build":
        return ["build", *dashboard_common_arguments(state)]
    packaged = state.application in {app.name for app in installed_apps()}
    if action in ("play", "launch") and not packaged:
        # A native application in a workspace without runtime packages.
        return ["run", "--app", state.application, *dashboard_common_arguments(state),
                *(["--no-build"] if action == "launch" else [])]
    if action == "play":
        return ["play", "--app", state.application, *dashboard_common_arguments(state)]
    if action == "build_app":
        # The runtime target stages every application package.
        return [
            "build",
            *dashboard_common_arguments(state),
            "--target",
            WASM_RUNTIME_APPLICATION,
        ]
    if action == "test":
        return ["test", *dashboard_common_arguments(state)]
    if action == "launch":
        return [
            "play",
            "--app",
            state.application,
            *dashboard_common_arguments(state),
            "--no-build",
        ]
    if action == "stats":
        return ["stats"]
    if action == "file_stats":
        return ["file-stats"]
    if action == "docs":
        return ["docs"]
    if action == "coverage":
        return ["coverage", *dashboard_parallel_arguments(state)]
    if action == "tidy":
        return ["tidy", *dashboard_parallel_arguments(state)]
    raise BuildError(f"Unknown dashboard action: {action}")


def progress_text(value: str) -> str:
    """Keep subprocess control sequences out of the dashboard itself."""
    value = re.sub(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))", "", value)
    return "".join(character if character.isprintable() else " " for character in value)


@dataclass
class DashboardProgress:
    title: str
    context: str
    log_path: Path
    started: float = field(default_factory=time.monotonic)
    phase: str = "Starting"
    phase_started: float = 0.0
    command: str = "Waiting for tool output"
    completed: int = 0
    total: int = 0
    unit: str = ""
    warnings: int = 0
    errors: int = 0
    tests_completed: int = 0
    tests_total: int = 0
    returncode: int | None = None
    finished: float | None = None
    cancelled: bool = False
    lines: deque[str] = field(default_factory=lambda: deque(maxlen=200))

    def __post_init__(self) -> None:
        self.phase_started = self.started

    def set_phase(self, name: str, now: float, *, reset: bool = False) -> None:
        if name != self.phase or reset:
            self.phase = name
            self.phase_started = now
            self.completed = self.total = 0
            self.unit = ""
            if name == "Testing":
                self.tests_completed = self.tests_total = 0

    def consume(self, line: str, now: float) -> None:
        line = progress_text(line).rstrip()
        if not line:
            return
        self.lines.append(line[:8192])
        if re.search(
            r"\bwarning\b\s*(?:[A-Z]+\d+\s*)?:|^\s*CMake Warning(?: \(dev\))? (?:at|in)\b",
            line, re.IGNORECASE,
        ):
            self.warnings += 1
        if re.search(
            r"\b(?:fatal error|error)\b\s*(?:[A-Z]+\d+\s*)?:|^\s*CMake Error (?:at|in)\b",
            line, re.IGNORECASE,
        ):
            self.errors += 1
        if line.startswith("> "):
            self.command = line[2:]
            lower = self.command.lower()
            if " --build " in lower:
                phase = "Building"
            elif "ctest" in lower:
                phase = "Testing"
            elif " -s " in lower and " -b " in lower:
                phase = "Configuring"
            elif "build.ps1" in lower:
                phase = "Documentation"
            else:
                phase = "Running tool"
            self.set_phase(phase, now, reset=True)
        test = re.search(r"\b(\d+)/(\d+)\s+Test\s+#", line)
        ninja = re.match(r"\s*\[(\d+)/(\d+)\]", line)
        percent = re.match(r"\s*\[\s*(\d+)%\]", line)
        if test:
            self.set_phase("Testing", now)
            done, total = map(int, test.groups())
            self.completed, self.total = max(self.completed, done), total
            self.tests_completed, self.tests_total = self.completed, total
            self.unit = "tests"
        elif ninja:
            self.set_phase("Building", now)
            self.completed, self.total = map(int, ninja.groups())
            self.unit = "steps"
        elif percent:
            self.set_phase("Building", now)
            self.completed, self.total = int(percent[1]), 100
            self.unit = "%"
        elif self.phase == "Testing" and (
            ".vcxproj ->" in line or line.lstrip().startswith(("Building ", "Linking "))
        ):
            self.set_phase("Building", now)


def render_dashboard_progress(
    progress: DashboardProgress, columns: int, rows: int,
    now: float | None = None, ansi: bool = True,
) -> str:
    now = time.monotonic() if now is None else now
    end = progress.finished if progress.finished is not None else now
    elapsed = max(0.0, end - progress.started)
    width = max(1, min(110, columns - 1))
    height = max(1, rows - 2)
    running = progress.returncode is None
    status = "RUNNING" if running else (
        "CANCELLED" if progress.cancelled else "SUCCEEDED" if progress.returncode == 0 else "FAILED"
    )
    spinner = "|/-\\"[int(elapsed * 5) % 4] if running else "*"
    try:
        log_label = str(progress.log_path.relative_to(REPOSITORY_ROOT))
    except ValueError:
        log_label = str(progress.log_path)
    if progress.total > 0:
        fraction = min(1.0, max(0.0, progress.completed / progress.total))
        bar_width = max(4, min(26, width - 36))
        fill = int(fraction * bar_width)
        count = f"{progress.completed}/{progress.total} {progress.unit}"
        if progress.unit == "%":
            count = f"{progress.completed}%"
        tool_progress = f"[{'#' * fill}{'-' * (bar_width - fill)}] {count} (tool-reported)"
    else:
        tool_progress = f"[{spinner}] Working; this tool has not reported a total" if running else "Action finished"
    heading = [
        f"ILLUMO  /  {progress.title}",
        progress.context,
        "=" * width,
        f"{status}   Elapsed {elapsed:.1f}s   Phase {max(0.0, end - progress.phase_started):.1f}s",
        f"Phase: {progress.phase}",
        tool_progress,
        f"Warning lines: {progress.warnings}   Error lines: {progress.errors}"
        + (f"   Last test run: {progress.tests_completed}/{progress.tests_total}" if progress.tests_total else ""),
        f"Command: {progress.command}",
        f"Full log: {log_label}",
        "-" * width,
    ]
    tail_count = max(0, height - len(heading) - 2)
    tail = list(progress.lines)[-tail_count:] if tail_count else []
    footer = "Ctrl+C cancels this action" if running else f"Exit code: {progress.returncode} | Full output saved to log"
    lines = (heading + tail + ["-" * width, footer])[:height]
    rendered: list[str] = []
    for index, line in enumerate(lines):
        text = progress_text(line)
        text = text[:width] if width < 4 else (text[:width - 3] + "..." if len(text) > width else text)
        style = ANSI_BOLD + ANSI_CYAN if index == 0 else ""
        if index == 3:
            style = ANSI_CYAN if running else ANSI_GREEN if progress.returncode == 0 else ANSI_YELLOW
        rendered.append(dashboard_style(text.ljust(width), style, ansi))
    return "\n".join(rendered)


def paint_dashboard_progress(progress: DashboardProgress) -> None:
    size = shutil.get_terminal_size((96, 30))
    sys.stdout.write(
        ANSI_CLEAR + tui_newlines(
            render_dashboard_progress(progress, size.columns, size.lines)
        )
    )
    sys.stdout.flush()


class WindowsProgressJob:
    """Keep every descendant owned even if the launcher exits first."""

    def __init__(self) -> None:
        import ctypes
        from ctypes import wintypes

        class BasicLimits(ctypes.Structure):
            _fields_ = [
                ("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                ("flags", wintypes.DWORD), ("minimum", ctypes.c_size_t),
                ("maximum", ctypes.c_size_t), ("active_limit", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                ("scheduling", wintypes.DWORD),
            ]

        class ExtendedLimits(ctypes.Structure):
            _fields_ = [
                ("basic", BasicLimits), ("io", ctypes.c_uint64 * 6),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t),
            ]

        class Accounting(ctypes.Structure):
            _fields_ = [
                ("times", ctypes.c_int64 * 4), ("page_faults", wintypes.DWORD),
                ("total", wintypes.DWORD), ("active", wintypes.DWORD),
                ("terminated", wintypes.DWORD),
            ]

        self.ctypes = ctypes
        self.accounting_type = Accounting
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            "SetInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            "QueryInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p], wintypes.BOOL),
            "OpenProcess": ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            "AssignProcessToJobObject": ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            "TerminateJobObject": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
            "WaitForSingleObject": ([wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = arguments, result
        self.handle = self.api.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            error = ctypes.WinError(ctypes.get_last_error())
            self.api.CloseHandle(self.handle)
            self.handle = None
            raise error

    def assign(self, process: subprocess.Popen) -> None:
        handle = self.api.OpenProcess(0x0101, False, process.pid)
        if not handle:
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        try:
            if not self.api.AssignProcessToJobObject(self.handle, handle):
                raise self.ctypes.WinError(self.ctypes.get_last_error())
        finally:
            self.api.CloseHandle(handle)

    def terminate(self) -> None:
        from ctypes import wintypes

        handles: list[int] = []
        try:
            capacity = 64
            while True:
                class ProcessList(self.ctypes.Structure):
                    _fields_ = [
                        ("assigned", wintypes.DWORD), ("count", wintypes.DWORD),
                        ("ids", self.ctypes.c_size_t * capacity),
                    ]
                processes = ProcessList()
                if self.api.QueryInformationJobObject(
                    self.handle, 3, self.ctypes.byref(processes), self.ctypes.sizeof(processes), None,
                ):
                    break
                error = self.ctypes.get_last_error()
                if error != 234:  # ERROR_MORE_DATA: the process list grew.
                    raise self.ctypes.WinError(error)
                capacity = max(capacity * 2, processes.assigned)
            for pid in processes.ids[:processes.count]:
                handle = self.api.OpenProcess(0x100000, False, pid)  # SYNCHRONIZE
                if handle:
                    handles.append(handle)
                elif self.ctypes.get_last_error() != 87:  # Already exited.
                    raise self.ctypes.WinError(self.ctypes.get_last_error())
            if not self.api.TerminateJobObject(self.handle, 130):
                raise self.ctypes.WinError(self.ctypes.get_last_error())
            deadline = time.monotonic() + 5
            while True:
                accounting = self.accounting_type()
                if not self.api.QueryInformationJobObject(
                    self.handle, 1, self.ctypes.byref(accounting), self.ctypes.sizeof(accounting), None,
                ):
                    raise self.ctypes.WinError(self.ctypes.get_last_error())
                if accounting.active == 0:
                    break
                if time.monotonic() >= deadline:
                    raise BuildError("Timed out waiting for cancelled build processes to exit.")
                time.sleep(0.02)
            # Job accounting reaches zero before process teardown necessarily
            # finishes. Signaled process handles also protect log-file reuse.
            for handle in handles:
                remaining = max(0, int((deadline - time.monotonic()) * 1000))
                if self.api.WaitForSingleObject(handle, remaining) != 0:
                    raise BuildError("Timed out waiting for build process cleanup.")
        finally:
            for handle in handles:
                self.api.CloseHandle(handle)

    def close(self) -> None:
        if self.handle:
            try:
                self.terminate()
            finally:
                self.api.CloseHandle(self.handle)
                self.handle = None


def stop_dashboard_process(
    process: subprocess.Popen, job: WindowsProgressJob | None = None,
) -> None:
    """Cancel only the process group created for the active dashboard action."""
    if job is not None:
        job.terminate()
        process.wait(timeout=5)
        return
    # A POSIX group can outlive its leader. Signal it even after the root exits.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=5)


def run_dashboard_progress(
    command: Sequence[str], progress: DashboardProgress,
) -> None:
    # A file-backed stream avoids pipe deadlocks and retains complete output;
    # only the bounded tail and one bounded read chunk live in memory.
    process: subprocess.Popen | None = None
    job: WindowsProgressJob | None = None
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    pending = ""
    last_paint = 0.0
    environment = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8",
                       ILLUMO_RUN_CONTEXT=str(progress.log_path.with_suffix(".commands.jsonl")))
    try:
        with progress.log_path.open("wb") as output, progress.log_path.open("rb") as reader:
            launch_command = list(command)
            if os.name == "nt":
                job = WindowsProgressJob()
                # The bootstrap cannot spawn descendants until it is assigned
                # to our job. Closing the gate on setup failure makes it exit.
                bootstrap = (
                    "import subprocess,sys; "
                    "gate=sys.stdin.buffer.read(1); "
                    "sys.exit(subprocess.call(sys.argv[1:],stdin=subprocess.DEVNULL) if gate else 1)"
                )
                launch_command = [sys.executable, "-u", "-c", bootstrap, *command]
            process = subprocess.Popen(
                launch_command, cwd=REPOSITORY_ROOT,
                stdin=subprocess.PIPE if job else subprocess.DEVNULL,
                stdout=output, stderr=subprocess.STDOUT, env=environment,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
                start_new_session=os.name != "nt",
            )
            if job is not None:
                try:
                    job.assign(process)
                    process.stdin.write(b"1")
                    process.stdin.flush()
                finally:
                    process.stdin.close()
            while True:
                try:
                    chunk = reader.read(65536)
                    code = process.poll()
                    text = decoder.decode(chunk, final=not chunk and code is not None)
                    parts = re.split(r"[\r\n]", pending + text)
                    pending = parts.pop()[-8192:]
                    for line in parts:
                        progress.consume(line, time.monotonic())
                    if not chunk and code is not None:
                        if pending:
                            progress.consume(pending, time.monotonic())
                        progress.returncode = 130 if progress.cancelled else code
                        break
                    now = time.monotonic()
                    if now - last_paint >= 0.1:
                        paint_dashboard_progress(progress)
                        last_paint = now
                    if not chunk:
                        time.sleep(0.1)
                except KeyboardInterrupt:
                    progress.cancelled = True
                    stop_dashboard_process(process, job)
    except (OSError, BuildError, subprocess.SubprocessError) as error:
        progress.consume(f"error: {error}", time.monotonic())
        progress.returncode = 1
    finally:
        if job is not None:
            job.close()
        if process is not None and process.poll() is None:
            if os.name == "nt":
                process.terminate()  # Only an unassigned bootstrap can remain.
                process.wait(timeout=5)
            else:
                stop_dashboard_process(process)
        progress.finished = time.monotonic()
    paint_dashboard_progress(progress)


def execute_dashboard_action(
    state: DashboardState,
    action: str,
    terminal: DashboardTerminal,
) -> None:
    if action == "tools":
        run_development_tools(state, terminal)
        return
    if action == "test":
        execute_toolbox_run(state, terminal, None, True)
        return
    arguments = dashboard_action_arguments(state, action)
    command = [sys.executable, str(Path(__file__).resolve()), *arguments]
    terminal.leave()
    if action in ("build", "build_app", "test", "coverage", "tidy", "docs"):
        log_directory = REPOSITORY_ROOT / "build-orchestrator-logs"
        try:
            log_directory.mkdir(exist_ok=True)
            with tempfile.NamedTemporaryFile(
                prefix=f"{action}-", suffix=".log", dir=log_directory, delete=False,
            ) as log:
                progress = DashboardProgress(
                    DASHBOARD_ITEMS[state.selected][1],
                    f"{state.configuration} | {state.application} | Parallel: {dashboard_parallel_label(state)}",
                    Path(log.name),
                )
            sys.stdout.write(ANSI_ENTER_SCREEN + ANSI_HIDE_CURSOR)
            sys.stdout.flush()
            try:
                record = new_run_record(state, action, command)
                progress.context = f"{record['identity']['configuration']} | {record['identity']['build_directory']} | Parallel: {dashboard_parallel_label(state)}"
                write_json_atomic(progress.log_path.with_suffix(".json"), record)
                run_dashboard_progress(command, progress)
                finish_run_record(progress, record)
                elapsed = (progress.finished or time.monotonic()) - progress.started
                outcome = "cancelled" if progress.cancelled else "succeeded" if progress.returncode == 0 else "failed"
                state.status = f"{progress.title} {outcome} ({elapsed:.1f}s)"
                state.status_kind = "success" if progress.returncode == 0 else "failure"
                sys.stdout.write(ANSI_SHOW_CURSOR)
                try:
                    input("\nPress Enter to return to the build console...")
                except EOFError:
                    pass
            finally:
                sys.stdout.write(ANSI_RESET + ANSI_SHOW_CURSOR + ANSI_LEAVE_SCREEN)
                sys.stdout.flush()
        except OSError as error:
            state.status = f"Could not start progress display: {error}"
            state.status_kind = "failure"
        terminal.enter()
        return
    print(f"\n=== {DASHBOARD_ITEMS[state.selected][1]} ===\n")
    print(f"> {format_command(command)}\n", flush=True)
    try:
        result = subprocess.run(command, cwd=REPOSITORY_ROOT, check=False)
        if result.returncode == 0:
            state.status = f"{DASHBOARD_ITEMS[state.selected][1]} succeeded"
            state.status_kind = "success"
        else:
            state.status = (
                f"{DASHBOARD_ITEMS[state.selected][1]} failed "
                f"(exit {result.returncode})"
            )
            state.status_kind = "failure"
    except OSError as error:
        state.status = f"Could not start action: {error}"
        state.status_kind = "failure"
    try:
        input("\nPress Enter to return to the build console...")
    except EOFError:
        pass
    terminal.enter()


def write_json_atomic(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def build_identity(settings: dict) -> dict:
    return {"checkout": str(REPOSITORY_ROOT.resolve()),
            "build_directory": str(resolve_build_directory(Path(settings["build_dir"]))),
            "configuration": settings["config"]}


def new_run_record(state: DashboardState, action: str, command: list[str]) -> dict:
    settings = dashboard_settings(state)
    if action in ("coverage", "tidy"):
        parsed = create_parser().parse_args(command[2:])
        settings = {"config": "Debug", "build_dir": str(parsed.build_dir), "generator": "Ninja",
                    "no_docs": True, "tracy": False, "no_tidy": action == "coverage",
                    "parallel": parsed.parallel, "cmake_arg": parsed.cmake_arg,
                    "coverage": action == "coverage", "compiler": "clang"}
    return {"version": 1, "identity": build_identity(settings), "settings": settings,
            "profile": state.profile_name, "action": action, "command": command,
            "working_directory": str(REPOSITORY_ROOT), "started": time.time(),
            "status": "running", "tests": [], "tests_complete": False}


def finish_run_record(progress: DashboardProgress, record: dict) -> None:
    record.update(status="cancelled" if progress.cancelled else
                  "succeeded" if progress.returncode == 0 else "failed",
                  exit_code=progress.returncode,
                  elapsed=(progress.finished or time.monotonic()) - progress.started)
    context_path = progress.log_path.with_suffix(".commands.jsonl")
    record["commands"] = []
    if context_path.is_file():
        for line in context_path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                context = json.loads(line)
                if isinstance(context, dict) and isinstance(context.get("command"), list) and isinstance(context.get("working_directory"), str):
                    record["commands"].append(context)
            except ValueError:
                pass  # Cancellation may interrupt the final context append.
    result_path = progress.log_path.with_suffix(".tests.json")
    if result_path.is_file():
        try:
            result = json.loads(result_path.read_text(encoding="utf-8"))
            record["tests"] = result.get("tests", [])
            record["tests_complete"] = bool(result.get("complete")) and not progress.cancelled
            record["test_message"] = result.get("message", "")
        except (OSError, ValueError, AttributeError):
            record["test_message"] = "Test results could not be read; raw log remains authoritative."
    for test in record["tests"]:
        if test["status"] == "pending":
            test["status"] = "cancelled" if progress.cancelled else "incomplete"
    write_json_atomic(progress.log_path.with_suffix(".json"), record)


def run_history(directory: Path | None = None) -> list[dict]:
    directory = directory or REPOSITORY_ROOT / "build-orchestrator-logs"
    runs = []
    for log in directory.glob("*.log"):
        record = {}
        try:
            candidate = json.loads(log.with_suffix(".json").read_text(encoding="utf-8"))
            if isinstance(candidate, dict) and candidate.get("version") == 1:
                record = candidate
        except (OSError, ValueError):
            pass
        runs.append({**record, "log": str(log), "recorded_at": log.stat().st_mtime})
    return sorted(runs, key=lambda run: run.get("started", run["recorded_at"]), reverse=True)


def failed_test_names(history: list[dict], identity: dict, inventory: list[dict]) -> list[str]:
    latest = next((run for run in history if run.get("identity") == identity
                   and run.get("action") == "test"), None)
    if latest is None:
        raise BuildError("No recorded test run for this checkout, build directory and configuration. Legacy logs cannot supply reruns.")
    if not latest.get("tests_complete"):
        raise BuildError("The latest test run is incomplete or cancelled. Inspect its log; choose tests explicitly to run again.")
    names = [test["name"] for test in latest.get("tests", []) if test["status"] == "failed"]
    missing = set(names) - {test["name"] for test in inventory}
    if missing:
        raise BuildError("Previously failed tests are missing from inventory: " + ", ".join(sorted(missing)))
    if not names:
        raise BuildError("The latest test run has no failed tests.")
    return names


def require_discovery(build_directory: Path, configuration: str, workspace: WorkspaceProjects) -> None:
    cache = read_cmake_cache(build_directory)
    if cache and not cache.get("CMAKE_CONFIGURATION_TYPES") and cache.get("CMAKE_BUILD_TYPE") != configuration:
        raise BuildError(f"Single-configuration cache is {cache.get('CMAKE_BUILD_TYPE') or 'unspecified'}, not {configuration}. Refresh Inventory for the selected configuration.")
    missing = [build_directory / project.directory.relative_to(workspace.root) /
               f"{runner}-{configuration}-discovered.cmake"
               for project in workspace.projects for runner in project.test_runners
               if runner + "Discover" in project.discovery_targets]
    missing = [str(path) for path in missing if not path.is_file()]
    if missing:
        raise BuildError(f"Missing {configuration} discovery files; other-configuration fallback is rejected. Use Refresh Inventory. " + "; ".join(missing))


def read_test_inventory(settings: dict) -> list[dict]:
    arguments = create_parser().parse_args(["configure", *profile_arguments(settings)])
    validate_build_settings(arguments)
    directory = resolve_build_directory(arguments.build_dir)
    require_discovery(directory, arguments.config, discover_workspace_projects(REPOSITORY_ROOT))
    command = [existing_tool("ctest", False), "--test-dir", str(directory), "-C",
               arguments.config, "--show-only=json-v1", "-L", "^IllumoWorkspace$"]
    result = subprocess.run(command, cwd=REPOSITORY_ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", check=False)
    if result.returncode:
        raise BuildError("CTest inventory failed: " + result.stdout + result.stderr)
    try:
        inventory = json.loads(result.stdout)["tests"]
        for test in inventory:
            test["properties"] = {item["name"]: item["value"] for item in test.get("properties", [])}
        return inventory
    except (ValueError, KeyError, TypeError) as error:
        raise BuildError(f"Invalid CTest JSON inventory: {error}") from error


def ctest_listing(ctest: str, build_directory: Path, configuration: str) -> list[dict]:
    """Best-effort workspace test listing; empty when the tree is not ready."""
    command = [ctest, "--test-dir", str(build_directory), "-C", configuration,
               "--show-only=json-v1", "-L", "^IllumoWorkspace$"]
    try:
        result = subprocess.run(command, cwd=REPOSITORY_ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=120, check=False)
        tests = json.loads(result.stdout)["tests"] if result.returncode == 0 else []
    except (OSError, subprocess.TimeoutExpired, ValueError, KeyError, TypeError):
        return []
    return [test for test in tests if isinstance(test, dict) and isinstance(test.get("name"), str)]


def ctest_executable_target(test: dict, build_directory: Path) -> str | None:
    """The CMake target behind a test command that runs a built executable."""
    command = test.get("command")
    if not isinstance(command, list) or not command or not isinstance(command[0], str):
        return None
    executable = Path(command[0])
    try:
        executable.resolve().relative_to(build_directory.resolve())
    except ValueError:
        return None
    return executable.stem


def build_test_executables(
    arguments: argparse.Namespace, cmake: str, runner: CommandRunner,
    workspace: WorkspaceProjects,
) -> None:
    """Build discovery and smoke targets, then every other executable CTest runs.

    Discovery only covers the scanned test runners; the WASM runtime and
    package tests are plain add_test() cases whose executables would
    otherwise be stale or missing when CTest starts.
    """
    built = [*workspace.discovery_targets, *workspace.smoke_targets]
    for target in built:
        runner.run(build_command(arguments, cmake, target))
    if runner.dry_run:
        return
    build_directory = resolve_build_directory(arguments.build_dir)
    built_names = set(built) | {target.removesuffix("Discover") for target in built}
    extra: list[str] = []
    for test in ctest_listing(existing_tool("ctest", False), build_directory, arguments.config):
        target = ctest_executable_target(test, build_directory)
        if target and target not in built_names and target not in extra:
            extra.append(target)
    if extra:
        runner.run(build_command(arguments, cmake, extra))


def matching_tests(inventory: list[dict], search: str, label: str = "All") -> list[dict]:
    return [test for test in inventory if search.casefold() in test["name"].casefold()
            and (label == "All" or label in test.get("properties", {}).get("LABELS", []))]


def test_name_batches(names: list[str], limit: int = 6000) -> list[tuple[list[str], str]]:
    if not names:
        raise BuildError("No tests selected; nothing was run.")
    batches = []
    selected: list[str] = []
    escaped: list[str] = []
    for name in dict.fromkeys(names):
        # CTest uses CMake regular expressions, not Python's regex extensions.
        token = re.sub(r"([.\[\]{}()*+?^$|\\])", r"\\\1", name)
        if len(token) + 4 > limit:
            raise BuildError("Test name exceeds the safe command length: " + name)
        if escaped and len("|".join([*escaped, token])) + 4 > limit:
            batches.append((selected, "^(" + "|".join(escaped) + ")$"))
            selected, escaped = [], []
        selected.append(name)
        escaped.append(token)
    batches.append((selected, "^(" + "|".join(escaped) + ")$"))
    return batches


def ctest_report(directory: Path) -> tuple[Path | None, tuple | None]:
    try:
        tag = (directory / "Testing" / "TAG").read_text(encoding="utf-8").splitlines()[0]
        if not re.fullmatch(r"[A-Za-z0-9_-]+", tag):
            return None, None
        path = directory / "Testing" / tag / "Test.xml"
        stat = path.stat()
        return path, (stat.st_mtime_ns, stat.st_size, path.read_bytes())
    except (OSError, IndexError):
        return None, None


def parse_ctest_results(xml: bytes, names: list[str]) -> list[dict]:
    root = ET.fromstring(xml)
    found = {}
    for node in root.findall(".//Testing/Test"):
        name = node.findtext("Name")
        if name not in names:
            continue
        measurements = {item.get("name"): item.findtext("Value", "")
                        for item in node.findall("Results/NamedMeasurement")}
        raw = node.get("Status", "notrun")
        status = {"passed": "passed", "failed": "failed"}.get(raw, "incomplete")
        if measurements.get("Completion Status") == "Disabled":
            status = "disabled"
        try:
            duration = float(measurements.get("Execution Time", "0"))
        except ValueError:
            duration = None
        found[name] = {"name": name, "status": status, "duration": duration,
                       "completion": measurements.get("Completion Status", raw)}
    return [found.get(name, {"name": name, "status": "incomplete", "duration": None,
                             "completion": "Missing from fresh CTest report"}) for name in names]


def run_toolbox_request(arguments: argparse.Namespace) -> None:
    try:
        request = json.loads(arguments.request.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        raise BuildError(f"Could not read toolbox request: {error}") from error
    if not isinstance(request, dict) or request.get("version") != 1:
        raise BuildError("Unsupported toolbox request version.")
    if request.get("names") == [] and request.get("mode") == "run":
        raise BuildError("No tests selected; nothing was run.")
    if (request.get("mode") not in ("run", "refresh") or type(request.get("build_first")) is not bool
            or not isinstance(request.get("result"), str) or not request["result"]
            or "names" not in request or (request["names"] is not None and
                (not isinstance(request["names"], list) or not all(isinstance(name, str) and name for name in request["names"])))):
        raise BuildError("Invalid toolbox request fields.")
    validate_profile("toolbox", request.get("settings"))
    settings, names = request["settings"], request["names"]
    if names == [] and request["mode"] != "refresh":
        raise BuildError("No tests selected; nothing was run.")
    parsed = create_parser().parse_args(["configure", *profile_arguments(settings)])
    if parsed.no_tests:
        raise BuildError("Tests are disabled in this profile. Enable tests before preparation or execution.")
    result_path = Path(request["result"])
    result = {"tests": [{"name": name, "status": "pending", "duration": None}
                        for name in names or []], "complete": False}
    write_json_atomic(result_path, result)
    runner = CommandRunner(False)
    if request["build_first"] or request["mode"] == "refresh":
        cmake = configure(parsed, runner)
        build_test_executables(parsed, cmake, runner, discover_workspace_projects(REPOSITORY_ROOT))
    inventory = read_test_inventory(settings)
    if request["mode"] == "refresh":
        print(f"Inventory refreshed: {len(inventory)} tests.", flush=True)
        return
    if names is None:
        names = [test["name"] for test in inventory]
    missing = set(names) - {test["name"] for test in inventory}
    if missing:
        raise BuildError("Selected tests are missing: " + ", ".join(sorted(missing)))
    batches = test_name_batches(names)
    result["tests"] = [{"name": name, "status": "pending", "duration": None} for name in names]
    write_json_atomic(result_path, result)
    directory = resolve_build_directory(parsed.build_dir)
    codes = []
    for index, (batch, expression) in enumerate(batches):
        _, previous = ctest_report(directory)
        command = [existing_tool("ctest", False), "--test-dir", str(directory), "-C", parsed.config,
                   "-T", "Test", "--no-compress-output", "--output-on-failure",
                   "--no-tests=error", "-L", "^IllumoWorkspace$", "-R", expression]
        try:
            runner.run(command)
            codes.append(0)
        except BuildError as error:
            print(str(error), flush=True)
            codes.append(error.exit_code)
        report, fresh = ctest_report(directory)
        completed = []
        if report is not None and fresh != previous and fresh is not None:
            snapshot = result_path.with_suffix(f".batch-{index + 1}.xml")
            snapshot.write_bytes(fresh[2])
            try:
                completed = parse_ctest_results(fresh[2], batch)
            except ET.ParseError as error:
                result["message"] = f"Incomplete CTest XML: {error}"
        by_name = {test["name"]: test for test in completed}
        result["tests"] = [by_name.get(test["name"], test) for test in result["tests"]]
        write_json_atomic(result_path, result)
    result["complete"] = all(test["status"] in ("passed", "failed", "disabled") for test in result["tests"])
    write_json_atomic(result_path, result)
    if any(codes) or not result["complete"]:
        raise BuildError("Test run failed or has incomplete results. Inspect the recorded log and test results.")


def execute_toolbox_run(state: DashboardState, terminal: DashboardTerminal,
                        names: list[str] | None, build_first: bool, refresh: bool = False) -> None:
    if names == [] and not refresh:
        raise BuildError("No tests selected; nothing was run.")
    directory = REPOSITORY_ROOT / "build-orchestrator-logs"
    directory.mkdir(exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix="tests-", suffix=".log", dir=directory, delete=False) as log:
        path = Path(log.name)
    request = path.with_suffix(".request.json")
    write_json_atomic(request, {"version": 1, "settings": dashboard_settings(state), "names": names,
                               "build_first": build_first, "mode": "refresh" if refresh else "run",
                               "result": str(path.with_suffix(".tests.json"))})
    command = [sys.executable, str(Path(__file__).resolve()), "_toolbox", str(request)]
    record = new_run_record(state, "inventory" if refresh else "test", command)
    write_json_atomic(path.with_suffix(".json"), record)
    progress = DashboardProgress("Refresh Inventory" if refresh else "Run tests",
                                 f"{state.configuration} | {'Prepare and run' if build_first else 'Run Existing'}", path)
    terminal.leave()
    try:
        sys.stdout.write(ANSI_ENTER_SCREEN + ANSI_HIDE_CURSOR)
        run_dashboard_progress(command, progress)
        finish_run_record(progress, record)
        state.status = f"{progress.title}: {record['status']} | {path.name}"
        state.status_kind = "success" if progress.returncode == 0 else "failure"
        try:
            input("\nPress Enter to return to Development Tools...")
        except EOFError:
            pass
    finally:
        sys.stdout.write(ANSI_RESET + ANSI_SHOW_CURSOR + ANSI_LEAVE_SCREEN)
        terminal.enter()


@dataclass
class ToolboxRow:
    key: str
    label: str
    details: tuple[str, ...] = ()
    action: bool = False


@dataclass
class ToolboxView:
    selected: int = 0
    top: int = 0
    focused: str | None = None
    search: str = ""
    draft: str | None = None


def toolbox_event(view: ToolboxView, rows: list[ToolboxRow], event: object,
                  regions: list[DashboardHitRegion], page_size: int) -> str | None:
    if view.draft is not None:
        if isinstance(event, DashboardTextEvent):
            view.draft += event.text
        elif event == "backspace":
            view.draft = view.draft[:-1]
        elif event == "enter":
            view.search, view.draft = view.draft, None
            view.selected, view.top = 0, 0
            return "search"
        elif event in ("escape", "quit"):
            view.draft = None
        return None
    if event == "search":
        view.draft = view.search
        return None
    if isinstance(event, DashboardMouseEvent):
        if event.kind in ("wheel_up", "wheel_down"):
            event = "up" if event.kind == "wheel_up" else "down"
        else:
            region = next((hit for hit in regions if hit.row == event.y and 1 <= event.x <= hit.right), None)
            if region is None:
                return None
            view.selected = region.index
            event = "enter" if event.kind == "left" else None
    if event in ("quit", "escape"):
        return "back"
    movement = {"up": -1, "down": 1, "page_up": -page_size, "page_down": page_size}
    if event in movement:
        view.selected += movement[event]
    elif event == "home":
        view.selected = 0
    elif event == "end":
        view.selected = len(rows) - 1
    view.selected = max(0, min(view.selected, len(rows) - 1))
    if not rows:
        return None
    row = rows[view.selected]
    if not row.action:
        view.focused = row.key
    if event == "enter" and row.action:
        return row.key
    return None


def render_toolbox(title: str, rows: list[ToolboxRow], view: ToolboxView,
                   width: int, height: int, status: str = "") -> tuple[str, list[DashboardHitRegion], int]:
    width = max(1, width - 1)
    page = max(1, height - 13)
    view.selected = max(0, min(view.selected, len(rows) - 1))
    view.top = max(0, min(view.top, view.selected))
    if view.selected >= view.top + page:
        view.top = view.selected - page + 1
    selected = next((row for row in rows if row.key == view.focused), None)
    if rows and not rows[view.selected].action:
        selected = rows[view.selected]
        view.focused = selected.key
    lines = [title, "Arrows / hover: select | Enter / click action: execute | /: search | q/Esc: back",
             ("Search editing (Enter applies, Escape cancels): " + view.draft)
             if view.draft is not None else "Search: " + (view.search or "(none)"), ""]
    regions = []
    for index in range(view.top, min(len(rows), view.top + page)):
        row = rows[index]
        lines.append(("> " if index == view.selected else "  ") + ("[Action] " if row.action else "") + row.label)
        regions.append(DashboardHitRegion(index, len(lines), width, 1))
    lines.extend([""] * (page - min(page, max(0, len(rows) - view.top))))
    lines.append(f"Rows {view.top + 1 if rows else 0}-{min(len(rows), view.top + page)} of {len(rows)}")
    details = selected.details if selected else ("Select an item to inspect its details.",)
    lines.extend(list(details[:6]) + [""] * max(0, 6 - len(details)))
    lines.append(status)
    rendered = "\n".join(progress_text(line)[:width] for line in lines[:max(1, height - 1)])
    return rendered, regions if width >= 30 and height >= 12 else [], page


def choose_toolbox(title: str, rows: list[ToolboxRow], view: ToolboxView,
                   terminal: DashboardTerminal, status: str = "") -> str:
    if not any(row.key == view.focused and not row.action for row in rows):
        view.focused = None
    last = None
    while True:
        size = shutil.get_terminal_size((100, 32))
        rendered, regions, page = render_toolbox(title, rows, view, size.columns, size.lines, status)
        if rendered != last:
            sys.stdout.write(ANSI_CLEAR + tui_newlines(rendered))
            sys.stdout.flush()
            last = rendered
        event = terminal.read_event(text_mode=view.draft is not None)
        if isinstance(event, DashboardMouseEvent) and shutil.get_terminal_size((100, 32)) != size:
            continue
        action = toolbox_event(view, rows, event, regions, page)
        if action:
            return action


def action_row(key: str, label: str) -> ToolboxRow:
    return ToolboxRow(key, label, action=True)


def show_toolbox_text(title: str, lines: list[str], terminal: DashboardTerminal) -> None:
    view = ToolboxView()
    while True:
        rows = [action_row("back", "Back")]
        width = max(20, shutil.get_terminal_size((100, 32)).columns - 4)
        wrapped = [part for line in lines if view.search.casefold() in line.casefold()
                   for part in (textwrap.wrap(progress_text(line), width=width, replace_whitespace=False) or [""])]
        rows += [ToolboxRow(str(index), line) for index, line in enumerate(wrapped)]
        if choose_toolbox(title, rows, view, terminal) == "back":
            return


def test_details(test: dict) -> tuple[str, ...]:
    properties = test.get("properties", {})
    return (test["name"], "Labels: " + ", ".join(properties.get("LABELS", [])),
            "Command: " + format_command(test.get("command", [])),
            f"Working directory: {properties.get('WORKING_DIRECTORY', '(CTest default)')} | Timeout: {properties.get('TIMEOUT', '(CTest default)')}")


def run_test_explorer(state: DashboardState, terminal: DashboardTerminal) -> None:
    view, label, build_first = ToolboxView(), "All", True
    inventory: list[dict] = []
    message = ""
    reload_inventory = True
    while True:
        if reload_inventory:
            try:
                inventory = read_test_inventory(dashboard_settings(state))
                message = f"{len(inventory)} tests. Selecting a row only displays details."
            except (BuildError, OSError) as error:
                inventory, message = [], str(error)
            reload_inventory = False
        labels = ["All", *sorted({item for test in inventory for item in test.get("properties", {}).get("LABELS", []) if item != "IllumoWorkspace"})]
        if label not in labels:
            label = "All"
        matching = matching_tests(inventory, view.search, label)
        rows = [action_row("back", "Back"), action_row("refresh", "Refresh Inventory (configure and build discovery)"),
                action_row("filter", f"Project label: {label}"),
                action_row("mode", "Execution: Prepare and run (default)" if build_first else "Execution: Run Existing"),
                action_row("selected", "Run Selected"), action_row("matching", f"Run Matching ({len(matching)})"),
                action_row("failed", "Rerun Failed"), action_row("details", "Full selected test details")]
        history = run_history()
        identity = build_identity(dashboard_settings(state))
        latest = next((run for run in history if run.get("identity") == identity and run.get("action") == "test"), {})
        outcomes = {test["name"]: test for test in latest.get("tests", [])}
        for test in matching:
            result = outcomes.get(test["name"], {})
            duration = f"{result['duration']:.3f}s" if result.get("duration") is not None else "duration unavailable"
            suffix = f" [{result['status']}, {duration}]" if result else ""
            rows.append(ToolboxRow(test["name"], test["name"] + suffix, test_details(test)))
        action = choose_toolbox("Test explorer", rows, view, terminal, message)
        try:
            if action == "back":
                return
            if action == "filter":
                label = labels[(labels.index(label) + 1) % len(labels)]
            elif action == "mode":
                build_first = not build_first
            elif action == "refresh":
                execute_toolbox_run(state, terminal, [], True, refresh=True)
                reload_inventory = True
            elif action == "details":
                test = next((test for test in matching if test["name"] == view.focused), None)
                if test is None:
                    raise BuildError("Select a visible test first.")
                show_toolbox_text("Test details", list(test_details(test)), terminal)
            elif action in ("selected", "matching", "failed"):
                names = ([view.focused] if any(test["name"] == view.focused for test in matching) else []) if action == "selected" else [test["name"] for test in matching]
                if action == "failed":
                    names = failed_test_names(history, identity, inventory)
                execute_toolbox_run(state, terminal, names, build_first)
                message = state.status
                reload_inventory = True
        except (BuildError, OSError) as error:
            message = str(error)


def parse_diagnostics(lines: list[str], working_directory: Path | None,
                      commands: list[dict] | None = None) -> list[dict]:
    diagnostics = []
    patterns = (
        re.compile(r"^(?P<path>.+?)\((?P<line>\d+)(?:,(?P<column>\d+))?\)\s*:\s*(?P<severity>(?:fatal )?error|warning)\b[: ]*(?P<message>.*)", re.I),
        re.compile(r"^(?P<path>.+?):(?P<line>\d+)(?::(?P<column>\d+))?:\s*(?P<severity>(?:fatal )?error|warning)\s*:\s*(?P<message>.*)", re.I),
        re.compile(r"^CMake (?P<severity>Error|Warning)(?: \([^)]*\))? at (?P<path>.+?):(?P<line>\d+)(?: \([^)]*\))?:\s*(?P<message>.*)", re.I),
    )
    for index, raw in enumerate(lines):
        line = progress_text(raw).strip()
        for command in commands or []:
            if line == "> " + format_command(command["command"]):
                working_directory = Path(command["working_directory"])
        line = re.sub(r"^\d+>", "", line)
        match = next((match for pattern in patterns if (match := pattern.match(line))), None)
        if match:
            item = match.groupdict()
            path = Path(item["path"].strip())
            item["source"] = str(path if path.is_absolute() else working_directory / path) if path.is_absolute() or working_directory else None
            item["line"] = int(item["line"])
            item["severity"] = "error" if "error" in item["severity"].lower() else "warning"
        else:
            severity = re.search(r"\b(error|warning)\b", line, re.I)
            if not severity:
                continue
            item = {"source": None, "line": None, "severity": severity.group(1).lower(), "message": line}
        diagnostics.append({**item, "log_line": index, "raw": line})
    return diagnostics


def diagnostic_preview(diagnostic: dict, lines: list[str]) -> list[str]:
    index = diagnostic["log_line"]
    result = ["Raw log context:", *[f"{n + 1}: {lines[n]}" for n in range(max(0, index - 4), min(len(lines), index + 7))],
              "", "CURRENT SOURCE (read-only; may have changed since this run)"]
    path, line = diagnostic.get("source"), diagnostic.get("line")
    if not path or not line or line < 1:
        return result + ["Source location unavailable or invalid; no replacement was guessed."]
    try:
        source = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        return result + [f"Source unavailable: {path}: {error}"]
    if line > len(source):
        return result + [f"Invalid location: {path}:{line} (current file has {len(source)} lines)."]
    return result + [f"{path}:{line}", *[f"{'>' if n + 1 == line else ' '} {n + 1}: {source[n]}" for n in range(max(0, line - 6), min(len(source), line + 5))]]


def browse_run_diagnostics(run: dict, terminal: DashboardTerminal) -> None:
    lines = Path(run["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
    cwd = Path(run["working_directory"]) if run.get("working_directory") else None
    diagnostics = parse_diagnostics(lines, cwd, run.get("commands"))
    view, severity = ToolboxView(), "all"
    while True:
        rows = [action_row("back", "Back"), action_row("filter", "Diagnostics: " + severity),
                action_row("preview", "Inspect selected diagnostic / current source"), action_row("raw", "Browse raw log")]
        rows += [ToolboxRow(str(index), item["raw"],
                            ("Log context: " + (lines[item["log_line"] - 1] if item["log_line"] else "(start of log)"),
                             item["raw"], lines[item["log_line"] + 1] if item["log_line"] + 1 < len(lines) else "(end of log)",
                             "CURRENT SOURCE (read-only; may have changed)",
                             str(item.get("source") or "Source location unavailable"),
                             next((line for line in diagnostic_preview(item, lines) if line.startswith("> ")),
                                  diagnostic_preview(item, lines)[-1])))
                 for index, item in enumerate(diagnostics)
                 if (severity == "all" or item["severity"] == severity) and view.search.casefold() in item["raw"].casefold()]
        action = choose_toolbox("Diagnostics: " + Path(run["log"]).name, rows, view, terminal,
                                "Legacy log: command directory unknown" if not cwd else str(cwd))
        if action == "back":
            return
        if action == "filter":
            filters = ("all", "error", "warning")
            severity = filters[(filters.index(severity) + 1) % len(filters)]
        elif action == "raw":
            show_toolbox_text("Raw log (authoritative)", lines, terminal)
        elif action == "preview" and view.focused and view.focused.isdigit():
            show_toolbox_text("Diagnostic and CURRENT SOURCE", diagnostic_preview(diagnostics[int(view.focused)], lines), terminal)


def run_diagnostic_browser(terminal: DashboardTerminal) -> None:
    view = ToolboxView()
    while True:
        runs = run_history()
        rows = [action_row("back", "Back"), action_row("open", "Browse selected run"), action_row("refresh", "Refresh runs")]
        rows += [ToolboxRow(run["log"], f"{Path(run['log']).name} | {run.get('status', 'legacy log')}",
                            (str(run.get("identity", "No trusted metadata")),
                             "Command: " + format_command(run.get("command", [])),
                             "Working directory: " + run.get("working_directory", "unknown")))
                 for run in runs if view.search.casefold() in (run["log"] + str(run.get("identity", ""))).casefold()]
        action = choose_toolbox("Recorded runs", rows, view, terminal)
        if action == "back":
            return
        if action == "open":
            run = next((run for run in runs if run["log"] == view.focused), None)
            if run:
                browse_run_diagnostics(run, terminal)


def run_profile_picker(state: DashboardState, terminal: DashboardTerminal) -> None:
    view, message = ToolboxView(), "Switching profiles clears session overrides. Saving is explicit."
    while True:
        profiles = load_profiles(state.profiles_file, allow_missing=True)
        rows = [action_row("back", "Back"), action_row("apply", "Apply selected profile"),
                action_row("save", "Save current settings (use / to enter the saved name)"),
                action_row("effective", "Inspect all current effective settings")]
        for name, settings in profiles.items():
            if view.search.casefold() not in name.casefold():
                continue
            preview = DashboardState()
            apply_dashboard_profile(preview, name, settings)
            effective = dashboard_settings(preview)
            kind = "built-in" if name in BUILTIN_PROFILES else "saved"
            rows.append(ToolboxRow(name, f"{name} ({kind})", (
                f"Config: {effective['config']} | Directory: {effective['build_dir']}",
                f"Generator: {effective.get('generator', 'CMake default')} | Architecture: {effective.get('architecture', 'default')}",
                f"Tests: {not effective['no_tests']} | Docs: {not effective['no_docs']} | Tidy: {not effective['no_tidy']} | Tracy: {effective['tracy']} | WASM: {not effective['no_wasm']}",
                f"Parallel: {dashboard_parallel_label(preview)} | CMake args: {effective.get('cmake_arg', [])}")))
        action = choose_toolbox("Build profiles", rows, view, terminal, message)
        try:
            if action == "back":
                return
            if action == "apply":
                if view.focused not in profiles:
                    raise BuildError("Select a profile first.")
                apply_dashboard_profile(state, view.focused, profiles[view.focused])
                message = f"Applied {view.focused}; session overrides cleared."
            elif action == "effective":
                show_toolbox_text("Effective profile settings", json.dumps(dashboard_settings(state), indent=2).splitlines(), terminal)
            elif action == "save":
                name = view.search.strip()
                if not name:
                    raise BuildError("Press /, type a profile name, and Enter; then choose Save current settings.")
                parsed = create_parser().parse_args(["profile-save", name, "--profiles-file", str(state.profiles_file), *profile_arguments(dashboard_settings(state))])
                with contextlib.redirect_stdout(io.StringIO()):
                    run_profile_save(parsed)
                message = f"Saved current settings as {name}."
        except BuildError as error:
            message = str(error)


def available_artifacts(settings: dict, history: list[dict]) -> list[tuple[str, Path]]:
    identity = build_identity(settings)
    latest = next((run for run in history if run.get("identity") == identity), None)
    artifacts = [("Selected build directory", Path(identity["build_directory"])),
                 ("Coverage HTML (coverage build)", resolve_build_directory(DEFAULT_COVERAGE_DIRECTORY) / "coverage-html" / "index.html"),
                 ("Documentation", REPOSITORY_ROOT / "docs" / "output" / "illumo.pdf"),
                 ("Architecture map", REPOSITORY_ROOT / "docs" / "output" / "architecture-map.pdf")]
    if latest:
        artifacts.insert(1, ("Latest applicable log", Path(latest["log"])))
    return [(label, path) for label, path in artifacts if path.exists()]


def open_artifact(path: Path) -> None:
    if not path.exists():
        raise BuildError(f"Artifact no longer exists: {path}")
    if os.name != "nt":
        raise BuildError("Artifact shortcuts currently require Windows.")
    os.startfile(str(path.resolve()))


def run_artifact_picker(state: DashboardState, terminal: DashboardTerminal) -> None:
    view, message = ToolboxView(), "Only existing artifacts are listed. Opening never builds."
    while True:
        artifacts = available_artifacts(dashboard_settings(state), run_history())
        rows = [action_row("back", "Back"), action_row("open", "Open selected artifact")]
        rows += [ToolboxRow(str(path), label, (str(path),)) for label, path in artifacts
                 if view.search.casefold() in (label + str(path)).casefold()]
        action = choose_toolbox("Artifacts", rows, view, terminal, message)
        if action == "back":
            return
        if action == "open" and any(str(path) == view.focused for _, path in artifacts):
            try:
                open_artifact(Path(view.focused))
                message = "Opened with the Windows default handler."
            except (OSError, BuildError) as error:
                message = str(error)


def run_development_tools(state: DashboardState, terminal: DashboardTerminal) -> None:
    view, message = ToolboxView(), ""
    rows = [action_row("back", "Back to dashboard"), action_row("tests", "Test explorer"),
            action_row("diagnostics", "Diagnostic browser"), action_row("profiles", "Profile picker"),
            action_row("artifacts", "Artifact shortcuts"), action_row("stats", "Source file statistics")]
    while True:
        action = choose_toolbox("Development Tools", rows, view, terminal, message)
        try:
            if action == "back":
                return
            if action == "tests":
                run_test_explorer(state, terminal)
            elif action == "diagnostics":
                run_diagnostic_browser(terminal)
            elif action == "profiles":
                run_profile_picker(state, terminal)
            elif action == "artifacts":
                run_artifact_picker(state, terminal)
            elif action == "stats":
                execute_dashboard_action(state, "file_stats", terminal)
        except (OSError, BuildError) as error:
            message = str(error)


def run_dashboard() -> int:
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print(
            "error: the interactive build console requires a terminal; "
            "use an explicit build.py subcommand instead.",
            file=sys.stderr,
        )
        return 2

    state = DashboardState()
    terminal = DashboardTerminal()
    try:
        terminal.enter()
        last_rendered: str | None = None
        while True:
            terminal_size = dashboard_terminal_size()
            regions: list[DashboardHitRegion] = []
            rendered = render_dashboard(
                state, terminal_size.columns, hit_regions=regions,
                mouse_enabled=terminal.windows_input is not None,
            )
            # Wrapped or vertically clipped output cannot be hit-tested safely.
            rendered_rows = len(rendered.splitlines())
            if terminal_size.columns < 56 or rendered_rows > terminal_size.lines:
                regions.clear()
            if rendered != last_rendered:
                sys.stdout.write(ANSI_CLEAR + tui_newlines(rendered))
                sys.stdout.flush()
                last_rendered = rendered
            event = terminal.read_event()
            if isinstance(event, DashboardMouseEvent):
                if dashboard_terminal_size() != terminal_size:
                    continue
                key = dashboard_mouse_key(state, event, regions)
            else:
                key = event
            if key == "resize":
                last_rendered = None
            if key == "quit":
                return 0
            if key == "up":
                state.selected = (state.selected - 1) % len(DASHBOARD_ITEMS)
            elif key == "down":
                state.selected = (state.selected + 1) % len(DASHBOARD_ITEMS)
            elif key in ("left", "right"):
                if DASHBOARD_ITEMS[state.selected][0] == "setting":
                    adjust_dashboard_setting(
                        state, -1 if key == "left" else 1
                    )
            elif key == "enter":
                kind, _label, action = DASHBOARD_ITEMS[state.selected]
                if kind == "setting":
                    adjust_dashboard_setting(state, 1)
                elif action == "quit":
                    return 0
                else:
                    execute_dashboard_action(state, action, terminal)
                    last_rendered = None
    finally:
        terminal.leave()


class CommandRunner:
    """Print and execute external commands from a predictable directory."""

    def __init__(self, dry_run: bool) -> None:
        self.dry_run = dry_run

    def run(
        self,
        command: Sequence[str],
        working_directory: Path = REPOSITORY_ROOT,
    ) -> None:
        # CMake's build tool operates in its binary tree; make that directory
        # explicit so relative compiler locations have recorded context.
        if len(command) > 2 and Path(command[0]).stem.lower() == "cmake" and command[1] == "--build":
            directory = resolve_build_directory(Path(command[2]))
            if directory.is_dir():
                working_directory = directory
        context_path = os.environ.get("ILLUMO_RUN_CONTEXT")
        if context_path and not self.dry_run:
            with Path(context_path).open("a", encoding="utf-8") as context:
                context.write(json.dumps({"command": list(command), "working_directory": str(working_directory)}) + "\n")
        print(f"> {format_command(command)}", flush=True)
        if self.dry_run:
            return

        started = time.monotonic()
        try:
            result = subprocess.run(
                list(command),
                cwd=working_directory,
                check=False,
            )
        except OSError as error:
            raise BuildError(
                f"Could not start {format_command(command)}\n"
                f"Working directory: {working_directory}\n{error}"
            ) from error
        if result.returncode != 0:
            raise BuildError(
                f"Command failed with exit code {result.returncode} "
                f"after {time.monotonic() - started:.1f}s:\n"
                f"{format_command(command)}\n"
                f"Working directory: {working_directory}\n"
                "See the tool diagnostics above; subsequent steps were skipped.",
                result.returncode,
            )


def format_command(command: Sequence[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(list(command))
    return shlex.join(command)


def existing_tool(name: str, dry_run: bool) -> str:
    path = shutil.which(name)
    if path is not None:
        return path
    if dry_run:
        return name
    raise BuildError(
        f"Required tool '{name}' was not found on PATH. "
        "Install it or open a developer shell that provides it."
    )


def git_output(root: Path, arguments: Sequence[str]) -> str | None:
    git = shutil.which("git")
    if git is None:
        return None
    try:
        result = subprocess.run(
            [git, *arguments],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout


def is_excluded_repository_path(path: Path) -> bool:
    parts = path.parts
    if not parts:
        return True
    first = parts[0].lower()
    normalized = path.as_posix().lower()
    return (
        first == ".git"
        or first == "archive"
        or (len(parts) > 1 and first.startswith("build"))
        or normalized.startswith("illumo/thirdparty/")
        or normalized.startswith("docs/output/")
    )


def repository_files(root: Path) -> tuple[list[Path], str]:
    tracked = git_output(root, ("ls-files", "-z"))
    if tracked is not None:
        paths = [
            Path(value)
            for value in tracked.split("\0")
            if value and (root / value).is_file()
        ]
        return paths, "tracked"

    paths = []
    for directory, child_directories, file_names in os.walk(root):
        directory_path = Path(directory)
        relative_directory = directory_path.relative_to(root)
        child_directories[:] = [
            name
            for name in child_directories
            if not is_excluded_repository_path(
                relative_directory / name / "directory-entry"
            )
        ]
        for name in file_names:
            relative = relative_directory / name
            if not is_excluded_repository_path(relative):
                paths.append(relative)
    return paths, "discovered"


def repository_text_category(path: Path) -> str | None:
    if is_excluded_repository_path(path):
        return None

    suffix = path.suffix.lower()
    parts = {part.lower() for part in path.parts}
    c_family = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
    if suffix in c_family:
        if "tests" in parts or "testsupport" in parts:
            return "Tests C/C++"
        return "Production C/C++"
    if suffix in {".vert", ".frag", ".glsl"}:
        return "Shaders"
    if path.name == "CMakeLists.txt" or suffix in {
        ".cmake",
        ".py",
        ".ps1",
        ".sh",
        ".bat",
        ".cmd",
    }:
        return "Build and tooling"
    if suffix in {".md", ".rst", ".tex"}:
        return "Documentation"
    if suffix in {".json", ".toml", ".yaml", ".yml", ".txt", ".in"}:
        return "Configuration and data"
    return None


def worktree_statistics(root: Path) -> WorktreeStatistics | None:
    output = git_output(
        root, ("status", "--porcelain=v1", "--untracked-files=all")
    )
    if output is None:
        return None

    staged = 0
    modified = 0
    untracked = 0
    conflicted = 0
    conflict_codes = {"DD", "AU", "UD", "UA", "DU", "AA", "UU"}
    for line in output.splitlines():
        if len(line) < 2:
            continue
        code = line[:2]
        if code == "??":
            untracked += 1
        elif code in conflict_codes:
            conflicted += 1
        else:
            if code[0] != " ":
                staged += 1
            if code[1] != " ":
                modified += 1
    return WorktreeStatistics(staged, modified, untracked, conflicted)


def collect_repository_statistics(root: Path) -> RepositoryStatistics:
    files, files_source = repository_files(root)
    category_order = (
        "Production C/C++",
        "Tests C/C++",
        "Shaders",
        "Build and tooling",
        "Documentation",
        "Configuration and data",
    )
    counts = {
        label: {"files": 0, "physical_lines": 0, "loc": 0}
        for label in category_order
    }
    source_files: list[SourceFileStatistics] = []
    for relative in files:
        category = repository_text_category(relative)
        if category is None:
            continue
        try:
            lines = (root / relative).read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
        except OSError as error:
            raise BuildError(
                f"Could not read repository file {relative}: {error}"
            ) from error
        physical_lines = len(lines)
        loc = sum(1 for line in lines if line.strip())
        counts[category]["files"] += 1
        counts[category]["physical_lines"] += physical_lines
        counts[category]["loc"] += loc
        source_files.append(
            SourceFileStatistics(
                path=relative,
                category=category,
                physical_lines=physical_lines,
                loc=loc,
            )
        )

    categories = tuple(
        LineStatistics(label, **counts[label]) for label in category_order
    )
    branch_output = git_output(root, ("rev-parse", "--abbrev-ref", "HEAD"))
    commit_output = git_output(root, ("rev-parse", "--short=10", "HEAD"))
    subject_output = git_output(root, ("log", "-1", "--format=%s"))
    workspace = discover_workspace_projects(root)
    return RepositoryStatistics(
        root=root,
        branch=branch_output.strip() if branch_output else None,
        commit=commit_output.strip() if commit_output else None,
        subject=subject_output.strip() if subject_output else None,
        worktree=worktree_statistics(root),
        repository_files=len(files),
        repository_files_source=files_source,
        categories=categories,
        projects=workspace.projects,
        files=tuple(source_files),
    )


def repository_statistics_json(
    statistics: RepositoryStatistics, include_files: bool = False
) -> str:
    worktree = None
    if statistics.worktree is not None:
        worktree = {
            "staged": statistics.worktree.staged,
            "modified": statistics.worktree.modified,
            "untracked": statistics.worktree.untracked,
            "conflicted": statistics.worktree.conflicted,
        }
    first_party: dict[str, object] = {
        "files": statistics.first_party_files,
        "loc": statistics.first_party_loc,
        "physical_lines": statistics.first_party_physical_lines,
        "categories": [
            {
                "name": category.label,
                "files": category.files,
                "loc": category.loc,
                "physical_lines": category.physical_lines,
            }
            for category in statistics.categories
        ],
    }
    if include_files and statistics.files:
        first_party["source_files"] = [
            {
                "path": item.path.as_posix(),
                "category": item.category,
                "loc": item.loc,
                "physical_lines": item.physical_lines,
                "blank_lines": item.blank_lines,
            }
            for item in statistics.files
        ]
    payload = {
        "root": str(statistics.root),
        "git": {
            "branch": statistics.branch,
            "commit": statistics.commit,
            "subject": statistics.subject,
            "worktree": worktree,
        },
        "repository_files": {
            "count": statistics.repository_files,
            "source": statistics.repository_files_source,
        },
        "first_party": first_party,
        "projects": [
            {
                "name": project.name,
                "directory": str(project.directory),
                "applications": list(project.applications),
                "test_runners": list(project.test_runners),
                "discovery_targets": list(project.discovery_targets),
                "smoke_targets": list(project.smoke_targets),
            }
            for project in statistics.projects
        ],
    }
    return json.dumps(payload, indent=2)


def print_repository_statistics(statistics: RepositoryStatistics) -> None:
    print("ILLUMO REPOSITORY STATISTICS")
    print(f"Root: {statistics.root}")
    if statistics.commit is None:
        print("Git: unavailable")
    else:
        branch = statistics.branch or "unknown"
        if branch == "HEAD":
            branch = "detached HEAD"
        subject = f" - {statistics.subject}" if statistics.subject else ""
        print(f"Git: {branch} @ {statistics.commit}{subject}")

    if statistics.worktree is None:
        print("Working tree: unavailable")
    elif statistics.worktree == WorktreeStatistics():
        print("Working tree: clean")
    else:
        print(
            "Working tree: "
            f"{statistics.worktree.staged} staged, "
            f"{statistics.worktree.modified} modified, "
            f"{statistics.worktree.untracked} untracked, "
            f"{statistics.worktree.conflicted} conflicted"
        )

    if statistics.projects:
        print(f"Discovered projects ({len(statistics.projects)}):")
        for project in statistics.projects:
            details: list[str] = []
            if project.applications:
                details.append(f"apps: {', '.join(project.applications)}")
            if project.test_runners:
                details.append(f"tests: {', '.join(project.test_runners)}")
            if project.smoke_targets:
                details.append(f"smoke: {', '.join(project.smoke_targets)}")
            detail_str = f" ({'; '.join(details)})" if details else ""
            print(f"  {project.name:<24}{detail_str}")

    print(
        f"Repository files ({statistics.repository_files_source}): "
        f"{statistics.repository_files:,}"
    )
    print(
        "First-party text: "
        f"{statistics.first_party_files:,} files, "
        f"{statistics.first_party_loc:,} LOC, "
        f"{statistics.first_party_physical_lines:,} physical lines"
    )
    for category in statistics.categories:
        print(
            f"  {category.label:<24} "
            f"{category.files:>4,} files  "
            f"{category.loc:>8,} LOC  "
            f"{category.physical_lines:>8,} physical"
        )
    source = (
        "tracked files"
        if statistics.repository_files_source == "tracked"
        else "discovered files"
    )
    print(
        f"Scope: current contents of {source}; excludes build directories, archive, "
        "Illumo/thirdparty, docs/output, binary assets, and blank lines from LOC."
    )


def resolve_category_filter(
    category: str, include_tests: bool = False
) -> set[str]:
    cat_lower = category.lower().strip()
    if include_tests:
        return {"Production C/C++", "Tests C/C++"}
    if cat_lower in ("production", "prod", "production c/c++"):
        return {"Production C/C++"}
    if cat_lower in ("tests", "test", "tests c/c++"):
        return {"Tests C/C++"}
    if cat_lower in ("cpp", "c++", "source", "sources"):
        return {"Production C/C++", "Tests C/C++"}
    if cat_lower in ("shaders", "shader"):
        return {"Shaders"}
    if cat_lower in ("build", "tooling", "build and tooling"):
        return {"Build and tooling"}
    if cat_lower in ("docs", "doc", "documentation"):
        return {"Documentation"}
    if cat_lower in ("config", "data", "configuration and data"):
        return {"Configuration and data"}
    if cat_lower in ("all", "*"):
        return {
            "Production C/C++",
            "Tests C/C++",
            "Shaders",
            "Build and tooling",
            "Documentation",
            "Configuration and data",
        }
    return {category}


def filter_and_sort_source_files(
    files: Sequence[SourceFileStatistics],
    category: str = "production",
    include_tests: bool = False,
    sort_by: str = "loc",
    descending: bool = True,
    min_loc: int = 0,
    limit: int | None = None,
    project: str | None = None,
) -> list[SourceFileStatistics]:
    allowed_categories = resolve_category_filter(category, include_tests)
    filtered = [
        item
        for item in files
        if item.category in allowed_categories and item.loc >= min_loc
    ]
    if project:
        proj_lower = project.lower()
        filtered = [
            item
            for item in filtered
            if item.path.parts and item.path.parts[0].lower() == proj_lower
        ]

    if sort_by in ("lines", "physical"):
        filtered.sort(
            key=lambda item: (
                item.physical_lines,
                item.loc,
                item.path.as_posix(),
            ),
            reverse=descending,
        )
    elif sort_by in ("name", "path"):
        filtered.sort(
            key=lambda item: item.path.as_posix().lower(),
            reverse=not descending,
        )
    else:  # default: "loc"
        filtered.sort(
            key=lambda item: (
                item.loc,
                item.physical_lines,
                item.path.as_posix(),
            ),
            reverse=descending,
        )

    if limit is not None and limit > 0:
        filtered = filtered[:limit]
    return filtered


def source_file_statistics_json(
    files: Sequence[SourceFileStatistics],
    total_matching: int,
    total_matching_loc: int,
    total_matching_physical: int,
    scope_label: str,
    sort_label: str,
) -> str:
    payload = {
        "scope": scope_label,
        "sorted_by": sort_label,
        "total_files": total_matching,
        "total_loc": total_matching_loc,
        "total_physical_lines": total_matching_physical,
        "files_count": len(files),
        "files": [
            {
                "rank": index,
                "path": item.path.as_posix(),
                "category": item.category,
                "loc": item.loc,
                "physical_lines": item.physical_lines,
                "blank_lines": item.blank_lines,
            }
            for index, item in enumerate(files, start=1)
        ],
    }
    return json.dumps(payload, indent=2)


def print_source_file_statistics(
    files: Sequence[SourceFileStatistics],
    total_matching: int,
    total_matching_loc: int,
    total_matching_physical: int,
    scope_label: str,
    sort_label: str,
    limit: int | None = None,
) -> None:
    print("ILLUMO FIRST-PARTY SOURCE FILE STATISTICS")
    print(f"Scope: {scope_label}")
    print(f"Sorted by: {sort_label}")
    print()
    if not files:
        print("  No source files matched the requested filters.")
        return

    print(
        f"  {'Rank':>4}  {'LOC':>7}  {'Physical':>8}  {'Category':<16}  Path"
    )
    print(
        f"  {'-' * 4}  {'-' * 7}  {'-' * 8}  {'-' * 16}  {'-' * 44}"
    )
    for index, item in enumerate(files, start=1):
        rel_str = item.path.as_posix()
        print(
            f"  {index:>4}  {item.loc:>7,}  {item.physical_lines:>8,}  "
            f"{item.category:<16}  {rel_str}"
        )
    print(
        f"  {'-' * 4}  {'-' * 7}  {'-' * 8}  {'-' * 16}  {'-' * 44}"
    )
    if limit is not None and limit > 0 and len(files) < total_matching:
        shown_loc = sum(item.loc for item in files)
        shown_phys = sum(item.physical_lines for item in files)
        print(
            f"  Shown: {len(files):,} of {total_matching:,} files "
            f"({shown_loc:,} LOC, {shown_phys:,} physical lines)"
        )
        print(
            f"  Total: {total_matching:,} files, {total_matching_loc:,} LOC, "
            f"{total_matching_physical:,} physical lines"
        )
    else:
        print(
            f"  Total: {total_matching:,} files, {total_matching_loc:,} LOC, "
            f"{total_matching_physical:,} physical lines"
        )
    print(
        "Scope: current contents of tracked files; excludes build directories, "
        "archive, Illumo/thirdparty, docs/output, binary assets, and blank lines from LOC."
    )


def resolve_build_directory(value: Path) -> Path:
    if value.is_absolute():
        return value.resolve()
    return (REPOSITORY_ROOT / value).resolve()


def load_profiles(
    path: Path, *, include_builtin: bool = True, allow_missing: bool = False,
) -> dict[str, dict]:
    profiles = (
        {name: dict(settings) for name, settings in BUILTIN_PROFILES.items()}
        if include_builtin else {}
    )
    path = resolve_build_directory(path)
    if not path.exists() and (allow_missing or path == DEFAULT_PROFILES_FILE.resolve()):
        return profiles
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BuildError(f"Could not read profiles from '{path}': {error}") from error
    if (
        not isinstance(data, dict)
        or set(data) != {"version", "profiles"}
        or type(data["version"]) is not int
        or data["version"] != 1
        or not isinstance(data["profiles"], dict)
    ):
        raise BuildError(
            f"Invalid profiles file '{path}': expected version 1 and a profiles object"
        )
    for name, settings in data["profiles"].items():
        validate_profile(name, settings)
        profiles[name] = settings
    return profiles


def validate_profile(name: str, settings: object) -> None:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name):
        raise BuildError(f"Invalid profile name '{name}'")
    allowed = set(PROFILE_STRINGS + PROFILE_FLAGS + ("parallel", "cmake_arg"))
    if not isinstance(settings, dict) or set(settings) - allowed:
        raise BuildError(f"Profile '{name}' contains unsupported settings")
    for key, value in settings.items():
        valid = True
        if key in PROFILE_STRINGS:
            valid = isinstance(value, str) and bool(value.strip()) and "\x00" not in value
        elif key in PROFILE_FLAGS:
            valid = type(value) is bool
        elif key == "parallel":
            valid = value is None or (type(value) is int and value >= 0)
        elif key == "cmake_arg":
            valid = isinstance(value, list) and all(
                isinstance(item, str) and "\x00" not in item for item in value
            )
        if not valid:
            raise BuildError(f"Profile '{name}' has an invalid value for '{key}'")
    if "config" in settings and settings["config"] not in (
        "Debug", "Release", "RelWithDebInfo", "MinSizeRel"
    ):
        raise BuildError(f"Profile '{name}' has an unsupported configuration")


def profile_arguments(settings: dict) -> list[str]:
    result: list[str] = []
    for key, value in settings.items():
        option = "--" + key.replace("_", "-")
        if key in PROFILE_STRINGS:
            result.append(f"{option}={value}")
        elif key in PROFILE_FLAGS and value:
            result.append(option)
        elif key == "no_wasm" and not WASM_HOST_SUPPORTED:
            # Only this flag's default depends on the host; keep an explicit
            # opt-in on hosts where it is off.
            result.append("--wasm")
        elif key == "parallel" and value is not None:
            result.append(f"{option}={'auto' if value == 0 else value}")
        elif key == "cmake_arg":
            result.extend(f"--cmake-arg={item}" for item in value)
    return result


def run_profiles(arguments: argparse.Namespace) -> None:
    profiles = load_profiles(arguments.profiles_file)
    if arguments.json:
        print(json.dumps({"version": 1, "profiles": profiles}, indent=2))
    else:
        for name, settings in profiles.items():
            print(f"{name}: {format_command(profile_arguments(settings))}")


def run_profile_save(arguments: argparse.Namespace) -> None:
    settings = {
        key: getattr(arguments, key)
        for key in PROFILE_FLAGS + ("parallel", "cmake_arg")
    }
    settings.update({
        key: str(getattr(arguments, key))
        for key in PROFILE_STRINGS if getattr(arguments, key) is not None
    })
    validate_profile(arguments.name, settings)
    path = resolve_build_directory(arguments.profiles_file)
    # An explicitly selected new file is valid when saving, but not when loading.
    profiles = load_profiles(path, include_builtin=False) if path.exists() else {}
    profiles[arguments.name] = settings
    if arguments.dry_run:
        print(f"Would save profile '{arguments.name}' to {path}")
        print(json.dumps(settings, indent=2))
        return
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=path.name + ".", suffix=".tmp", delete=False,
        ) as output:
            temporary = output.name
            json.dump({"version": 1, "profiles": profiles}, output, indent=2)
            output.write("\n")
        os.replace(temporary, path)
    except OSError as error:
        raise BuildError(f"Could not save profiles to '{path}': {error}") from error
    finally:
        if temporary is not None and os.path.exists(temporary):
            os.unlink(temporary)
    print(f"Saved profile '{arguments.name}' to {path}")


def read_cmake_cache(build_directory: Path) -> dict[str, str]:
    cache = build_directory / "CMakeCache.txt"
    if not cache.is_file():
        return {}
    try:
        lines = cache.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        raise BuildError(f"Could not read {cache}: {error}") from error
    values: dict[str, str] = {}
    for line in lines:
        match = re.match(r"([^:#/][^:]*):[^=]+=(.*)$", line)
        if match:
            values[match[1]] = match[2]
    return values


@dataclass(frozen=True)
class AppOutput:
    """One staged application package beside the runtime."""

    name: str
    module: Path | None = None
    manifest: Path | None = None

    @property
    def staged(self) -> bool:
        return self.module is not None and self.manifest is not None

    @property
    def label(self) -> str:
        return APP_LABELS.get(self.name, self.name)


@dataclass(frozen=True)
class RuntimeOutputs:
    """What a build tree holds for the WASM runtime and its applications."""

    runtime: Path | None = None
    apps: tuple[AppOutput, ...] = ()
    retired: tuple[Path, ...] = ()

    def app(self, name: str) -> AppOutput | None:
        for app in self.apps:
            if app.name == name:
                return app
        return None

    def playable(self, name: str = DEFAULT_APP) -> bool:
        app = self.app(name)
        return self.runtime is not None and app is not None and app.staged

    @property
    def complete(self) -> bool:
        return self.runtime is not None and bool(self.apps) and all(
            app.staged for app in self.apps
        )

    def describe(self) -> str:
        if self.runtime is None:
            return "IllumoRuntime is not built"
        staged = [f"{app.name} ({format_size(app.module.stat().st_size)})"
                  for app in self.apps if app.staged]
        missing = [app.name for app in self.apps if not app.staged]
        text = f"{self.runtime} with {', '.join(staged) if staged else 'no apps'}"
        return text + (f"; missing {', '.join(missing)}" if missing else "")


def format_size(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    if size < 1024 * 1024:
        return f"{size / 1024:.1f} KB"
    return f"{size / (1024 * 1024):.1f} MB"


def format_duration(seconds: float) -> str:
    minutes, remainder = divmod(int(round(seconds)), 60)
    return f"{minutes}m {remainder:02d}s" if minutes else f"{seconds:.1f}s"


def runtime_outputs(
    build_directory: Path, configuration: str,
    apps: Sequence[AppPackage] | None = None,
) -> RuntimeOutputs:
    suffix = ".exe" if os.name == "nt" else ""
    output_directories = (
        build_directory / configuration,
        build_directory,
    )
    runtime: Path | None = None
    for directory in output_directories:
        candidate = directory / f"{WASM_RUNTIME_APPLICATION}{suffix}"
        if candidate.is_file():
            runtime = candidate
            break
    staged: list[AppOutput] = []
    for package in installed_apps() if apps is None else apps:
        module = manifest = None
        if runtime is not None:
            folder = runtime.parent / APPS_DIRECTORY / package.name
            if (folder / package.module).is_file():
                module = folder / package.module
            if (folder / "app.json").is_file():
                manifest = folder / "app.json"
        staged.append(AppOutput(package.name, module, manifest))
    retired: list[Path] = []
    for name in RETIRED_EXECUTABLES:
        for directory in (
            *output_directories,
            build_directory / name / configuration,
            build_directory / name,
        ):
            candidate = directory / f"{name}{suffix}"
            if candidate.is_file() and candidate not in retired:
                retired.append(candidate)
    for name in RETIRED_DIRECTORIES:
        for directory in output_directories:
            candidate = directory / name
            if candidate.is_dir() and candidate not in retired:
                retired.append(candidate)
    return RuntimeOutputs(runtime, tuple(staged), tuple(retired))


def retired_output_message(path: Path) -> str:
    return (
        f"{path} predates the WASM cutover and is no longer built or updated. "
        f"Applications now run as packages in {APPS_DIRECTORY}/ inside "
        f"{WASM_RUNTIME_APPLICATION}; delete it."
    )


def print_build_summary(
    arguments: argparse.Namespace, title: str, started: float,
) -> None:
    """Close a successful build with its outputs, so a missing app is obvious."""
    if arguments.dry_run:
        return
    ansi = sys.stdout.isatty()
    build_directory = resolve_build_directory(arguments.build_dir)
    rows: list[tuple[str, str, str]] = []
    outputs = runtime_outputs(build_directory, arguments.config)
    if not getattr(arguments, "no_wasm", not WASM_HOST_SUPPORTED):
        if outputs.runtime is not None:
            rows.append(("ok", "Runtime", str(outputs.runtime)))
            for app in outputs.apps:
                if app.staged:
                    rows.append(("ok", app.label,
                                 f"{APPS_DIRECTORY}/{app.name}/{app.module.name} "
                                 f"({format_size(app.module.stat().st_size)})"))
                else:
                    rows.append(("warn", app.label,
                                 f"{APPS_DIRECTORY}/{app.name} is not staged"))
        elif getattr(arguments, "target", None) in (None, WASM_RUNTIME_APPLICATION):
            rows.append(("warn", "Runtime", f"{WASM_RUNTIME_APPLICATION} was not produced"))
    else:
        rows.append(("info", "WASM runtime", "skipped (--no-wasm); no playable apps"))
    for retired in outputs.retired:
        rows.append(("warn", "Stale output", f"{retired} (pre-WASM; delete it)"))
    marks = {"ok": ("+", ANSI_GREEN), "warn": ("!", ANSI_YELLOW), "info": ("-", ANSI_DIM)}
    try:
        "✔⚠·".encode(sys.stdout.encoding or "utf-8")
        marks = {"ok": ("✔", ANSI_GREEN), "warn": ("⚠", ANSI_YELLOW), "info": ("·", ANSI_DIM)}
    except UnicodeEncodeError:
        pass
    heading = f"{title} in {format_duration(time.monotonic() - started)}"
    print()
    print(dashboard_style(heading, ANSI_BOLD + ANSI_GREEN, ansi))
    label_width = max((len(label) for _kind, label, _detail in rows), default=0)
    for kind, label, detail in rows:
        mark, style = marks[kind]
        print(f"  {dashboard_style(mark, style, ansi)} {label.ljust(label_width)}  {detail}")
    sys.stdout.flush()

def validate_build_settings(arguments: argparse.Namespace) -> None:
    directory = resolve_build_directory(arguments.build_dir)
    validate_workspace_build_directory(directory)
    cache = read_cmake_cache(directory)
    if (directory / "CMakeCache.txt").exists() and not cache.get("CMAKE_HOME_DIRECTORY"):
        raise BuildError(
            f"Build tree '{directory}' has an incomplete cache; choose another --build-dir."
        )
    for attribute, key in (
        ("generator", "CMAKE_GENERATOR"),
        ("architecture", "CMAKE_GENERATOR_PLATFORM"),
    ):
        requested = getattr(arguments, attribute, None)
        if (
            requested is not None and key in cache
            and requested != cache[key] and not arguments.fresh
        ):
            raise BuildError(
                f"Requested {attribute} '{requested}' conflicts with cached '{cache[key]}' "
                f"in '{directory}'. Choose another --build-dir or explicitly use --fresh."
            )


def run_doctor(arguments: argparse.Namespace) -> None:
    checks: list[dict[str, str]] = []

    def record(name: str, status: str, detail: str) -> None:
        checks.append({"name": name, "status": status, "detail": detail})

    directory = resolve_build_directory(arguments.build_dir)
    cache: dict[str, str] = {}
    try:
        validate_build_settings(arguments)
        cache = read_cmake_cache(directory)
        record("cache", "ok", str(directory) if cache else f"No cache yet: {directory}")
    except BuildError as error:
        record("cache", "error", str(error))
    definitions = configure_definitions(arguments)

    def enabled(key: str) -> bool:
        return cmake_truthy(definitions.get(key, ""))

    generator = arguments.generator or cache.get("CMAKE_GENERATOR", "")
    required = {"cmake": "cmake"}
    if enabled("BUILD_TESTING"):
        required["ctest"] = "ctest"
    if enabled("ILLUMO_ENABLE_CLANG_TIDY"):
        cached_tidy = cache.get("ILLUMO_CLANG_TIDY_EXECUTABLE", "clang-tidy")
        if not cached_tidy or cached_tidy.endswith("-NOTFOUND"):
            cached_tidy = "clang-tidy"
        required["clang-tidy"] = definitions.get(
            "ILLUMO_CLANG_TIDY_EXECUTABLE", cached_tidy,
        )
    if "Ninja" in generator:
        required["ninja"] = "ninja"
    for name, executable in required.items():
        path = shutil.which(executable)
        if not path:
            record(name, "error", "Not found on PATH; install it or use a developer shell.")
            continue
        if arguments.dry_run:
            record(name, "warning", f"Version probe skipped (--dry-run): {path}")
            continue
        try:
            result = subprocess.run(
                [path, "--version"], capture_output=True, text=True,
                errors="replace", timeout=10, check=False,
            )
            output = (result.stdout or result.stderr).strip()
            status = "ok" if result.returncode == 0 else "error"
            if name == "cmake":
                match = re.search(r"cmake version (\d+)\.(\d+)", output)
                minimum = (3, 24) if arguments.fresh else (3, 20)
                if not match or tuple(map(int, match.groups())) < minimum:
                    status = "error"
                    output += f"; CMake {minimum[0]}.{minimum[1]} or later required"
            record(name, status, f"{path}: {output}")
        except (OSError, subprocess.TimeoutExpired) as error:
            record(name, "error", f"{path}: {error}")
    if enabled("ILLUMO_BUILD_DOCUMENTATION"):
        for name, path in (
            ("PowerShell", shutil.which("pwsh") or shutil.which("powershell")),
            ("latexmk", shutil.which("latexmk")),
        ):
            record(name, "ok" if path else "warning", path or "Optional documentation tool missing")
    compiler = cache.get("CMAKE_CXX_COMPILER")
    compiler_found = bool(compiler and Path(compiler).is_file())
    record(
        "compiler", "ok" if compiler_found else "warning",
        compiler if compiler_found else
        "No existing compiler verified. CMake configure must resolve the compiler and SDK.",
    )
    wasm = enabled("ILLUMO_BUILD_WASM_RUNTIME")
    if wasm and not WASM_HOST_SUPPORTED:
        record("wasm toolchain", "error", "IllumoRuntime requires Windows x64; use --no-wasm.")
    elif wasm:
        tools = wasm_tools_directory(arguments.cmake_arg, cache)
        missing = missing_wasm_tools(tools)
        if missing:
            record("wasm toolchain", "error", wasm_toolchain_error(missing))
        else:
            record("wasm toolchain", "ok", f"{' + '.join(wasm_toolchain_packages())} in {tools}")
    elif WASM_HOST_SUPPORTED:
        record("wasm toolchain", "warning",
               "Disabled (--no-wasm): IllumoRuntime and its applications are not built.")
    else:
        record("wasm toolchain", "ok", "Not built on this host (the WASM runtime is Windows x64 only).")
    cached_wasm = cache.get("ILLUMO_BUILD_WASM_RUNTIME")
    if cached_wasm is not None and cmake_truthy(cached_wasm) != wasm:
        record(
            "wasm cache", "warning",
            f"Cached ILLUMO_BUILD_WASM_RUNTIME={cached_wasm}; the next configure "
            f"switches it {'ON' if wasm else 'OFF'}.",
        )
    if cache:
        outputs = runtime_outputs(directory, arguments.config)
        if wasm and outputs.complete:
            record("apps", "ok", outputs.describe())
        elif wasm:
            record("apps", "warning",
                   f"Not all built for {arguments.config} ({outputs.describe()}); "
                   "run 'python build.py build'.")
        for retired in outputs.retired:
            record("stale output", "warning", retired_output_message(retired))
    ok = not any(check["status"] == "error" for check in checks)
    if arguments.json:
        print(json.dumps({"ok": ok, "checks": checks}, indent=2))
    else:
        styles = {"ok": ANSI_GREEN, "warning": ANSI_YELLOW, "error": ANSI_RED}
        ansi = sys.stdout.isatty()
        for check in checks:
            status = dashboard_style(f"[{check['status']}]", styles.get(check["status"], ""), ansi)
            print(f"{status} {check['name']}: {check['detail']}")
    if not ok:
        raise BuildError("Build diagnostics found errors. No configuration or build was started.")


def cached_source_directory(build_directory: Path) -> Path | None:
    cache_file = build_directory / "CMakeCache.txt"
    if not cache_file.is_file():
        return None

    prefix = "CMAKE_HOME_DIRECTORY:INTERNAL="
    try:
        cache_lines = cache_file.read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()
    except OSError as error:
        raise BuildError(f"Could not read {cache_file}: {error}") from error

    for line in cache_lines:
        if line.startswith(prefix):
            return Path(line[len(prefix) :]).resolve()
    return None


def validate_workspace_build_directory(build_directory: Path) -> None:
    cached_source = cached_source_directory(build_directory)
    if cached_source is None:
        return

    expected_source = SOURCE_DIRECTORY.resolve()
    if os.path.normcase(str(cached_source)) == os.path.normcase(
        str(expected_source)
    ):
        return

    raise BuildError(
        f"Build tree '{build_directory}' belongs to source "
        f"'{cached_source}', but this orchestrator configures "
        f"'{expected_source}'. Choose a different --build-dir; the existing "
        "tree was left untouched."
    )


def add_common_build_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--profile", help="named build profile (CLI options override it)")
    parser.add_argument("--profiles-file", type=Path, default=DEFAULT_PROFILES_FILE,
                        help="profile JSON file (relative paths use the repository root)")
    parser.add_argument(
        "--config",
        choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"),
        default="Release",
        help="CMake build configuration (default: Release)",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIRECTORY,
        metavar="PATH",
        help=(
            "build tree, relative to the repository root by default "
            "(default: build-workspace)"
        ),
    )
    parser.add_argument("--generator", help="CMake generator passed with -G")
    parser.add_argument(
        "--architecture", help="generator architecture passed with -A"
    )
    parser.add_argument(
        "--parallel",
        nargs="?",
        const=0,
        type=positive_job_count,
        metavar="JOBS",
        help="build in parallel, optionally with a job limit",
    )
    parser.add_argument(
        "--tracy",
        action="store_true",
        help="enable Tracy instrumentation with ILLUMO_ENABLE_TRACY",
    )
    parser.add_argument("--no-tracy", dest="tracy", action="store_false")
    parser.add_argument("--tests", dest="no_tests", action="store_false")
    parser.add_argument("--docs", dest="no_docs", action="store_false")
    parser.add_argument("--tidy", dest="no_tidy", action="store_false")
    parser.add_argument(
        "--no-tests",
        "--no-testing",
        dest="no_tests",
        action="store_true",
        help="disable building and running tests with BUILD_TESTING=OFF",
    )
    parser.add_argument(
        "--no-docs",
        action="store_true",
        help="disable the optional IllumoDocs target for this build tree",
    )
    parser.add_argument(
        "--no-tidy",
        action="store_true",
        help="disable clang-tidy during compile with ILLUMO_ENABLE_CLANG_TIDY=OFF",
    )
    parser.add_argument(
        "--no-wasm",
        action="store_true",
        help=(
            "skip IllumoRuntime and the IllumoGame WASM package with "
            "ILLUMO_BUILD_WASM_RUNTIME=OFF (default: "
            + ("on" if WASM_HOST_SUPPORTED else "off")
            + " on this host)"
        ),
    )
    parser.add_argument("--wasm", dest="no_wasm", action="store_false")
    parser.add_argument(
        "--clean",
        "--clean-first",
        dest="clean",
        action="store_true",
        help="clean build targets first before building (--clean-first)",
    )
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="configure a fresh build tree without removing the directory (--fresh)",
    )
    parser.add_argument(
        "--cmake-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra configure argument; repeat and use --cmake-arg=-DNAME=VALUE",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="request verbose build output"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without executing them",
    )
    parser.set_defaults(
        tracy=False, no_tests=False, no_docs=False, no_tidy=False,
        no_wasm=not WASM_HOST_SUPPORTED,
    )


def create_parser(
    workspace: WorkspaceProjects | None = None,
) -> argparse.ArgumentParser:
    if workspace is None:
        workspace = discover_workspace_projects(REPOSITORY_ROOT)

    parser = argparse.ArgumentParser(
        description=(
            "Configure, build, test, run, lint, and measure the Illumo workspace. "
            "Running without a command opens the "
            "interactive build console in a terminal."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    toolbox_parser = subparsers.add_parser("_toolbox", help=argparse.SUPPRESS)
    toolbox_parser.add_argument("request", type=Path)

    common = argparse.ArgumentParser(add_help=False)
    add_common_build_arguments(common)

    profiles_parser = subparsers.add_parser("profiles", help="list built-in and saved profiles")
    profiles_parser.add_argument("--profiles-file", type=Path, default=DEFAULT_PROFILES_FILE)
    profiles_parser.add_argument("--json", action="store_true")
    save_parser = subparsers.add_parser("profile-save", parents=[common],
                                       help="save effective build settings as a named profile")
    save_parser.add_argument("name")
    doctor_parser = subparsers.add_parser("doctor", parents=[common],
                                         help="inspect tools and cache without configuring")
    doctor_parser.add_argument("--json", action="store_true")

    menu_parser = subparsers.add_parser(
        "menu", help="open the interactive terminal build console"
    )
    menu_parser.add_argument(
        "--snapshot",
        action="store_true",
        help=argparse.SUPPRESS,
    )

    subparsers.add_parser(
        "configure", parents=[common], help="configure the selected build tree"
    )
    subparsers.add_parser(
        "build",
        parents=[common],
        help="configure and build the selected configuration",
    ).add_argument(
        "--target", help="build a focused CMake target instead of the default"
    )

    test_parser = subparsers.add_parser(
        "test",
        parents=[common],
        help="build and run headless tests across all discovered projects",
    )
    test_mode = test_parser.add_mutually_exclusive_group()
    test_mode.add_argument(
        "--test",
        metavar="NAME",
        help="run one exact test from any discovered test runner",
    )
    test_mode.add_argument(
        "--list-tests",
        action="store_true",
        help="list exact case names across all discovered test runners",
    )

    app_choices = (
        workspace.applications
        if workspace.applications
        else ("IllumoRuntime", "IllEd")
    )
    default_app = workspace.primary_application

    run_parser = subparsers.add_parser(
        "run", parents=[common], help="build and launch an Illumo application"
    )
    run_parser.add_argument(
        "--app",
        choices=app_choices,
        default=default_app,
        help=f"application to launch (default: {default_app})",
    )
    run_parser.add_argument(
        "--target",
        dest="app",
        choices=app_choices,
        help="alias for --app",
    )
    run_parser.add_argument(
        "--no-build",
        action="store_true",
        help="launch the existing executable without configuring or building",
    )
    run_parser.add_argument(
        "app_arguments",
        nargs=argparse.REMAINDER,
        help="arguments after -- are passed to the application",
    )

    packages = app_names()
    play_parser = subparsers.add_parser(
        "play",
        parents=[common],
        help=f"build {WASM_RUNTIME_APPLICATION} and its applications, then run one",
    )
    play_parser.add_argument(
        "--app",
        dest="package",
        choices=packages,
        default=DEFAULT_APP if DEFAULT_APP in packages else packages[0],
        help=f"installed application to run (default: {DEFAULT_APP}; "
             f"{', '.join(packages)})",
    )
    play_parser.add_argument(
        "--no-build",
        action="store_true",
        help="play the existing build without configuring or building",
    )
    play_parser.add_argument(
        "app_arguments",
        nargs=argparse.REMAINDER,
        help=f"arguments after -- are passed to {WASM_RUNTIME_APPLICATION} "
             "(e.g. -- --open scene.ilsc, or -- --capture frame.png)",
    )
    play_parser.set_defaults(app=WASM_RUNTIME_APPLICATION)

    wasm_tools_parser = subparsers.add_parser(
        "wasm-tools",
        help="download and verify the pinned Wasmtime and WASI SDK (tools/bootstrap-wasm.ps1)",
    )
    wasm_tools_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without executing them",
    )

    coverage_parser = subparsers.add_parser(
        "coverage", help="configure and run the Clang/LLVM coverage target"
    )
    coverage_parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_COVERAGE_DIRECTORY,
        metavar="PATH",
        help="coverage build tree (default: build-workspace-coverage)",
    )
    coverage_parser.add_argument(
        "--parallel",
        nargs="?",
        const=0,
        type=positive_job_count,
        metavar="JOBS",
        help="build in parallel, optionally with a job limit",
    )
    coverage_parser.add_argument(
        "--cmake-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra configure argument; repeat and use --cmake-arg=-DNAME=VALUE",
    )
    coverage_parser.add_argument(
        "--verbose", action="store_true", help="request verbose build output"
    )
    coverage_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without executing them",
    )

    tidy_parser = subparsers.add_parser(
        "tidy", help="configure and run the workspace clang-tidy target"
    )
    tidy_parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_TIDY_DIRECTORY,
        metavar="PATH",
        help="clang-tidy build tree (default: build-workspace-tidy)",
    )
    tidy_parser.add_argument(
        "--parallel",
        nargs="?",
        const=0,
        type=positive_job_count,
        metavar="JOBS",
        help="configure/build in parallel, optionally with a job limit",
    )
    tidy_parser.add_argument(
        "--cmake-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra configure argument; repeat and use --cmake-arg=-DNAME=VALUE",
    )
    tidy_parser.add_argument(
        "--verbose", action="store_true", help="request verbose build output"
    )
    tidy_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without executing them",
    )

    docs_parser = subparsers.add_parser(
        "docs", help="build the two documentation PDFs through docs/build.ps1"
    )
    docs_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without executing them",
    )
    stats_parser = subparsers.add_parser(
        "stats", help="show Git state and first-party repository statistics"
    )
    stats_parser.add_argument(
        "--json",
        action="store_true",
        help="emit machine-readable JSON",
    )
    stats_parser.add_argument(
        "--files",
        "--by-file",
        dest="by_file",
        action="store_true",
        help="include per-file breakdown for first-party source files sorted largest to smallest",
    )
    stats_parser.add_argument(
        "-n",
        "--top",
        "--limit",
        dest="limit",
        type=positive_job_count,
        metavar="COUNT",
        help="limit per-file output to the top COUNT files",
    )
    stats_parser.add_argument(
        "--include-tests",
        action="store_true",
        help="include test files alongside production source files",
    )

    file_stats_parser = subparsers.add_parser(
        "file-stats",
        aliases=["source-stats"],
        help="show per-file statistics for first-party source files sorted largest to smallest",
    )
    file_stats_parser.add_argument(
        "-n",
        "--top",
        "--limit",
        dest="limit",
        type=positive_job_count,
        metavar="COUNT",
        help="limit output to the top COUNT largest files",
    )
    file_stats_parser.add_argument(
        "--min-loc",
        type=int,
        default=0,
        metavar="LOC",
        help="only include files with at least LOC nonblank lines",
    )
    file_stats_parser.add_argument(
        "--category",
        choices=["production", "tests", "cpp", "shaders", "all"],
        default="production",
        help="file category to analyze (default: %(default)s)",
    )
    file_stats_parser.add_argument(
        "--include-tests",
        action="store_true",
        help="include test files alongside production source files (shorthand for --category cpp)",
    )
    file_stats_parser.add_argument(
        "--sort",
        choices=["loc", "lines", "name"],
        default="loc",
        help="sort key: loc (nonblank lines), lines (physical lines), or name (default: %(default)s)",
    )
    file_stats_parser.add_argument(
        "--reverse",
        "--asc",
        dest="reverse",
        action="store_true",
        help="sort in ascending order (smallest to largest) instead of descending",
    )
    file_stats_parser.add_argument(
        "--project",
        metavar="NAME",
        help="filter files to a specific project (e.g., IllumoGame, IllEd, Illumo)",
    )
    file_stats_parser.add_argument(
        "--json",
        action="store_true",
        help="emit machine-readable JSON",
    )

    new_project_parser = subparsers.add_parser(
        "new-project",
        aliases=["create-project"],
        help="generate a new Illumo application project (Unreal style with engine framework, debug tools, and spinning-cube starter template)",
    )
    new_project_parser.add_argument(
        "destination",
        type=Path,
        help="path where the new project workspace will be created",
    )
    new_project_parser.add_argument(
        "-n",
        "--name",
        default="IllumoGame",
        help="name of the game application (default: %(default)s)",
    )
    new_project_parser.add_argument(
        "-t",
        "--template",
        default="spinning-cube",
        choices=["spinning-cube"],
        help="starter template to instantiate (default: %(default)s)",
    )
    new_project_parser.add_argument(
        "--no-debug-tools",
        action="store_true",
        help="do not include the IllEd debug editor in the generated workspace",
    )
    new_project_parser.add_argument(
        "--in-workspace",
        action="store_true",
        help="create as an application folder inside the current workspace",
    )
    new_project_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="overwrite or create within an existing non-empty directory",
    )
    new_project_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="enable detailed logging of files copied",
    )
    return parser


def normalize_arguments(arguments: Sequence[str]) -> list[str]:
    if not arguments:
        return ["build"]
    if arguments[0] in ("-h", "--help"):
        return list(arguments)
    if arguments[0].startswith("-"):
        return ["build", *arguments]
    return list(arguments)


def positive_job_count(value: str) -> int:
    if value == "auto":
        return 0
    try:
        count = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("job count must be an integer or 'auto'") from error
    if count < 1:
        raise argparse.ArgumentTypeError("job count must be at least 1")
    return count


def configure_command(arguments: argparse.Namespace, cmake: str) -> list[str]:
    build_directory = resolve_build_directory(arguments.build_dir)
    command = [
        cmake,
        "-S",
        str(SOURCE_DIRECTORY),
        "-B",
        str(build_directory),
    ]
    if getattr(arguments, "fresh", False):
        command.append("--fresh")
    if arguments.generator:
        command.extend(("-G", arguments.generator))
    if arguments.architecture:
        command.extend(("-A", arguments.architecture))

    testing_enabled = "OFF" if getattr(arguments, "no_tests", False) else "ON"
    docs_enabled = "OFF" if arguments.no_docs else "ON"
    tracy_enabled = "ON" if arguments.tracy else "OFF"
    tidy_enabled = "OFF" if getattr(arguments, "no_tidy", False) else "ON"
    # Always explicit: CMake's option() keeps a stale cached OFF, which
    # silently dropped IllumoRuntime and the game package from old trees.
    wasm_enabled = "OFF" if getattr(arguments, "no_wasm", not WASM_HOST_SUPPORTED) else "ON"
    command.extend(
        (
            f"-DCMAKE_BUILD_TYPE={arguments.config}",
            f"-DBUILD_TESTING={testing_enabled}",
            f"-DILLUMO_BUILD_DOCUMENTATION={docs_enabled}",
            f"-DILLUMO_ENABLE_TRACY={tracy_enabled}",
            "-DILLUMO_ENABLE_COVERAGE=OFF",
            f"-DILLUMO_ENABLE_CLANG_TIDY={tidy_enabled}",
            f"-DILLUMO_BUILD_WASM_RUNTIME={wasm_enabled}",
        )
    )
    command.extend(arguments.cmake_arg)
    return command


def cmake_truthy(value: str) -> bool:
    upper = value.upper()
    return (
        upper not in ("", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND")
        and not upper.endswith("-NOTFOUND")
    )


def configure_definitions(arguments: argparse.Namespace) -> dict[str, str]:
    """Effective -D values of the configure command, later ones winning."""
    definitions: dict[str, str] = {}
    for option in configure_command(arguments, "cmake"):
        match = re.fullmatch(r"-D([^:=]+)(?::[^=]+)?=(.*)", option)
        if match:
            definitions[match[1]] = match[2]
    return definitions


def wasm_requested(arguments: argparse.Namespace) -> bool:
    return cmake_truthy(configure_definitions(arguments).get("ILLUMO_BUILD_WASM_RUNTIME", ""))


def wasm_tools_directory(cmake_arguments: Sequence[str], cache: dict[str, str]) -> Path:
    configured = cache.get("ILLUMO_WASM_TOOLS")
    for item in cmake_arguments:
        match = re.fullmatch(r"-DILLUMO_WASM_TOOLS(?::[^=]+)?=(.*)", item)
        if match:
            configured = match[1]
    # configure runs from the repository root, so relative values resolve there.
    return resolve_build_directory(Path(configured)) if configured else DEFAULT_WASM_TOOLS_DIRECTORY


def wasm_toolchain_packages() -> tuple[str, str]:
    """Wasmtime C API and WASI SDK directory names, as pinned by CMake."""
    names = ["wasmtime-v48.0.2-x86_64-windows-c-api", "wasi-sdk-34.0-x86_64-windows"]
    try:
        text = WASM_CMAKE_MODULE.read_text(encoding="utf-8", errors="replace")
    except OSError:
        text = ""
    for index, variable in enumerate(("_wasmtime", "_wasi_sdk")):
        match = re.search(
            rf'set\(\s*{variable}\s+"\$\{{ILLUMO_WASM_TOOLS\}}/([^"]+)"\s*\)', text
        )
        if match:
            names[index] = match[1]
    return names[0], names[1]


def missing_wasm_tools(directory: Path) -> list[Path]:
    wasmtime, wasi_sdk = wasm_toolchain_packages()
    required = (
        directory / wasmtime / "include" / "wasmtime.h",
        directory / wasmtime / "lib" / "wasmtime.dll.lib",
        directory / wasmtime / "lib" / "wasmtime.dll",
        directory / wasi_sdk / "bin" / "clang++.exe",
    )
    return [path for path in required if not path.is_file()]


def wasm_toolchain_error(missing: list[Path]) -> str:
    wasmtime, wasi_sdk = wasm_toolchain_packages()
    return (
        f"The pinned WASM toolchain is incomplete; missing {missing[0]}"
        + (f" and {len(missing) - 1} more" if len(missing) > 1 else "")
        + f".\nIllumoGame builds as a WASM package ({wasmtime}, {wasi_sdk}). "
        "Run 'python build.py wasm-tools' to download and verify the pinned "
        "toolchain, or pass --no-wasm to build without IllumoRuntime and the game."
    )


def require_wasm_toolchain(arguments: argparse.Namespace, dry_run: bool) -> None:
    if not wasm_requested(arguments):
        return
    if not WASM_HOST_SUPPORTED:
        raise BuildError(
            "IllumoRuntime and the IllumoGame package require Windows x64; "
            "drop --wasm or pass --no-wasm on this host."
        )
    directory = resolve_build_directory(arguments.build_dir)
    missing = missing_wasm_tools(
        wasm_tools_directory(arguments.cmake_arg, read_cmake_cache(directory))
    )
    if not missing:
        return
    if dry_run:
        print(f"warning: {wasm_toolchain_error(missing)}", file=sys.stderr)
        return
    raise BuildError(wasm_toolchain_error(missing))


def build_command(
    arguments: argparse.Namespace,
    cmake: str,
    target: str | Sequence[str] | None = None,
) -> list[str]:
    build_directory = resolve_build_directory(arguments.build_dir)
    command = [
        cmake,
        "--build",
        str(build_directory),
        "--config",
        arguments.config,
    ]
    if getattr(arguments, "clean", False):
        command.append("--clean-first")
    selected_target = (
        target if target is not None else getattr(arguments, "target", None)
    )
    if isinstance(selected_target, str):
        command.extend(("--target", selected_target))
    elif selected_target:
        command.extend(("--target", *selected_target))
    if arguments.parallel is not None:
        command.append("--parallel")
        if arguments.parallel > 0:
            command.append(str(arguments.parallel))
    if arguments.verbose:
        command.append("--verbose")
    return command


def configure(arguments: argparse.Namespace, runner: CommandRunner) -> str:
    cmake = existing_tool("cmake", runner.dry_run)
    validate_build_settings(arguments)
    require_wasm_toolchain(arguments, runner.dry_run)
    runner.run(configure_command(arguments, cmake))
    return cmake


def executable_path(
    build_directory: Path,
    configuration: str,
    name: str,
    dry_run: bool,
    workspace: WorkspaceProjects | None = None,
) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    candidates: list[Path] = [
        build_directory / configuration / f"{name}{suffix}",
        build_directory / f"{name}{suffix}",
    ]
    if workspace is None:
        workspace = discover_workspace_projects(REPOSITORY_ROOT)
    for project in workspace.projects:
        candidates.append(
            build_directory / project.name / configuration / f"{name}{suffix}"
        )
        candidates.append(
            build_directory / project.name / f"{name}{suffix}"
        )
    if dry_run:
        return candidates[0]
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    target_filename = f"{name}{suffix}".lower()
    for root_dir, _subdirs, files in os.walk(build_directory):
        for file in files:
            if file.lower() == target_filename:
                found = Path(root_dir) / file
                if configuration.lower() in found.parts or len(candidates) <= 2:
                    return found

    rendered = " or ".join(str(candidate) for candidate in candidates)
    raise BuildError(f"Expected executable was not produced at {rendered}")


def run_configure(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    configure(arguments, runner)


def run_build(arguments: argparse.Namespace) -> None:
    started = time.monotonic()
    runner = CommandRunner(arguments.dry_run)
    cmake = configure(arguments, runner)
    runner.run(build_command(arguments, cmake))
    print_build_summary(arguments, "Build succeeded", started)


def run_ctest_case(
    arguments: argparse.Namespace, cmake: str, runner: CommandRunner, test: dict,
) -> bool:
    """Run one exact CTest case that is not owned by a discovered runner.

    Returns False when the case belongs to a discovered runner, which keeps
    its direct --run invocation.
    """
    build_directory = resolve_build_directory(arguments.build_dir)
    workspace = discover_workspace_projects(REPOSITORY_ROOT)
    target = ctest_executable_target(test, build_directory)
    if target in workspace.test_runners or target in workspace.smoke_targets:
        return False
    if target:
        runner.run(build_command(arguments, cmake, target))
    runner.run((
        existing_tool("ctest", runner.dry_run), "--test-dir", str(build_directory),
        "-C", arguments.config, "-R", f"^{re.escape(test['name'])}$",
        "--no-tests=error", "--output-on-failure",
    ))
    return True


def run_tests(arguments: argparse.Namespace) -> None:
    if getattr(arguments, "no_tests", False):
        raise BuildError(
            "Cannot run tests when testing is disabled via --no-tests."
        )
    started = time.monotonic()
    workspace = discover_workspace_projects(REPOSITORY_ROOT)
    runner = CommandRunner(arguments.dry_run)
    cmake = configure(arguments, runner)
    build_directory = resolve_build_directory(arguments.build_dir)

    if arguments.test:
        # Plain add_test() cases (the WASM runtime and package tests) share
        # name prefixes with discovered runners, so ask CTest first.
        if not runner.dry_run and arguments.test != PUBLIC_HEADER_SMOKE_TEST:
            listing = ctest_listing(existing_tool("ctest", False), build_directory, arguments.config)
            test = next((item for item in listing if item["name"] == arguments.test), None)
            if test is not None and run_ctest_case(arguments, cmake, runner, test):
                return
        target = workspace.resolve_test_target(arguments.test)
        runner.run(build_command(arguments, cmake, target))
        test_binary = executable_path(
            build_directory,
            arguments.config,
            target,
            runner.dry_run,
            workspace,
        )
        safe_case_name = "".join(
            character
            if character.isalnum() or character in (".", "-", "_")
            else "_"
            for character in arguments.test
        )
        case_directory = (
            build_directory / "Testing" / "Manual" / target / safe_case_name
        )
        if not runner.dry_run:
            case_directory.mkdir(parents=True, exist_ok=True)
        test_command = (str(test_binary),)
        if target not in workspace.smoke_targets:
            test_command = (str(test_binary), "--run", arguments.test)
        runner.run(test_command, case_directory)
        return

    if arguments.list_tests:
        for target in workspace.test_runners:
            runner.run(build_command(arguments, cmake, target))
            test_binary = executable_path(
                build_directory,
                arguments.config,
                target,
                runner.dry_run,
                workspace,
            )
            runner.run((str(test_binary), "--list"), test_binary.parent)
        for smoke_target in workspace.smoke_targets:
            runner.run(build_command(arguments, cmake, smoke_target))
            if smoke_target == "IllumoPublicHeaderSmoke":
                print(PUBLIC_HEADER_SMOKE_TEST, flush=True)
            else:
                print(smoke_target, flush=True)
        if not runner.dry_run:
            owned = {*workspace.test_runners, *workspace.smoke_targets}
            for test in ctest_listing(existing_tool("ctest", False), build_directory, arguments.config):
                if ctest_executable_target(test, build_directory) not in owned:
                    print(test["name"], flush=True)
        return

    build_test_executables(arguments, cmake, runner, workspace)

    ctest = existing_tool("ctest", runner.dry_run)
    runner.run(
        (
            ctest,
            "--test-dir",
            str(build_directory),
            "-C",
            arguments.config,
            "-L",
            "IllumoWorkspace",
            "--output-on-failure",
        )
    )
    print_build_summary(arguments, "Tests passed", started)


def run_application(arguments: argparse.Namespace) -> None:
    workspace = discover_workspace_projects(REPOSITORY_ROOT)
    runner = CommandRunner(arguments.dry_run)
    app_name = getattr(arguments, "app", None) or workspace.primary_application
    if app_name == WASM_RUNTIME_APPLICATION and not arguments.no_build and not wasm_requested(arguments):
        raise BuildError(
            f"{WASM_RUNTIME_APPLICATION} is only built with the WASM runtime enabled; "
            "drop --no-wasm (or choose another --app)."
        )
    if not arguments.no_build:
        cmake = configure(arguments, runner)
        runner.run(build_command(arguments, cmake, app_name))

    build_directory = resolve_build_directory(arguments.build_dir)
    if arguments.no_build:
        validate_workspace_build_directory(build_directory)
    app_arguments = list(arguments.app_arguments)
    if app_arguments and app_arguments[0] == "--":
        app_arguments.pop(0)
    # A runtime argument naming its own app, package or module replaces --app.
    explicit_package = any(
        item.split("=", 1)[0] in ("--app", "--package", "--game") for item in app_arguments
    )
    package = getattr(arguments, "package", None) or DEFAULT_APP
    if app_name == WASM_RUNTIME_APPLICATION and not explicit_package:
        app_arguments = ["--app", package, *app_arguments]
    if app_name == WASM_RUNTIME_APPLICATION and not runner.dry_run:
        outputs = runtime_outputs(build_directory, arguments.config)
        for retired in outputs.retired:
            print(f"warning: {retired_output_message(retired)}", file=sys.stderr)
        staged = outputs.app(package)
        if outputs.runtime is not None and not explicit_package and not outputs.playable(package):
            raise BuildError(
                f"{outputs.runtime} has no {APPS_DIRECTORY}/{package} package beside it; "
                f"run 'python build.py play --app {package}' to build it first."
            )
        if staged is not None and staged.staged and not explicit_package:
            print(dashboard_style(
                f"Playing {staged.label}: {APPS_DIRECTORY}/{package}/{staged.module.name} "
                f"({format_size(staged.module.stat().st_size)}) in {WASM_RUNTIME_APPLICATION}",
                ANSI_BOLD + ANSI_CYAN, sys.stdout.isatty(),
            ), flush=True)
    application = executable_path(
        build_directory,
        arguments.config,
        app_name,
        runner.dry_run,
        workspace,
    )
    runner.run((str(application), *app_arguments), application.parent)


def run_wasm_tools(arguments: argparse.Namespace) -> None:
    if not WASM_HOST_SUPPORTED:
        raise BuildError("The pinned Wasmtime and WASI SDK toolchain is Windows x64 only.")
    runner = CommandRunner(arguments.dry_run)
    missing = missing_wasm_tools(DEFAULT_WASM_TOOLS_DIRECTORY)
    wasmtime, wasi_sdk = wasm_toolchain_packages()
    if not missing:
        print(f"WASM toolchain ready: {wasmtime} + {wasi_sdk} in {DEFAULT_WASM_TOOLS_DIRECTORY}")
        return
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    if powershell is None:
        if not runner.dry_run:
            raise BuildError(f"PowerShell was not found on PATH; {WASM_BOOTSTRAP_SCRIPT.name} requires it.")
        powershell = "powershell"
    print(
        f"Fetching the SHA256-pinned {wasmtime} and {wasi_sdk} release archives "
        f"from GitHub into {DEFAULT_WASM_TOOLS_DIRECTORY}.",
        flush=True,
    )
    runner.run((
        powershell, "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", str(WASM_BOOTSTRAP_SCRIPT),
        "-Destination", str(DEFAULT_WASM_TOOLS_DIRECTORY),
    ))
    if runner.dry_run:
        return
    missing = missing_wasm_tools(DEFAULT_WASM_TOOLS_DIRECTORY)
    if missing:
        raise BuildError(wasm_toolchain_error(missing))
    print("WASM toolchain ready. 'python build.py play' builds and launches IllumoGame.")


def run_coverage(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    cmake = existing_tool("cmake", runner.dry_run)
    build_directory = resolve_build_directory(arguments.build_dir)
    validate_workspace_build_directory(build_directory)
    configure_coverage = [
        cmake,
        "-S",
        str(SOURCE_DIRECTORY),
        "-B",
        str(build_directory),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER=clang++",
        "-DILLUMO_BUILD_DOCUMENTATION=OFF",
        "-DILLUMO_ENABLE_TRACY=OFF",
        "-DILLUMO_ENABLE_COVERAGE=ON",
        "-DILLUMO_ENABLE_CLANG_TIDY=OFF",
    ]
    configure_coverage.extend(arguments.cmake_arg)
    runner.run(configure_coverage)

    build_coverage = [
        cmake,
        "--build",
        str(build_directory),
        "--target",
        "IllumoCoverage",
    ]
    if arguments.parallel is not None:
        build_coverage.append("--parallel")
        if arguments.parallel > 0:
            build_coverage.append(str(arguments.parallel))
    if arguments.verbose:
        build_coverage.append("--verbose")
    runner.run(build_coverage)


def run_tidy(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    cmake = existing_tool("cmake", runner.dry_run)
    existing_tool("clang-tidy", runner.dry_run)
    existing_tool("ninja", runner.dry_run)
    build_directory = resolve_build_directory(arguments.build_dir)
    validate_workspace_build_directory(build_directory)
    configure_tidy = [
        cmake,
        "-S",
        str(SOURCE_DIRECTORY),
        "-B",
        str(build_directory),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER=clang++",
        "-DILLUMO_BUILD_DOCUMENTATION=OFF",
        "-DILLUMO_ENABLE_TRACY=OFF",
        "-DILLUMO_ENABLE_COVERAGE=OFF",
        "-DILLUMO_ENABLE_CLANG_TIDY=ON",
    ]
    configure_tidy.extend(arguments.cmake_arg)
    runner.run(configure_tidy)

    build_tidy = [
        cmake,
        "--build",
        str(build_directory),
        "--target",
        "IllumoTidy",
    ]
    if arguments.parallel is not None:
        build_tidy.append("--parallel")
        if arguments.parallel > 0:
            build_tidy.append(str(arguments.parallel))
    if arguments.verbose:
        build_tidy.append("--verbose")
    runner.run(build_tidy)


def run_docs(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    if powershell is None:
        if runner.dry_run:
            powershell = "powershell"
        else:
            raise BuildError(
                "PowerShell was not found on PATH; docs/build.ps1 requires it."
            )
    runner.run(
        (
            powershell,
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(REPOSITORY_ROOT / "docs" / "build.ps1"),
        )
    )


def run_source_file_statistics(arguments: argparse.Namespace) -> None:
    statistics = collect_repository_statistics(REPOSITORY_ROOT)
    category = getattr(arguments, "category", "production")
    include_tests = getattr(arguments, "include_tests", False)
    sort_by = getattr(arguments, "sort", "loc")
    descending = not getattr(arguments, "reverse", False)
    min_loc = getattr(arguments, "min_loc", 0) or 0
    limit = getattr(arguments, "limit", None)
    project = getattr(arguments, "project", None)

    scope_parts: list[str] = []
    if include_tests:
        scope_parts.append("Production C/C++ and Tests C/C++")
    elif category.lower() in ("production", "prod", "production c/c++"):
        scope_parts.append("Production C/C++")
    elif category.lower() in ("tests", "test", "tests c/c++"):
        scope_parts.append("Tests C/C++")
    elif category.lower() in ("cpp", "c++", "source", "sources"):
        scope_parts.append("All C/C++ (Production and Tests)")
    elif category.lower() in ("all", "*"):
        scope_parts.append("All first-party files")
    else:
        scope_parts.append(category)

    if project:
        scope_parts.append(f"project: {project}")
    if min_loc > 0:
        scope_parts.append(f"min LOC: {min_loc}")
    scope_label = ", ".join(scope_parts)

    sort_direction = (
        "smallest to largest" if not descending else "largest to smallest"
    )
    sort_label = f"{sort_by.upper()} ({sort_direction})"

    all_matching = filter_and_sort_source_files(
        statistics.files,
        category=category,
        include_tests=include_tests,
        sort_by=sort_by,
        descending=descending,
        min_loc=min_loc,
        limit=None,
        project=project,
    )
    total_matching = len(all_matching)
    total_matching_loc = sum(item.loc for item in all_matching)
    total_matching_physical = sum(item.physical_lines for item in all_matching)

    displayed_files = (
        all_matching[:limit]
        if limit is not None and limit > 0
        else all_matching
    )

    if getattr(arguments, "json", False):
        print(
            source_file_statistics_json(
                displayed_files,
                total_matching=total_matching,
                total_matching_loc=total_matching_loc,
                total_matching_physical=total_matching_physical,
                scope_label=scope_label,
                sort_label=sort_label,
            )
        )
    else:
        print_source_file_statistics(
            displayed_files,
            total_matching=total_matching,
            total_matching_loc=total_matching_loc,
            total_matching_physical=total_matching_physical,
            scope_label=scope_label,
            sort_label=sort_label,
            limit=limit,
        )


def run_repository_statistics(arguments: argparse.Namespace) -> None:
    statistics = collect_repository_statistics(REPOSITORY_ROOT)
    by_file = getattr(arguments, "by_file", False)
    if arguments.json:
        print(repository_statistics_json(statistics, include_files=by_file))
    else:
        print_repository_statistics(statistics)
        if by_file:
            print()
            run_source_file_statistics(arguments)


def run_new_project(arguments: argparse.Namespace) -> None:
    tool_script = REPOSITORY_ROOT / "tools" / "create_project.py"
    if not tool_script.is_file():
        raise BuildError(f"Project creation tool not found at {tool_script}")
    import importlib.util

    spec = importlib.util.spec_from_file_location("create_project", tool_script)
    if spec is None or spec.loader is None:
        raise BuildError("Failed to load create_project module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    try:
        module.create_project(
            destination=arguments.destination,
            project_name=arguments.name,
            template_name=arguments.template,
            include_debug_tools=not arguments.no_debug_tools,
            in_workspace=arguments.in_workspace,
            force=arguments.force,
            verbose=arguments.verbose,
        )
    except module.ProjectCreationError as err:
        raise BuildError(str(err)) from err


def main(arguments: Sequence[str] | None = None) -> int:
    workspace = discover_workspace_projects(REPOSITORY_ROOT)
    parser = create_parser(workspace)
    command_line = sys.argv[1:] if arguments is None else list(arguments)
    if not command_line:
        if sys.stdin.isatty() and sys.stdout.isatty():
            try:
                return run_dashboard()
            except KeyboardInterrupt:
                print("\nBuild console closed.", file=sys.stderr)
                return 130
            except BuildError as error:
                print(f"error: {error}", file=sys.stderr)
                return error.exit_code
        command_line = ["build"]
    parsed = parser.parse_args(normalize_arguments(command_line))
    try:
        if getattr(parsed, "profile", None):
            profiles = load_profiles(
                parsed.profiles_file, allow_missing=parsed.command == "profile-save"
            )
            if parsed.profile not in profiles:
                raise BuildError(f"Unknown profile '{parsed.profile}'. Available: {', '.join(profiles)}")
            normalized = normalize_arguments(command_line)
            parsed = parser.parse_args([
                normalized[0], *profile_arguments(profiles[parsed.profile]), *normalized[1:]
            ])
    except BuildError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.exit_code

    if parsed.command == "menu":
        if parsed.snapshot:
            print(
                render_dashboard(
                    DashboardState(),
                    96,
                    ansi=False,
                )
            )
            return 0
        try:
            return run_dashboard()
        except KeyboardInterrupt:
            print("\nBuild console closed.", file=sys.stderr)
            return 130
        except BuildError as error:
            print(f"error: {error}", file=sys.stderr)
            return error.exit_code

    actions = {
        "_toolbox": run_toolbox_request,
        "profiles": run_profiles,
        "profile-save": run_profile_save,
        "doctor": run_doctor,
        "configure": run_configure,
        "build": run_build,
        "test": run_tests,
        "run": run_application,
        "play": run_application,
        "wasm-tools": run_wasm_tools,
        "coverage": run_coverage,
        "tidy": run_tidy,
        "docs": run_docs,
        "stats": run_repository_statistics,
        "file-stats": run_source_file_statistics,
        "source-stats": run_source_file_statistics,
        "new-project": run_new_project,
        "create-project": run_new_project,
    }
    try:
        actions[parsed.command](parsed)
        return 0
    except BuildError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.exit_code
    except KeyboardInterrupt:
        print("\nBuild interrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
