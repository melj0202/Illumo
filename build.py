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
import math
import os
from pathlib import Path
import platform
import random
import re
import shlex
import shutil
import signal
import statistics
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
# "debug" is the AddressSanitizer profile (explicit WASM guest bounds checks);
# "dev" is optimized with symbols and debug tools, the one to play with; and
# "debug-noasan" keeps Debug code generation with fast guest traps; and
# "tracy" is Release with Tracy zones in the host and every WASM guest.
BUILTIN_PROFILES = {
    "debug": {"config": "Debug", "build_dir": "build-workspace-debug"},
    "release": {"config": "Release", "build_dir": "build-workspace-release"},
    "dev": {"config": "RelWithDebInfo", "build_dir": "build-workspace-dev"},
    "debug-noasan": {"config": "Debug", "build_dir": "build-workspace-debug-noasan",
                     "sanitizer": "off"},
    "tracy": {"config": "Release", "build_dir": "build-workspace-tracy",
              "tracy": True, "no_docs": True, "no_tidy": True},
}
PROFILE_STRINGS = (
    "config", "build_dir", "generator", "architecture",
    "compiler", "optimization", "cpu", "warnings", "sanitizer",
)
PROFILE_FLAGS = ("tracy", "no_tests", "no_docs", "no_tidy", "no_wasm")
# Abstract compiler menu. Configure turns these into driver paths and flags;
# the menu itself never stores /O2 or -O3.
COMPILER_CHOICES = (
    ("default", "Default"),
    ("msvc", "Microsoft MSVC"),
    ("intel", "Intel oneAPI"),
    ("clang", "LLVM Clang"),
    ("gcc", "GCC"),
)
OPTIMIZATION_CHOICES = (
    ("default", "Match configuration"),
    ("none", "None"),
    ("size", "Size"),
    ("speed", "Speed"),
    ("fast", "Fast"),
)
# Instruction set. "host" is Intel's /QxHost or -xHost, which is off unless chosen.
CPU_CHOICES = (
    ("default", "Baseline"),
    ("avx2", "AVX2"),
    ("avx512", "AVX-512"),
    ("host", "This CPU"),
)
WARNING_CHOICES = (
    ("default", "Project default"),
    ("strict", "Strict"),
    ("quiet", "Quiet"),
)
SANITIZER_CHOICES = (
    ("default", "Debug on"),
    ("off", "Off"),
    ("address", "On"),
)
COMPILER_OPTION_CHOICES = {
    "compiler": COMPILER_CHOICES,
    "optimization": OPTIMIZATION_CHOICES,
    "cpu": CPU_CHOICES,
    "warnings": WARNING_CHOICES,
    "sanitizer": SANITIZER_CHOICES,
}
COMPILER_OPTION_LABELS = {
    "compiler": "Compiler",
    "optimization": "Optimization",
    "cpu": "CPU",
    "warnings": "Warnings",
    "sanitizer": "Address sanitizer",
}
# Warning lists use "|" so the CMake command line is not split on ";".
OPTIMIZATION_FLAGS = {
    "msvc": {"none": "/Od", "size": "/O1", "speed": "/O2", "fast": "/Ox"},
    "intel-windows": {"none": "/Od", "size": "/O1", "speed": "/O2", "fast": "/O3"},
    "gnu": {"none": "-O0", "size": "-Os", "speed": "-O2", "fast": "-O3"},
}
# AVX2 and AVX-512 stay portable across Intel and AMD. Host is the Intel
# processor switch (/QxHost, -xHost), except GCC and Clang, which get
# -march=native. MSVC has no host switch.
CPU_FLAGS = {
    "msvc": {"avx2": "/arch:AVX2", "avx512": "/arch:AVX512", "host": ""},
    "intel-windows": {"avx2": "/arch:AVX2", "avx512": "/arch:AVX512", "host": "/QxHost"},
    "gnu": {"avx2": "-march=x86-64-v3", "avx512": "-march=x86-64-v4", "host": "-march=native"},
}
WARNING_FLAGS = {
    "msvc": {"strict": "/W4|/permissive-", "quiet": "/W0"},
    "intel-windows": {"strict": "/W4|/permissive-", "quiet": "/W0"},
    "gnu": {"strict": "-Wall|-Wextra|-Wpedantic", "quiet": "-w"},
}
# Dropped when the build command is formed. CMake turns AddressSanitizer and
# clang-tidy on unless the command says otherwise, so an ignored feature is
# passed as off rather than left at that default.
COMPILER_UNSUPPORTED = {
    "intel": frozenset({"tidy", "asan"}),
}
COMPILER_COMMANDS = (
    "configure", "build", "dist", "test", "run", "play",
    "doctor", "watch", "profile-save",
)

# IllumoGame ships only as a WASM package played by IllumoRuntime. The pinned
# Wasmtime/WASI SDK pair (cmake/IllumoWasm.cmake) is Windows x64 only.
WASM_HOST_SUPPORTED = os.name == "nt" and platform.machine().lower() in ("amd64", "x86_64")
WASM_CMAKE_MODULE = REPOSITORY_ROOT / "cmake" / "IllumoWasm.cmake"
WASM_BOOTSTRAP_SCRIPT = REPOSITORY_ROOT / "tools" / "bootstrap-wasm.ps1"
DEFAULT_WASM_TOOLS_DIRECTORY = REPOSITORY_ROOT / "build-wasm-tools"
WASM_RUNTIME_APPLICATION = "IllumoRuntime"
# Installed applications live in <runtime dir>/apps/<name>/ with illumo.json.
APPS_DIRECTORY = "apps"
DEFAULT_APP = "game"
# The applications `dist --apps` ships reach cmake/IllumoDistribution.cmake here.
DISTRIBUTION_APPS_VARIABLE = "ILLUMO_DIST_APPS"
APP_LABELS = {"game": "IllumoGame", "illed": "IllEd", "meshviewer": "Mesh Viewer",
              "playground": "Playground"}
# Native programs from before the WASM cutover; nothing builds or launches
# them any more. The pre-apps game package directory is stale too.
RETIRED_EXECUTABLES = ("IllumoGame", "IllEd", "IllMeshViewer", "IllumoCapture")
RETIRED_DIRECTORIES = ("game",)


@dataclass(frozen=True)
class AppPackage:
    name: str
    module: str
    title: str = ""

    @property
    def label(self) -> str:
        return APP_LABELS.get(self.name) or self.title or self.name


def documentation_available(root: Path | None = None) -> bool:
    """Whether the engine LaTeX documentation sources are present (mirrors Illumo/CMakeLists.txt)."""
    base = root or REPOSITORY_ROOT
    return (
        (base / "docs" / "build.ps1").is_file()
        and (base / "docs" / "latex" / "illumo.tex").is_file()
        and (base / "docs" / "latex" / "architecture-map.tex").is_file()
    )


def minimum_cmake_version(root: Path | None = None) -> tuple[int, int]:
    """Minimum CMake version declared by the root CMakeLists.txt."""
    text = _read_root_cmake(root)
    match = re.search(r"cmake_minimum_required\s*\(\s*VERSION\s+(\d+)\.(\d+)", text, re.I)
    if match:
        return int(match[1]), int(match[2])
    return 3, 25


def _read_root_cmake(root: Path | None = None) -> str:
    base = root or REPOSITORY_ROOT
    try:
        return (base / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _game_directory_name_from_text(text: str) -> str | None:
    match = re.search(r"set\(\s*ILLUMO_GAME_DIR\s+\"?([^\")\s]+)\"?\s*\)", text)
    return match[1] if match else None


def game_directory_name(root: Path | None = None) -> str | None:
    """The root CMakeLists.txt's ILLUMO_GAME_DIR: where the optional game repository goes."""
    return _game_directory_name_from_text(_read_root_cmake(root))


def game_directory(root: Path | None = None) -> Path | None:
    """The optional game repository's folder, when it is cloned."""
    base = root or REPOSITORY_ROOT
    name = game_directory_name(base)
    if name is None or not (base / name / "CMakeLists.txt").is_file():
        return None
    return base / name


def game_missing_message(root: Path | None = None) -> str | None:
    """Why the game is not in this workspace, or None when it is (or has no slot)."""
    base = root or REPOSITORY_ROOT
    name = game_directory_name(base)
    if name is None or (base / name / "CMakeLists.txt").is_file():
        return None
    return (f"The game is not in this workspace: clone its repository into "
            f"{name}/ at the engine root (see README.md). The engine and its "
            f"other applications build without it.")


def _nested_program_directories_from_text(base: Path, text: str) -> tuple[Path, ...]:
    match = re.search(r"set\(\s*ILLUMO_NESTED_PROGRAMS\s+([^)]*)\)", text)
    if match is None:
        return ()
    names = (name.strip('"') for name in match[1].split())
    return tuple(base / name for name in names
                 if (base / name / "CMakeLists.txt").is_file())


def nested_program_directories(root: Path | None = None) -> tuple[Path, ...]:
    """The root CMakeLists.txt's ILLUMO_NESTED_PROGRAMS that hold a CMakeLists.txt.

    Like the game, each is an optional folder at the engine root, and the root
    CMakeLists.txt appends the present ones after it.
    """
    base = root or REPOSITORY_ROOT
    return _nested_program_directories_from_text(base, _read_root_cmake(base))


def program_directories(root: Path | None = None) -> tuple[Path, ...]:
    """WASM program directories, in the root CMakeLists.txt's ILLUMO_PROGRAMS order.

    The optional game (ILLUMO_GAME_DIR) and then the optional nested programs
    (ILLUMO_NESTED_PROGRAMS) come last, as the root CMakeLists.txt appends
    them, and only when their folders hold a CMakeLists.txt.
    """
    base = root or REPOSITORY_ROOT
    text = _read_root_cmake(base)
    match = re.search(r"set\(\s*ILLUMO_PROGRAMS\s+([^)]*)\)", text)
    if match is None:
        return ()
    programs = [base / name.strip('"') for name in match[1].split()]
    game_name = _game_directory_name_from_text(text)
    if game_name is not None and (base / game_name / "CMakeLists.txt").is_file():
        game = base / game_name
        if game not in programs:
            programs.append(game)
    # Honor patches on nested_program_directories (e.g. in tests) when root is default.
    nested = (
        nested_program_directories(base)
        if nested_program_directories is not _ORIGINAL_NESTED_PROGRAM_DIRECTORIES
        else _nested_program_directories_from_text(base, text)
    )
    for directory in nested:
        if directory not in programs:
            programs.append(directory)
    return tuple(programs)


_ORIGINAL_NESTED_PROGRAM_DIRECTORIES = nested_program_directories


def _manifest_title(directory: Path) -> str:
    manifest = directory / "illumo.json"
    try:
        data = json.loads(manifest.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return ""
    title = data.get("title") if isinstance(data, dict) else ""
    return title.strip() if isinstance(title, str) else ""


def installed_apps(root: Path | None = None) -> tuple[AppPackage, ...]:
    """Applications the runtime build stages: each program's PackageTargets.cmake."""
    apps: list[AppPackage] = []
    for directory in program_directories(root):
        try:
            text = (directory / "PackageTargets.cmake").read_text(
                encoding="utf-8", errors="replace"
            )
        except OSError:
            continue
        title = _manifest_title(directory)
        for match in re.finditer(
            r"illumo_stage_app\(\s*\w+\s+([a-z0-9._-]+)\s+MODULE\s+([A-Za-z0-9._-]+)", text
        ):
            apps.append(AppPackage(match[1], match[2], title))
    return tuple(apps)


def app_label(name: str, packages: Sequence[AppPackage] | None = None) -> str:
    if name in APP_LABELS:
        return APP_LABELS[name]
    if packages is not None:
        for package in packages:
            if package.name == name:
                return package.label
    return name


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
ANSI_UNDERLINE = "\x1b[4m"
ANSI_CYAN = "\x1b[38;5;45m"
ANSI_GREEN = "\x1b[38;5;82m"
ANSI_YELLOW = "\x1b[38;5;220m"
ANSI_BLUE = "\x1b[38;5;111m"
ANSI_RED = "\x1b[38;5;203m"
ANSI_VIOLET = "\x1b[38;5;141m"
ANSI_FRAME = "\x1b[38;5;61m"
ANSI_SELECTED = "\x1b[48;5;24m\x1b[38;5;231m"
ANSI_REVERSE = "\x1b[7m"
ANSI_CLEAR = "\x1b[2J\x1b[H"
ANSI_HOME = "\x1b[H"
ANSI_ERASE_LINE = "\x1b[K"
ANSI_ERASE_BELOW = "\x1b[J"
ANSI_ENTER_SCREEN = "\x1b[?1049h"
ANSI_LEAVE_SCREEN = "\x1b[?1049l"
ANSI_HIDE_CURSOR = "\x1b[?25l"
ANSI_SHOW_CURSOR = "\x1b[?25h"
ANSI_DISABLE_WRAP = "\x1b[?7l"
ANSI_ENABLE_WRAP = "\x1b[?7h"
# xterm title stack: keep the shell's own title across the console session.
ANSI_PUSH_TITLE = "\x1b[22;0t"
ANSI_POP_TITLE = "\x1b[23;0t"
_ANSI_SEQUENCE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# Cyan to violet; the wordmark and progress bars sweep across it.
GRADIENT_COLORS = (51, 45, 39, 33, 69, 105, 141, 177)

RGB = tuple[int, int, int]


def rgb_fg(color: RGB) -> str:
    return f"\x1b[38;2;{color[0]};{color[1]};{color[2]}m"


def rgb_bg(color: RGB) -> str:
    return f"\x1b[48;2;{color[0]};{color[1]};{color[2]}m"


def lerp_rgb(a: RGB, b: RGB, t: float) -> RGB:
    clamped = 0.0 if t <= 0.0 else (1.0 if t >= 1.0 else t)
    return (
        int(a[0] + (b[0] - a[0]) * clamped + 0.5),
        int(a[1] + (b[1] - a[1]) * clamped + 0.5),
        int(a[2] + (b[2] - a[2]) * clamped + 0.5),
    )


def boost_rgb(color: RGB, amount: float) -> RGB:
    return lerp_rgb(color, (255, 255, 255), amount)


def scale_rgb(color: RGB, factor: float) -> RGB:
    return (
        max(0, min(255, int(color[0] * factor + 0.5))),
        max(0, min(255, int(color[1] * factor + 0.5))),
        max(0, min(255, int(color[2] * factor + 0.5))),
    )


def hsv_to_rgb(hue: float, saturation: float, value: float) -> RGB:
    hue = hue % 1.0
    sector = int(hue * 6.0)
    frac = hue * 6.0 - sector
    p = value * (1.0 - saturation)
    q = value * (1.0 - frac * saturation)
    t = value * (1.0 - (1.0 - frac) * saturation)
    r, g, b = (
        (value, t, p), (q, value, p), (p, value, t),
        (p, q, value), (t, p, value), (value, p, q),
    )[sector % 6]
    return int(r * 255 + 0.5), int(g * 255 + 0.5), int(b * 255 + 0.5)


def sample_gradient(stops: Sequence[RGB], t: float, wrap: bool = False) -> RGB:
    if not stops:
        return (200, 220, 255)
    if len(stops) == 1:
        return stops[0]
    if wrap:
        pos = (t % 1.0) * len(stops)
        low = int(pos) % len(stops)
        high = (low + 1) % len(stops)
        return lerp_rgb(stops[low], stops[high], pos - math.floor(pos))
    clamped = 0.0 if t <= 0.0 else (1.0 if t >= 1.0 else t)
    pos = clamped * (len(stops) - 1)
    low = int(pos)
    high = min(len(stops) - 1, low + 1)
    return lerp_rgb(stops[low], stops[high], pos - low)


def cli_ansi_enabled() -> bool:
    return getattr(sys.stdout, "isatty", lambda: False)() and not os.environ.get("NO_COLOR")


@dataclass(frozen=True)
class DashboardTheme:
    key: str
    name: str
    tagline: str
    banner_stops: tuple[RGB, ...]
    frame_base: RGB
    frame_glow: RGB
    accent: RGB
    selected_left: RGB
    selected_right: RGB
    selected_fg: RGB
    pulse_stops: tuple[RGB, ...]
    prism: bool = False


DASHBOARD_THEMES: dict[str, DashboardTheme] = {
    "illumo": DashboardTheme(
        key="illumo", name="Illumo", tagline="bioluminescent cyan to ultraviolet",
        banner_stops=((0, 245, 255), (48, 155, 255), (156, 108, 255), (232, 121, 249)),
        frame_base=(76, 86, 146), frame_glow=(72, 240, 255), accent=(56, 220, 255),
        selected_left=(20, 62, 104), selected_right=(44, 30, 82), selected_fg=(240, 248, 255),
        pulse_stops=((0, 225, 255), (130, 248, 255), (218, 162, 255), (130, 248, 255)),
    ),
    "synthwave": DashboardTheme(
        key="synthwave", name="Synthwave", tagline="80s neon magenta and laser cyan",
        banner_stops=((255, 42, 164), (186, 85, 255), (0, 240, 255), (255, 198, 64)),
        frame_base=(122, 54, 134), frame_glow=(255, 86, 200), accent=(255, 92, 196),
        selected_left=(84, 22, 74), selected_right=(24, 54, 88), selected_fg=(255, 240, 252),
        pulse_stops=((255, 64, 180), (255, 198, 86), (0, 240, 255), (190, 96, 255)),
    ),
    "ember": DashboardTheme(
        key="ember", name="Ember", tagline="molten solar gold and forge crimson",
        banner_stops=((255, 224, 82), (255, 126, 38), (236, 54, 82), (176, 46, 124)),
        frame_base=(136, 68, 52), frame_glow=(255, 196, 76), accent=(255, 158, 54),
        selected_left=(92, 42, 18), selected_right=(66, 20, 38), selected_fg=(255, 246, 232),
        pulse_stops=((255, 140, 40), (255, 230, 120), (255, 82, 82), (255, 210, 96)),
    ),
    "matrix": DashboardTheme(
        key="matrix", name="Matrix", tagline="phosphor emerald CRT bloom",
        banner_stops=((180, 255, 108), (0, 242, 122), (0, 198, 154), (24, 146, 106)),
        frame_base=(40, 110, 74), frame_glow=(120, 255, 168), accent=(64, 246, 140),
        selected_left=(16, 68, 42), selected_right=(12, 46, 38), selected_fg=(226, 255, 236),
        pulse_stops=((32, 220, 112), (175, 255, 152), (0, 240, 180), (120, 255, 150)),
    ),
    "aurora": DashboardTheme(
        key="aurora", name="Aurora", tagline="glacial mint and northern indigo",
        banner_stops=((94, 255, 214), (56, 189, 248), (129, 140, 248), (244, 114, 182)),
        frame_base=(68, 96, 134), frame_glow=(116, 255, 224), accent=(96, 238, 220),
        selected_left=(20, 68, 76), selected_right=(44, 38, 86), selected_fg=(236, 252, 255),
        pulse_stops=((94, 255, 214), (110, 214, 255), (196, 148, 255), (110, 214, 255)),
    ),
    "prism": DashboardTheme(
        key="prism", name="Prism", tagline="full-spectrum animated RGB rainbow",
        banner_stops=((255, 72, 118), (255, 206, 64), (64, 242, 148), (64, 196, 255), (188, 112, 255)),
        frame_base=(96, 84, 144), frame_glow=(255, 236, 140), accent=(110, 236, 255),
        selected_left=(56, 30, 88), selected_right=(22, 64, 92), selected_fg=(255, 255, 255),
        pulse_stops=((255, 92, 148), (255, 224, 96), (96, 255, 168), (96, 216, 255), (200, 128, 255)),
        prism=True,
    ),
}
THEME_ORDER = tuple(DASHBOARD_THEMES)


def default_theme_name() -> str:
    chosen = os.environ.get("ILLUMO_THEME", "").strip().lower()
    return chosen if chosen in DASHBOARD_THEMES else "illumo"


def resolve_theme(name: str | None = None) -> DashboardTheme:
    key = (name or default_theme_name()).strip().lower()
    return DASHBOARD_THEMES.get(key, DASHBOARD_THEMES["illumo"])


def theme_color(theme: DashboardTheme, t: float, phase: float = 0.0) -> RGB:
    if theme.prism:
        return hsv_to_rgb((t * 0.85 + phase) % 1.0, 0.84, 1.0)
    if phase != 0.0:
        return sample_gradient(theme.banner_stops, t + phase, wrap=True)
    return sample_gradient(theme.banner_stops, t, wrap=False)


def perimeter_glow_rgb(
    theme: DashboardTheme,
    pos: float,
    perimeter: int,
    frame: int | None,
    *,
    override_base: RGB | None = None,
    override_glow: RGB | None = None,
) -> RGB:
    """24-bit color along the box frame with two orbiting neon glints when animated."""
    span = max(1, perimeter)
    u = (pos % span) / span
    if override_base is not None:
        base = override_base
    elif theme.prism:
        phase = (frame or 0) * 0.015
        base = scale_rgb(hsv_to_rgb((u + phase) % 1.0, 0.65, 1.0), 0.72)
    else:
        tint = sample_gradient(theme.banner_stops, u, wrap=True)
        base = lerp_rgb(theme.frame_base, tint, 0.34)
    if frame is None:
        return base
    glow_color = override_glow or (
        boost_rgb(hsv_to_rgb((u + frame * 0.015) % 1.0, 0.55, 1.0), 0.35)
        if theme.prism else theme.frame_glow
    )
    head_a = (frame * 1.6) % span
    head_b = (head_a + span * 0.5) % span
    dist_a = min(abs(pos - head_a), span - abs(pos - head_a))
    dist_b = min(abs(pos - head_b), span - abs(pos - head_b))
    intensity = max(
        math.exp(-(dist_a * dist_a) / 48.0),
        0.72 * math.exp(-(dist_b * dist_b) / 36.0),
    )
    return lerp_rgb(base, glow_color, min(1.0, intensity))


def _horizontal_border_segments(
    left: str,
    right: str,
    fill: str,
    width: int,
    label: Sequence[tuple[str, str]],
    glyphs: dict,
    ansi: bool,
    *,
    theme: DashboardTheme,
    frame: int | None,
    start_pos: float,
    step: float,
    perimeter: int,
    override_base: RGB | None = None,
    override_glow: RGB | None = None,
    bg_rgb: RGB | None = None,
) -> str:
    """Box rule identical in text to frame_rule(), shaded in 24-bit TrueColor."""
    if not ansi:
        return frame_rule(left, right, label, width, glyphs, False)
    inner = max(0, width - 2)
    label_width = sum(dashboard_visible_width(text) for text, _style in label)
    has_label = bool(label and inner >= 6)
    used = min(label_width, inner - 3) if has_label else 0
    bg_prefix = rgb_bg(bg_rgb) if bg_rgb is not None else ""
    # Paint border cells in 2-cell chunks for smooth gradients with compact escapes.
    parts: list[str] = []

    def paint_char(col: int, text: str) -> None:
        rgb = perimeter_glow_rgb(
            theme, start_pos + step * col, perimeter, frame,
            override_base=override_base, override_glow=override_glow,
        )
        parts.append(f"{bg_prefix}{rgb_fg(rgb)}{text}{ANSI_RESET}")

    paint_char(0, left)
    if has_label:
        paint_char(1, fill + " ")
        parts.append(render_segments(label, used, True, base=bg_prefix, ellipsis=glyphs["ellipsis"]))
        tail_start = 3 + used
        tail_len = max(0, inner - 3 - used)
        if tail_len:
            paint_char(tail_start, " ")
            col = tail_start + 1
            remaining = tail_len
            while remaining > 0:
                chunk = min(2, remaining)
                paint_char(col, fill * chunk)
                col += chunk
                remaining -= chunk
        else:
            paint_char(tail_start, " ")
    else:
        col = 1
        remaining = inner
        while remaining > 0:
            chunk = min(2, remaining)
            paint_char(col, fill * chunk)
            col += chunk
            remaining -= chunk
    paint_char(width - 1, right)
    return "".join(parts)


def ambient_bg_rgb(
    theme: DashboardTheme,
    col: int,
    row_y: int,
    width: int,
    total_rows: int,
    frame: int | None,
    *,
    header_glow: float = 0.0,
    wave_pos: float | None = None,
) -> RGB:
    """Deep living 24-bit cosmic nebula background for one dashboard cell."""
    nx = col / max(1, width - 1)
    ny = row_y / max(1, total_rows - 1)
    deep = scale_rgb(theme.selected_right, 0.24)
    mid = scale_rgb(theme.selected_left, 0.42)
    if frame is not None:
        t = frame * 0.095
        # Smooth vertical aurora wave across rows so text stays unbroken while the backdrop breathes.
        swirl = 0.5 + 0.5 * math.sin(ny * 4.4 - t)
        bg = lerp_rgb(deep, mid, swirl * 0.78)
        if theme.prism:
            prism_tint = scale_rgb(hsv_to_rgb((ny * 0.45 + frame * 0.012) % 1.0, 0.65, 1.0), 0.16)
            bg = lerp_rgb(bg, prism_tint, 0.55)
    else:
        bg = lerp_rgb(deep, mid, 0.28 + 0.48 * (1.0 - ny))
    if header_glow > 0.0:
        # Backlit radial aura centered behind the ILLUMO logo.
        dx = (nx - 0.5) * 2.2
        aura = math.exp(-(dx * dx) * 2.1) * header_glow
        if frame is not None:
            aura *= 0.84 + 0.16 * math.sin(frame * 0.14)
        glow_tint = scale_rgb(theme_color(theme, nx, phase=(frame or 0) * 0.01 if theme.prism else 0.0), 0.36)
        bg = lerp_rgb(bg, glow_tint, min(0.85, aura))
    if wave_pos is not None:
        # Diagonal full-console energy shockwave sweeping across all rows.
        diag = nx * 0.75 + ny * 0.45
        dist = diag - wave_pos
        ring = math.exp(-(dist * dist) / 0.009)
        if ring > 0.02:
            wave_rgb = scale_rgb(boost_rgb(theme.accent, 0.35), 0.68)
            bg = lerp_rgb(bg, wave_rgb, min(0.9, ring))
    return bg


def render_gradient_row(
    segments: Sequence[tuple[str, str]],
    width: int,
    ansi: bool,
    left_bg: RGB,
    right_bg: RGB,
    fg_rgb: RGB,
    ellipsis: str = "...",
    flash: float = 0.0,
    flash_rgb: RGB = (255, 255, 255),
    shimmer_x: float | None = None,
    shimmer_rgb: RGB = (255, 255, 255),
    ambient_fn=None,
) -> str:
    """Lay styled runs across a 24-bit horizontal background shader ribbon."""
    if not ansi or width < 1:
        return render_segments(segments, width, ansi, "", ellipsis)
    total = sum(dashboard_visible_width(text) for text, _style in segments)
    overflow = total > width
    if overflow and dashboard_visible_width(ellipsis) > width:
        ellipsis = "." * width
    limit = width - dashboard_visible_width(ellipsis) if overflow else width
    cells: list[tuple[str, str, int]] = []
    used = 0
    full = False
    for text, style in segments:
        for character in text:
            cell = dashboard_visible_width(character)
            if used + cell > limit:
                full = True
                break
            cells.append((character, style, used))
            used += cell
        if full:
            break
    if overflow:
        tail_style = cells[-1][1] if cells else ""
        for character in ellipsis:
            cell = dashboard_visible_width(character)
            cells.append((character, tail_style, used))
            used += cell
    while used < width:
        cells.append((" ", "", used))
        used += 1
    default_fg = rgb_fg(fg_rgb) if fg_rgb is not None else ""
    span = max(1, width - 1)
    out: list[str] = []
    # Coalesce runs sharing identical (bg, style) so strings stay intact and ANSI stays compact.
    last_bg: RGB | None = None
    last_style = None
    for character, style, col in cells:
        b_col = (col // 2) * 2
        if ambient_fn is not None:
            bg = ambient_fn(b_col)
        else:
            t = min(1.0, b_col / span)
            bg = lerp_rgb(left_bg, right_bg, t)
            if shimmer_x is not None:
                d = abs(b_col - shimmer_x)
                if d < 10.0:
                    bg = lerp_rgb(bg, shimmer_rgb, 0.28 * math.exp(-(d * d) / 18.0))
            if flash > 0.0:
                bg = lerp_rgb(bg, flash_rgb, min(0.75, flash))
        if bg != last_bg or style != last_style:
            fg = style if style else default_fg
            if out:
                out.append(ANSI_RESET)
            out.append(f"{rgb_bg(bg)}{fg}")
            last_bg = bg
            last_style = style
        out.append(character)
    if out:
        out.append(ANSI_RESET)
    return "".join(out)

# Unicode glyphs, with ASCII stand-ins for consoles whose encoding lacks them
# (a redirected Windows stdout is cp1252). Block and arrow glyphs are CP437.
UNICODE_GLYPHS = {
    "ok": "✔", "bad": "✘", "warn": "▲", "info": "·", "bullet": "●", "hollow": "○",
    "top_left": "╭", "top_right": "╮", "middle_left": "├", "middle_right": "┤",
    "bottom_left": "╰", "bottom_right": "╯", "horizontal": "─", "vertical": "│",
    "heavy": "━", "marker": "▸", "left": "‹", "right": "›", "separator": "·",
    "chevron": "›", "block": "█", "half": "▌", "track": "░", "thumb": "┃",
    "rail": "│", "ellipsis": "…",
    "spinner": ("⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"),
    "sparks": ("▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"),
    "up_down": "↑↓", "left_right": "←→", "refresh": "⟳",
    # Thin-line progress bars: filled, half-filled tip, and track.
    "bar_fill": "━", "bar_half": "╸", "bar_track": "━", "caret": "▏",
}
ASCII_GLYPHS = {
    "ok": "+", "bad": "x", "warn": "!", "info": "-", "bullet": "*", "hollow": "o",
    "top_left": "+", "top_right": "+", "middle_left": "+", "middle_right": "+",
    "bottom_left": "+", "bottom_right": "+", "horizontal": "-", "vertical": "|",
    "heavy": "=", "marker": ">", "left": "<", "right": ">", "separator": "|",
    "chevron": ">", "block": "#", "half": "#", "track": "-", "thumb": "#",
    "rail": "|", "ellipsis": "...",
    "spinner": ("|", "/", "-", "\\"),
    "sparks": ("_", ".", ":", "-", "=", "+", "*", "#"),
    "up_down": "Up/Down", "left_right": "Left/Right", "refresh": "@",
    "bar_fill": "#", "bar_half": "#", "bar_track": "-", "caret": "_",
}
# Block-letter wordmark for terminals with room to spare (CP437 glyphs).
BANNER_ROWS = (
    "▀█▀ █   █   █ █ █▄ ▄█ █▀█",
    " █  █   █   █ █ █ ▀ █ █ █",
    "▄█▄ █▄▄ █▄▄ █▄█ █   █ █▄█",
)
# The banner needs three extra rows over the compact 30-row console.
BANNER_MIN_ROWS = 34
ANSI_TRACK = "\x1b[38;5;238m"

# Motion. The menu advances one frame per timed input tick; nothing moves
# for redirected output or with ILLUMO_NO_ANIMATION set.
DASHBOARD_TICK_SECONDS = 0.07
INTRO_ROWS_PER_FRAME = 3
SHINE_PERIOD_FRAMES = 64
ANSI_SHINE = "\x1b[1;38;5;231m"
MARKER_PULSE = (45, 51, 87, 123, 159, 123, 87, 51)
# Tail to head of the indeterminate progress streak.
COMET_COLORS = (236, 24, 31, 38, 45, 51, 195)


def motion_enabled() -> bool:
    return (os.environ.get("ILLUMO_NO_ANIMATION", "") in ("", "0")
            and getattr(sys.stdout, "isatty", lambda: False)())


def shine_band(frame: int, length: int, speed: float = 1.5, band: int = 3,
               period: int = SHINE_PERIOD_FRAMES) -> tuple[int, int] | None:
    """The [start, end) cells lit by a sheen sweeping once per period."""
    head = int((frame % period) * speed) - band
    if head >= length:
        return None
    return head, head + band


def apply_shine(segments: Sequence[tuple[str, str]], band: tuple[int, int] | None,
                offset: int = 0) -> list[tuple[str, str]]:
    """Light the glyphs of one-glyph segments that fall inside the band."""
    if band is None:
        return list(segments)
    low, high = band[0] + offset, band[1] + offset
    return [(text, ANSI_SHINE if low <= index < high and not text.isspace() else style)
            for index, (text, style) in enumerate(segments)]


def terminal_glyphs(encoding: str | None = None) -> dict:
    encoding = encoding or getattr(sys.stdout, "encoding", None) or "utf-8"
    sample = "".join(
        "".join(value) if isinstance(value, tuple) else value
        for value in UNICODE_GLYPHS.values()
    )
    try:
        sample.encode(encoding)
    except (UnicodeEncodeError, LookupError):
        return ASCII_GLYPHS
    return UNICODE_GLYPHS


DASHBOARD_CONFIGURATIONS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")
DASHBOARD_PARALLEL_OPTIONS = (
    ("Auto", 0),
    ("Off", None),
    ("2 jobs", 2),
    ("4 jobs", 4),
    ("8 jobs", 8),
    ("16 jobs", 16),
)
DASHBOARD_DEFAULT_PROFILE = "Default"
DASHBOARD_ITEMS = (
    ("setting", "Profile", "profile"),
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
    ("action", "Assemble distribution", "dist"),
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
    "dist": "engine.ilpk, the apps you pick and their shortcuts",
    "test": "all discovered test runners",
    "launch": "skip configure and build",
    "stats": "Git state, files, and first-party LOC",
    "file_stats": "first-party source files sorted by LOC",
    "tools": "compiler, tests, diagnostics, profiles, and artifacts",
    "docs": "illumo.pdf and architecture-map.pdf",
    "coverage": "Ninja, Clang, and the 85% gate",
    "tidy": "Ninja, Clang, and first-party clang-tidy",
    "quit": "return to the shell",
}
# Single-key shortcuts; h/j/k/l and q stay navigation and quit.
DASHBOARD_HOTKEYS = {
    "play": "p", "build": "b", "build_app": "a", "dist": "e", "test": "t", "launch": "r",
    "stats": "s", "tools": "o", "docs": "d", "coverage": "c", "tidy": "i",
    "quit": "q",
}
DASHBOARD_SETTING_HINTS = {
    "profile": "Cycles built-in and saved profiles; later changes become session overrides",
    "configuration": "Debug is the AddressSanitizer profile; play and measure with RelWithDebInfo",
    "application": "The package that Play and Run existing build launch in IllumoRuntime",
    "testing": "BUILD_TESTING: test runners, discovery and every registered CTest case",
    "documentation": "ILLUMO_BUILD_DOCUMENTATION: the PDFs, when latexmk is available",
    "tracy": "ILLUMO_ENABLE_TRACY: Tracy profiler instrumentation",
    "wasm": "ILLUMO_BUILD_WASM_RUNTIME: IllumoRuntime and every app package",
    "parallel": "Job limit passed to cmake --build --parallel",
}
ACTION_TITLES = {
    **{key: label for kind, label, key in DASHBOARD_ITEMS if kind == "action"},
    "file_stats": "Source file statistics",
    "doctor": "Toolchain doctor",
    "wasm_tools": "Fetch WASM toolchain",
    "watch": "Watch and rebuild",
}
# Actions that stream through the live progress view and leave a run record.
PROGRESS_ACTIONS = ("build", "build_app", "dist", "test", "coverage", "tidy", "docs", "wasm_tools")


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
    runner_prefixes: tuple[tuple[str, str], ...] = ()


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
        candidates: list[tuple[str, str]] = []
        for project in self.projects:
            candidates.extend(project.runner_prefixes)
            if project.test_runners:
                for prefix in project.test_prefixes:
                    candidates.append((prefix, project.test_runners[0]))
                candidates.append((f"{project.name}.", project.test_runners[0]))
        for runner in self.test_runners:
            candidates.append((runner.removesuffix("Tests") + ".", runner))
        best: tuple[int, str] | None = None
        for prefix, runner in candidates:
            if test_name.startswith(prefix):
                if best is None or len(prefix) > best[0]:
                    best = (len(prefix), runner)
        if best is not None:
            return best[1]
        valid_prefixes = sorted(
            {prefix for prefix, _runner in candidates}
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
                    or name_lower.startswith("cmake-build")
                ):
                    ordered_names.append(item.name)

    wasm_cmake = root / "cmake" / "IllumoWasm.cmake"
    programs = program_directories(root)
    wasm_runtime_defined = False
    if programs and wasm_cmake.is_file():
        try:
            wasm_text = wasm_cmake.read_text(encoding="utf-8", errors="replace")
        except OSError:
            wasm_text = ""
        wasm_runtime_defined = bool(
            re.search(rf"add_executable\s*\(\s*{WASM_RUNTIME_APPLICATION}\b", wasm_text)
        )

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

        executables: list[str] = []
        excluded_executables: set[str] = set()
        for match in re.finditer(
            r"add_executable\s*\(\s*([A-Za-z0-9_]+)(?:\s+([A-Za-z0-9_]+))?",
            cmake_text,
        ):
            exe = match.group(1)
            if exe not in executables:
                executables.append(exe)
            if match.group(2) == "EXCLUDE_FROM_ALL":
                excluded_executables.add(exe)

        discover_matches = re.findall(
            r"illumo_discover_test_runner\s*\(\s*([A-Za-z0-9_]+)(?:\s+([A-Za-z0-9_]+))?\)",
            cmake_text,
        )
        test_runners: list[str] = []
        discovery_targets: list[str] = []
        test_prefixes: list[str] = []
        runner_prefixes: list[tuple[str, str]] = []
        for runner_target, label in discover_matches:
            if runner_target not in test_runners:
                test_runners.append(runner_target)
                discovery_targets.append(f"{runner_target}Discover")
                prefix = f"{label}." if label else f"{name}."
                test_prefixes.append(prefix)
                runner_prefixes.append((prefix, runner_target))
                stem = runner_target.removesuffix("Tests")
                if f"{stem}." != prefix:
                    runner_prefixes.append((f"{stem}.", runner_target))
                # Sub-runners like IllumoPhysicsTests under project Illumo register
                # dotted cases such as Illumo.Physics.*; map that prefix too.
                if stem.startswith(name) and len(stem) > len(name):
                    sub = stem[len(name):]
                    runner_prefixes.append((f"{name}.{sub}.", runner_target))

        # Only fall back to *Tests executables when a project declares no
        # illumo_discover_test_runner(); real-GPU and EXCLUDE_FROM_ALL targets
        # (IllumoGpuTests, IllumoCaptureGpuTests, IllEdCloseWindowTests) do not
        # implement --list/--run and must not be swept into test_runners.
        if not discover_matches:
            for exe in executables:
                if exe.endswith("Tests") and exe not in excluded_executables and exe not in test_runners:
                    test_runners.append(exe)
                    prefix = f"{exe.removesuffix('Tests')}."
                    test_prefixes.append(prefix)
                    runner_prefixes.append((prefix, exe))

        smoke_targets: list[str] = []
        for exe in executables:
            if "Smoke" in exe and exe not in test_runners and exe not in excluded_executables:
                smoke_targets.append(exe)

        applications: list[str] = []
        for exe in executables:
            if (
                exe not in test_runners
                and exe not in smoke_targets
                and exe not in excluded_executables
                and not exe.endswith("Tests")
            ):
                applications.append(exe)

        # Programs ship as WASM packages; IllumoRuntime (defined by the root
        # WASM CMake module) is the executable that plays them, offered with
        # the first program.
        if programs and programs[0].name == name and wasm_runtime_defined:
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
                runner_prefixes=tuple(runner_prefixes),
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
    documentation_enabled: bool = field(default_factory=documentation_available)
    tracy_enabled: bool = False
    wasm_enabled: bool = WASM_HOST_SUPPORTED
    parallel_index: int = 0
    compiler: str = "default"
    optimization: str = "default"
    cpu: str = "default"
    warnings: str = "default"
    sanitizer: str = "default"
    status: str = "Ready"
    status_kind: str = "normal"
    applications: tuple[str, ...] = field(default_factory=tuple)
    # Cached AppPackage list populated by refresh_dashboard_context (None in tests/isolated state).
    installed_packages: tuple[AppPackage, ...] | None = None
    # The apps "Assemble distribution" ships; None means every installed one.
    distribution_apps: tuple[str, ...] | None = None
    profile_name: str | None = None
    profile_settings: dict = field(default_factory=dict)
    overrides: dict = field(default_factory=dict)
    profiles_file: Path = DEFAULT_PROFILES_FILE
    # Context refreshed on entry and after each action, never per keystroke.
    git_summary: str | None = None
    history: list[dict] | None = None
    last_action: str | None = None
    # Wall-clock "HH:MM" of the last status change, shown beside it.
    status_time: str | None = None
    # Animation clock (ticks) and the tick at which the status last changed.
    frame: int = 0
    status_frame: int | None = None
    # Active TrueColor visual theme key, transient row flash, full-console wave, and sidekick reactor.
    theme: str = field(default_factory=default_theme_name)
    flash_row: int | None = None
    flash_frame: int | None = None
    wave_frame: int | None = None
    reactor_mode: str = "torus"
    # (key, monotonic time, value) for the file-backed workspace status line.
    workspace_cache: tuple | None = None

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
        return app_label(self.application, self.installed_packages)

    @property
    def parallel_value(self) -> int | None:
        if "parallel" in self.overrides:
            return self.overrides["parallel"]
        if "parallel" in self.profile_settings:
            return self.profile_settings["parallel"]
        return DASHBOARD_PARALLEL_OPTIONS[self.parallel_index][1]

    @property
    def profile_label(self) -> str:
        name = self.profile_name or DASHBOARD_DEFAULT_PROFILE
        return f"{name} (edited)" if self.overrides else name


def refresh_dashboard_context(state: DashboardState) -> None:
    """Git position and recorded runs for the header and last-run badges."""
    branch = git_output(REPOSITORY_ROOT, ("rev-parse", "--abbrev-ref", "HEAD"))
    commit = git_output(REPOSITORY_ROOT, ("rev-parse", "--short=8", "HEAD"))
    if branch and commit:
        worktree = worktree_statistics(REPOSITORY_ROOT)
        changed = 0 if worktree is None else (
            worktree.staged + worktree.modified + worktree.untracked + worktree.conflicted
        )
        branch = branch.strip()
        state.git_summary = (
            f"{'detached HEAD' if branch == 'HEAD' else branch} @ {commit.strip()}"
            + (" | clean" if worktree is not None and not changed else "")
            + (f" | {changed} changed" if changed else "")
        )
    else:
        state.git_summary = None
    try:
        state.history = run_history()
    except OSError:
        state.history = []
    state.installed_packages = installed_apps()
    state.workspace_cache = None


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

    def read_event(
        self, text_mode: bool = False, timeout: float | None = None,
    ) -> str | DashboardMouseEvent | DashboardTextEvent:
        """The next input event, or "tick" once `timeout` seconds pass."""
        if self.windows_input is not None:
            return self.windows_input.read_event(text_mode=text_mode, timeout=timeout)
        return read_dashboard_key(text_mode=text_mode, timeout=timeout)


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
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.WaitForSingleObject.restype = wintypes.DWORD
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

    def read_event(
        self, text_mode: bool = False, timeout: float | None = None,
    ) -> str | DashboardMouseEvent | DashboardTextEvent:
        from ctypes import wintypes

        record = self.record_type()
        count = wintypes.DWORD()
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            if deadline is not None:
                # Ignored records (key releases) must not extend the wait.
                remaining = max(0, int((deadline - time.monotonic()) * 1000))
                if self.api.WaitForSingleObject(self.handle, remaining) == 0x102:  # WAIT_TIMEOUT
                    return "tick"
            if not self.api.ReadConsoleInputW(
                self.handle, self.ctypes.byref(record), 1, self.ctypes.byref(count)
            ):
                raise BuildError("Could not read console input.")
            if not count.value:
                continue
            if record.kind == 1 and record.event.key.down:
                key = record.event.key
                # Ignore bare modifier presses (Shift, Ctrl, Alt, Win, locks)
                # so holding Shift never interrupts animations or re-filters views.
                if key.character == "\x00" and key.key in (0x10, 0x11, 0x12, 0x14, 0x5B, 0x5C, 0x90, 0x91):
                    continue
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
                }.get(key.character, printable_key(key.character)))
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
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetStdHandle.argtypes = [wintypes.DWORD]
        kernel32.GetStdHandle.restype = wintypes.HANDLE
        kernel32.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel32.GetConsoleMode.restype = wintypes.BOOL
        kernel32.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel32.SetConsoleMode.restype = wintypes.BOOL
        output_handle = kernel32.GetStdHandle(0xFFFFFFF5)
        if not output_handle or output_handle == wintypes.HANDLE(-1).value:
            return
        mode = wintypes.DWORD()
        if kernel32.GetConsoleMode(output_handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(output_handle, mode.value | 0x0004)
    except (AttributeError, OSError):
        return


def dashboard_terminal_size(fallback: tuple[int, int] = (80, 24)) -> os.terminal_size:
    """Prefer a live TTY ioctl. Zero-sized answers fall back instead of
    rendering a 94-column menu into a narrow Ubuntu/Alacritty window."""
    for stream in (sys.stdout, sys.stdin, sys.stderr):
        try:
            size = os.get_terminal_size(stream.fileno())
        except (AttributeError, OSError, ValueError):
            continue
        if size.columns > 0 and size.lines > 0:
            return size
    return shutil.get_terminal_size(fallback)


def dashboard_visible_width(text: str) -> int:
    """Terminal cells used by text. ANSI is ignored. Wide and fullwidth
    glyphs count as two cells so a boxed row cannot wrap."""
    if text.isascii() and "\x1b" not in text:
        return len(text)
    width = 0
    for char in _ANSI_SEQUENCE.sub("", text):
        if unicodedata.combining(char):
            continue
        if unicodedata.east_asian_width(char) in ("W", "F"):
            width += 2
        else:
            width += 1
    return width


def write_in_place_frame(frame: str, suffix: str = "") -> None:
    """Overwrite the alternate screen from row 1 without clearing (no flicker)."""
    sys.stdout.write(
        ANSI_HOME + tui_newlines(frame.replace("\n", ANSI_ERASE_LINE + "\n"))
        + ANSI_ERASE_LINE + ANSI_ERASE_BELOW + suffix
    )
    sys.stdout.flush()


def tui_newlines(text: str) -> str:
    """Keep each TUI row at column 0 even if the tty is in raw/cbreak."""
    return text.replace("\r\n", "\n").replace("\n", "\r\n")


def dashboard_style(text: str, style: str, ansi: bool) -> str:
    if not ansi or not style:
        return text
    return f"{style}{text}{ANSI_RESET}"


def dashboard_value(state: DashboardState, key: str) -> str:
    if key == "profile":
        return state.profile_label
    if key == "configuration":
        return state.configuration
    if key == "application":
        return state.application_label
    if key == "testing":
        return "On" if state.testing_enabled else "Off"
    if key == "documentation":
        if not documentation_available() and not state.documentation_enabled:
            return "No docs tree"
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
    """One header line saying whether the selected tree can play its apps.
    Animation frames repaint often; the file checks refresh every second."""
    settings = dashboard_settings(state)
    key = (settings["build_dir"], settings["config"], state.wasm_enabled,
           tuple(settings.get("cmake_arg", [])), ok, bad)
    now = time.monotonic()
    if state.workspace_cache and state.workspace_cache[0] == key and now - state.workspace_cache[1] < 1.0:
        return state.workspace_cache[2]
    value = _dashboard_workspace_status(state, ok, bad)
    state.workspace_cache = (key, now, value)
    return value


def _dashboard_workspace_status(state: DashboardState, ok: str, bad: str) -> tuple[str, str]:
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


def gradient_segments(
    text: str, offset: float = 0.0, theme: DashboardTheme | None = None,
) -> list[tuple[str, str]]:
    """One styled run per glyph, sweeping the active TrueColor theme gradient."""
    palette = theme or resolve_theme()
    visible = sum(1 for character in text if not character.isspace())
    span = max(1, visible - 1)
    phase = (offset / 8.0) if offset else 0.0
    segments: list[tuple[str, str]] = []
    seen = 0
    for character in text:
        if character.isspace():
            segments.append((character, ""))
            continue
        color = theme_color(palette, seen / span, phase=phase)
        segments.append((character, f"{ANSI_BOLD}{rgb_fg(color)}"))
        seen += 1
    return segments


def render_segments(
    segments: Sequence[tuple[str, str]], width: int, ansi: bool,
    base: str = "", ellipsis: str = "...",
) -> str:
    """Lay styled runs into exactly `width` cells. Plain text is measured and
    truncated before styling, so a cut never splits an escape sequence."""
    if width < 1:
        return ""
    total = sum(dashboard_visible_width(text) for text, _style in segments)
    overflow = total > width
    if overflow and dashboard_visible_width(ellipsis) > width:
        ellipsis = "." * width
    limit = width - dashboard_visible_width(ellipsis) if overflow else width
    runs: list[tuple[str, str]] = []
    used = 0
    full = False
    for text, style in segments:
        kept: list[str] = []
        for character in text:
            cell = dashboard_visible_width(character)
            if used + cell > limit:
                full = True
                break
            kept.append(character)
            used += cell
        if kept:
            runs.append(("".join(kept), style))
        if full:
            break
    if overflow:
        runs.append((ellipsis, runs[-1][1] if runs else ""))
        used += dashboard_visible_width(ellipsis)
    if used < width:
        runs.append((" " * (width - used), ""))
    if not ansi:
        return "".join(text for text, _style in runs)
    return "".join(
        f"{base}{style}{text}{ANSI_RESET}" if base or style else text
        for text, style in runs
    )


def centered_segments(
    segments: Sequence[tuple[str, str]], width: int,
) -> list[tuple[str, str]]:
    used = sum(dashboard_visible_width(text) for text, _style in segments)
    if used >= width:
        return list(segments)
    return [(" " * ((width - used) // 2), ""), *segments]


def dashboard_value_style(key: str, value: str, state: DashboardState) -> str:
    if value == "On":
        return ANSI_GREEN
    if value in ("Off", "Windows x64 only", "No docs tree"):
        return ANSI_DIM
    if key == "profile":
        return ANSI_BOLD + (ANSI_YELLOW if state.overrides else ANSI_CYAN)
    if key == "application":
        return ANSI_BOLD + ANSI_VIOLET
    if key == "configuration":
        # Debug is the sanitizer build; the rest shade from fast to small.
        return ANSI_BOLD + {"Debug": ANSI_YELLOW, "Release": ANSI_GREEN,
                            "RelWithDebInfo": ANSI_CYAN}.get(value, ANSI_VIOLET)
    return ANSI_BOLD + ANSI_CYAN


def dashboard_value_text(value: str, glyphs: dict) -> str:
    """Switch-style On/Off: a filled or hollow dot before the word."""
    if value == "On":
        return f"{glyphs['bullet']} On"
    if value == "Off":
        return f"{glyphs['hollow']} Off"
    return value


def banner_segments(
    row: str,
    row_index: int = 0,
    frame: int | None = None,
    theme: DashboardTheme | None = None,
) -> list[tuple[str, str]]:
    """One banner row shaded in 24-bit TrueColor with a subtle 2D diagonal shimmer."""
    palette = theme or resolve_theme()
    width = max(1, len(BANNER_ROWS[0]) - 1)
    phase = (frame * 0.014) if (frame is not None and palette.prism) else 0.0
    result: list[tuple[str, str]] = []
    for column, character in enumerate(row):
        if character == " ":
            result.append((character, ""))
            continue
        t = column / width
        if frame is not None:
            wave = 0.08 * math.sin(frame * 0.14 + column * 0.28 - row_index * 0.85)
            shimmer = 0.16 * max(0.0, math.sin(frame * 0.11 - column * 0.22 + row_index * 0.7))
            rgb = boost_rgb(theme_color(palette, t + wave, phase=phase), shimmer)
        else:
            rgb = theme_color(palette, t + row_index * 0.04, phase=0.0)
        result.append((character, f"{ANSI_BOLD}{rgb_fg(rgb)}"))
    return result


def format_age(seconds: float) -> str:
    seconds = max(0.0, seconds)
    if seconds < 60:
        return "just now"
    for unit, size in (("d", 86400), ("h", 3600), ("m", 60)):
        if seconds >= size:
            return f"{int(seconds // size)}{unit} ago"
    return "just now"


def dashboard_run_badge(
    state: DashboardState, action: str, glyphs: dict, now: float | None = None,
    identity: dict | None = None,
) -> tuple[str, str] | None:
    """The latest recorded outcome of an action, for its menu row."""
    if not state.history or action not in PROGRESS_ACTIONS:
        return None
    target_identity = None
    if action in ("build", "build_app", "dist", "test"):
        if identity is not None:
            target_identity = identity
        else:
            try:
                target_identity = build_identity(dashboard_settings(state))
            except (BuildError, OSError):
                return None
    run = next((run for run in state.history if run.get("action") == action
                and (target_identity is None or run.get("identity") == target_identity)), None)
    if run is None:
        return None
    started = run.get("started", run.get("recorded_at"))
    age = format_age((time.time() if now is None else now) - started) if isinstance(
        started, (int, float)) else ""
    separator = f" {glyphs['separator']} " if age else ""
    elapsed = run.get("elapsed")
    if run.get("status") == "succeeded" and isinstance(elapsed, (int, float)):
        return f"{glyphs['ok']} {format_duration(elapsed)}{separator}{age}", ANSI_GREEN
    if run.get("status") == "failed":
        return f"{glyphs['bad']} failed{separator}{age}", ANSI_RED
    if run.get("status") == "cancelled":
        return f"{glyphs['info']} cancelled{separator}{age}", ANSI_DIM
    return None


def dashboard_selection_hint(state: DashboardState, index: int | None = None) -> str:
    """What a menu row does: a setting's meaning, an action's command."""
    target = state.selected if index is None else index
    kind, _label, key = DASHBOARD_ITEMS[target % len(DASHBOARD_ITEMS)]
    if kind == "setting":
        return DASHBOARD_SETTING_HINTS.get(key, "")
    if key == "tools":
        return "Compiler options, tests, diagnostics, trends, profiles, artifacts, watch and doctor"
    if key == "quit":
        return "Leave the build console"
    try:
        arguments = dashboard_action_arguments(state, key)
    except (BuildError, OSError):
        return ""
    return "$ " + format_command(["python", "build.py", *arguments])


def render_dashboard(
    state: DashboardState, terminal_width: int, ansi: bool = True,
    hit_regions: list[DashboardHitRegion] | None = None,
    mouse_enabled: bool = False,
    terminal_rows: int | None = None,
    frame: int | None = None,
    reveal: int | None = None,
) -> str:
    """The menu. `frame` animates it (None renders the still image);
    `reveal` shows only the first rows, for the opening unroll."""
    # Never emit a row as wide as the TTY: the cursor wrap-around on the last
    # column splits labels and descriptions into overlapping columns.
    usable = max(20, terminal_width - 1)
    width = min(100, usable)
    inner_width = max(1, width - 2)
    lines: list[str] = []
    if hit_regions is not None:
        hit_regions.clear()
    glyphs = terminal_glyphs()
    ellipsis = glyphs["ellipsis"]
    theme = resolve_theme(state.theme)
    accent_style = ANSI_BOLD + rgb_fg(theme.accent)
    tall_banner = (
        terminal_rows is not None and terminal_rows >= BANNER_MIN_ROWS
        and glyphs is UNICODE_GLYPHS and inner_width >= 40
    )
    keys_pairs = (
        (glyphs["up_down"], " move  "),
        (glyphs["left_right"], " change  "),
        ("Enter", " run  "),
        ("/", " find  "),
        ("g", " theme  "),
        ("f", " flair  "),
        ("?", " help  "),
        ("q", " quit"),
    )
    keys_help = "".join(k + v for k, v in keys_pairs)
    mouse_help = "Mouse: click, wheel"
    combined_help = f"{keys_help} | {mouse_help}"
    split_mouse = mouse_enabled and dashboard_visible_width(combined_help) + 1 > inner_width
    # Exact row count so the orbiting TrueColor perimeter glow closes seamlessly.
    total_rows = 1 + (4 if tall_banner else 1) + 2 + 2 + len(DASHBOARD_ITEMS) + 1 + (2 if split_mouse else 1) + 2
    perimeter = 2 * (width - 1) + 2 * max(1, total_rows - 1)
    wave_pos: float | None = None
    if frame is not None and state.wave_frame is not None:
        age = frame - state.wave_frame
        if 0 <= age < 16:
            wave_pos = -0.15 + (age / 15.0) * 1.55
    base_glass = scale_rgb(theme.selected_right, 0.22)

    def _side_rails(row_y: int, scanline: bool = False) -> tuple[str, str]:
        if not ansi:
            return glyphs["vertical"], glyphs["vertical"]
        if scanline:
            beam = rgb_fg(boost_rgb(theme.accent, 0.5))
            v = f"{rgb_bg(scale_rgb(theme.accent, 0.35))}{beam}{glyphs['vertical']}{ANSI_RESET}"
            return v, v
        left_rgb = perimeter_glow_rgb(theme, perimeter - row_y, perimeter, frame)
        right_rgb = perimeter_glow_rgb(theme, (width - 1) + row_y, perimeter, frame)
        left_bg = ambient_bg_rgb(theme, 0, row_y, width, total_rows, frame, wave_pos=wave_pos)
        right_bg = ambient_bg_rgb(theme, width - 1, row_y, width, total_rows, frame, wave_pos=wave_pos)
        return (
            f"{rgb_bg(left_bg)}{rgb_fg(left_rgb)}{glyphs['vertical']}{ANSI_RESET}",
            f"{rgb_bg(right_bg)}{rgb_fg(right_rgb)}{glyphs['vertical']}{ANSI_RESET}",
        )

    def border(
        left: str, right: str, title: str = "", title_style: str = "",
        *, outer: str = "", title_segments: Sequence[tuple[str, str]] | None = None,
    ) -> None:
        # Section titles sit in the rule itself, so the whole console still
        # fits a default 30-row terminal (taller output disables the mouse).
        label: Sequence[tuple[str, str]] = ()
        if inner_width >= 12:
            if title_segments is not None:
                label = title_segments
            elif title:
                label = [(title, title_style)]
        row_y = len(lines)
        scanline = ansi and reveal is not None and 0 < reveal < total_rows and row_y == reveal - 1
        row_bg = ambient_bg_rgb(theme, width // 2, row_y, width, total_rows, frame, wave_pos=wave_pos) if ansi else None
        if scanline:
            beam_rgb = boost_rgb(theme.accent, 0.45)
            lines.append(_horizontal_border_segments(
                left, right, glyphs["horizontal"], width, label, glyphs, ansi,
                theme=theme, frame=None, start_pos=0, step=1, perimeter=perimeter,
                override_base=beam_rgb, bg_rgb=scale_rgb(theme.accent, 0.32),
            ))
            return
        if outer == "top":
            lines.append(_horizontal_border_segments(
                left, right, glyphs["horizontal"], width, label, glyphs, ansi,
                theme=theme, frame=frame, start_pos=0, step=1, perimeter=perimeter,
                bg_rgb=row_bg,
            ))
        elif outer == "bottom":
            bottom_start = (width - 1) + (total_rows - 1) + (width - 1)
            lines.append(_horizontal_border_segments(
                left, right, glyphs["horizontal"], width, label, glyphs, ansi,
                theme=theme, frame=frame, start_pos=bottom_start, step=-1, perimeter=perimeter,
                bg_rgb=row_bg,
            ))
        else:
            # Inner dividers: subtly shaded with the theme accent on the rails.
            lines.append(_horizontal_border_segments(
                left, right, glyphs["horizontal"], width, label, glyphs, ansi,
                theme=theme, frame=None, start_pos=row_y * 2, step=0.6, perimeter=perimeter,
                override_base=scale_rgb(theme.frame_base, 0.92), bg_rgb=row_bg,
            ))

    def row(
        segments: Sequence[tuple[str, str]],
        selected: bool = False,
        flash: float = 0.0,
        header_glow: float = 0.0,
    ) -> None:
        row_y = len(lines)
        scanline = ansi and reveal is not None and 0 < reveal < total_rows and row_y == reveal - 1
        left_rail, right_rail = _side_rails(row_y, scanline=scanline)
        if scanline:
            body = render_gradient_row(
                segments, inner_width, ansi,
                scale_rgb(theme.accent, 0.38), scale_rgb(theme.frame_glow, 0.22),
                boost_rgb(theme.selected_fg, 0.4), ellipsis,
            )
        elif selected:
            shimmer_x = ((frame * 2.2) % (inner_width + 24)) - 12 if frame is not None else None
            body = render_gradient_row(
                segments, inner_width, ansi,
                theme.selected_left, theme.selected_right, theme.selected_fg,
                ellipsis, flash=flash, flash_rgb=theme.accent,
                shimmer_x=shimmer_x, shimmer_rgb=boost_rgb(theme.accent, 0.45),
            )
        elif flash > 0.0 and ansi:
            body = render_gradient_row(
                segments, inner_width, ansi,
                scale_rgb(theme.accent, 0.4 * flash), scale_rgb(theme.frame_glow, 0.18 * flash),
                theme.selected_fg, ellipsis,
            )
        elif ansi:
            body = render_gradient_row(
                segments, inner_width, True,
                base_glass, base_glass, theme.selected_fg, ellipsis,
                ambient_fn=lambda col, _y=row_y, _g=header_glow: ambient_bg_rgb(
                    theme, col + 1, _y, width, total_rows, frame,
                    header_glow=_g, wave_pos=wave_pos,
                ),
            )
        else:
            body = render_segments(segments, inner_width, False, "", ellipsis)
        lines.append(left_rail + body + right_rail)

    border(glyphs["top_left"], glyphs["top_right"], outer="top")
    if tall_banner:
        band = None if frame is None else shine_band(frame, len(BANNER_ROWS[0]) + len(BANNER_ROWS))
        for index, banner_row in enumerate(BANNER_ROWS):
            # Offsetting each row by its index tilts the sheen diagonally.
            row(centered_segments(
                apply_shine(banner_segments(banner_row, index, frame=frame, theme=theme), band, index),
                inner_width,
            ), header_glow=0.95 if index == 1 else 0.72)
        row(centered_segments([("workspace build console", ANSI_DIM)], inner_width), header_glow=0.35)
    else:
        band = None if frame is None else shine_band(frame, 11, speed=1.0)
        word_offset = (frame * 0.12) if (frame is not None and theme.prism) else (
            0.35 * math.sin(frame * 0.12) if frame is not None else 0.0
        )
        wordmark = apply_shine(gradient_segments("I L L U M O", offset=word_offset, theme=theme), band)
        subtitle = f"  {glyphs['separator']}  workspace build console"
        if 11 + dashboard_visible_width(subtitle) <= inner_width:
            wordmark.append((subtitle, ANSI_DIM))
        row(centered_segments(wordmark, inner_width), header_glow=0.85)
    settings = dashboard_settings(state)
    try:
        current_identity = build_identity(settings) if state.history else None
    except (BuildError, OSError):
        current_identity = None
    compiler_name = (
        "default compiler" if settings.get("compiler", "default") == "default"
        else choice_label(COMPILER_CHOICES, settings.get("compiler", "default"))
    )
    context = [f"tree {settings['build_dir']} ({settings['config']}, {compiler_name})"]
    if state.git_summary:
        context.insert(0, state.git_summary)
    row(centered_segments([(f" {glyphs['separator']} ".join(context), ANSI_DIM)], inner_width))
    workspace_status, workspace_style = dashboard_workspace_status(
        state, glyphs["ok"], glyphs["bad"]
    )
    row(centered_segments([(workspace_status, workspace_style)], inner_width))

    last_kind = None
    hotkey_rgb = lerp_rgb(theme.accent, theme.banner_stops[-1], 0.45)
    hotkey_style = rgb_fg(hotkey_rgb)
    hotkey_selected_style = ANSI_BOLD + rgb_fg(boost_rgb(theme.accent, 0.35))
    for index, (kind, label, key) in enumerate(DASHBOARD_ITEMS):
        if kind != last_kind:
            section = "Settings" if kind == "setting" else "Actions"
            section_segs = gradient_segments(
                section, offset=(frame * 0.08 if frame is not None and theme.prism else 0.0), theme=theme
            )
            border(
                glyphs["middle_left"], glyphs["middle_right"],
                section, ANSI_BOLD + ANSI_BLUE, title_segments=section_segs,
            )
            last_kind = kind
        selected = index == state.selected
        hotkey = DASHBOARD_HOTKEYS.get(key, " ") if kind == "action" else " "
        if frame is None:
            marker_rgb = theme.accent
        else:
            pulse_t = (frame * 0.055) % 1.0
            marker_rgb = boost_rgb(sample_gradient(theme.pulse_stops, pulse_t, wrap=True), 0.12)
        marker_style = ANSI_BOLD + rgb_fg(marker_rgb)
        prefix = [
            (f" {glyphs['marker'] if selected else ' '} ", marker_style),
            (f"{hotkey} ", (hotkey_selected_style if selected else hotkey_style) if kind == "action" else ""),
            (label, ANSI_BOLD if selected else ""),
        ]
        prefix_width = sum(dashboard_visible_width(text) for text, _style in prefix)
        segments = list(prefix)
        previous_x = None
        if kind == "setting":
            value = dashboard_value(state, key)
            chevron_style = rgb_fg(boost_rgb(theme.accent, 0.25)) if selected else ANSI_DIM
            suffix = [
                (f"{glyphs['left']} ", chevron_style),
                (dashboard_value_text(value, glyphs), dashboard_value_style(key, value, state)),
                (f" {glyphs['right']} ", chevron_style),
            ]
            gap = inner_width - prefix_width - sum(
                dashboard_visible_width(text) for text, _style in suffix
            )
            if gap >= 1:
                segments += [(" " * gap, ""), *suffix]
                previous_x = prefix_width + gap + 2
        else:
            if key in ("play", "launch", "build_app", "dist") and not state.wasm_enabled:
                description = "needs the WASM runtime setting"
            elif key == "play":
                description = f"build, then run {state.application_label}"
            elif key == "launch":
                description = f"run the built {state.application_label}"
            else:
                description = DASHBOARD_DESCRIPTIONS[key]
            badge = dashboard_run_badge(state, key, glyphs, identity=current_identity)
            choices = []
            if badge is not None:
                choices.append([(description, ANSI_DIM), (f"  {badge[0]} ", badge[1])])
            choices.append([(f"{description} ", ANSI_DIM)])
            for suffix in choices:
                gap = inner_width - prefix_width - sum(
                    dashboard_visible_width(text) for text, _style in suffix
                )
                if gap >= 2:
                    segments += [(" " * gap, ""), *suffix]
                    break
        if hit_regions is not None:
            hit_regions.append(DashboardHitRegion(index, len(lines) + 1, width, previous_x))
        flash = 0.0
        if (frame is not None and state.flash_row == index
                and state.flash_frame is not None and 0 <= frame - state.flash_frame < 6):
            flash = 1.0 - (frame - state.flash_frame) / 6.0
        row(segments, selected=selected, flash=flash)

    border(glyphs["middle_left"], glyphs["middle_right"])
    help_segments: list[tuple[str, str]] = [(" ", "")]
    for keycap, desc in keys_pairs:
        help_segments.append((keycap, accent_style))
        help_segments.append((desc, ANSI_DIM))
    if mouse_enabled and not split_mouse:
        help_segments.append((f" | {mouse_help}", ANSI_DIM))
        row(help_segments)
    else:
        row(help_segments)
        if mouse_enabled:
            row([(" Click select | Right-click previous | Wheel navigate", ANSI_DIM)])
    icon, status_style = {
        "success": (glyphs["ok"], ANSI_GREEN),
        "failure": (glyphs["bad"], ANSI_YELLOW),
    }.get(state.status_kind, (glyphs["bullet"], rgb_fg(theme.accent)))
    status = [(f" {icon} ", status_style), (state.status, status_style)]
    if frame is not None and state.status_frame is not None:
        # One sheen across a status that just changed.
        band = shine_band(frame - state.status_frame, len(state.status) + 3,
                          speed=3.0, band=6, period=1 << 30)
        if band is not None and frame >= state.status_frame:
            status = [status[0], *apply_shine(
                [(character, status_style) for character in state.status], band)]
    if state.status_time:
        stamp = f"{state.status_time} "
        used = sum(dashboard_visible_width(text) for text, _style in status)
        if used + dashboard_visible_width(stamp) + 2 <= inner_width:
            status += [(" " * (inner_width - used - dashboard_visible_width(stamp)), ""),
                       (stamp, ANSI_DIM)]
    row(status)
    border(
        glyphs["bottom_left"], glyphs["bottom_right"],
        dashboard_selection_hint(state), ANSI_DIM, outer="bottom",
    )
    if reveal is not None:
        lines = [line if index < reveal else "" for index, line in enumerate(lines)]
    return "\n".join(lines)


def printable_key(character: str) -> str:
    """Unmapped printable keys reach the menus as hotkeys (key:p)."""
    return f"key:{character}" if len(character) == 1 and character.isprintable() else "unknown"


def posix_escape_to_key(sequence: str, text_mode: bool) -> str:
    """Map bytes after ESC. Alternate-screen terminals send OA/OB for arrows
    instead of [A/[B; unknown CSI must not quit the dashboard."""
    if not sequence:
        return "escape" if text_mode else "quit"
    final = sequence[-1]
    if final in "ABCD":
        return {"A": "up", "B": "down", "C": "right", "D": "left"}[final]
    if final in "HF":
        return "home" if final == "H" else "end"
    editing = {"[1~": "home", "[7~": "home", "[4~": "end", "[8~": "end",
               "[5~": "page_up", "[6~": "page_down"}
    if sequence in editing:
        return editing[sequence]
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


def read_dashboard_key(
    text_mode: bool = False, timeout: float | None = None,
) -> str | DashboardTextEvent:
    if os.name == "nt":
        import msvcrt

        if timeout is not None:
            deadline = time.monotonic() + timeout
            while not msvcrt.kbhit():
                if time.monotonic() >= deadline:
                    return "tick"
                time.sleep(0.01)
        character = msvcrt.getwch()
        if character in ("\x00", "\xe0"):
            return {
                "H": "up",
                "P": "down",
                "K": "left",
                "M": "right",
                "G": "home",
                "O": "end",
                "I": "page_up",
                "Q": "page_down",
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
        }.get(character, printable_key(character))

    file_descriptor = sys.stdin.fileno()
    # Without a caller timeout, poll so window resizes still repaint.
    first = _posix_read_byte(file_descriptor, 0.25 if timeout is None else timeout)
    if first is None:
        return "resize" if timeout is None else "tick"
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
    }.get(character, printable_key(character))


def cycle_dashboard_profile(state: DashboardState, direction: int) -> None:
    """Step through Default, the built-ins and saved profiles; overrides clear."""
    profiles = load_profiles(state.profiles_file, allow_missing=True)
    names = [DASHBOARD_DEFAULT_PROFILE, *profiles]
    current = state.profile_name if state.profile_name in profiles else DASHBOARD_DEFAULT_PROFILE
    name = names[(names.index(current) + direction) % len(names)]
    if name == DASHBOARD_DEFAULT_PROFILE:
        apply_dashboard_profile(state, None, {})
    else:
        apply_dashboard_profile(state, name, profiles[name])


def cycle_dashboard_theme(state: DashboardState, direction: int = 1) -> DashboardTheme:
    """Step through the built-in 24-bit TrueColor themes with a full-console wave."""
    current = state.theme if state.theme in DASHBOARD_THEMES else THEME_ORDER[0]
    next_key = THEME_ORDER[(THEME_ORDER.index(current) + direction) % len(THEME_ORDER)]
    state.theme = next_key
    state.wave_frame = state.frame
    theme = DASHBOARD_THEMES[next_key]
    state.status, state.status_kind = f"Theme: {theme.name} — {theme.tagline}", "normal"
    return theme


def adjust_dashboard_setting(state: DashboardState, direction: int) -> None:
    key = DASHBOARD_ITEMS[state.selected][2]
    if key == "profile":
        cycle_dashboard_profile(state, direction)
        state.flash_row, state.flash_frame = state.selected, state.frame
        return
    if key == "configuration":
        state.configuration_index = (
            state.configuration_index + direction
        ) % len(DASHBOARD_CONFIGURATIONS)
    elif key == "application":
        apps = state.applications if state.applications else (DEFAULT_APP,)
        state.application_index = (state.application_index + direction) % len(apps)
    elif key == "testing":
        state.testing_enabled = not state.testing_enabled
    elif key == "documentation" and (documentation_available() or state.documentation_enabled):
        state.documentation_enabled = not state.documentation_enabled
    elif key == "tracy":
        state.tracy_enabled = not state.tracy_enabled
    elif key == "wasm" and WASM_HOST_SUPPORTED:
        state.wasm_enabled = not state.wasm_enabled
    elif key == "parallel":
        current_parallel = state.parallel_value
        known = [value for _label, value in DASHBOARD_PARALLEL_OPTIONS]
        if current_parallel in known:
            state.parallel_index = (known.index(current_parallel) + direction) % len(known)
        elif isinstance(current_parallel, int) and current_parallel > 0:
            # Step from a custom profile job count to the adjacent preset instead of Off.
            numeric = [(index, value) for index, value in enumerate(known) if isinstance(value, int) and value > 0]
            if direction > 0:
                state.parallel_index = next((i for i, v in numeric if v > current_parallel), 0)
            else:
                state.parallel_index = next((i for i, v in reversed(numeric) if v < current_parallel), 1)
        else:
            state.parallel_index = (state.parallel_index + direction) % len(known)
    else:
        return
    state.flash_row, state.flash_frame = state.selected, state.frame
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
        "compiler": state.compiler, "optimization": state.optimization,
        "cpu": state.cpu, "warnings": state.warnings, "sanitizer": state.sanitizer,
        "tracy": state.tracy_enabled, "no_tests": not state.testing_enabled,
        "no_docs": not state.documentation_enabled, "no_tidy": False,
        "no_wasm": not state.wasm_enabled,
        "parallel": state.parallel_value, "cmake_arg": [],
    }
    settings.update(state.profile_settings)
    settings.update(state.overrides)
    compiler = settings.get("compiler") or "default"
    settings["compiler"] = compiler
    settings["optimization"] = settings.get("optimization") or "default"
    settings["cpu"] = settings.get("cpu") or "default"
    settings["warnings"] = settings.get("warnings") or "default"
    settings["sanitizer"] = settings.get("sanitizer") or "default"
    omit_unsupported_settings(settings)
    settings["build_dir"] = str(assigned_build_directory(settings["build_dir"], compiler))
    generator = assigned_generator(settings.get("generator"), compiler)
    if generator:
        settings["generator"] = generator
    elif not settings.get("generator"):
        settings.pop("generator", None)
    return settings


def apply_dashboard_profile(state: DashboardState, name: str | None, settings: dict) -> None:
    validate_profile(name or "default", settings)
    state.profile_name = name
    state.profile_settings = dict(settings)
    state.overrides.clear()
    state.configuration_index = DASHBOARD_CONFIGURATIONS.index(settings.get("config", "Release"))
    state.testing_enabled = not settings.get("no_tests", False)
    state.documentation_enabled = not settings.get("no_docs", not documentation_available())
    state.tracy_enabled = settings.get("tracy", False)
    state.wasm_enabled = not settings.get("no_wasm", not WASM_HOST_SUPPORTED)
    state.parallel_index = next((i for i, item in enumerate(DASHBOARD_PARALLEL_OPTIONS)
                                 if item[1] == settings.get("parallel", 0)), 0)
    state.compiler = settings.get("compiler", "default")
    state.optimization = settings.get("optimization", "default")
    state.cpu = settings.get("cpu", "default")
    state.warnings = settings.get("warnings", "default")
    state.sanitizer = settings.get("sanitizer", "default")


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
    packages = state.installed_packages if state.installed_packages is not None else installed_apps()
    packaged = state.application in {app.name for app in packages}
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
    if action == "dist":
        chosen = state.distribution_apps
        subset = chosen is not None and set(chosen) != set(state.applications)
        return ["dist", *dashboard_common_arguments(state),
                *(["--apps", ",".join(chosen)] if subset and chosen else [])]
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
    if action == "doctor":
        return ["doctor", *dashboard_common_arguments(state)]
    if action == "wasm_tools":
        return ["wasm-tools"]
    if action == "watch":
        return ["watch", *dashboard_common_arguments(state)]
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


WARNING_LINE = re.compile(
    r"\bwarning\b\s*(?:[A-Z]+\d+\s*)?:|^\s*CMake Warning(?: \(dev\))? (?:at|in)\b", re.IGNORECASE,
)
ERROR_LINE = re.compile(
    r"\b(?:fatal error|error)\b\s*(?:[A-Z]+\d+\s*)?:|^\s*CMake Error (?:at|in)\b", re.IGNORECASE,
)


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
    # Duration of the last successful run of the same action and build tree.
    previous_elapsed: float | None = None
    # Phase timeline: [name, started, ended or None while current].
    phases: list[list] = field(default_factory=list)
    first_errors: list[str] = field(default_factory=list)
    # (time, completed) for the current phase's reported total; drives the
    # throughput sparkline.
    samples: deque[tuple[float, int]] = field(default_factory=lambda: deque(maxlen=512))
    theme: str = field(default_factory=default_theme_name)

    def __post_init__(self) -> None:
        self.phase_started = self.started

    def set_phase(self, name: str, now: float, *, reset: bool = False) -> None:
        if name != self.phase or reset:
            if name != self.phase or not self.phases:
                if self.phases and self.phases[-1][2] is None:
                    self.phases[-1][2] = now
                self.phases.append([name, now, None])
            self.phase = name
            self.phase_started = now
            self.completed = self.total = 0
            self.unit = ""
            self.samples.clear()
            if name == "Testing":
                self.tests_completed = self.tests_total = 0

    def consume(self, line: str, now: float) -> None:
        line = progress_text(line).rstrip()
        if not line:
            return
        self.lines.append(line[:8192])
        if WARNING_LINE.search(line):
            self.warnings += 1
        if ERROR_LINE.search(line):
            self.errors += 1
            if len(self.first_errors) < 5:
                self.first_errors.append(line[:512])
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
        if test or ninja:
            self.samples.append((now, self.completed))

    def finish(self, now: float) -> None:
        self.finished = now
        if self.phases and self.phases[-1][2] is None:
            self.phases[-1][2] = now


def progress_bar(
    fraction: float, width: int, glyphs: dict, style: str = "",
    shine: float | None = None, theme: DashboardTheme | None = None,
) -> list[tuple[str, str]]:
    """Half-cell resolution; the filled part sweeps the active TrueColor gradient
    unless styled. `shine` lights the filled cells around that position."""
    fraction = min(1.0, max(0.0, fraction))
    full, half = divmod(int(round(fraction * width * 2)), 2)
    palette = theme or resolve_theme()
    span = max(1, width - 1)
    segments: list[tuple[str, str]] = []
    for index in range(full + half):
        color = style or rgb_fg(theme_color(palette, index / span))
        if shine is not None and abs(index - shine) < 1.5:
            color = ANSI_SHINE
        segments.append((glyphs["bar_fill"] if index < full else glyphs["bar_half"], color))
    segments.append((glyphs["bar_track"] * max(0, width - full - half), ANSI_TRACK))
    return segments


def sparkline(values: Sequence[float], glyphs: dict,
              low: float | None = None, high: float | None = None) -> str:
    """One eighth-height bar per value, scaled between low and high."""
    if not values:
        return ""
    sparks = glyphs["sparks"]
    low = min(values) if low is None else low
    high = max(values) if high is None else high
    span = high - low
    if span <= 0:
        return sparks[0 if high <= 0 else len(sparks) // 2] * len(values)
    top = len(sparks) - 1
    return "".join(sparks[max(0, min(top, int((value - low) / span * top + 0.5)))] for value in values)


def sparkline_segments(
    values: Sequence[float], glyphs: dict,
    low: float | None = None, high: float | None = None,
    theme: DashboardTheme | None = None,
) -> list[tuple[str, str]]:
    """Sparkline with per-cell 24-bit TrueColor heat shading by bar height."""
    text = sparkline(values, glyphs, low=low, high=high)
    if not text:
        return []
    palette = theme or resolve_theme()
    sparks = glyphs["sparks"]
    top = max(1, len(sparks) - 1)
    result: list[tuple[str, str]] = []
    for glyph in text:
        level = sparks.index(glyph) / top if glyph in sparks else 0.5
        rgb = boost_rgb(theme_color(palette, level), 0.2 * max(0.0, level - 0.6))
        result.append((glyph, rgb_fg(rgb)))
    return result


def progress_throughput(progress: DashboardProgress, now: float, window: int = 24) -> list[float]:
    """Completions per second over the last `window` seconds of the phase."""
    samples = list(progress.samples)
    if len(samples) < 2:
        return []
    start = max(samples[0][0], now - window)
    rates: list[float] = []
    index, completed = 0, samples[0][1]
    while index < len(samples) and samples[index][0] <= start:
        completed = samples[index][1]
        index += 1
    edge = start
    while edge < now:
        previous = completed
        edge = min(now, edge + 1)
        while index < len(samples) and samples[index][0] <= edge:
            completed = samples[index][1]
            index += 1
        rates.append(max(0, completed - previous))
    return rates


def compare_with_last_run(elapsed: float, previous: float) -> str:
    change = (elapsed - previous) / previous * 100 if previous > 0 else 0.0
    if abs(change) < 2:
        return f"about the same as the last run ({format_duration(previous)})"
    return (f"{abs(change):.0f}% {'faster' if change < 0 else 'slower'} than the last run "
            f"({format_duration(previous)})")


def activity_bar(elapsed: float, width: int, glyphs: dict) -> list[tuple[str, str]]:
    """A streak sweeping back and forth while a tool reports no total; its
    bright head leads and its tail fades behind it."""
    size = max(2, width // 4)
    travel = max(1, width - size)
    step = int(elapsed * 12) % (2 * travel)
    forward = step <= travel
    start = step if forward else 2 * travel - step
    ramp = [COMET_COLORS[round(index * (len(COMET_COLORS) - 1) / (size - 1))] for index in range(size)]
    if not forward:
        ramp.reverse()
    return [
        (glyphs["bar_track"] * start, ANSI_TRACK),
        *((glyphs["bar_fill"], f"\x1b[38;5;{color}m") for color in ramp),
        (glyphs["bar_track"] * max(0, width - start - size), ANSI_TRACK),
    ]


def progress_eta(progress: DashboardProgress, now: float) -> str:
    """Remaining time from the current phase's reported rate."""
    if progress.unit not in ("steps", "tests") or not 0 < progress.completed < progress.total:
        return ""
    spent = now - progress.phase_started
    if spent < 3:
        return ""
    remaining = spent * (progress.total - progress.completed) / progress.completed
    return f"ETA ~{format_duration(remaining)}"


PROGRESS_BADGES = {
    "RUNNING": "\x1b[1;48;5;25;38;5;231m",
    "SUCCEEDED": "\x1b[1;48;5;28;38;5;231m",
    "FAILED": "\x1b[1;48;5;160;38;5;231m",
    "CANCELLED": "\x1b[1;48;5;136;38;5;16m",
}
# Brighter badges alternated with the normal ones as an action finishes.
PROGRESS_BADGE_FLASHES = {
    "SUCCEEDED": "\x1b[1;48;5;46;38;5;16m",
    "FAILED": "\x1b[1;48;5;196;38;5;231m",
    "CANCELLED": "\x1b[1;48;5;220;38;5;16m",
}
PROGRESS_FINALE_SECONDS = 0.9


def frame_rule(
    left: str, right: str, label: Sequence[tuple[str, str]], width: int,
    glyphs: dict, ansi: bool,
) -> str:
    """A border row exactly `width` cells wide, with an optional label that
    sits in the rule ("╭─ label ───╮") and is trimmed to fit."""
    inner = max(0, width - 2)
    fill = glyphs["horizontal"]
    label_width = sum(dashboard_visible_width(text) for text, _style in label)
    if label and inner >= 6:
        used = min(label_width, inner - 3)
        middle = (dashboard_style(fill + " ", ANSI_FRAME, ansi)
                  + render_segments(label, used, ansi, ellipsis=glyphs["ellipsis"])
                  + dashboard_style(" " + fill * (inner - 3 - used), ANSI_FRAME, ansi))
    else:
        middle = dashboard_style(fill * inner, ANSI_FRAME, ansi)
    return dashboard_style(left, ANSI_FRAME, ansi) + middle + dashboard_style(right, ANSI_FRAME, ansi)


def frame_line(
    segments: Sequence[tuple[str, str]], width: int, glyphs: dict, ansi: bool,
    base: str = "",
) -> str:
    """One boxed content row: a border, a one-cell margin, content, margin."""
    edge = dashboard_style(glyphs["vertical"], ANSI_FRAME, ansi)
    content = render_segments(segments, max(1, width - 4), ansi, base, glyphs["ellipsis"])
    margin = dashboard_style(" ", base, ansi) if base else " "
    return edge + margin + content + margin + edge


def progress_line_style(line: str) -> str:
    if line.startswith("> "):
        return ANSI_VIOLET
    if ERROR_LINE.search(line) or re.search(r"\*\*\*Failed|\bFAILED\b", line):
        return ANSI_RED
    if WARNING_LINE.search(line):
        return ANSI_YELLOW
    if re.search(r"Test\s+#\d+:.*\bPassed\b|\b100% tests passed\b", line):
        return ANSI_GREEN
    return ""


def render_dashboard_progress(
    progress: DashboardProgress, columns: int, rows: int,
    now: float | None = None, ansi: bool = True,
) -> str:
    now = time.monotonic() if now is None else now
    end = progress.finished if progress.finished is not None else now
    elapsed = max(0.0, end - progress.started)
    width = max(1, min(110, columns - 1))
    height = max(1, rows - 2)
    glyphs = terminal_glyphs()
    separator = glyphs["separator"]
    running = progress.returncode is None
    succeeded = not running and progress.returncode == 0 and not progress.cancelled
    status = "RUNNING" if running else (
        "CANCELLED" if progress.cancelled else "SUCCEEDED" if progress.returncode == 0 else "FAILED"
    )
    if running:
        frames = glyphs["spinner"]
        icon, tone = frames[int(elapsed * 10) % len(frames)], ANSI_CYAN
    elif succeeded:
        icon, tone = glyphs["ok"], ANSI_GREEN
    elif progress.cancelled:
        icon, tone = glyphs["info"], ANSI_YELLOW
    else:
        icon, tone = glyphs["bad"], ANSI_RED
    try:
        log_label = str(progress.log_path.relative_to(REPOSITORY_ROOT))
    except ValueError:
        log_label = str(progress.log_path)
    phase_elapsed = max(0.0, end - progress.phase_started)
    # Framed like the menu when there is room; tiny terminals get bare lines.
    framed = width >= 24 and height >= 8
    content_width = width - 4 if framed else width
    bar_width = max(4, min(32, content_width - 44))

    theme = resolve_theme(progress.theme)
    # Motion: the title's colors flow and a sheen runs along the bar while
    # running; the result badge flashes for a moment after finishing.
    moving = ansi and motion_enabled()
    finale = (moving and not running and progress.finished is not None
              and 0 <= now - progress.finished < PROGRESS_FINALE_SECONDS)
    count = f"{progress.completed}/{progress.total} {progress.unit}"
    if progress.unit == "%":
        count = f"{progress.completed}%"
    if not running:
        # A result bar: full and green on success, red where a failure stopped.
        reached = progress.completed / progress.total if progress.total else 0.0
        summary = f"Action finished in {format_duration(elapsed)}"
        if progress.total:
            summary += f" {separator} {count}"
        tool_line = [(summary, tone)]
        if succeeded or progress.total:
            sweep = (now - progress.finished) * bar_width * 2.5 if finale else None
            tool_line = [*progress_bar(1.0 if succeeded else reached, bar_width, glyphs,
                                       ANSI_GREEN if succeeded else ANSI_RED, shine=sweep, theme=theme),
                         (" ", ""), *tool_line]
        if succeeded and progress.previous_elapsed is not None and progress.previous_elapsed > 0:
            tool_line.append((f"  {compare_with_last_run(elapsed, progress.previous_elapsed)}", ANSI_DIM))
    elif progress.total > 0:
        fraction = min(1.0, max(0.0, progress.completed / progress.total))
        eta = progress_eta(progress, end)
        filled = fraction * bar_width
        shine = (elapsed * 16) % (filled + 12) - 3 if moving and running else None
        tool_line = [*progress_bar(fraction, bar_width, glyphs, shine=shine, theme=theme),
                     (f" {count} (tool-reported)", "")]
        if eta:
            tool_line.append((f"  {eta}", ANSI_DIM))
    elif running and progress.previous_elapsed is not None and progress.previous_elapsed > 0:
        fraction = min(0.99, elapsed / progress.previous_elapsed)
        tool_line = [
            *progress_bar(fraction, bar_width, glyphs, ANSI_BLUE, theme=theme),
            (f" ~{int(fraction * 100)}% of the last run ({format_duration(progress.previous_elapsed)}); "
             "this tool has not reported a total", ANSI_DIM),
        ]
    else:
        tool_line = [*activity_bar(elapsed, bar_width, glyphs),
                     (" Working; this tool has not reported a total", ANSI_DIM)]
    rates = progress_throughput(progress, end) if running and progress.unit in ("steps", "tests") else []
    throughput_line = None
    if len(rates) >= 3:
        first_time, first_count = progress.samples[0]
        last_time, last_count = progress.samples[-1]
        average = (last_count - first_count) / max(0.001, last_time - first_time)
        throughput_line = [("Rate:   ", ANSI_DIM), *sparkline_segments(rates, glyphs, low=0, theme=theme),
                           (f"  {average:.1f} {progress.unit}/s", ANSI_DIM)]

    if progress.phases:
        chips = []
        for index, (name, started, ended) in enumerate(progress.phases):
            current = index == len(progress.phases) - 1
            if current and running:
                mark, style = icon, ANSI_BOLD + ANSI_CYAN
            elif current and not succeeded:
                mark, style = (glyphs["info"], ANSI_YELLOW) if progress.cancelled else (glyphs["bad"], ANSI_RED)
            else:
                mark, style = glyphs["ok"], ANSI_GREEN
            phase_end = end if ended is None else ended
            chips.append((f"{mark} {name} {format_duration(phase_end - started)}", style))
        joint = f" {glyphs['chevron']} "
        available = content_width - dashboard_visible_width("Phases: ")
        dropped = False
        while len(chips) > 1 and sum(
            dashboard_visible_width(text) for text, _style in chips
        ) + dashboard_visible_width(joint) * (len(chips) - 1 + dropped) > available:
            chips.pop(0)
            dropped = True
        phase_line = [("Phases: ", ANSI_DIM)]
        if dropped:
            phase_line.append((glyphs["ellipsis"] + joint, ANSI_DIM))
        for index, chip in enumerate(chips):
            if index:
                phase_line.append((joint, ANSI_DIM))
            phase_line.append(chip)
    else:
        phase_line = [("Phase: ", ANSI_DIM), (progress.phase, ANSI_BOLD)]

    badge = PROGRESS_BADGES[status]
    if finale and int((now - progress.finished) / 0.15) % 2 == 0:
        badge = PROGRESS_BADGE_FLASHES.get(status, badge)
    status_line = [(f" {status} ", badge),
                   (f"  Elapsed {elapsed:.1f}s   Phase {phase_elapsed:.1f}s", "")]
    if progress.previous_elapsed is not None and progress.previous_elapsed > 0:
        status_line.append((f"   Last run {format_duration(progress.previous_elapsed)}", ANSI_DIM))
    counts = [
        (f"{glyphs['warn']} Warning lines: {progress.warnings}",
         ANSI_YELLOW if progress.warnings else ANSI_DIM),
        ("   ", ""),
        (f"{glyphs['bad']} Error lines: {progress.errors}", ANSI_RED if progress.errors else ANSI_DIM),
    ]
    if progress.tests_total:
        counts.append((f"   Last test run: {progress.tests_completed}/{progress.tests_total}", ""))
    title = [
        (f"{icon} ", tone),
        *gradient_segments("ILLUMO", elapsed * 6 if moving and running else 0, theme=theme),
        (f" {glyphs['chevron']} {progress_text(progress.title)}", ANSI_BOLD),
    ]
    info: list[list[tuple[str, str]]] = [
        [(progress_text(progress.context), ANSI_DIM)],
        status_line,
        phase_line,
        tool_line,
        *([throughput_line] if throughput_line else []),
        counts,
        [("$ ", rgb_fg(theme.accent)), (progress_text(progress.command), "")],
        [("Full log: ", ANSI_DIM), (log_label, "")],
    ]
    errors: list[list[tuple[str, str]]] = []
    if not running and not succeeded and not progress.cancelled and progress.first_errors:
        errors = [[(line, ANSI_RED)] for line in progress.first_errors[:3]]
    if running:
        footer = [("Ctrl+C cancels this action", ANSI_DIM)]
    else:
        footer = [(f"Exit code: {progress.returncode} {separator} ", ANSI_DIM),
                  ("Enter", ANSI_BOLD), (f" back {separator} ", ANSI_DIM),
                  ("d", ANSI_BOLD + rgb_fg(theme.accent)), (f" diagnostics {separator} ", ANSI_DIM),
                  ("l", ANSI_BOLD + rgb_fg(theme.accent)), (" full log", ANSI_DIM)]
    ellipsis = glyphs["ellipsis"]

    if framed:
        # Keep the footer visible even on short terminals by trimming body rows first.
        chrome = 1 + len(info) + (1 + len(errors) if errors else 0) + 2
        tail_count = max(0, height - chrome)
        tail = list(progress.lines)[-tail_count:] if tail_count else []
        total_box_rows = min(height, chrome + len(tail))
        perimeter = 2 * (width - 1) + 2 * max(1, total_box_rows - 1)
        if running and moving:
            anim_frame: int | None = int(elapsed * 14)
            frame_base, frame_glow = None, None
        elif succeeded:
            anim_frame = int((now - (progress.finished or now)) * 28) if finale else None
            frame_base, frame_glow = (46, 152, 92), (255, 240, 120)
        elif progress.cancelled:
            anim_frame = None
            frame_base, frame_glow = (176, 132, 48), (255, 224, 96)
        else:
            anim_frame = int((now - (progress.finished or now)) * 22) if finale else None
            frame_base, frame_glow = (172, 52, 64), (255, 118, 128)

        def p_line(segments: Sequence[tuple[str, str]], row_y: int) -> str:
            if not ansi:
                return frame_line(segments, width, glyphs, False)
            left_rgb = perimeter_glow_rgb(
                theme, perimeter - row_y, perimeter, anim_frame,
                override_base=frame_base, override_glow=frame_glow,
            )
            right_rgb = perimeter_glow_rgb(
                theme, (width - 1) + row_y, perimeter, anim_frame,
                override_base=frame_base, override_glow=frame_glow,
            )
            content = render_segments(segments, max(1, width - 4), True, "", ellipsis)
            return (
                f"{rgb_fg(left_rgb)}{glyphs['vertical']}{ANSI_RESET} "
                f"{content} "
                f"{rgb_fg(right_rgb)}{glyphs['vertical']}{ANSI_RESET}"
            )

        lines = [_horizontal_border_segments(
            glyphs["top_left"], glyphs["top_right"], glyphs["horizontal"], width, title, glyphs, ansi,
            theme=theme, frame=anim_frame, start_pos=0, step=1, perimeter=perimeter,
            override_base=frame_base, override_glow=frame_glow,
        )]
        for segments in info:
            lines.append(p_line(segments, len(lines)))
        if errors:
            lines.append(_horizontal_border_segments(
                glyphs["middle_left"], glyphs["middle_right"], glyphs["horizontal"], width,
                [("First errors", ANSI_BOLD + ANSI_RED)], glyphs, ansi,
                theme=theme, frame=None, start_pos=0, step=0.6, perimeter=perimeter,
                override_base=frame_base or theme.frame_base,
            ))
            for segments in errors:
                lines.append(p_line(segments, len(lines)))
        lines.append(_horizontal_border_segments(
            glyphs["middle_left"], glyphs["middle_right"], glyphs["horizontal"], width,
            gradient_segments("Output", theme=theme), glyphs, ansi,
            theme=theme, frame=None, start_pos=0, step=0.6, perimeter=perimeter,
            override_base=frame_base or theme.frame_base,
        ))
        for line in tail:
            lines.append(p_line([(line, progress_line_style(line))], len(lines)))
        if finale and succeeded and ansi and glyphs is UNICODE_GLYPHS:
            spare = min(4, max(0, height - len(lines) - 1))
            if spare > 0 and content_width >= 20:
                t_fin = max(0.0, now - (progress.finished or now))
                sparks = "✦✧⋆✴·"
                centers = (content_width * 0.22, content_width * 0.5, content_width * 0.78)
                for r_i in range(spare):
                    row_segs: list[tuple[str, str]] = []
                    for c_i in range(content_width):
                        ch, col_rgb = " ", None
                        for b_idx, cx in enumerate(centers):
                            radius = (t_fin * 22.0 + b_idx * 4.5) % 14.0
                            dx = (c_i - cx) * 0.55
                            dy = (r_i - spare * 0.5) * 1.4
                            dist = math.sqrt(dx * dx + dy * dy)
                            if abs(dist - radius) < 0.75 and ((c_i * 7 + r_i * 13 + int(t_fin * 12) + b_idx) % 5 == 0):
                                ch = sparks[(c_i + r_i + b_idx) % len(sparks)]
                                col_rgb = boost_rgb(theme_color(theme, (b_idx * 0.33 + t_fin * 0.6) % 1.0), 0.45)
                                break
                        row_segs.append((ch, ANSI_BOLD + rgb_fg(col_rgb) if col_rgb else ""))
                    lines.append(p_line(row_segs, len(lines)))
        bottom_start = (width - 1) + max(1, total_box_rows - 1) + (width - 1)
        footer_line = _horizontal_border_segments(
            glyphs["bottom_left"], glyphs["bottom_right"], glyphs["horizontal"], width, footer, glyphs, ansi,
            theme=theme, frame=anim_frame, start_pos=bottom_start, step=-1, perimeter=perimeter,
            override_base=frame_base, override_glow=frame_glow,
        )
        if len(lines) + 1 > height:
            lines = lines[:max(0, height - 1)]
        lines.append(footer_line)
        return "\n".join(lines[:height])

    rule = [(glyphs["horizontal"] * width, ANSI_FRAME)]
    heading = [title, *info, rule]
    if errors:
        heading += [[("First errors", ANSI_BOLD + ANSI_RED)], *errors, rule]
    tail_count = max(0, height - len(heading) - 2)
    tail = list(progress.lines)[-tail_count:] if tail_count else []
    body = heading + [[(line, progress_line_style(line))] for line in tail]
    if len(body) + 2 <= height:
        body.extend((rule, footer))
    else:
        body = body[:max(0, height - 1)]
        body.append(footer)
    return "\n".join(
        render_segments(segments, width, ansi, ellipsis=ellipsis) for segments in body[:height]
    )


def supports_taskbar_progress() -> bool:
    """OSC 9;4 drives the taskbar ring in Windows Terminal and ConEmu; other
    terminals (iTerm2) read OSC 9 as a desktop notification instead."""
    return bool(os.environ.get("WT_SESSION") or os.environ.get("ConEmuPID"))


def terminal_progress_signals(progress: DashboardProgress | None) -> str:
    """Window title and taskbar progress for the running action; None clears."""
    if progress is None:
        clear = "\x1b]9;4;0;0\x07" if supports_taskbar_progress() else ""
        return "\x1b]2;Illumo build console\x07" + clear
    running = progress.returncode is None
    percent = None
    if progress.total > 0:
        percent = min(100, max(0, int(100 * progress.completed / progress.total)))
    if running:
        detail = f"{progress.phase}" + (f" {percent}%" if percent is not None else "")
    elif progress.cancelled:
        detail = "cancelled"
    else:
        detail = "succeeded" if progress.returncode == 0 else "failed"
    signals = f"\x1b]2;{progress_text(f'Illumo | {progress.title} | {detail}')}\x07"
    if supports_taskbar_progress():
        if running:
            state = f"1;{percent}" if percent is not None else "3;0"
        else:
            state = "1;100" if progress.returncode == 0 else "4;100" if progress.cancelled else "2;100"
        signals += f"\x1b]9;4;{state}\x07"
    return signals


def play_progress_finale(progress: DashboardProgress) -> None:
    """Repaint briefly after an action ends so its result badge flashes."""
    if not motion_enabled() or progress.finished is None:
        return
    while time.monotonic() - progress.finished < PROGRESS_FINALE_SECONDS:
        paint_dashboard_progress(progress)
        time.sleep(0.05)
    paint_dashboard_progress(progress)


def paint_dashboard_progress(progress: DashboardProgress) -> None:
    # Home and overwrite instead of clearing, so 10 Hz repaints never flicker.
    size = dashboard_terminal_size((96, 30))
    frame = render_dashboard_progress(progress, size.columns, size.lines)
    write_in_place_frame(frame, terminal_progress_signals(progress))


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
                    # Keep the start of an overlong line so paths and "error:" survive truncation.
                    pending = parts.pop()[:8192]
                    for line in parts:
                        progress.consume(line, time.monotonic())
                    if not chunk and code is not None:
                        if pending:
                            progress.consume(pending, time.monotonic())
                        progress.returncode = 130 if progress.cancelled else code
                        break
                    now = time.monotonic()
                    # About 16 frames a second keeps the spinner, sheen and
                    # streak smooth; output is still read between frames.
                    if now - last_paint >= 0.06:
                        paint_dashboard_progress(progress)
                        last_paint = now
                    if not chunk:
                        time.sleep(0.05)
                except KeyboardInterrupt:
                    progress.cancelled = True
                    try:
                        stop_dashboard_process(process, job)
                    except KeyboardInterrupt:
                        pass
    except KeyboardInterrupt:
        progress.cancelled = True
        progress.returncode = 130
        if process is not None and process.poll() is None:
            try:
                stop_dashboard_process(process, job)
            except (OSError, BuildError, subprocess.SubprocessError, KeyboardInterrupt):
                pass
    except (OSError, BuildError, subprocess.SubprocessError) as error:
        progress.consume(f"error: {error}", time.monotonic())
        progress.returncode = 130 if progress.cancelled else 1
    finally:
        if job is not None:
            try:
                job.close()
            except (OSError, BuildError, KeyboardInterrupt):
                pass
        if process is not None and process.poll() is None:
            try:
                if os.name == "nt":
                    process.terminate()  # Only an unassigned bootstrap can remain.
                    process.wait(timeout=5)
                else:
                    stop_dashboard_process(process)
            except (OSError, BuildError, subprocess.SubprocessError, KeyboardInterrupt):
                pass
        progress.finish(time.monotonic())
    paint_dashboard_progress(progress)


def read_action_choice() -> str:
    """One key after an action: a letter picks a follow-up, anything else
    returns. Keys typed while the action ran are discarded first."""
    sys.stdout.write(ANSI_SHOW_CURSOR)
    sys.stdout.flush()
    try:
        if not sys.stdin.isatty():
            return input().strip().lower()[:1]
        if os.name == "nt":
            import msvcrt

            while msvcrt.kbhit():
                msvcrt.getwch()
            character = msvcrt.getwch()
            if character in ("\x00", "\xe0"):
                msvcrt.getwch()  # Arrow and function keys arrive as two reads.
                return ""
        else:
            import termios
            import tty

            descriptor = sys.stdin.fileno()
            original = termios.tcgetattr(descriptor)
            try:
                tty.setcbreak(descriptor)
                termios.tcflush(descriptor, termios.TCIFLUSH)
                data = os.read(descriptor, 1)
            finally:
                termios.tcsetattr(descriptor, termios.TCSADRAIN, original)
            character = data.decode("latin1") if data else ""
    except (EOFError, KeyboardInterrupt, OSError):
        return ""
    return character.lower() if character.isalpha() else ""


def previous_success_elapsed(action: str, identity: dict) -> float | None:
    """How long the last successful run of this action on this tree took."""
    try:
        history = run_history()
    except OSError:
        return None
    for run in history:
        elapsed = run.get("elapsed")
        if (run.get("action") == action and run.get("identity") == identity
                and run.get("status") == "succeeded" and isinstance(elapsed, (int, float))):
            return float(elapsed)
    return None


def ring_completion_bell(progress: DashboardProgress) -> None:
    """Long actions ring the bell; Windows Terminal flashes the taskbar."""
    finished = progress.started if progress.finished is None else progress.finished
    if finished - progress.started >= 10 and sys.stdout.isatty():
        sys.stdout.write("\a")


def dashboard_follow_up(
    choice: str, log_path: Path, record: dict, state: DashboardState,
    terminal: DashboardTerminal,
) -> None:
    """Open the finished action's diagnostics (d) or raw log (l)."""
    try:
        if choice == "d":
            browse_run_diagnostics({**record, "log": str(log_path)}, terminal)
        elif choice == "l":
            lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
            show_toolbox_text("Raw log (authoritative): " + log_path.name, lines, terminal)
    except OSError as error:
        state.status = f"Could not open {log_path.name}: {error}"
        state.status_kind = "failure"


def run_progress_session(
    state: DashboardState,
    terminal: DashboardTerminal,
    *,
    action: str,
    title: str,
    log_prefix: str,
    command: list[str],
    context_extra: str | None = None,
    log_path: Path | None = None,
) -> None:
    """Run one command inside the full-screen live progress view and record its run."""
    choice, record, progress = "", None, None
    terminal.leave()
    try:
        if log_path is None:
            log_directory = REPOSITORY_ROOT / "build-orchestrator-logs"
            log_directory.mkdir(exist_ok=True)
            with tempfile.NamedTemporaryFile(
                prefix=f"{log_prefix}-", suffix=".log", dir=log_directory, delete=False,
            ) as log:
                log_path = Path(log.name)
        record = new_run_record(state, action, command)
        identity = record["identity"]
        extra = context_extra if context_extra is not None else f"Parallel: {dashboard_parallel_label(state)}"
        progress = DashboardProgress(
            title,
            f"{identity['configuration']} | {identity['build_directory']} | {extra}",
            log_path,
            theme=state.theme,
        )
        progress.previous_elapsed = previous_success_elapsed(action, identity)
        write_json_atomic(log_path.with_suffix(".json"), record)
        sys.stdout.write(ANSI_ENTER_SCREEN + ANSI_HIDE_CURSOR)
        sys.stdout.flush()
        try:
            run_dashboard_progress(command, progress)
            finish_run_record(progress, record)
            finished = time.monotonic() if progress.finished is None else progress.finished
            elapsed = finished - progress.started
            outcome = "cancelled" if progress.cancelled else "succeeded" if progress.returncode == 0 else "failed"
            state.status = f"{progress.title} {outcome} ({elapsed:.1f}s)"
            state.status_kind = "success" if progress.returncode == 0 else "failure"
            ring_completion_bell(progress)
            play_progress_finale(progress)
            choice = read_action_choice()
        finally:
            sys.stdout.write(
                ANSI_RESET + ANSI_SHOW_CURSOR + ANSI_LEAVE_SCREEN
                + terminal_progress_signals(None)
            )
            sys.stdout.flush()
    except OSError as error:
        state.status = f"Could not start progress display: {error}"
        state.status_kind = "failure"
    finally:
        terminal.enter()
    if record is not None and progress is not None:
        dashboard_follow_up(choice, progress.log_path, record, state, terminal)


def execute_dashboard_action(
    state: DashboardState,
    action: str,
    terminal: DashboardTerminal,
) -> bool:
    """Execute one dashboard action. Returns False when a submenu or picker cancelled."""
    if action == "tools":
        run_development_tools(state, terminal)
        return False
    if action in ("build_app", "dist") and not state.wasm_enabled:
        raise BuildError(
            "This action requires the WASM runtime setting; turn 'WASM runtime + apps' On first."
        )
    if action == "dist" and not run_distribution_picker(state, terminal):
        return False
    if action == "test":
        execute_toolbox_run(state, terminal, None, True)
        return True
    title = ACTION_TITLES.get(action, action)
    arguments = dashboard_action_arguments(state, action)
    command = [sys.executable, str(Path(__file__).resolve()), *arguments]
    if action in PROGRESS_ACTIONS:
        run_progress_session(
            state, terminal, action=action, title=title,
            log_prefix=action, command=command,
        )
        return True
    terminal.leave()
    glyphs = terminal_glyphs()
    ansi = sys.stdout.isatty()
    heavy = glyphs["heavy"]
    print()
    print(dashboard_style(f"{heavy * 2} {title} ", ANSI_BOLD + ANSI_CYAN, ansi)
          + dashboard_style(heavy * 40, ANSI_FRAME, ansi))
    print(dashboard_style(f"> {format_command(command)}", ANSI_DIM, ansi) + "\n", flush=True)
    started = time.monotonic()
    try:
        process = subprocess.Popen(command, cwd=REPOSITORY_ROOT)
        interrupted = False
        while True:
            try:
                returncode = process.wait()
                break
            except KeyboardInterrupt:
                # The child shares the console and receives Ctrl+C itself
                # (watch stops, a launch closes); keep the console open.
                interrupted = True
        elapsed = time.monotonic() - started
        if returncode == 0:
            state.status = f"{title} {'stopped' if interrupted else 'succeeded'} ({elapsed:.1f}s)"
            state.status_kind = "success"
        else:
            state.status = f"{title} failed (exit {returncode})"
            state.status_kind = "failure"
    except OSError as error:
        state.status = f"Could not start action: {error}"
        state.status_kind = "failure"
    mark, style = (glyphs["ok"], ANSI_GREEN) if state.status_kind == "success" else (glyphs["bad"], ANSI_RED)
    print("\n" + dashboard_style(f"{mark} {state.status}", ANSI_BOLD + style, ansi))
    print(dashboard_style("Press any key to return to the build console...", ANSI_DIM, ansi),
          end="", flush=True)
    read_action_choice()
    terminal.enter()
    return True


def write_json_atomic(path: Path, value: dict) -> None:
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=path.name + ".", suffix=".tmp", delete=False,
        ) as output:
            temporary = output.name
            json.dump(value, output, indent=2, ensure_ascii=True)
            output.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary is not None and os.path.exists(temporary):
            os.unlink(temporary)


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
    finished = time.monotonic() if progress.finished is None else progress.finished
    record.update(status="cancelled" if progress.cancelled else
                  "succeeded" if progress.returncode == 0 else "failed",
                  exit_code=progress.returncode,
                  elapsed=finished - progress.started)
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
    apply_compiler_selection(arguments)
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
    otherwise be stale or missing when CTest starts. CTest lists a test whose
    executable does not exist yet with an empty command, so its target cannot
    be named; a fresh tree then builds the default target once, which also
    stages the app packages the distribution and pack scripts read.
    """
    built = [*workspace.discovery_targets, *workspace.smoke_targets]
    # Build discovery/smoke targets in one invocation so --clean-first runs at most once
    # and never wipes earlier test binaries before later ones compile.
    if built:
        runner.run(build_command(arguments, cmake, built))
    if runner.dry_run:
        return
    build_directory = resolve_build_directory(arguments.build_dir)
    built_names = set(built) | {target.removesuffix("Discover") for target in built}
    extra: list[str] = []
    for test in ctest_listing(existing_tool("ctest", False), build_directory, arguments.config):
        if not test.get("command"):
            runner.run(build_command(arguments, cmake, [], clean=False if built else None))
            return
        target = ctest_executable_target(test, build_directory)
        if target and target not in built_names and target not in extra:
            extra.append(target)
    if extra:
        runner.run(build_command(arguments, cmake, extra, clean=False if built else None))


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
    wanted = set(names)
    found = {}
    for node in root.findall(".//Testing/Test"):
        name = node.findtext("Name")
        if name not in wanted:
            continue
        measurements = {item.get("name"): item.findtext("Value", "")
                        for item in node.findall("Results/NamedMeasurement")}
        raw = node.get("Status", "notrun")
        status = {"passed": "passed", "failed": "failed"}.get(raw, "incomplete")
        completion = measurements.get("Completion Status", raw)
        if completion == "Disabled":
            status = "disabled"
        elif completion == "Skipped":
            status = "skipped"
        try:
            duration = float(measurements.get("Execution Time", "0"))
        except ValueError:
            duration = None
        found[name] = {"name": name, "status": status, "duration": duration,
                       "completion": completion}
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
    parsed = create_parser().parse_args(["configure", *profile_arguments(settings)])
    apply_compiler_selection(parsed)
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
    result["complete"] = all(test["status"] in ("passed", "failed", "disabled", "skipped") for test in result["tests"])
    write_json_atomic(result_path, result)
    if any(codes) or not result["complete"]:
        raise BuildError("Test run failed or has incomplete results. Inspect the recorded log and test results.")


def execute_toolbox_run(state: DashboardState, terminal: DashboardTerminal,
                        names: list[str] | None, build_first: bool, refresh: bool = False) -> None:
    if names == [] and not refresh:
        raise BuildError("No tests selected; nothing was run.")
    directory = REPOSITORY_ROOT / "build-orchestrator-logs"
    try:
        directory.mkdir(exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix="tests-", suffix=".log", dir=directory, delete=False) as log:
            path = Path(log.name)
        request = path.with_suffix(".request.json")
        write_json_atomic(request, {"version": 1, "settings": dashboard_settings(state), "names": names,
                                   "build_first": build_first, "mode": "refresh" if refresh else "run",
                                   "result": str(path.with_suffix(".tests.json"))})
    except OSError as error:
        state.status = f"Could not start test run: {error}"
        state.status_kind = "failure"
        return
    command = [sys.executable, str(Path(__file__).resolve()), "_toolbox", str(request)]
    run_progress_session(
        state, terminal,
        action="inventory" if refresh else "test",
        title="Refresh Inventory" if refresh else "Run tests",
        log_prefix="tests",
        command=command,
        context_extra="Prepare and run" if build_first else "Run Existing",
        log_path=path,
    )


@dataclass
class ToolboxRow:
    key: str
    label: str
    details: tuple[str, ...] = ()
    action: bool = False
    # Color only: "ok", "bad", "warn" or "dim"; the text carries the meaning.
    tone: str = ""
    # Optional styled runs drawn in place of the label; their text must
    # spell the label so search and tests still see it.
    segments: tuple[tuple[str, str], ...] = ()


TOOLBOX_TONES = {"ok": ANSI_GREEN, "bad": ANSI_RED, "warn": ANSI_YELLOW, "dim": ANSI_DIM}


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
                   width: int, height: int, status: str = "",
                   ansi: bool = False, help_text: str | None = None,
                   prompt: str | None = None, caret: str = "",
                   detail_provider=None,
                   theme_name: str | None = None) -> tuple[str, list[DashboardHitRegion], int]:
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
    glyphs = terminal_glyphs()
    ellipsis = glyphs["ellipsis"]
    theme = resolve_theme(theme_name)
    # Boxed like the menu: title in the top border, the list count in a
    # divider and the status message in the bottom border. Very narrow
    # terminals fall back to bare lines with the same row positions.
    framed = width >= 20
    content_width = width - 4 if framed else width
    box_rows = min(max(1, height - 1), 12 + page)
    perimeter = 2 * (width - 1) + 2 * max(1, box_rows - 1)

    def rule(left: str, right: str, label: list[tuple[str, str]], outer: str = "") -> str:
        if not framed:
            return render_segments(label, width, ansi, ellipsis=ellipsis).rstrip()
        if not ansi:
            return frame_rule(left, right, label, width, glyphs, False)
        if outer == "top":
            return _horizontal_border_segments(
                left, right, glyphs["horizontal"], width, label, glyphs, True,
                theme=theme, frame=None, start_pos=0, step=1, perimeter=perimeter,
            )
        if outer == "bottom":
            bottom_start = (width - 1) + max(1, box_rows - 1) + (width - 1)
            return _horizontal_border_segments(
                left, right, glyphs["horizontal"], width, label, glyphs, True,
                theme=theme, frame=None, start_pos=bottom_start, step=-1, perimeter=perimeter,
            )
        return _horizontal_border_segments(
            left, right, glyphs["horizontal"], width, label, glyphs, True,
            theme=theme, frame=None, start_pos=0, step=0.6, perimeter=perimeter,
            override_base=scale_rgb(theme.frame_base, 0.92),
        )

    def line(segments: list[tuple[str, str]], row_y: int) -> str:
        if not framed:
            return render_segments(segments, width, ansi, ellipsis=ellipsis).rstrip()
        if not ansi:
            return frame_line(segments, width, glyphs, False)
        left_rgb = perimeter_glow_rgb(theme, perimeter - row_y, perimeter, None)
        right_rgb = perimeter_glow_rgb(theme, (width - 1) + row_y, perimeter, None)
        content = render_segments(segments, max(1, width - 4), True, "", ellipsis)
        return (
            f"{rgb_fg(left_rgb)}{glyphs['vertical']}{ANSI_RESET} "
            f"{content} "
            f"{rgb_fg(right_rgb)}{glyphs['vertical']}{ANSI_RESET}"
        )

    search = (
        [(prompt or "Search editing (Enter applies, Escape cancels): ", ANSI_YELLOW),
         (progress_text(view.draft), ANSI_BOLD), (caret, rgb_fg(theme.accent))]
        if view.draft is not None else
        [("Search: ", ANSI_DIM), (progress_text(view.search) or "(none)",
                                  ANSI_BOLD if view.search else ANSI_DIM)]
    )
    lines = [
        rule(glyphs["top_left"], glyphs["top_right"],
             [*gradient_segments("ILLUMO", theme=theme), (f" {glyphs['chevron']} ", ANSI_DIM),
              (progress_text(title), ANSI_BOLD)], outer="top"),
        line([(help_text or "Arrows / hover: select | Enter / click action: execute | /: search | q/Esc: back",
               ANSI_DIM)], 1),
        line(search, 2),
        rule(glyphs["middle_left"], glyphs["middle_right"], []),
    ]
    regions = []
    visible = min(page, max(0, len(rows) - view.top))
    # A proportional scrollbar in the last content column once rows overflow.
    scrollbar = len(rows) > page and width >= 30
    thumb_size = max(1, page * page // len(rows)) if scrollbar else 0
    thumb_top = (view.top * (page - thumb_size) // max(1, len(rows) - page)) if scrollbar else 0
    label_width = content_width - 2 if scrollbar else content_width
    marker_style = ANSI_BOLD + rgb_fg(theme.accent)
    thumb_style = rgb_fg(theme.accent)
    for offset in range(page):
        segments: list[tuple[str, str]] = []
        chosen = False
        index = view.top + offset
        if offset < visible:
            row = rows[index]
            chosen = index == view.selected
            segments = [
                (f"{glyphs['marker']} " if chosen else "  ", marker_style),
                ("[Action] " if row.action else "", ANSI_VIOLET),
                *(row.segments or [(
                    progress_text(row.label),
                    ANSI_CYAN if row.action and not row.tone else TOOLBOX_TONES.get(row.tone, ""),
                )]),
            ]
        if chosen and ansi:
            text = render_gradient_row(
                segments, label_width, True,
                theme.selected_left, theme.selected_right, theme.selected_fg, ellipsis,
            )
        else:
            text = render_segments(segments, label_width, ansi, ANSI_SELECTED if chosen else "", ellipsis)
        if scrollbar:
            on_thumb = thumb_top <= offset < thumb_top + thumb_size
            text += " " + dashboard_style(
                glyphs["thumb"] if on_thumb else glyphs["rail"],
                thumb_style if on_thumb else ANSI_TRACK, ansi,
            )
        if framed:
            row_y = len(lines)
            if ansi:
                left_rgb = perimeter_glow_rgb(theme, perimeter - row_y, perimeter, None)
                right_rgb = perimeter_glow_rgb(theme, (width - 1) + row_y, perimeter, None)
                left_edge = f"{rgb_fg(left_rgb)}{glyphs['vertical']}{ANSI_RESET}"
                right_edge = f"{rgb_fg(right_rgb)}{glyphs['vertical']}{ANSI_RESET}"
                margin = f"{rgb_bg(theme.selected_left)} {ANSI_RESET}" if chosen else " "
                text = left_edge + margin + text + " " + right_edge
            else:
                text = glyphs["vertical"] + " " + text + " " + glyphs["vertical"]
        lines.append(text)
        if offset < visible:
            regions.append(DashboardHitRegion(index, len(lines), width, 1))
    counter = f"Rows {view.top + 1 if rows else 0}-{min(len(rows), view.top + page)} of {len(rows)}"
    lines.append(rule(glyphs["middle_left"], glyphs["middle_right"], [(counter, ANSI_DIM)]))
    if selected:
        details = detail_provider(selected) if detail_provider is not None else selected.details
    else:
        details = ("Select an item to inspect its details.",)
    for number, detail in enumerate(list(details[:6]) + [""] * max(0, 6 - len(details))):
        lines.append(line([(progress_text(detail), ANSI_BOLD if number == 0 else ANSI_DIM)], len(lines)))
    lines.append(rule(glyphs["bottom_left"], glyphs["bottom_right"],
                      [(progress_text(status), ANSI_YELLOW)] if status else [], outer="bottom"))
    rendered = "\n".join(lines[:max(1, height - 1)])
    return rendered, regions if width >= 30 and height >= 12 else [], page


def choose_toolbox(title: str, rows: list[ToolboxRow], view: ToolboxView,
                   terminal: DashboardTerminal, status: str = "",
                   detail_provider=None) -> str:
    if not any(row.key == view.focused and not row.action for row in rows):
        view.focused = None
    last = None
    while True:
        size = dashboard_terminal_size((100, 32))
        rendered, regions, page = render_toolbox(
            title, rows, view, size.columns, size.lines, status, ansi=True,
            detail_provider=detail_provider,
        )
        if rendered != last:
            sys.stdout.write(ANSI_CLEAR + tui_newlines(rendered))
            sys.stdout.flush()
            last = rendered
        event = terminal.read_event(text_mode=view.draft is not None)
        if isinstance(event, DashboardMouseEvent) and dashboard_terminal_size((100, 32)) != size:
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
        width = max(20, dashboard_terminal_size((100, 32)).columns - 4)
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
            tone = {"passed": "ok", "failed": "bad", "disabled": "dim"}.get(
                result.get("status"), "warn" if result else "")
            rows.append(ToolboxRow(test["name"], test["name"] + suffix, test_details(test), tone=tone))
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


DIAGNOSTIC_PATTERNS = (
    ("compiler", re.compile(r"^(?P<path>.+?)\((?P<line>\d+)(?:,(?P<column>\d+))?\)\s*:\s*(?P<severity>(?:fatal )?error|warning)\b[: ]*(?P<message>.*)", re.I)),
    ("compiler", re.compile(r"^(?P<path>.+?):(?P<line>\d+)(?::(?P<column>\d+))?:\s*(?P<severity>(?:fatal )?error|warning)\s*:\s*(?P<message>.*)", re.I)),
    ("cmake", re.compile(r"^CMake (?P<severity>Error|Warning)(?: \([^)]*\))? at (?P<path>.+?):(?P<line>\d+)(?: \([^)]*\))?:\s*(?P<message>.*)", re.I)),
)
MSBUILD_SUMMARY_LINE = re.compile(r"^\d+\s+(?:Warning|Error)\(s\)$", re.I)
CTEST_PASSED_LINE = re.compile(r"Test\s+#\d+:.*\bPassed\b")
FATAL_ERROR_FALLBACK = re.compile(r"\bfatal error\b", re.I)


def parse_diagnostics(lines: list[str], working_directory: Path | None,
                      commands: list[dict] | None = None) -> list[dict]:
    diagnostics = []
    command_directories = {
        "> " + format_command(command["command"]): Path(command["working_directory"])
        for command in (commands or [])
        if isinstance(command, dict) and isinstance(command.get("command"), list) and isinstance(command.get("working_directory"), str)
    }
    for index, raw in enumerate(lines):
        line = progress_text(raw).strip()
        if line in command_directories:
            working_directory = command_directories[line]
            continue
        if line.startswith("> ") or MSBUILD_SUMMARY_LINE.match(line) or CTEST_PASSED_LINE.search(line):
            continue
        line = re.sub(r"^\d+>", "", line)
        matched_kind, match = next(
            ((kind, found) for kind, pattern in DIAGNOSTIC_PATTERNS if (found := pattern.match(line))),
            (None, None),
        )
        if match:
            item = match.groupdict()
            path = Path(item["path"].strip())
            if path.is_absolute():
                source_path: Path | None = path
            elif working_directory is not None:
                candidate = working_directory / path
                if matched_kind == "cmake" and not candidate.exists() and (REPOSITORY_ROOT / path).exists():
                    source_path = REPOSITORY_ROOT / path
                else:
                    source_path = candidate
            else:
                source_path = None
            item["source"] = str(source_path) if source_path is not None else None
            item["line"] = int(item["line"])
            item["severity"] = "error" if "error" in item["severity"].lower() else "warning"
        else:
            if ERROR_LINE.search(line) or FATAL_ERROR_FALLBACK.search(line):
                severity = "error"
            elif WARNING_LINE.search(line):
                severity = "warning"
            else:
                continue
            item = {"source": None, "line": None, "severity": severity, "message": line}
        diagnostics.append({**item, "log_line": index, "raw": line})
    return diagnostics


def _read_cached_source(path: str, cache: dict[str, list[str] | str] | None) -> list[str] | str:
    if cache is not None and path in cache:
        return cache[path]
    try:
        loaded: list[str] | str = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        loaded = f"Source unavailable: {path}: {error}"
    if cache is not None:
        cache[path] = loaded
    return loaded


def diagnostic_preview(diagnostic: dict, lines: list[str],
                       source_cache: dict[str, list[str] | str] | None = None) -> list[str]:
    index = diagnostic["log_line"]
    result = ["Raw log context:", *[f"{n + 1}: {lines[n]}" for n in range(max(0, index - 4), min(len(lines), index + 7))],
              "", "CURRENT SOURCE (read-only; may have changed since this run)"]
    path, line = diagnostic.get("source"), diagnostic.get("line")
    if not path or not line or line < 1:
        return result + ["Source location unavailable or invalid; no replacement was guessed."]
    source = _read_cached_source(path, source_cache)
    if isinstance(source, str):
        return result + [source]
    if line > len(source):
        return result + [f"Invalid location: {path}:{line} (current file has {len(source)} lines)."]
    return result + [f"{path}:{line}", *[f"{'>' if n + 1 == line else ' '} {n + 1}: {source[n]}" for n in range(max(0, line - 6), min(len(source), line + 5))]]


def _diagnostic_row_details(item: dict, lines: list[str],
                            source_cache: dict[str, list[str] | str]) -> tuple[str, ...]:
    path, line_number = item.get("source"), item.get("line")
    if not path or not line_number or line_number < 1:
        snippet = "Source location unavailable or invalid; no replacement was guessed."
    else:
        source = _read_cached_source(path, source_cache)
        if isinstance(source, str):
            snippet = source
        elif line_number > len(source):
            snippet = f"Invalid location: {path}:{line_number} (current file has {len(source)} lines)."
        else:
            snippet = f"> {line_number}: {source[line_number - 1]}"
    return (
        "Log context: " + (lines[item["log_line"] - 1] if item["log_line"] else "(start of log)"),
        item["raw"],
        lines[item["log_line"] + 1] if item["log_line"] + 1 < len(lines) else "(end of log)",
        "CURRENT SOURCE (read-only; may have changed)",
        str(path or "Source location unavailable"),
        snippet,
    )


def browse_run_diagnostics(run: dict, terminal: DashboardTerminal) -> None:
    try:
        lines = Path(run["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        raise BuildError(f"Could not read log '{run['log']}': {error}") from error
    cwd = Path(run["working_directory"]) if run.get("working_directory") else None
    diagnostics = parse_diagnostics(lines, cwd, run.get("commands"))
    source_cache: dict[str, list[str] | str] = {}
    view, severity = ToolboxView(), "all"
    status = "Legacy log: command directory unknown" if not cwd else str(cwd)

    def details_for(row: ToolboxRow) -> tuple[str, ...]:
        if row.key.isdigit():
            return _diagnostic_row_details(diagnostics[int(row.key)], lines, source_cache)
        return row.details

    while True:
        rows = [action_row("back", "Back"), action_row("filter", "Diagnostics: " + severity),
                action_row("preview", "Inspect selected diagnostic / current source"), action_row("raw", "Browse raw log")]
        rows += [ToolboxRow(str(index), item["raw"], tone="bad" if item["severity"] == "error" else "warn")
                 for index, item in enumerate(diagnostics)
                 if (severity == "all" or item["severity"] == severity) and view.search.casefold() in item["raw"].casefold()]
        action = choose_toolbox("Diagnostics: " + Path(run["log"]).name, rows, view, terminal,
                                status, detail_provider=details_for)
        status = "Legacy log: command directory unknown" if not cwd else str(cwd)
        if action == "back":
            return
        if action == "filter":
            filters = ("all", "error", "warning")
            severity = filters[(filters.index(severity) + 1) % len(filters)]
        elif action == "raw":
            show_toolbox_text("Raw log (authoritative)", lines, terminal)
        elif action == "preview":
            if view.focused and view.focused.isdigit():
                show_toolbox_text(
                    "Diagnostic and CURRENT SOURCE",
                    diagnostic_preview(diagnostics[int(view.focused)], lines, source_cache),
                    terminal,
                )
            else:
                status = "Select a diagnostic first."


def run_history_label(run: dict, glyphs: dict, now: float | None = None) -> str:
    """Status, action, configuration, duration and age of one recorded run."""
    name = Path(run["log"]).name
    status = run.get("status")
    if status is None:
        return f"{glyphs['info']} legacy log  {name}"
    mark = {"succeeded": glyphs["ok"], "failed": glyphs["bad"]}.get(status, glyphs["info"])
    elapsed = run.get("elapsed")
    duration = format_duration(elapsed) if isinstance(elapsed, (int, float)) else "-"
    started = run.get("started", run.get("recorded_at"))
    age = format_age((time.time() if now is None else now) - started) if isinstance(
        started, (int, float)) else ""
    configuration = run.get("identity", {}).get("configuration", "?")
    return (f"{mark} {status:<9} {str(run.get('action', '?')):<10} {configuration:<14} "
            f"{duration:>8}  {age:>9}  {name}")


def action_trends(history: list[dict], limit: int = 24) -> list[tuple[str, list[dict]]]:
    """Recorded runs per action, oldest first, most recently used action first."""
    grouped: dict[str, list[dict]] = {}
    for run in history:  # Newest first.
        action = run.get("action")
        if isinstance(action, str) and run.get("status") in ("succeeded", "failed", "cancelled"):
            runs = grouped.setdefault(action, [])
            if len(runs) < limit:
                runs.append(run)
    return [(action, list(reversed(runs))) for action, runs in grouped.items()]


def trend_row(action: str, runs: list[dict], glyphs: dict, now: float | None = None) -> ToolboxRow:
    """A duration sparkline of one action's runs, colored by outcome."""
    title = ACTION_TITLES.get(action, "Refresh test inventory" if action == "inventory" else action)
    durations = [run["elapsed"] if isinstance(run.get("elapsed"), (int, float)) else 0.0 for run in runs]
    # Scale from a little under the fastest run so differences stay visible.
    positive = [duration for duration in durations if duration > 0]
    bars = sparkline(durations, glyphs, low=min(positive) * 0.8 if positive else 0)
    colors = {"succeeded": ANSI_GREEN, "failed": ANSI_RED, "cancelled": ANSI_DIM}
    successes = sorted(duration for run, duration in zip(runs, durations) if run["status"] == "succeeded")
    counts = {status: sum(1 for run in runs if run["status"] == status)
              for status in ("succeeded", "failed", "cancelled")}
    median = format_duration(statistics.median(successes)) if successes else "-"
    rate = f"{100 * counts['succeeded'] // len(runs)}%"
    name = f"{title:<24} "
    tail = f"{' ' * (24 - len(bars))}  median {median:>8}  {rate:>4} ok"
    segments = ((name, ANSI_BOLD),
                *((bar, colors[run["status"]]) for bar, run in zip(bars, runs)),
                (tail, ANSI_DIM))
    latest = runs[-1]
    started = latest.get("started", latest.get("recorded_at"))
    age = format_age((time.time() if now is None else now) - started) if isinstance(
        started, (int, float)) else "at an unknown time"
    details = [
        f"{title}: {len(runs)} recorded runs ({counts['succeeded']} succeeded, "
        f"{counts['failed']} failed, {counts['cancelled']} cancelled)",
        (f"Successful runs: median {median}, fastest {format_duration(successes[0])}, "
         f"slowest {format_duration(successes[-1])}") if successes else "No successful runs recorded",
        f"Latest: {latest['status']} {age} in {format_duration(durations[-1])} "
        f"({latest.get('identity', {}).get('configuration', '?')})",
    ]
    if successes and latest["status"] == "succeeded" and len(successes) > 1:
        details.append("Latest success is " + compare_with_last_run(
            durations[-1], statistics.median(successes)).replace("the last run", "the median"))
    details.append("Bars are durations, oldest to newest: green succeeded, red failed, dim cancelled.")
    return ToolboxRow(action, name + bars + tail, tuple(details),
                      tone={"succeeded": "ok", "failed": "bad"}.get(latest["status"], "dim"),
                      segments=segments)


def run_trends_view(terminal: DashboardTerminal) -> None:
    view = ToolboxView()
    glyphs = terminal_glyphs()
    while True:
        trends = action_trends(run_history())
        rows = [action_row("back", "Back"), action_row("refresh", "Refresh")]
        rows += [trend_row(action, runs, glyphs) for action, runs in trends
                 if view.search.casefold() in ACTION_TITLES.get(action, action).casefold()]
        message = (f"{sum(len(runs) for _action, runs in trends)} runs across {len(trends)} actions"
                   if trends else "No recorded runs yet; progress actions record them.")
        if choose_toolbox("Build trends", rows, view, terminal, message) == "back":
            return


def run_diagnostic_browser(terminal: DashboardTerminal) -> None:
    view = ToolboxView()
    glyphs = terminal_glyphs()
    while True:
        runs = run_history()
        rows = [action_row("back", "Back"), action_row("open", "Browse selected run"), action_row("refresh", "Refresh runs")]
        rows += [ToolboxRow(run["log"], run_history_label(run, glyphs),
                            (str(run.get("identity", "No trusted metadata")),
                             "Command: " + format_command(run.get("command", [])),
                             "Working directory: " + run.get("working_directory", "unknown")),
                            tone={"succeeded": "ok", "failed": "bad", "cancelled": "warn"}.get(
                                run.get("status"), "dim"))
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
            preview = DashboardState(
                applications=state.applications,
                installed_packages=state.installed_packages,
            )
            apply_dashboard_profile(preview, name, settings)
            effective = dashboard_settings(preview)
            kind = "built-in" if name in BUILTIN_PROFILES else "saved"
            rows.append(ToolboxRow(name, f"{name} ({kind})", (
                f"Config: {effective['config']} | Directory: {effective['build_dir']}",
                f"Generator: {effective.get('generator', 'CMake default')} | Architecture: {effective.get('architecture', 'default')}",
                f"Compiler: {choice_label(COMPILER_CHOICES, effective.get('compiler', 'default'))} | "
                f"Optimization: {choice_label(OPTIMIZATION_CHOICES, effective.get('optimization', 'default'))} | "
                f"Warnings: {choice_label(WARNING_CHOICES, effective.get('warnings', 'default'))} | "
                f"Sanitizer: {choice_label(SANITIZER_CHOICES, effective.get('sanitizer', 'default'))}",
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


def run_distribution_picker(state: DashboardState, terminal: DashboardTerminal) -> bool:
    """Choose which apps the distribution ships; True to assemble, False to go back.

    The choice is kept for the session, so the next distribution starts from it.
    Every app is selected the first time.
    """
    chosen = list(state.distribution_apps if state.distribution_apps is not None
                  else state.applications)
    view, message = ToolboxView(), "Enter toggles an app. Each ships as apps/<name>.ilpk with a shortcut."
    while True:
        rows = [action_row("back", "Back"), action_row("assemble", "Assemble distribution")]
        for name in state.applications:
            mark = "[x]" if name in chosen else "[ ]"
            rows.append(ToolboxRow(
                f"toggle:{name}", f"{mark} {app_label(name, state.installed_packages)} ({name})",
                (f"{'Ships' if name in chosen else 'Left out'}: apps/{name}.ilpk",),
                action=True, tone="ok" if name in chosen else "dim"))
        action = choose_toolbox("Distribution apps", rows, view, terminal, message)
        if action == "back":
            return False
        if action == "assemble":
            if not chosen:
                message = "Select at least one app."
                continue
            state.distribution_apps = tuple(name for name in state.applications if name in chosen)
            return True
        if action.startswith("toggle:"):
            name = action.removeprefix("toggle:")
            if name in chosen:
                chosen.remove(name)
            else:
                chosen.append(name)
            message = ""


def available_artifacts(settings: dict, history: list[dict]) -> list[tuple[str, Path]]:
    identity = build_identity(settings)
    latest = next((run for run in history if run.get("identity") == identity), None)
    artifacts = [("Selected build directory", Path(identity["build_directory"])),
                 ("Coverage HTML (coverage build)", resolve_build_directory(DEFAULT_COVERAGE_DIRECTORY) / "coverage-html" / "index.html"),
                 ("Documentation", REPOSITORY_ROOT / "docs" / "output" / "illumo.pdf"),
                 ("Architecture map", REPOSITORY_ROOT / "docs" / "output" / "architecture-map.pdf"),
                 ("Orchestrator logs folder", REPOSITORY_ROOT / "build-orchestrator-logs")]
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


DEVELOPMENT_TOOLS = (
    ("compiler", "Compiler options"),
    ("tests", "Test explorer"),
    ("diagnostics", "Diagnostic browser"),
    ("trends", "Build trends (durations and outcomes per action)"),
    ("profiles", "Profile picker"),
    ("artifacts", "Artifact shortcuts"),
    ("stats", "Source file statistics"),
    ("watch", "Watch: rebuild on every save"),
    ("doctor", "Toolchain doctor (tools, cache, staged apps, stale outputs)"),
    ("wasm_tools", "Fetch the pinned WASM toolchain"),
    ("shortcuts", "Keyboard shortcuts and command-line equivalents"),
)


def cycle_choice(choices: Sequence[tuple[str, str]], current: str, direction: int) -> str:
    keys = [key for key, _label in choices]
    index = keys.index(current) if current in keys else 0
    return keys[(index + direction) % len(keys)]


def set_compiler_option(state: DashboardState, key: str, value: str) -> None:
    setattr(state, key, value)
    state.overrides[key] = value


def compiler_option_rows(state: DashboardState) -> list[ToolboxRow]:
    """The compiler menu: each option cycles, and the last row shows the flags."""
    settings = dashboard_settings(state)
    resolved = resolve_compiler(
        settings.get("compiler", "default"), generator=settings.get("generator"),
    )
    rows = [action_row("back", "Back to dashboard")]
    for key in ("compiler", "optimization", "cpu", "warnings", "sanitizer"):
        current = settings.get(key, "default")
        rows.append(ToolboxRow(
            key,
            f"{COMPILER_OPTION_LABELS[key]:<18} {choice_label(COMPILER_OPTION_CHOICES[key], current)}",
            compiler_option_details(key, settings, resolved),
            tone="warn" if key == "compiler" and resolved.missing else "",
        ))
    preview = compiler_cmake_arguments(namespace_from_settings(settings))
    rows.append(ToolboxRow(
        "flags", "Flags passed at configure",
        compiler_flag_details(settings, resolved, preview),
        tone="warn" if resolved.missing else "ok",
    ))
    return rows


def compiler_option_details(
    key: str, settings: dict, resolved: ResolvedCompiler,
) -> tuple[str, ...]:
    if key == "compiler":
        lines = [resolved.summary]
        if resolved.missing:
            lines.append(resolved.missing)
        lines.append(f"Build tree: {settings['build_dir']}")
        lines.append(f"Generator: {settings.get('generator', 'CMake default')}")
        if resolved.language == "intel-windows":
            lines.append(
                "The build loads setvars.bat so libircmt.lib and the Windows libraries "
                "are both on the link path. AddressSanitizer stays off: icx rejects it "
                "with the Debug runtime. clang-tidy and coverage stay on Clang."
            )
        conflict = compiler_generator_error(settings.get("generator"), settings.get("compiler", "default"))
        if conflict:
            lines.append(conflict)
        lines.append("WASM packages still compile with the pinned WASI clang.")
        return tuple(lines)
    if key == "optimization":
        current = settings.get("optimization", "default")
        if current == "default":
            if resolved.language == "gnu":
                return ("CMake keeps Debug at -O0 -g and Release at -O3.",
                        "None, Size, Speed and Fast replace that for every configuration.")
            return ("CMake keeps Debug at /Od and Release at /O2.",
                    "None, Size, Speed and Fast replace that for every configuration.")
        flag = OPTIMIZATION_FLAGS[resolved.language][current]
        return (f"Compile flag: {flag}",
                "MSVC Fast is /Ox, Intel on Windows Fast is /O3, Clang and GCC Fast is -O3.")
    if key == "cpu":
        return cpu_option_details(
            settings.get("compiler", "default"), resolved, settings.get("cpu", "default"),
        )
    if key == "warnings":
        current = settings.get("warnings", "default")
        if current == "default":
            if resolved.language == "gnu":
                return ("CMake keeps -Wall -Wextra.",)
            return ("CMake keeps /W4, and still passes /FS /MP /we4530.",)
        flag = flag_text(WARNING_FLAGS[resolved.language][current])
        if resolved.language == "gnu":
            return (f"Compile flags: {flag}",)
        return (f"Compile flags: {flag}",
                "MSVC-style builds still pass /FS /MP /we4530.")
    current = settings.get("sanitizer", "default")
    if settings.get("compiler") == "intel":
        return (
            "AddressSanitizer stays off for Intel.",
            "icx rejects /fsanitize=address together with the Debug runtime (-MDd).",
        )
    if current == "off":
        return ("Passes -DILLUMO_ENABLE_ASAN=OFF.",
                "Debug then uses the fast guest bounds traps.")
    if current == "address":
        return ("Passes -DILLUMO_ENABLE_ASAN=ON.",
                "The sanitizer still applies to Debug only: /fsanitize=address or -fsanitize=address.")
    return ("Debug uses AddressSanitizer. Release does not.",
            "Off disables it. On pins the CMake default explicitly.")


def cpu_compile_flag(family: str, language: str, cpu: str) -> str:
    """The instruction-set flag for one menu choice. Empty means the baseline."""
    if cpu == "default":
        return ""
    if cpu == "host" and family == "intel" and language == "gnu":
        return "-xHost"
    return CPU_FLAGS[language][cpu]


def cpu_option_details(family: str, resolved: ResolvedCompiler, cpu: str) -> tuple[str, ...]:
    if cpu == "default":
        return (
            "No instruction-set flag. Intel's /QxHost is not on.",
            "The binary stays on the compiler's portable baseline.",
        )
    flag = cpu_compile_flag(family, resolved.language, cpu)
    if cpu == "host" and not flag:
        return (
            "Microsoft's compiler has no switch for this Intel CPU.",
            "Choose AVX2 for an instruction set Intel and AMD both run.",
        )
    if cpu == "host" and family == "intel":
        return (
            f"Compile flag: {flag}",
            "Tuned for this Intel processor. It can refuse to run on AMD or older Intel CPUs.",
        )
    if cpu == "host":
        return (f"Compile flag: {flag}", "Tuned for the CPU that compiles this build.")
    if cpu == "avx2":
        return (f"Compile flag: {flag}", "Runs on Intel and AMD CPUs with AVX2.")
    return (f"Compile flag: {flag}", "Needs a CPU with AVX-512.")


def compiler_flag_details(
    settings: dict, resolved: ResolvedCompiler, arguments: Sequence[str],
) -> tuple[str, ...]:
    lines = list(arguments)
    if resolved.missing:
        lines.insert(0, resolved.missing)
    lines.append("Exceptions stay off. Coverage and clang-tidy stay on Clang.")
    return tuple(lines[:6])


def compiler_status_line(state: DashboardState) -> str:
    settings = dashboard_settings(state)
    parts = [choice_label(COMPILER_CHOICES, settings.get("compiler", "default"))]
    if settings.get("optimization", "default") != "default":
        parts.append(choice_label(OPTIMIZATION_CHOICES, settings["optimization"]))
    if settings.get("cpu", "default") != "default":
        parts.append(choice_label(CPU_CHOICES, settings["cpu"]))
    if settings.get("warnings", "default") != "default":
        parts.append(choice_label(WARNING_CHOICES, settings["warnings"]) + " warnings")
    if settings.get("sanitizer", "default") != "default":
        parts.append("ASan " + choice_label(SANITIZER_CHOICES, settings["sanitizer"]))
    return "Compiler: " + ", ".join(parts)


def run_compiler_options(state: DashboardState, terminal: DashboardTerminal) -> str:
    """Cycle compiler, optimization, warnings and the sanitizer."""
    view = ToolboxView()
    last = None
    cyclable = tuple(COMPILER_OPTION_CHOICES)

    def cycle_option(key: str, direction: int) -> None:
        current_settings = dashboard_settings(state)
        if key == "sanitizer" and "asan" in unsupported_features(current_settings.get("compiler", "default")):
            return
        set_compiler_option(
            state, key,
            cycle_choice(
                COMPILER_OPTION_CHOICES[key],
                current_settings.get(key, "default"),
                direction,
            ),
        )

    while True:
        rows = compiler_option_rows(state)
        size = dashboard_terminal_size((100, 32))
        rendered, regions, page = render_toolbox(
            "Compiler options", rows, view, size.columns, size.lines,
            compiler_status_line(state), ansi=True,
            help_text="Left/Right or Enter: change | q or Escape: back",
        )
        if rendered != last:
            sys.stdout.write(ANSI_CLEAR + tui_newlines(rendered))
            sys.stdout.flush()
            last = rendered
        event = terminal.read_event()
        if isinstance(event, DashboardMouseEvent) and dashboard_terminal_size((100, 32)) != size:
            continue
        if isinstance(event, DashboardMouseEvent) and event.kind in ("left", "right"):
            region = next((hit for hit in regions if hit.row == event.y and 1 <= event.x <= hit.right), None)
            if region is not None:
                view.selected = region.index
                clicked = rows[region.index].key
                if clicked == "back" and event.kind == "left":
                    return compiler_status_line(state)
                if clicked in cyclable:
                    cycle_option(clicked, 1 if event.kind == "left" else -1)
            continue
        selected = rows[view.selected].key if rows else ""
        if event in ("left", "right", "enter") and selected in cyclable:
            cycle_option(selected, -1 if event == "left" else 1)
            continue
        action = toolbox_event(view, rows, event, regions, page)
        if action == "back":
            return compiler_status_line(state)


def run_development_tool(state: DashboardState, terminal: DashboardTerminal, tool: str) -> str:
    """Open one Development Tools entry; returns a status message."""
    if tool == "compiler":
        return run_compiler_options(state, terminal)
    if tool == "tests":
        run_test_explorer(state, terminal)
    elif tool == "diagnostics":
        run_diagnostic_browser(terminal)
    elif tool == "trends":
        run_trends_view(terminal)
    elif tool == "profiles":
        run_profile_picker(state, terminal)
    elif tool == "artifacts":
        run_artifact_picker(state, terminal)
    elif tool == "stats":
        if execute_dashboard_action(state, "file_stats", terminal):
            state.last_action = "file_stats"
    elif tool in ("doctor", "wasm_tools", "watch"):
        if execute_dashboard_action(state, tool, terminal):
            state.last_action = tool
        return state.status
    elif tool == "shortcuts":
        show_toolbox_text("Keyboard shortcuts", dashboard_shortcut_lines(state), terminal)
    else:
        raise BuildError(f"Unknown development tool: {tool}")
    return ""


def run_development_tools(state: DashboardState, terminal: DashboardTerminal) -> None:
    view, message = ToolboxView(), ""
    rows = [action_row("back", "Back to dashboard"),
            *(action_row(key, label) for key, label in DEVELOPMENT_TOOLS)]
    while True:
        action = choose_toolbox("Development Tools", rows, view, terminal, message)
        if action == "back":
            return
        try:
            message = run_development_tool(state, terminal, action)
        except (OSError, BuildError) as error:
            message = str(error)


def fuzzy_match(query: str, text: str) -> tuple[int, tuple[int, ...]] | None:
    """Subsequence match, scored for word starts and runs; None if absent.
    Returns the score and the matched character positions."""
    folded = text.casefold()
    score, position, previous = 0, 0, -2
    matched: list[int] = []
    for character in query.casefold():
        if character.isspace():
            continue
        found = folded.find(character, position)
        if found < 0:
            return None
        if found == 0 or not folded[found - 1].isalnum():
            score += 10
        if found == previous + 1:
            score += 6
        score -= min(found - position, 8)
        matched.append(found)
        previous, position = found, found + 1
    return score, tuple(matched)


def palette_entries(state: DashboardState) -> list[tuple[str, str, str, str]]:
    """(key, category, text, detail) for every action, setting value and tool."""
    entries = []
    for index, (kind, label, key) in enumerate(DASHBOARD_ITEMS):
        if kind == "action" and key != "quit":
            entries.append((f"action:{key}", "Action", label, dashboard_selection_hint(state, index)))
    entries += [(f"tool:{key}", "Tool", label, "Development Tools") for key, label in DEVELOPMENT_TOOLS]
    for key, choices in COMPILER_OPTION_CHOICES.items():
        title = COMPILER_OPTION_LABELS[key]
        for _ident, value in choices:
            entries.append((f"set:{key}={value}", "Setting", f"{title}: {value}",
                            "Compiler options; m opens the menu"))
    try:
        profiles = [DASHBOARD_DEFAULT_PROFILE, *load_profiles(state.profiles_file, allow_missing=True)]
    except BuildError:
        profiles = [DASHBOARD_DEFAULT_PROFILE]
    entries += [(f"profile:{name}", "Profile", f"Profile: {name}", "Clears session overrides")
                for name in profiles]
    options = {
        "configuration": list(DASHBOARD_CONFIGURATIONS),
        "application": [app_label(app, state.installed_packages) for app in state.applications],
        "parallel": [label for label, _value in DASHBOARD_PARALLEL_OPTIONS],
    }
    docs_ready = documentation_available()
    for kind, label, key in DASHBOARD_ITEMS:
        if (kind != "setting" or key == "profile"
                or (key == "wasm" and not WASM_HOST_SUPPORTED)
                or (key == "documentation" and not docs_ready)):
            continue
        for value in options.get(key, ["On", "Off"]):
            entries.append((f"set:{key}={value}", "Setting", f"{label}: {value}",
                            DASHBOARD_SETTING_HINTS.get(key, "")))
    for theme_key in THEME_ORDER:
        theme = DASHBOARD_THEMES[theme_key]
        entries.append((f"theme:{theme_key}", "Theme", f"Theme: {theme.name}",
                        f"{theme.tagline}; press g to cycle"))
    entries.append(("flair:lightshow", "Theme", "Spectacle & 3D visualizers",
                    "3D Torus, Tunnel, Fireworks, Inferno, CSim Life, Plasma & Hyperwarp (f opens)"))
    return entries


def palette_rows(
    state: DashboardState, query: str,
    entries: Sequence[tuple[str, str, str, str]] | None = None,
) -> tuple[list[ToolboxRow], int]:
    all_entries = palette_entries(state) if entries is None else entries
    ranked = []
    for order, (key, category, text, detail) in enumerate(all_entries):
        match = fuzzy_match(query, text)
        if match is not None:
            ranked.append((-match[0], order, key, category, text, detail, set(match[1])))
    ranked.sort()
    # A verb per category: what choosing the row will do.
    verbs = {"Action": ("Run", ANSI_VIOLET), "Tool": ("Open", ANSI_CYAN),
             "Profile": ("Use", ANSI_YELLOW), "Setting": ("Set", ANSI_BLUE),
             "Theme": ("Look", ANSI_GREEN)}
    rows = []
    for _score, _order, key, category, text, detail, positions in ranked:
        verb, color = verbs[category]
        segments = ((f"{verb:<6}", color),
                    *((character, ANSI_BOLD + ANSI_UNDERLINE if index in positions else "")
                      for index, character in enumerate(text)))
        rows.append(ToolboxRow(key, f"{verb:<6}{text}", (text, detail), segments=segments))
    return rows, len(all_entries)


def run_command_palette(state: DashboardState, terminal: DashboardTerminal) -> str | None:
    """Type to filter every action, tool, profile and setting value."""
    view = ToolboxView(draft="")
    last = None
    moving = motion_enabled()
    caret_on = True
    entries = palette_entries(state)
    glyphs = terminal_glyphs()
    rows: list[ToolboxRow] = []
    total = 0
    event: object = None
    while True:
        if event != "tick":  # A blink repaints; only input can change the rows.
            rows, total = palette_rows(state, view.draft or "", entries=entries)
        size = dashboard_terminal_size((100, 32))
        rendered, regions, page = render_toolbox(
            "Command palette", rows, view, size.columns, size.lines,
            f"{len(rows)} of {total} commands", ansi=True,
            help_text="Type to filter | Up/Down or hover: choose | Enter or click: run | Escape: close",
            prompt=f"{glyphs['chevron']} ",
            caret=glyphs["caret"] if caret_on else " ",
            theme_name=state.theme,
        )
        if rendered != last:
            if event == "tick" and last is not None:
                write_in_place_frame(rendered)
            else:
                sys.stdout.write(ANSI_CLEAR + tui_newlines(rendered))
                sys.stdout.flush()
            last = rendered
        event = terminal.read_event(text_mode=True, timeout=0.5 if moving else None)
        if event == "tick":
            caret_on = not caret_on
            continue
        caret_on = True  # Typing keeps the cursor solid.
        if isinstance(event, DashboardTextEvent):
            view.draft += event.text
            view.selected = view.top = 0
        elif event == "backspace":
            view.draft = view.draft[:-1]
            view.selected = view.top = 0
        elif event in ("escape", "quit"):
            return None
        elif event == "enter":
            return rows[view.selected].key if rows else None
        elif isinstance(event, DashboardMouseEvent):
            if event.kind in ("wheel_up", "wheel_down"):
                view.selected += -1 if event.kind == "wheel_up" else 1
            else:
                region = next((hit for hit in regions if hit.row == event.y and 1 <= event.x <= hit.right), None)
                if region is not None:
                    view.selected = region.index
                    if event.kind == "left":
                        return rows[region.index].key
        else:
            movement = {"up": -1, "down": 1, "page_up": -page, "page_down": page}
            if event in movement:
                view.selected += movement[event]
            elif event in ("home", "end"):
                view.selected = 0 if event == "home" else len(rows) - 1
        view.selected = max(0, min(view.selected, len(rows) - 1))


def apply_palette_choice(state: DashboardState, terminal: DashboardTerminal, choice: str) -> str | None:
    """Apply a palette entry; returns a dashboard action still to execute."""
    kind, _separator, value = choice.partition(":")
    if kind == "action":
        state.selected = [item[2] for item in DASHBOARD_ITEMS].index(value)
        return value
    if kind == "tool":
        message = run_development_tool(state, terminal, value)
        if message:
            state.status = message
        refresh_dashboard_context(state)
        return None
    if kind == "theme" and value in DASHBOARD_THEMES:
        state.theme = value
        state.wave_frame = state.frame
        theme = DASHBOARD_THEMES[value]
        state.status, state.status_kind = f"Theme: {theme.name} — {theme.tagline}", "normal"
        return None
    if kind == "flair":
        state.theme = run_lightshow(terminal, theme_name=state.theme, auto=True)
        state.wave_frame = state.frame
        theme = resolve_theme(state.theme)
        state.status, state.status_kind = f"Spectacle closed (theme: {theme.name})", "normal"
        return None
    if kind == "profile":
        profiles = load_profiles(state.profiles_file, allow_missing=True)
        if value in profiles:
            apply_dashboard_profile(state, value, profiles[value])
        else:
            apply_dashboard_profile(state, None, {})
        state.selected = [item[2] for item in DASHBOARD_ITEMS].index("profile")
        state.status, state.status_kind = f"Profile {state.profile_label} applied", "normal"
        return None
    if kind == "set":
        key, _equals, target = value.partition("=")
        if key in COMPILER_OPTION_CHOICES:
            ident = next(ident for ident, text in COMPILER_OPTION_CHOICES[key] if text == target)
            set_compiler_option(state, key, ident)
            state.status, state.status_kind = f"{COMPILER_OPTION_LABELS[key]}: {target}", "normal"
            return None
        state.selected = [item[2] for item in DASHBOARD_ITEMS].index(key)
        # Step through the same path as Left/Right so overrides are recorded.
        for _attempt in range(len(DASHBOARD_PARALLEL_OPTIONS) + len(state.applications) + 4):
            if dashboard_value(state, key) == target:
                break
            adjust_dashboard_setting(state, 1)
        state.status, state.status_kind = f"{DASHBOARD_ITEMS[state.selected][1]}: {dashboard_value(state, key)}", "normal"
    return None


def dashboard_shortcut_lines(state: DashboardState) -> list[str]:
    lines = ["Build console", "  Up/Down or j/k      move the selection (wheel too)",
             "  Left/Right or h/l   change a setting (right-click steps back)",
             "  Enter or click      run the selected action",
             "  Tab                 jump between Settings and Actions",
             "  Home/End            first or last row",
             "  m                   compiler options (also under Development Tools)",
             "  g                   cycle 24-bit color theme with a full-console energy wave",
             "  v                   cycle wide-screen Live Reactor sidekick (click panel to burst)",
             "  f                   launch 7-scene TrueColor Spectacle (3D Torus, Tunnel, Fireworks, Inferno, CSim Life, Plasma, Warp)",
             "  /                   command palette: type to find any action, tool,",
             "                      profile, theme or setting value; Enter runs it",
             "  .                   repeat the last action",
             "  ?                   this page", "  q or Escape         leave", "",
             "Action hotkeys (command-line equivalent)"]
    for index, (kind, label, key) in enumerate(DASHBOARD_ITEMS):
        if kind != "action" or key == "quit":
            continue
        lines.append(f"  {DASHBOARD_HOTKEYS.get(key, ' ')}  {label:<24} "
                     f"{dashboard_selection_hint(state, index)}")
    lines += ["", "After a progress action",
              "  d   browse its diagnostics     l   read its full log     any other key: back",
              "", "Subviews", "  /   edit the search     Enter apply     Escape cancel / back",
              "  Page Up/Down, Home/End scroll long lists"]
    return lines


LIGHTSHOW_SCENES = (
    ("torus", "3D Torus"),
    ("tunnel", "Tunnel"),
    ("fireworks", "Fireworks"),
    ("fire", "Inferno"),
    ("life", "CSim Life"),
    ("plasma", "Plasma"),
    ("warp", "Hyperwarp"),
)
LIGHTSHOW_SCENE_KEYS = tuple(key for key, _label in LIGHTSHOW_SCENES)
REACTOR_MODES = ("torus", "tunnel", "fireworks", "fire", "life", "plasma", "off")
_ASCII_SHADE = " .:-=+*#%@"
# Authentic Gosper Glider Gun (36x9) for the CSim Life tribute.
_GOSPER_GUN: tuple[tuple[int, int], ...] = (
    (0, 4), (0, 5), (1, 4), (1, 5),
    (10, 4), (10, 5), (10, 6), (11, 3), (11, 7), (12, 2), (12, 8), (13, 2), (13, 8),
    (14, 5), (15, 3), (15, 7), (16, 4), (16, 5), (16, 6), (17, 5),
    (20, 2), (20, 3), (20, 4), (21, 2), (21, 3), (21, 4), (22, 1), (22, 5),
    (24, 0), (24, 1), (24, 5), (24, 6),
    (34, 2), (34, 3), (35, 2), (35, 3),
)
_TORUS_SHAPES = ("Torus", "Trefoil Knot", "Hyper-Cube")


class LightshowState:
    """Real-time half-block 24-bit TrueColor demoscene & 3D shader engine."""

    def __init__(
        self,
        scene: str = "torus",
        theme_name: str | None = None,
        seed: int = 42,
        *,
        auto: bool = False,
        show_logo: bool = True,
    ) -> None:
        self.scene = scene if scene in LIGHTSHOW_SCENE_KEYS else "torus"
        self.theme_name = resolve_theme(theme_name).key
        self.show_logo = show_logo
        self.auto = auto
        self.scene_age = 0
        self.transition = 0.0
        self.frame = 0
        self.pulse = 0.0
        self.shape_index = 0
        self.rng = random.Random(seed)
        self.pw = 0
        self.ph = 0
        # 3D Torus background stars: (x_frac, y_frac, phase)
        self.bg_stars: list[tuple[float, float, float]] = [
            (self.rng.random(), self.rng.random(), self.rng.uniform(0.0, math.tau))
            for _ in range(70)
        ]
        # 3D Tunnel precomputed polar LUT: list of rows of (angle_norm, depth_inv, fog)
        self.tunnel_lut_dim: tuple[int, int] = (0, 0)
        self.tunnel_lut: list[list[tuple[float, float, float]]] = []
        # Fireworks physics: rockets [x, y, vx, vy, target_y, hue], sparks [x, y, vx, vy, life, max_life, rgb]
        self.fw_dim: tuple[int, int] = (0, 0)
        self.fw_buf: list[list[list[float]]] = []
        self.rockets: list[list[float]] = []
        self.sparks: list[list] = []
        # Cellular Inferno (DOOM fire) buffer: 0.0 .. 1.0 intensity
        self.fire_dim: tuple[int, int] = (0, 0)
        self.fire_grid: list[list[float]] = []
        # CSim Life toroidal grid + phosphor heat
        self.life_dim: tuple[int, int] = (0, 0)
        self.life_cells: list[list[int]] = []
        self.life_heat: list[list[float]] = []
        # Plasma expanding shockwaves: [radius, strength]
        self.shockwaves: list[list[float]] = []
        # Hyperwarp starfield + decay buffer
        self.warp_dim: tuple[int, int] = (0, 0)
        self.stars: list[list[float]] = []
        self.warp_buf: list[list[list[float]]] = []

    def set_scene(self, scene: str) -> None:
        if scene in LIGHTSHOW_SCENE_KEYS and scene != self.scene:
            self.scene = scene
            self.scene_age = 0
            self.transition = 1.0

    def trigger_burst(self) -> None:
        self.pulse = 1.0
        if self.scene == "torus":
            self.shape_index = (self.shape_index + 1) % len(_TORUS_SHAPES)
        elif self.scene == "fireworks" and self.fw_dim[0]:
            # Grand Finale 5-shell salvo!
            pw, ph = self.fw_dim
            for slot in range(5):
                x = pw * (0.16 + 0.17 * slot) + self.rng.uniform(-3.0, 3.0)
                self._launch_rocket(x, ph)
        elif self.scene == "life" and self.life_dim[0]:
            pw, ph = self.life_dim
            self._seed_life_burst(self.rng.randrange(pw), self.rng.randrange(ph))
        elif self.scene == "plasma":
            self.shockwaves.append([0.0, 1.0])
            if len(self.shockwaves) > 4:
                self.shockwaves.pop(0)
        elif self.scene == "fire" and self.fire_dim[0]:
            pw, ph = self.fire_dim
            for y in range(max(0, ph - 4), ph):
                for x in range(pw):
                    self.fire_grid[y][x] = 1.0

    def _ensure_tunnel(self, pw: int, ph: int) -> None:
        if self.tunnel_lut_dim == (pw, ph):
            return
        self.tunnel_lut_dim = (pw, ph)
        cx, cy = pw * 0.5, ph * 0.5
        scale = min(pw, ph) * 0.42
        lut: list[list[tuple[float, float, float]]] = []
        for y in range(ph):
            dy = (y - cy) * 1.05
            row: list[tuple[float, float, float]] = []
            for x in range(pw):
                dx = x - cx
                dist = math.sqrt(dx * dx + dy * dy)
                angle = (math.atan2(dy, dx) / math.tau) % 1.0
                inv = scale / max(1.8, dist)
                fog = min(1.0, (dist / max(1.0, scale * 0.72)) ** 1.15)
                row.append((angle, inv, fog))
            lut.append(row)
        self.tunnel_lut = lut

    def _ensure_fireworks(self, pw: int, ph: int) -> None:
        if self.fw_dim == (pw, ph) and self.fw_buf:
            return
        self.fw_dim = (pw, ph)
        self.fw_buf = [[[0.0, 0.0, 0.0] for _ in range(pw)] for _ in range(ph)]
        self.rockets.clear()
        self.sparks.clear()
        for frac in (0.28, 0.52, 0.74):
            self._launch_rocket(pw * frac, ph)

    def _launch_rocket(self, x: float, ph: int) -> None:
        sky_h = max(6, int(ph * 0.76))
        target_y = self.rng.uniform(sky_h * 0.16, sky_h * 0.58)
        vy = -self.rng.uniform(1.35, 2.05)
        vx = self.rng.uniform(-0.22, 0.22)
        self.rockets.append([x, float(sky_h - 1), vx, vy, target_y, self.rng.random()])

    def _detonate_rocket(self, x: float, y: float, hue_seed: float, theme: DashboardTheme) -> None:
        pattern = self.rng.choice(("chrysanthemum", "ring", "double", "spiral"))
        base_rgb = boost_rgb(theme_color(theme, hue_seed, phase=self.frame * 0.015 if theme.prism else 0.0), 0.25)
        alt_rgb = boost_rgb(theme_color(theme, (hue_seed + 0.42) % 1.0), 0.3)
        count = 46 if pattern == "double" else 34
        for idx in range(count):
            angle = (idx / count) * math.tau + self.rng.uniform(-0.08, 0.08)
            if pattern == "ring":
                speed = 1.45 + self.rng.uniform(-0.08, 0.08)
                color = base_rgb
            elif pattern == "double":
                outer = idx % 2 == 0
                speed = (1.65 if outer else 0.92) + self.rng.uniform(-0.1, 0.1)
                color = base_rgb if outer else alt_rgb
            elif pattern == "spiral":
                speed = 0.55 + (idx / count) * 1.45
                color = boost_rgb(theme_color(theme, (hue_seed + idx / count) % 1.0), 0.2)
            else:
                speed = self.rng.uniform(0.45, 1.85)
                color = base_rgb if self.rng.random() < 0.75 else (255, 250, 220)
            life = self.rng.uniform(16.0, 30.0)
            self.sparks.append([
                x, y, math.cos(angle) * speed * 1.15, math.sin(angle) * speed * 0.95,
                life, life, color,
            ])

    def _step_fireworks(self, pw: int, ph: int, theme: DashboardTheme) -> None:
        self._ensure_fireworks(pw, ph)
        sky_h = max(6, int(ph * 0.76))
        if len(self.rockets) < 3 and self.rng.random() < 0.22:
            self._launch_rocket(self.rng.uniform(pw * 0.12, pw * 0.88), ph)
        buf = self.fw_buf
        for y in range(sky_h):
            row_b = buf[y]
            for x in range(pw):
                c = row_b[x]
                c[0] *= 0.76
                c[1] *= 0.76
                c[2] *= 0.76
        kept_rockets: list[list[float]] = []
        for r in self.rockets:
            r[0] += r[2]
            r[1] += r[3]
            r[3] += 0.035
            ix, iy = int(r[0]), int(r[1])
            if 0 <= ix < pw and 0 <= iy < sky_h:
                cell = buf[iy][ix]
                cell[0], cell[1], cell[2] = 255.0, 245.0, 200.0
                if iy + 1 < sky_h:
                    tail = buf[iy + 1][ix]
                    tail[0] = min(255.0, tail[0] + 220.0)
                    tail[1] = min(255.0, tail[1] + 150.0)
                    tail[2] = min(255.0, tail[2] + 70.0)
            if r[1] <= r[4] or r[3] >= -0.15:
                self._detonate_rocket(r[0], max(2.0, r[1]), r[5], theme)
            elif 0 <= ix < pw and r[1] < sky_h:
                kept_rockets.append(r)
        self.rockets = kept_rockets

        kept_sparks: list[list] = []
        for s in self.sparks:
            s[0] += s[2]
            s[1] += s[3]
            s[2] *= 0.94
            s[3] = s[3] * 0.94 + 0.055
            s[4] -= 1.0
            if s[4] <= 0.0:
                continue
            ratio = s[4] / s[5]
            twinkle = 1.0 if (ratio > 0.35 or self.rng.random() > 0.35) else 0.25
            gain = (ratio ** 0.75) * twinkle
            ix, iy = int(s[0]), int(s[1])
            if 0 <= ix < pw and 0 <= iy < sky_h:
                rgb = s[6]
                cell = buf[iy][ix]
                cell[0] = min(255.0, cell[0] + rgb[0] * gain)
                cell[1] = min(255.0, cell[1] + rgb[1] * gain)
                cell[2] = min(255.0, cell[2] + rgb[2] * gain)
                # Subtle 4-neighbor additive bloom on bright sparks.
                if gain > 0.55:
                    bloom = gain * 0.28
                    for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                        nx, ny = ix + ox, iy + oy
                        if 0 <= nx < pw and 0 <= ny < sky_h:
                            nc = buf[ny][nx]
                            nc[0] = min(255.0, nc[0] + rgb[0] * bloom)
                            nc[1] = min(255.0, nc[1] + rgb[1] * bloom)
                            nc[2] = min(255.0, nc[2] + rgb[2] * bloom)
            kept_sparks.append(s)
        self.sparks = kept_sparks[-360:]

    def _ensure_fire(self, pw: int, ph: int) -> None:
        if self.fire_dim == (pw, ph) and self.fire_grid:
            return
        self.fire_dim = (pw, ph)
        self.fire_grid = [[0.0] * pw for _ in range(ph)]
        for x in range(pw):
            self.fire_grid[ph - 1][x] = 1.0

    def _step_fire(self, pw: int, ph: int) -> None:
        self._ensure_fire(pw, ph)
        grid = self.fire_grid
        # Roaring white-hot coals along the base, pulsing with wave turbulence.
        base = grid[ph - 1]
        t = self.frame * 0.22
        for x in range(pw):
            wave = 0.78 + 0.22 * math.sin(x * 0.27 + t) * math.cos(x * 0.13 - t * 0.7)
            base[x] = min(1.0, wave + self.pulse * 0.35 + self.rng.uniform(-0.12, 0.12))
        cool = max(0.018, 1.35 / max(10, ph))
        for y in range(ph - 1):
            dst = grid[y]
            src = grid[y + 1]
            src2 = grid[min(ph - 1, y + 2)]
            for x in range(pw):
                drift = self.rng.choice((-1, 0, 0, 1))
                sx = (x + drift) % pw
                avg = (src[sx] * 2.0 + src[(x - 1) % pw] + src[(x + 1) % pw] + src2[x]) * 0.2
                decay = cool * self.rng.uniform(0.45, 1.55)
                dst[x] = max(0.0, avg - decay)
        # Occasional rising glowing sparks.
        if self.rng.random() < 0.7:
            sx = self.rng.randrange(pw)
            sy = self.rng.randrange(max(1, ph // 3), max(2, ph - 3))
            grid[sy][sx] = min(1.0, grid[sy][sx] + 0.65)

    def _ensure_life(self, pw: int, ph: int) -> None:
        if self.life_dim == (pw, ph) and self.life_cells:
            return
        self.life_dim = (pw, ph)
        self.life_cells = [[0] * pw for _ in range(ph)]
        self.life_heat = [[0.0] * pw for _ in range(ph)]
        if pw >= 42 and ph >= 18:
            # Stamp an authentic Gosper Glider Gun in the upper-left.
            for gx, gy in _GOSPER_GUN:
                self._set_life((3 + gx) % pw, (3 + gy) % ph)
        # Plus a central R-pentomino for immediate organic fireworks.
        cx, cy = (pw * 2) // 3, ph // 2
        for dx, dy in ((0, -1), (1, -1), (-1, 0), (0, 0), (0, 1)):
            self._set_life((cx + dx) % pw, (cy + dy) % ph)
        for _ in range(max(12, (pw * ph) // 55)):
            self._set_life(self.rng.randrange(pw), self.rng.randrange(ph))

    def _set_life(self, x: int, y: int) -> None:
        self.life_cells[y][x] = 1
        self.life_heat[y][x] = 1.0

    def _seed_life_burst(self, cx: int, cy: int) -> None:
        pw, ph = self.life_dim
        radius = max(4, min(pw, ph) // 6)
        for _ in range(radius * 7):
            angle = self.rng.uniform(0.0, math.tau)
            dist = self.rng.uniform(0.0, radius)
            x = int(cx + math.cos(angle) * dist) % pw
            y = int(cy + math.sin(angle) * dist) % ph
            self._set_life(x, y)

    def _step_life(self) -> None:
        pw, ph = self.life_dim
        cells = self.life_cells
        heat = self.life_heat
        nxt = [[0] * pw for _ in range(ph)]
        alive_count = 0
        for y in range(ph):
            ym = (y - 1) % ph
            yp = (y + 1) % ph
            row_m, row_0, row_p = cells[ym], cells[y], cells[yp]
            nxt_row, heat_row = nxt[y], heat[y]
            for x in range(pw):
                xm = (x - 1) % pw
                xp = (x + 1) % pw
                neighbors = (
                    row_m[xm] + row_m[x] + row_m[xp]
                    + row_0[xm] + row_0[xp]
                    + row_p[xm] + row_p[x] + row_p[xp]
                )
                was = row_0[x]
                now = 1 if (neighbors == 3 or (was and neighbors == 2)) else 0
                nxt_row[x] = now
                if now:
                    alive_count += 1
                    heat_row[x] = 1.0 if not was else max(0.72, heat_row[x] * 0.96)
                else:
                    heat_row[x] *= 0.80
        self.life_cells = nxt
        if alive_count < max(16, (pw * ph) // 160):
            self._seed_life_burst(self.rng.randrange(pw), self.rng.randrange(ph))

    def _ensure_warp(self, pw: int, ph: int) -> None:
        if self.warp_dim != (pw, ph) or not self.warp_buf:
            self.warp_dim = (pw, ph)
            self.warp_buf = [[[0.0, 0.0, 0.0] for _ in range(pw)] for _ in range(ph)]
        target = max(100, min(240, (pw * ph) // 24))
        while len(self.stars) < target:
            self.stars.append([
                self.rng.uniform(-1.0, 1.0),
                self.rng.uniform(-1.0, 1.0),
                self.rng.uniform(0.08, 1.0),
                self.rng.uniform(0.0, 1.0),
            ])

    def step(self, width: int, canvas_rows: int) -> None:
        self.frame += 1
        self.scene_age += 1
        self.pulse = max(0.0, self.pulse - 0.055)
        self.transition = max(0.0, self.transition - 0.14)
        if self.auto and self.scene_age >= 95:
            next_idx = (LIGHTSHOW_SCENE_KEYS.index(self.scene) + 1) % len(LIGHTSHOW_SCENE_KEYS)
            self.scene = LIGHTSHOW_SCENE_KEYS[next_idx]
            self.scene_age = 0
            self.transition = 1.0
            theme_idx = (THEME_ORDER.index(self.theme_name) + 1) % len(THEME_ORDER)
            self.theme_name = THEME_ORDER[theme_idx]
        elif self.auto and self.scene_age == 48:
            self.trigger_burst()

        pw, ph = max(8, width), max(4, canvas_rows * 2)
        self.pw, self.ph = pw, ph
        theme = resolve_theme(self.theme_name)
        if self.scene == "tunnel":
            self._ensure_tunnel(pw, ph)
        elif self.scene == "fireworks":
            self._step_fireworks(pw, ph, theme)
        elif self.scene == "fire":
            self._step_fire(pw, ph)
        elif self.scene == "life":
            self._ensure_life(pw, ph)
            self._step_life()
        elif self.scene == "plasma":
            for wave in self.shockwaves:
                wave[0] += max(1.2, max(pw, ph) * 0.032)
                wave[1] *= 0.94
            self.shockwaves = [w for w in self.shockwaves if w[1] > 0.05]
        elif self.scene == "warp":
            self._ensure_warp(pw, ph)


def _render_scene_pixels(ls: LightshowState, pw: int, ph: int, theme: DashboardTheme) -> list[list[RGB]]:
    """Compute the (ph x pw) 24-bit RGB pixel framebuffer for the active shader."""
    bg_deep = (6, 8, 16)
    pixels: list[list[RGB]] = [[bg_deep] * pw for _ in range(ph)]
    phase = ls.frame * 0.015 if theme.prism else 0.0

    if ls.scene == "torus":
        # Twinkling cosmic backdrop behind the 3D geometry.
        for sx, sy, sph in ls.bg_stars:
            px, py = int(sx * (pw - 1)), int(sy * (ph - 1))
            tw = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(ls.frame * 0.16 + sph))
            pixels[py][px] = scale_rgb(boost_rgb(theme.accent, 0.5), tw * 0.55)
        zbuf = [[-1e9] * pw for _ in range(ph)]
        a = ls.frame * 0.075
        b = ls.frame * 0.052
        ca, sa = math.cos(a), math.sin(a)
        cb, sb = math.cos(b), math.sin(b)
        lx, ly, lz = math.cos(ls.frame * 0.04) * 0.55, 0.55, -0.72
        l_inv = 1.0 / math.sqrt(lx * lx + ly * ly + lz * lz)
        lx, ly, lz = lx * l_inv, ly * l_inv, lz * l_inv
        scale = min(pw * 0.44, ph * 0.44)
        cx, cy = pw * 0.5, ph * 0.5
        cam_z = 4.6

        def splat_3d(x: float, y: float, z: float, nx: float, ny: float, nz: float, u_col: float) -> None:
            # Rotate around X then Z/Y.
            y1, z1 = y * ca - z * sa, y * sa + z * ca
            x2, z2 = x * cb + z1 * sb, -x * sb + z1 * cb
            ny1, nz1 = ny * ca - nz * sa, ny * sa + nz * ca
            nx2, nz2 = nx * cb + nz1 * sb, -nx * sb + nz1 * cb
            inv_z = 1.0 / max(0.8, cam_z + z2)
            px = int(cx + x2 * inv_z * scale * 2.15)
            py = int(cy - y1 * inv_z * scale * 2.15)
            if not (0 <= px < pw and 0 <= py < ph) or inv_z <= zbuf[py][px]:
                return
            zbuf[py][px] = inv_z
            ndotl = max(0.0, nx2 * lx + ny1 * ly + nz2 * lz)
            # Half-vector Phong specular + Fresnel rim glow.
            hx, hy, hz = lx, ly, lz - 1.0
            h_inv = 1.0 / max(1e-6, math.sqrt(hx * hx + hy * hy + hz * hz))
            spec = max(0.0, (nx2 * hx + ny1 * hy + nz2 * hz) * h_inv) ** 10
            rim = max(0.0, 1.0 - abs(nz2)) ** 2.4
            base = theme_color(theme, u_col, phase=phase)
            lit = scale_rgb(base, 0.16 + 0.84 * ndotl)
            lit = lerp_rgb(lit, boost_rgb(theme.frame_glow, 0.3), min(0.75, rim * 0.55))
            pixels[py][px] = boost_rgb(lit, min(0.92, spec * 0.9 + ls.pulse * 0.25))

        if ls.shape_index == 0:  # Classic Torus
            r1, r2 = 0.62, 1.38
            u_steps = max(60, min(110, pw + 24))
            v_steps = max(36, min(64, ph + 16))
            for ui in range(u_steps):
                theta = (ui / u_steps) * math.tau
                ct, st = math.cos(theta), math.sin(theta)
                for vi in range(v_steps):
                    phi = (vi / v_steps) * math.tau
                    cp, sp = math.cos(phi), math.sin(phi)
                    ring = r2 + r1 * cp
                    splat_3d(ring * ct, ring * st, r1 * sp, cp * ct, cp * st, sp, (ui / u_steps + vi / v_steps * 0.25) % 1.0)
        elif ls.shape_index == 1:  # 3D Trefoil Knot tube
            u_steps = max(96, min(150, pw * 2))
            v_steps = 20
            tube = 0.36
            for ui in range(u_steps):
                t = (ui / u_steps) * math.tau
                x0 = (math.sin(t) + 2.0 * math.sin(2.0 * t)) * 0.58
                y0 = (math.cos(t) - 2.0 * math.cos(2.0 * t)) * 0.58
                z0 = (-math.sin(3.0 * t)) * 0.65
                tx = math.cos(t) + 4.0 * math.cos(2.0 * t)
                ty = -math.sin(t) + 4.0 * math.sin(2.0 * t)
                tz = -3.0 * math.cos(3.0 * t)
                t_len = max(1e-5, math.sqrt(tx * tx + ty * ty + tz * tz))
                tx, ty, tz = tx / t_len, ty / t_len, tz / t_len
                # Arbitrary perpendicular frame.
                bx, by, bz = -ty, tx, 0.0
                b_len = max(1e-5, math.sqrt(bx * bx + by * by))
                bx, by = bx / b_len, by / b_len
                nx0, ny0, nz0 = by * tz - bz * ty, bz * tx - bx * tz, bx * ty - by * tx
                for vi in range(v_steps):
                    phi = (vi / v_steps) * math.tau
                    cp, sp = math.cos(phi), math.sin(phi)
                    nx = cp * bx + sp * nx0
                    ny = cp * by + sp * ny0
                    nz = cp * bz + sp * nz0
                    splat_3d(x0 + nx * tube, y0 + ny * tube, z0 + nz * tube, nx, ny, nz, (ui / u_steps) % 1.0)
        else:  # Shaded Spinning Cube with glowing neon edges
            grid_n = max(14, min(26, min(pw, ph) // 2))
            faces = (
                ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 0.0),
                ((0.0, 0.0, -1.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 0.16),
                ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0), 0.33),
                ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0), 0.50),
                ((0.0, 1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, -1.0), 0.66),
                ((0.0, -1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0), 0.83),
            )
            half = 1.15
            for normal, right_v, up_v, hue in faces:
                for i in range(grid_n):
                    u = -1.0 + 2.0 * (i / (grid_n - 1))
                    for j in range(grid_n):
                        v = -1.0 + 2.0 * (j / (grid_n - 1))
                        edge = max(abs(u), abs(v)) > 0.88
                        x = (normal[0] + right_v[0] * u + up_v[0] * v) * half
                        y = (normal[1] + right_v[1] * u + up_v[1] * v) * half
                        z = (normal[2] + right_v[2] * u + up_v[2] * v) * half
                        splat_3d(x, y, z, normal[0], normal[1], normal[2], (hue + (0.25 if edge else 0.0)) % 1.0)

    elif ls.scene == "tunnel":
        ls._ensure_tunnel(pw, ph)
        t_fly = ls.frame * (0.22 + 0.35 * ls.pulse)
        t_spin = ls.frame * 0.014 + 0.08 * math.sin(ls.frame * 0.04)
        lut = ls.tunnel_lut
        for y in range(ph):
            lut_row = lut[y]
            row_px = pixels[y]
            for x in range(pw):
                angle, inv, fog = lut_row[x]
                u = (angle + t_spin + 0.04 * math.sin(inv * 0.6 - t_fly * 0.4)) % 1.0
                v = inv + t_fly
                grid_u = abs(((u * 12.0) % 1.0) - 0.5)
                grid_v = abs((v % 1.0) - 0.5)
                wire = 1.0 if (grid_u < 0.08 or grid_v < 0.09) else 0.0
                checker = 0.28 if ((int(u * 12.0) + int(v)) & 1) else 0.10
                ring = max(0.0, math.sin(v * 2.2 - ls.frame * 0.15)) ** 6
                rgb = theme_color(theme, (u * 0.65 + v * 0.08) % 1.0, phase=phase)
                intensity = (checker + wire * 0.72 + ring * 0.55) * fog
                row_px[x] = boost_rgb(scale_rgb(rgb, min(1.0, intensity)), wire * 0.38 * fog + ring * 0.35 * fog)

    elif ls.scene == "fireworks":
        ls._ensure_fireworks(pw, ph)
        sky_h = max(6, int(ph * 0.76))
        buf = ls.fw_buf
        # Night sky gradient + twinkling stars.
        for y in range(sky_h):
            ny = y / max(1, sky_h - 1)
            sky_rgb = lerp_rgb((5, 7, 18), scale_rgb(theme.selected_left, 0.38), ny * ny)
            row_b = buf[y]
            row_px = pixels[y]
            for x in range(pw):
                c = row_b[x]
                row_px[x] = (
                    min(255, int(sky_rgb[0] + c[0])),
                    min(255, int(sky_rgb[1] + c[1])),
                    min(255, int(sky_rgb[2] + c[2])),
                )
        for sx, sy, sph in ls.bg_stars[:45]:
            px, py = int(sx * (pw - 1)), int(sy * (sky_h - 2))
            tw = 0.3 + 0.7 * (0.5 + 0.5 * math.sin(ls.frame * 0.2 + sph))
            c = pixels[py][px]
            add = int(110 * tw)
            pixels[py][px] = (min(255, c[0] + add), min(255, c[1] + add), min(255, c[2] + add + 20))
        # Shimmering wavy water reflection across the bottom 24%.
        water_tint = scale_rgb(theme.frame_base, 0.22)
        for y in range(sky_h, ph):
            depth = (y - sky_h) / max(1, ph - sky_h)
            mirror_y = max(0, sky_h - 1 - int((y - sky_h) * 1.85))
            ripple = math.sin(y * 1.15 - ls.frame * 0.35) * (1.2 + 2.4 * depth)
            row_px = pixels[y]
            src_row = pixels[mirror_y]
            fade = 0.58 * (1.0 - 0.55 * depth)
            for x in range(pw):
                rx = max(0, min(pw - 1, int(x + ripple)))
                ref = src_row[rx]
                row_px[x] = lerp_rgb(water_tint, ref, fade)
            if y == sky_h:
                for x in range(pw):
                    row_px[x] = boost_rgb(row_px[x], 0.14)

    elif ls.scene == "fire":
        ls._ensure_fire(pw, ph)
        grid = ls.fire_grid
        # Build a 4-stop theme-aware inferno palette (obsidian -> deep -> accent -> white-hot).
        fire_stops = (
            (5, 5, 10),
            scale_rgb(theme.banner_stops[-1], 0.65),
            theme.banner_stops[1 if len(theme.banner_stops) > 1 else 0],
            theme.banner_stops[0],
            (255, 252, 232),
        )
        for y in range(ph):
            g_row = grid[y]
            row_px = pixels[y]
            for x in range(pw):
                v = g_row[x]
                if v < 0.03:
                    row_px[x] = (5, 5, 10)
                elif theme.prism:
                    row_px[x] = boost_rgb(scale_rgb(hsv_to_rgb((v * 0.55 + ls.frame * 0.012) % 1.0, 0.9, 1.0), min(1.0, v * 1.35)), max(0.0, (v - 0.82) * 4.5))
                else:
                    row_px[x] = sample_gradient(fire_stops, v)

    elif ls.scene == "life":
        ls._ensure_life(pw, ph)
        bg_rgb = scale_rgb(theme.frame_base, 0.12)
        for y in range(ph):
            h_row = ls.life_heat[y]
            c_row = ls.life_cells[y]
            row_px = pixels[y]
            for x in range(pw):
                h = h_row[x]
                if h < 0.03:
                    row_px[x] = bg_rgb
                elif c_row[x]:
                    base = theme_color(theme, (x / max(1, pw) * 0.7 + y / max(1, ph) * 0.3), phase=phase)
                    row_px[x] = boost_rgb(base, max(0.0, (h - 0.78) * 3.6))
                else:
                    trail = theme_color(theme, 1.0 - h * 0.75, phase=phase)
                    row_px[x] = lerp_rgb(bg_rgb, scale_rgb(trail, 0.78), h)

    elif ls.scene == "plasma":
        t = ls.frame * 0.095
        cx, cy = pw * 0.5, ph * 0.5
        sin_x = [math.sin(x * 0.11 + t) for x in range(pw)]
        cos_x = [math.cos(x * 0.07 - t * 0.65) for x in range(pw)]
        for y in range(ph):
            sy = math.sin(y * 0.14 - t * 1.25)
            dy = (y - cy) * 1.15
            dy2 = dy * dy
            row_px = pixels[y]
            for x in range(pw):
                dx = x - cx
                dist = math.sqrt(dx * dx + dy2)
                v = (
                    sin_x[x] + sy
                    + math.sin((x * 0.08 + y * 0.09) + t * 0.85)
                    + math.sin(dist * 0.20 - t * 1.8)
                    + 0.45 * cos_x[x]
                )
                norm = (v + 3.45) / 6.9
                rgb = theme_color(theme, norm, phase=phase)
                ridge = max(0.0, math.sin(norm * math.pi * 3.0)) ** 5
                for sw_r, sw_s in ls.shockwaves:
                    ring = math.exp(-((dist - sw_r) ** 2) / 11.0) * sw_s
                    ridge = min(1.0, ridge + ring * 0.95)
                brightness = 0.26 + 0.74 * max(0.0, min(1.0, norm))
                row_px[x] = boost_rgb(scale_rgb(rgb, brightness), ridge * 0.58)

    else:  # warp
        ls._ensure_warp(pw, ph)
        buf = ls.warp_buf
        decay = 0.76 if ls.pulse > 0.1 else 0.58
        for y in range(ph):
            row_b = buf[y]
            for x in range(pw):
                c = row_b[x]
                c[0] *= decay
                c[1] *= decay
                c[2] *= decay
        cx = pw * 0.5 + math.sin(ls.frame * 0.04) * pw * 0.09
        cy = ph * 0.5 + math.cos(ls.frame * 0.03) * ph * 0.09
        speed = 0.034 + 0.072 * ls.pulse
        for star in ls.stars:
            prev_z = star[2]
            star[2] -= speed
            if star[2] <= 0.03:
                star[0] = ls.rng.uniform(-1.0, 1.0)
                star[1] = ls.rng.uniform(-1.0, 1.0)
                star[2] = 1.0
                star[3] = ls.rng.uniform(0.0, 1.0)
                continue
            x1 = int(cx + (star[0] / prev_z) * pw * 0.44)
            y1 = int(cy + (star[1] / prev_z) * ph * 0.44)
            x2 = int(cx + (star[0] / star[2]) * pw * 0.44)
            y2 = int(cy + (star[1] / star[2]) * ph * 0.44)
            depth = max(0.0, min(1.0, 1.0 - star[2]))
            rgb = boost_rgb(theme_color(theme, star[3], phase=phase), depth * 0.55)
            steps = max(1, min(14, max(abs(x2 - x1), abs(y2 - y1))))
            for s in range(steps + 1):
                frac = s / steps
                px = int(x1 + (x2 - x1) * frac)
                py = int(y1 + (y2 - y1) * frac)
                if 0 <= px < pw and 0 <= py < ph:
                    gain = (0.35 + 0.65 * frac) * (0.28 + 0.72 * depth)
                    cell = buf[py][px]
                    cell[0] = min(255.0, cell[0] + rgb[0] * gain)
                    cell[1] = min(255.0, cell[1] + rgb[1] * gain)
                    cell[2] = min(255.0, cell[2] + rgb[2] * gain)
        for y in range(ph):
            row_b = buf[y]
            row_px = pixels[y]
            for x in range(pw):
                c = row_b[x]
                row_px[x] = (max(6, int(c[0])), max(8, int(c[1])), max(15, int(c[2])))

    if ls.transition > 0.02:
        # Smooth radial iris-flash transition when switching scenes.
        flash = ls.transition * 0.55
        glow = boost_rgb(theme.accent, 0.5)
        for y in range(ph):
            row_px = pixels[y]
            for x in range(pw):
                row_px[x] = lerp_rgb(row_px[x], glow, flash)
    return pixels


def _pack_half_blocks(
    pixels: list[list[RGB]],
    width: int,
    canvas_rows: int,
    ansi: bool,
    glyphs: dict,
    logo_cells: dict[tuple[int, int], tuple[str, RGB, bool]] | None = None,
) -> list[str]:
    """Pack a (2 * canvas_rows) x width RGB pixel buffer into half-block terminal lines."""
    use_half = ansi and glyphs is UNICODE_GLYPHS
    lines: list[str] = []
    logo = logo_cells or {}
    for row in range(canvas_rows):
        top_row = pixels[row * 2]
        bot_row = pixels[row * 2 + 1]
        if not use_half:
            chars: list[str] = []
            for x in range(width):
                overlay = logo.get((row, x))
                if overlay is not None and not overlay[2]:
                    chars.append(overlay[0])
                    continue
                r1, g1, b1 = top_row[x]
                r2, g2, b2 = bot_row[x]
                luma = (0.299 * (r1 + r2) + 0.587 * (g1 + g2) + 0.114 * (b1 + b2)) / 510.0
                idx = max(0, min(len(_ASCII_SHADE) - 1, int(luma * (len(_ASCII_SHADE) - 1) + 0.5)))
                chars.append(_ASCII_SHADE[idx])
            lines.append("".join(chars))
            continue
        out: list[str] = []
        last_fg: RGB | None = None
        last_bg: RGB | None = None
        for x in range(width):
            overlay = logo.get((row, x))
            t_rgb = top_row[x]
            b_rgb = bot_row[x]
            if overlay is not None:
                ch, fg, is_shadow = overlay
                if is_shadow:
                    bg = scale_rgb(lerp_rgb(t_rgb, b_rgb, 0.5), 0.16)
                    out.append(f"\x1b[48;2;{bg[0]};{bg[1]};{bg[2]}m \x1b[0m")
                else:
                    bg = scale_rgb(lerp_rgb(t_rgb, b_rgb, 0.5), 0.24)
                    out.append(f"\x1b[1;38;2;{fg[0]};{fg[1]};{fg[2]};48;2;{bg[0]};{bg[1]};{bg[2]}m{ch}\x1b[0m")
                last_fg = last_bg = None
                continue
            if t_rgb != last_fg or b_rgb != last_bg:
                out.append(f"\x1b[38;2;{t_rgb[0]};{t_rgb[1]};{t_rgb[2]};48;2;{b_rgb[0]};{b_rgb[1]};{b_rgb[2]}m")
                last_fg, last_bg = t_rgb, b_rgb
            out.append("▀")
        out.append(ANSI_RESET)
        lines.append("".join(out))
    return lines


def _logo_overlay_cells(
    width: int, canvas_rows: int, theme: DashboardTheme, frame: int, glyphs: dict,
) -> dict[tuple[int, int], tuple[str, RGB, bool]]:
    """Centered 3D drop-shadowed glowing ILLUMO wordmark over the live shader."""
    if width < 32 or canvas_rows < 7 or glyphs is not UNICODE_GLYPHS:
        return {}
    bw = len(BANNER_ROWS[0])
    x0 = (width - bw) // 2
    y0 = max(1, (canvas_rows - len(BANNER_ROWS)) // 2)
    cells: dict[tuple[int, int], tuple[str, RGB, bool]] = {}
    band = shine_band(frame, bw + len(BANNER_ROWS), speed=1.5, band=4, period=64)
    # Extruded dark drop-shadow at (+1, +1) first so the glowing face pops in 3D.
    for r_idx, text in enumerate(BANNER_ROWS):
        for c_idx, ch in enumerate(text):
            if ch != " ":
                sy, sx = y0 + r_idx + 1, x0 + c_idx + 1
                if 0 <= sy < canvas_rows and 0 <= sx < width:
                    cells[(sy, sx)] = (" ", (0, 0, 0), True)
    for r_idx, text in enumerate(BANNER_ROWS):
        for c_idx, ch in enumerate(text):
            if ch == " ":
                continue
            t = c_idx / max(1, bw - 1)
            phase = frame * 0.015 if theme.prism else 0.0
            rgb = boost_rgb(theme_color(theme, t, phase=phase), 0.42)
            if band is not None and band[0] + r_idx <= c_idx < band[1] + r_idx:
                rgb = (255, 255, 255)
            cells[(y0 + r_idx, x0 + c_idx)] = (ch, rgb, False)
    return cells


def render_lightshow_frame(
    ls: LightshowState, columns: int, rows: int, *, ansi: bool = True, fps: float = 0.0,
) -> str:
    """Render one frame of the 7-scene TrueColor Spectacle plus its bottom HUD."""
    width = max(20, columns - 1)
    canvas_rows = max(4, rows - 3)
    glyphs = terminal_glyphs()
    theme = resolve_theme(ls.theme_name)
    pw, ph = width, canvas_rows * 2
    logo = _logo_overlay_cells(width, canvas_rows, theme, ls.frame, glyphs) if ls.show_logo else {}
    pixels = _render_scene_pixels(ls, pw, ph, theme)
    body = _pack_half_blocks(pixels, width, canvas_rows, ansi, glyphs, logo)

    # Bottom HUD bar with scene pills, Auto-Director indicator, theme, and FPS.
    short_labels = {
        "torus": _TORUS_SHAPES[ls.shape_index].split()[0],
        "tunnel": "Tunnel", "fireworks": "Fireworks", "fire": "Inferno",
        "life": "Life", "plasma": "Plasma", "warp": "Warp",
    }
    hud: list[tuple[str, str]] = [(" ", "")]
    for idx, (s_key, _s_label) in enumerate(LIGHTSHOW_SCENES, start=1):
        label = short_labels[s_key]
        active = s_key == ls.scene
        if active:
            pill_style = ANSI_BOLD + rgb_bg(theme.accent) + "\x1b[38;2;10;12;20m"
            hud.append((f" {idx}:{label} ", pill_style))
        else:
            hud.append((f"{idx}:{label}", ANSI_DIM))
        hud.append((" ", ""))
    sep = glyphs["separator"]
    hud.extend([
        (f"{sep} ", ANSI_DIM),
        ("a", ANSI_BOLD + (ANSI_GREEN if ls.auto else rgb_fg(theme.accent))),
        (f":{'AUTO' if ls.auto else 'auto'} ", ANSI_GREEN if ls.auto else ANSI_DIM),
        ("g", ANSI_BOLD + rgb_fg(theme.accent)),
        (f":{theme.name} ", ANSI_DIM),
        ("Spc", ANSI_BOLD + rgb_fg(theme.accent)),
        (":boom ", ANSI_DIM),
        ("q", ANSI_BOLD + rgb_fg(theme.accent)),
        (":exit", ANSI_DIM),
    ])
    if fps > 0.0:
        hud.append((f" {sep} {fps:4.1f}fps", ANSI_DIM))
    rule_line = _horizontal_border_segments(
        glyphs["horizontal"], glyphs["horizontal"], glyphs["horizontal"], width, (), glyphs, ansi,
        theme=theme, frame=ls.frame, start_pos=0, step=1, perimeter=max(20, width * 2),
    )
    hud_line = render_segments(hud, width, ansi, ellipsis=glyphs["ellipsis"])
    return "\n".join([*body, rule_line, hud_line])


def render_sidekick_panel(
    ls: LightshowState,
    panel_width: int,
    panel_rows: int,
    *,
    theme: DashboardTheme,
    frame: int | None,
    ansi: bool = True,
) -> list[str]:
    """Compact orbiting-glow Live Reactor sidekick docked beside wide dashboards."""
    glyphs = terminal_glyphs()
    inner_w = max(8, panel_width - 2)
    canvas_rows = max(4, panel_rows - 2)
    ls.theme_name = theme.key
    ls.step(inner_w, canvas_rows)
    pixels = _render_scene_pixels(ls, inner_w, canvas_rows * 2, theme)
    body = _pack_half_blocks(pixels, inner_w, canvas_rows, ansi, glyphs)
    perimeter = 2 * (panel_width - 1) + 2 * max(1, panel_rows - 1)
    scene_label = dict(LIGHTSHOW_SCENES).get(ls.scene, ls.scene)
    if ls.scene == "torus":
        scene_label = _TORUS_SHAPES[ls.shape_index]
    top_title = [
        *gradient_segments("Reactor", theme=theme),
        (f" {glyphs['separator']} {scene_label}", ANSI_DIM),
    ]
    bottom_hint = [
        ("v", ANSI_BOLD + rgb_fg(theme.accent)),
        (" mode  ", ANSI_DIM),
        ("g", ANSI_BOLD + rgb_fg(theme.accent)),
        (" theme  ", ANSI_DIM),
        ("f", ANSI_BOLD + rgb_fg(theme.accent)),
        (" full", ANSI_DIM),
    ]
    out = [_horizontal_border_segments(
        glyphs["top_left"], glyphs["top_right"], glyphs["horizontal"], panel_width,
        top_title, glyphs, ansi, theme=theme, frame=frame, start_pos=0, step=1, perimeter=perimeter,
    )]
    for idx, b_line in enumerate(body, start=1):
        if ansi:
            l_rgb = perimeter_glow_rgb(theme, perimeter - idx, perimeter, frame)
            r_rgb = perimeter_glow_rgb(theme, (panel_width - 1) + idx, perimeter, frame)
            out.append(
                f"{rgb_fg(l_rgb)}{glyphs['vertical']}{ANSI_RESET}"
                f"{b_line}"
                f"{rgb_fg(r_rgb)}{glyphs['vertical']}{ANSI_RESET}"
            )
        else:
            out.append(f"{glyphs['vertical']}{b_line}{glyphs['vertical']}")
    bottom_start = (panel_width - 1) + max(1, panel_rows - 1) + (panel_width - 1)
    out.append(_horizontal_border_segments(
        glyphs["bottom_left"], glyphs["bottom_right"], glyphs["horizontal"], panel_width,
        bottom_hint, glyphs, ansi, theme=theme, frame=frame, start_pos=bottom_start, step=-1, perimeter=perimeter,
    ))
    return out


def compose_dashboard_with_sidekick(dashboard_text: str, sidekick_lines: list[str], gap: int = 2) -> str:
    """Stitch the Live Reactor panel beside the left-aligned dashboard without moving hit regions."""
    left_lines = dashboard_text.splitlines()
    spacer = " " * gap
    combined: list[str] = []
    for idx, left in enumerate(left_lines):
        if idx < len(sidekick_lines) and left:
            combined.append(left + spacer + sidekick_lines[idx])
        else:
            combined.append(left)
    return "\n".join(combined)


def run_lightshow(
    terminal: DashboardTerminal,
    *,
    scene: str = "torus",
    theme_name: str | None = None,
    max_frames: int | None = None,
    auto: bool = False,
) -> str:
    """Interactive 7-scene TrueColor Spectacle loop; returns the last selected theme key."""
    ls = LightshowState(scene=scene, theme_name=theme_name, auto=auto)
    fps = 0.0
    last_t = time.monotonic()
    frames_rendered = 0
    while True:
        size = dashboard_terminal_size((100, 32))
        ls.step(max(20, size.columns - 1), max(4, size.lines - 3))
        now = time.monotonic()
        dt = max(0.001, now - last_t)
        fps = (1.0 / dt) if fps == 0.0 else (fps * 0.85 + (1.0 / dt) * 0.15)
        last_t = now
        frame_text = render_lightshow_frame(ls, size.columns, size.lines, ansi=True, fps=fps)
        write_in_place_frame(frame_text)
        frames_rendered += 1
        if max_frames is not None and frames_rendered >= max_frames:
            return ls.theme_name
        event = terminal.read_event(timeout=0.045)
        if event == "tick":
            continue
        if isinstance(event, DashboardMouseEvent):
            if event.kind == "left":
                ls.trigger_burst()
            elif event.kind == "right":
                idx = (LIGHTSHOW_SCENE_KEYS.index(ls.scene) + 1) % len(LIGHTSHOW_SCENE_KEYS)
                ls.set_scene(LIGHTSHOW_SCENE_KEYS[idx])
            continue
        if event in ("quit", "escape"):
            return ls.theme_name
        if event in ("left", "right"):
            ls.auto = False
            step = -1 if event == "left" else 1
            idx = (LIGHTSHOW_SCENE_KEYS.index(ls.scene) + step) % len(LIGHTSHOW_SCENE_KEYS)
            ls.set_scene(LIGHTSHOW_SCENE_KEYS[idx])
        elif isinstance(event, str) and event.startswith("key:") and len(event) == 5 and "1" <= event[4] <= str(len(LIGHTSHOW_SCENE_KEYS)):
            ls.auto = False
            ls.set_scene(LIGHTSHOW_SCENE_KEYS[int(event[4]) - 1])
        elif event in ("key:a", "key:A"):
            ls.auto = not ls.auto
            ls.scene_age = 0
        elif event in ("key:g", "key:G", "key:t", "key:T"):
            idx = (THEME_ORDER.index(ls.theme_name) + 1) % len(THEME_ORDER)
            ls.theme_name = THEME_ORDER[idx]
        elif event in ("key:b", "key:B"):
            ls.show_logo = not ls.show_logo
        elif event in ("key: ", "enter"):
            ls.trigger_burst()


def run_lightshow_cli(arguments: argparse.Namespace) -> None:
    auto = bool(getattr(arguments, "auto", False)) or getattr(arguments, "command", "") == "spectacle"
    scene = getattr(arguments, "scene", None) or "torus"
    theme_name = getattr(arguments, "theme", None)
    if getattr(arguments, "snapshot", False):
        ls = LightshowState(scene=scene, theme_name=theme_name, auto=auto)
        for _ in range(4):
            ls.step(96, 26)
        ansi = bool(getattr(arguments, "color", False)) or cli_ansi_enabled()
        print(render_lightshow_frame(ls, 97, 29, ansi=ansi, fps=22.0))
        return
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        raise BuildError(
            "The interactive spectacle requires a terminal (use --snapshot for a single frame)."
        )
    terminal = DashboardTerminal()
    sys.stdout.write(ANSI_PUSH_TITLE + "\x1b]2;Illumo Spectacle\x07")
    try:
        terminal.enter()
        run_lightshow(
            terminal,
            scene=scene,
            theme_name=theme_name,
            max_frames=getattr(arguments, "frames", None),
            auto=auto,
        )
    finally:
        terminal.leave()
        sys.stdout.write(ANSI_POP_TITLE)
        sys.stdout.flush()


def dashboard_navigate(state: DashboardState, key: str) -> bool:
    """Apply a selection-only key; False when the key is not navigation."""
    count = len(DASHBOARD_ITEMS)
    first_action = next(index for index, item in enumerate(DASHBOARD_ITEMS) if item[0] == "action")
    if key == "up":
        state.selected = (state.selected - 1) % count
    elif key == "down":
        state.selected = (state.selected + 1) % count
    elif key == "home":
        state.selected = 0
    elif key == "end":
        state.selected = count - 1
    elif key == "page_up":
        state.selected = max(0, state.selected - 5)
    elif key == "page_down":
        state.selected = min(count - 1, state.selected + 5)
    elif key == "tab":
        state.selected = 0 if state.selected >= first_action else first_action
    else:
        return False
    return True


def dashboard_key_action(state: DashboardState, key: str) -> str | None:
    """The action a key runs: a hotkey, '.' (repeat) or Enter on an action."""
    for action, letter in DASHBOARD_HOTKEYS.items():
        if key == f"key:{letter}":
            state.selected = [item[2] for item in DASHBOARD_ITEMS].index(action)
            return action
    if key == "key:.":
        if state.last_action is None:
            state.status, state.status_kind = "Nothing to repeat yet", "normal"
            return None
        if state.last_action in [item[2] for item in DASHBOARD_ITEMS]:
            state.selected = [item[2] for item in DASHBOARD_ITEMS].index(state.last_action)
        return state.last_action
    if key == "enter" and DASHBOARD_ITEMS[state.selected][0] == "action":
        return DASHBOARD_ITEMS[state.selected][2]
    return None


def run_dashboard(theme_name: str | None = None) -> int:
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print(
            "error: the interactive build console requires a terminal; "
            "use an explicit build.py subcommand instead.",
            file=sys.stderr,
        )
        return 2

    state = DashboardState(theme=resolve_theme(theme_name).key)
    sidekick = LightshowState(scene="torus", theme_name=state.theme, show_logo=False)
    terminal = DashboardTerminal()
    refresh_dashboard_context(state)
    sys.stdout.write(ANSI_PUSH_TITLE + terminal_progress_signals(None))
    try:
        terminal.enter()
        last_rendered: str | None = None
        last_status = state.status
        animate = motion_enabled()
        # The opening unroll reveals a few rows per tick; None shows all.
        reveal: int | None = 0 if animate else None
        last_size = None
        ticked = False
        while True:
            if state.status != last_status:
                state.status_time, last_status = time.strftime("%H:%M"), state.status
                state.status_frame = state.frame
            terminal_size = dashboard_terminal_size()
            regions: list[DashboardHitRegion] = []
            rendered = render_dashboard(
                state, terminal_size.columns, hit_regions=regions,
                mouse_enabled=terminal.windows_input is not None,
                terminal_rows=terminal_size.lines,
                frame=state.frame if animate else None, reveal=reveal,
            )
            # Wrapped or vertically clipped output cannot be hit-tested safely.
            rendered_rows = len(rendered.splitlines())
            if terminal_size.columns < 56 or rendered_rows > terminal_size.lines:
                regions.clear()
            # Wide terminals (>= 126 cols) dock a live 24-bit half-block Reactor beside the console!
            panel_space = terminal_size.columns - 103
            if animate and state.reactor_mode != "off" and panel_space >= 24 and rendered_rows >= 14:
                sidekick.set_scene(state.reactor_mode)
                panel_w = min(56, panel_space)
                panel_lines = render_sidekick_panel(
                    sidekick, panel_w, rendered_rows,
                    theme=resolve_theme(state.theme), frame=state.frame, ansi=True,
                )
                if reveal is not None:
                    panel_lines = [line if idx < reveal else "" for idx, line in enumerate(panel_lines)]
                rendered = compose_dashboard_with_sidekick(rendered, panel_lines, gap=2)
            if rendered != last_rendered:
                if ticked and last_rendered is not None and terminal_size == last_size:
                    # Animation frames overwrite in place; clearing would flicker.
                    write_in_place_frame(rendered)
                else:
                    sys.stdout.write(ANSI_CLEAR + tui_newlines(rendered))
                    sys.stdout.flush()
                last_rendered = rendered
            last_size = terminal_size
            event = terminal.read_event(timeout=DASHBOARD_TICK_SECONDS if animate else None)
            ticked = event == "tick"
            if ticked:
                state.frame += 1
                if reveal is not None:
                    reveal += INTRO_ROWS_PER_FRAME
                    if reveal >= rendered_rows:
                        reveal = None
                continue
            reveal = None  # Any input finishes the opening unroll at once.
            if isinstance(event, DashboardMouseEvent):
                if dashboard_terminal_size() != terminal_size:
                    continue
                if event.kind == "left" and event.x > 102 and state.reactor_mode != "off":
                    sidekick.trigger_burst()
                    continue
                key = dashboard_mouse_key(state, event, regions)
            else:
                key = event
            if key == "resize":
                last_rendered = None
            if key == "quit":
                return 0
            if dashboard_navigate(state, key):
                continue
            try:
                if key in ("left", "right") or (
                    key == "enter" and DASHBOARD_ITEMS[state.selected][0] == "setting"
                ):
                    if DASHBOARD_ITEMS[state.selected][0] == "setting":
                        adjust_dashboard_setting(state, -1 if key == "left" else 1)
                        sidekick.trigger_burst()
                    continue
                if key == "key:?":
                    show_toolbox_text("Keyboard shortcuts", dashboard_shortcut_lines(state), terminal)
                    last_rendered = None
                    continue
                if key == "key:m":
                    state.status, state.status_kind = run_compiler_options(state, terminal), "normal"
                    last_rendered = None
                    continue
                if key in ("key:g", "key:G"):
                    cycle_dashboard_theme(state, -1 if key == "key:G" else 1)
                    sidekick.trigger_burst()
                    last_rendered = None
                    continue
                if key in ("key:v", "key:V"):
                    step = -1 if key == "key:V" else 1
                    idx = (REACTOR_MODES.index(state.reactor_mode) + step) % len(REACTOR_MODES)
                    state.reactor_mode = REACTOR_MODES[idx]
                    label = "Off" if state.reactor_mode == "off" else dict(LIGHTSHOW_SCENES).get(state.reactor_mode, state.reactor_mode)
                    state.status, state.status_kind = f"Live Reactor: {label} (widen terminal >= 126 cols)", "normal"
                    last_rendered = None
                    continue
                if key in ("key:f", "key:F"):
                    start_scene = state.reactor_mode if state.reactor_mode in LIGHTSHOW_SCENE_KEYS else "torus"
                    state.theme = run_lightshow(terminal, scene=start_scene, theme_name=state.theme, auto=True)
                    state.wave_frame = state.frame
                    theme = resolve_theme(state.theme)
                    state.status, state.status_kind = f"Spectacle closed (theme: {theme.name})", "normal"
                    last_rendered = None
                    continue
                if key == "search":
                    choice = run_command_palette(state, terminal)
                    last_rendered = None
                    action = apply_palette_choice(state, terminal, choice) if choice else None
                else:
                    action = dashboard_key_action(state, key)
                if action == "quit":
                    return 0
                if action is not None:
                    if execute_dashboard_action(state, action, terminal):
                        state.last_action = action
                    refresh_dashboard_context(state)
                    sys.stdout.write(terminal_progress_signals(None))
                    last_rendered = None
            except BuildError as error:
                state.status, state.status_kind = str(error), "failure"
    finally:
        terminal.leave()
        sys.stdout.write(ANSI_POP_TITLE)
        sys.stdout.flush()


def prepend_env_path(env: dict[str, str], key: str, entries: Sequence[Path]) -> None:
    current = [part for part in env.get(key, "").split(os.pathsep) if part]
    folded = {os.path.normcase(part) for part in current}
    prefix: list[str] = []
    for entry in entries:
        text = str(entry)
        if os.path.normcase(text) not in folded:
            prefix.append(text)
            folded.add(os.path.normcase(text))
    env[key] = os.pathsep.join([*prefix, *current])


def intel_compiler_path(command: Sequence[str]) -> Path | None:
    """The icx path a configure or build command will use, if it uses one."""
    for item in command:
        match = re.fullmatch(r"-D(CMAKE_(?:C|CXX)_COMPILER)(?::[^=]+)?=(.*)", item)
        if not match:
            continue
        value = match[2].strip().strip('"')
        if Path(value).stem.lower() in ("icx", "icpx"):
            candidate = Path(value)
            if not candidate.is_absolute() and candidate.parent == Path("."):
                located = shutil.which(value)
                if located:
                    return Path(located)
            return candidate
    if len(command) >= 3 and Path(command[0]).stem.lower() == "cmake" and command[1] == "--build":
        cache = read_cmake_cache(resolve_build_directory(Path(command[2])))
        for key in ("CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER"):
            value = cache.get(key, "")
            if Path(value).stem.lower() in ("icx", "icpx"):
                return Path(value)
    return None


def intel_runtime_directories(compiler: Path) -> tuple[Path, ...]:
    """Directories that hold libircmt.lib next to an icx installation."""
    root = compiler.parent.parent
    found: list[Path] = []
    for directory in (root / "lib", root / "lib" / "intel64"):
        if (directory / "libircmt.lib").is_file():
            found.append(directory)
    return tuple(found)


def _version_sort_key(path: Path) -> tuple[int, ...]:
    parts = [int(piece) for piece in path.name.split(".") if piece.isdigit()]
    return tuple(parts) if parts else (0,)


def windows_sdk_tool_directory() -> Path | None:
    """The newest Windows SDK x64 bin directory that contains mt.exe."""
    roots = []
    program_files = os.environ.get("ProgramFiles(x86)")
    if program_files:
        roots.append(Path(program_files) / "Windows Kits" / "10" / "bin")
    roots.append(Path(r"C:\Program Files (x86)\Windows Kits\10\bin"))
    seen: set[str] = set()
    for root in roots:
        key = os.path.normcase(str(root))
        if key in seen or not root.is_dir():
            continue
        seen.add(key)
        versions = sorted(
            (path for path in root.iterdir() if path.is_dir()),
            key=_version_sort_key,
            reverse=True,
        )
        for version in versions:
            manifest_tool = version / "x64" / "mt.exe"
            if manifest_tool.is_file():
                return manifest_tool.parent
    return None


_oneapi_environment_cache: dict[str, dict[str, str] | None] = {}


def oneapi_setvars(compiler: Path) -> Path | None:
    """setvars.bat beside this icx, which also loads the Visual Studio libraries."""
    for parent in compiler.parents:
        candidate = parent / "setvars.bat"
        if candidate.is_file():
            return candidate
    return None


def parse_windows_set_output(text: str) -> dict[str, str]:
    """Parse `cmd /c set` output, preserving names like ProgramFiles(x86)."""
    environment: dict[str, str] = {}
    for line in text.splitlines():
        name, separator, value = line.partition("=")
        # setvars prints some names in lowercase (path=). Windows treats them
        # as the same variable, so keep one canonical spelling. Allow parentheses
        # so ProgramFiles(x86) and CommonProgramFiles(x86) are preserved.
        if separator and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_()]*", name):
            environment[name.upper()] = value
    return environment


def oneapi_environment(setvars: Path) -> dict[str, str] | None:
    """The environment after setvars.bat. Cached: the batch file takes several seconds."""
    key = os.path.normcase(str(setvars))
    if key in _oneapi_environment_cache:
        return _oneapi_environment_cache[key]
    # cmd.exe quote rules mangle a /c string that both quotes a path and uses &&.
    # A unique temporary batch file keeps the call intact and avoids cross-process races.
    script_path: str | None = None
    try:
        encoding = "mbcs" if os.name == "nt" else "utf-8"
        with tempfile.NamedTemporaryFile(
            mode="w", prefix="illumo-oneapi-", suffix=".bat",
            encoding=encoding, errors="strict", delete=False,
        ) as script:
            script_path = script.name
            script.write(f'@echo off\r\ncall "{setvars}" intel64 >nul\r\nset\r\n')
        result = subprocess.run(
            ["cmd", "/d", "/c", script_path],
            stdin=subprocess.DEVNULL, capture_output=True, timeout=60, check=False,
        )
    except (OSError, UnicodeEncodeError, subprocess.TimeoutExpired):
        _oneapi_environment_cache[key] = None
        return None
    finally:
        if script_path is not None:
            Path(script_path).unlink(missing_ok=True)
    if result.returncode != 0:
        _oneapi_environment_cache[key] = None
        return None
    text = result.stdout.decode("mbcs" if os.name == "nt" else "utf-8", errors="replace")
    environment = parse_windows_set_output(text)
    if "LIB" not in environment:
        _oneapi_environment_cache[key] = None
        return None
    _oneapi_environment_cache[key] = environment
    return environment


def compiler_subprocess_environment(command: Sequence[str]) -> dict[str, str] | None:
    """Environment for icx. Without Intel's libraries, link.exe cannot find libircmt.lib.

    setvars.bat supplies those libraries and the MSVC and Windows SDK libraries
    together. Returns None when the command does not use the Intel compiler.
    """
    if os.name != "nt":
        return None
    try:
        compiler = intel_compiler_path(command)
    except BuildError:
        return None
    if compiler is None:
        return None
    libraries = intel_runtime_directories(compiler)
    setvars = oneapi_setvars(compiler)
    loaded = oneapi_environment(setvars) if setvars is not None else None
    # Copy: the cached setvars environment must stay intact across commands.
    environment = dict(loaded) if loaded is not None else dict(os.environ)
    if libraries:
        prepend_env_path(environment, "LIB", libraries)
    manifest_tools = windows_sdk_tool_directory()
    path_entries = [compiler.parent]
    if manifest_tools is not None:
        path_entries.insert(0, manifest_tools)
    prepend_env_path(environment, "PATH", path_entries)
    if libraries:
        environment["ILLUMO_INTEL_RUNTIME"] = str(libraries[0])
    elif environment.get("LIB"):
        environment["ILLUMO_INTEL_RUNTIME"] = environment["LIB"].split(os.pathsep)[0]
    else:
        return None
    return environment


def _apply_env_overrides(
    base: dict[str, str] | None, overrides: dict[str, str | None] | None,
) -> dict[str, str] | None:
    if not overrides:
        return base
    merged = dict(os.environ) if base is None else dict(base)
    for key, value in overrides.items():
        if os.name == "nt":
            folded = key.casefold()
            for existing in [name for name in merged if name.casefold() == folded]:
                del merged[existing]
        else:
            merged.pop(key, None)
        if value is not None:
            merged[key] = value
    return merged


class CommandRunner:
    """Print and execute external commands from a predictable directory."""

    def __init__(self, dry_run: bool) -> None:
        self.dry_run = dry_run

    def run(
        self,
        command: Sequence[str],
        working_directory: Path = REPOSITORY_ROOT,
        *,
        env_overrides: dict[str, str | None] | None = None,
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
        # Progress views parse "> command" from a redirected stream; style
        # only what a person reads in a terminal.
        ansi = sys.stdout.isatty()
        print(dashboard_style("> ", ANSI_BOLD + ANSI_VIOLET, ansi)
              + dashboard_style(format_command(command), ANSI_BOLD, ansi), flush=True)
        if self.dry_run:
            return
        environment = compiler_subprocess_environment(list(command))
        if environment is not None and "ILLUMO_INTEL_RUNTIME" in environment:
            print(f"Intel runtime libraries: {environment['ILLUMO_INTEL_RUNTIME']}", flush=True)
        environment = _apply_env_overrides(environment, env_overrides)

        started = time.monotonic()
        try:
            result = subprocess.run(
                list(command),
                cwd=working_directory,
                check=False,
                env=environment,
            )
            elapsed = time.monotonic() - started
            if ansi and result.returncode == 0 and elapsed >= 1:
                print(dashboard_style(
                    f"  {terminal_glyphs()['ok']} {format_duration(elapsed)}", ANSI_DIM + ANSI_GREEN, ansi,
                ), flush=True)
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


def find_powershell(dry_run: bool, requirement: str) -> str:
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    if powershell is not None:
        return powershell
    if dry_run:
        return "powershell"
    raise BuildError(f"PowerShell was not found on PATH; {requirement} requires it.")


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


RELEASE_PATTERN = re.compile(r"\d{2}\.(0[1-9]|1[0-2])")


def read_build_version(root: Path = REPOSITORY_ROOT) -> dict[str, str]:
    """Return the build version fields of the tree at root.

    cmake/IllumoVersion.cmake is the one definition (the build stamps
    BuildInfo with it), so this runs it rather than restating the rules.
    """
    cmake = existing_tool("cmake", False)
    script = REPOSITORY_ROOT / "cmake" / "IllumoVersion.cmake"
    result = subprocess.run(
        [cmake, f"-DSOURCE_ROOT={root.as_posix()}", "-DPRINT=ON", "-P", str(script)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        check=False,
    )
    if result.returncode != 0:
        raise BuildError(
            "Could not compute the build version:\n"
            + (result.stderr or result.stdout).strip()
        )
    fields: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            fields[key.strip()] = value.strip()
    return fields


def set_release(
    release: str, root: Path = REPOSITORY_ROOT, commit: bool = True, dry_run: bool = False,
) -> None:
    """Start release YY.MM: write VERSION.txt and commit it on its own.

    Build numbers count first-parent commits after the commit that last
    changed VERSION.txt, so that commit is build _0 of the new release.
    """
    if not RELEASE_PATTERN.fullmatch(release):
        raise BuildError(f"A release is YY.MM (for example 26.10), not '{release}'.")
    version_file = root / "VERSION.txt"
    current = (
        version_file.read_text(encoding="utf-8").strip() if version_file.is_file() else ""
    )
    # Zero-padded YY.MM compares correctly as text.
    if RELEASE_PATTERN.fullmatch(current) and release <= current:
        raise BuildError(f"Release {release} is not after the current release {current}.")
    git = existing_tool("git", dry_run) if commit else None
    if dry_run:
        print(f"Would update VERSION.txt: {current or '(none)'} -> {release}")
        if commit:
            print(f"Would commit 'Start release v{release}' for VERSION.txt.")
        return
    version_file.write_text(release + "\n", encoding="utf-8", newline="\n")
    print(f"VERSION.txt: {current or '(none)'} -> {release}")
    if not commit or git is None:
        print("Commit VERSION.txt to start the release; its build numbers count from that commit.")
        return
    result = subprocess.run(
        [git, "commit", "--only", "-m", f"Start release v{release}", "--", "VERSION.txt"],
        cwd=root, capture_output=True, text=True, encoding="utf-8",
        errors="replace", check=False,
    )
    if result.returncode != 0:
        raise BuildError(
            "VERSION.txt was written but could not be committed:\n"
            + (result.stderr or result.stdout).strip()
        )
    print(f"Committed 'Start release v{release}'; this commit is build 0.")


def run_version(arguments: argparse.Namespace) -> None:
    if arguments.set:
        set_release(arguments.set, commit=not arguments.no_commit, dry_run=arguments.dry_run)
        if arguments.dry_run:
            return
    fields = read_build_version()
    if arguments.json:
        print(json.dumps(fields, indent=2))
    else:
        print(fields.get("full", ""))


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
        or (len(parts) > 1 and (first.startswith("build") or first.startswith("cmake-build")))
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
    c_family = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
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


def _gradient_text_line(text: str, stops: Sequence[RGB] | None = None, *, bold: bool = True) -> str:
    theme = resolve_theme()
    span = max(1, len(text) - 1)
    prefix = ANSI_BOLD if bold else ""
    parts: list[str] = []
    for idx, ch in enumerate(text):
        if ch == " ":
            parts.append(" ")
            continue
        rgb = sample_gradient(stops, idx / span) if stops else theme_color(theme, idx / span)
        parts.append(f"{prefix}{rgb_fg(rgb)}{ch}")
    return "".join(parts) + ANSI_RESET


_STATS_CATEGORY_GRADIENTS: tuple[tuple[RGB, RGB], ...] = (
    ((0, 235, 255), (150, 110, 255)),
    ((52, 238, 152), (168, 255, 112)),
    ((255, 76, 178), (255, 206, 86)),
    ((255, 154, 62), (242, 74, 108)),
    ((72, 186, 255), (146, 128, 255)),
    ((204, 118, 255), (255, 112, 188)),
)


def print_repository_statistics(statistics: RepositoryStatistics) -> None:
    ansi = cli_ansi_enabled()
    if ansi and terminal_glyphs() is UNICODE_GLYPHS:
        theme = resolve_theme()
        for r_idx, row_text in enumerate(BANNER_ROWS):
            print("  " + "".join(
                f"{style}{ch}{ANSI_RESET}" if style else ch
                for ch, style in banner_segments(row_text, r_idx, theme=theme)
            ))
        print("  " + _gradient_text_line("REPOSITORY STATISTICS  " + "━" * 44))
    else:
        print(_gradient_text_line("ILLUMO REPOSITORY STATISTICS") if ansi else "ILLUMO REPOSITORY STATISTICS")
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
    glyphs = terminal_glyphs()
    largest = max((category.loc for category in statistics.categories), default=0)
    for index, category in enumerate(statistics.categories):
        cells = round(24 * category.loc / largest) if largest else 0
        if cells:
            bar_raw = glyphs["block"] * cells
            bar = (
                "  " + _gradient_text_line(
                    bar_raw, _STATS_CATEGORY_GRADIENTS[index % len(_STATS_CATEGORY_GRADIENTS)], bold=False
                )
                if ansi else "  " + bar_raw
            )
        else:
            bar = ""
        print(
            f"  {category.label:<24} "
            f"{category.files:>4,} files  "
            f"{category.loc:>8,} LOC  "
            f"{category.physical_lines:>8,} physical"
            + bar
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
    files_source: str = "tracked",
) -> None:
    print("ILLUMO FIRST-PARTY SOURCE FILE STATISTICS")
    print(f"Scope: {scope_label}")
    print(f"Sorted by: {sort_label}")
    print()
    if not files:
        print("  No source files matched the requested filters.")
        return

    category_width = max(16, max(len(item.category) for item in files))
    print(
        f"  {'Rank':>4}  {'LOC':>7}  {'Physical':>8}  {'Category':<{category_width}}  Path"
    )
    print(
        f"  {'-' * 4}  {'-' * 7}  {'-' * 8}  {'-' * category_width}  {'-' * 44}"
    )
    for index, item in enumerate(files, start=1):
        rel_str = item.path.as_posix()
        print(
            f"  {index:>4}  {item.loc:>7,}  {item.physical_lines:>8,}  "
            f"{item.category:<{category_width}}  {rel_str}"
        )
    print(
        f"  {'-' * 4}  {'-' * 7}  {'-' * 8}  {'-' * category_width}  {'-' * 44}"
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
    source_text = "tracked files" if files_source == "tracked" else "discovered files"
    print(
        f"Scope: current contents of {source_text}; excludes build directories, "
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
        if key in COMPILER_OPTION_CHOICES and value not in {
            ident for ident, _label in COMPILER_OPTION_CHOICES[key]
        }:
            raise BuildError(f"Profile '{name}' has an unsupported {key}")
    if "config" in settings and settings["config"] not in (
        "Debug", "Release", "RelWithDebInfo", "MinSizeRel"
    ):
        raise BuildError(f"Profile '{name}' has an unsupported configuration")


def profile_arguments(settings: dict) -> list[str]:
    result: list[str] = []
    for key, value in settings.items():
        option = "--" + key.replace("_", "-")
        if key in COMPILER_OPTION_CHOICES and value == "default":
            continue
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
        if not (key == "no_wasm" and not getattr(arguments, key) and WASM_HOST_SUPPORTED)
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
    try:
        write_json_atomic(path, {"version": 1, "profiles": profiles})
    except OSError as error:
        raise BuildError(f"Could not save profiles to '{path}': {error}") from error
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
    title: str = ""

    @property
    def staged(self) -> bool:
        return self.module is not None and self.manifest is not None

    @property
    def label(self) -> str:
        return APP_LABELS.get(self.name) or self.title or self.name


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
            if (folder / "illumo.json").is_file():
                manifest = folder / "illumo.json"
        staged.append(AppOutput(package.name, module, manifest, package.title))
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
    ansi = cli_ansi_enabled()
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
    glyphs = terminal_glyphs()
    marks = {"ok": (glyphs["ok"], ANSI_GREEN), "warn": (glyphs["warn"], ANSI_YELLOW), "info": (glyphs["info"], ANSI_DIM)}
    heading = f"{title} in {format_duration(time.monotonic() - started)}"
    print()
    if ansi:
        heavy = glyphs["heavy"]
        print(
            _gradient_text_line(f"{glyphs['ok']} {heavy * 2} {heading} {heavy * 6}",
                                ((64, 242, 148), (112, 248, 255), (255, 232, 118)))
        )
    else:
        print(heading)
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
    resolved = resolve_compiler(
        getattr(arguments, "compiler", "default") or "default",
        generator=getattr(arguments, "generator", None),
    )
    cached_compiler = cache.get("CMAKE_CXX_COMPILER", "")
    if (
        resolved.cxx
        and cached_compiler
        and not getattr(arguments, "fresh", False)
        and Path(cached_compiler).stem.lower() != Path(resolved.cxx).stem.lower()
    ):
        raise BuildError(
            f"This tree was configured with '{cached_compiler}', but the selected "
            f"compiler is '{resolved.cxx}'. Choose another --build-dir or pass --fresh."
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
                minimum = max(minimum_cmake_version(), (3, 24) if arguments.fresh else (0, 0))
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
    selected_compiler = getattr(arguments, "compiler", "default") or "default"
    if selected_compiler != "default":
        resolved = resolve_compiler(
            selected_compiler, generator=getattr(arguments, "generator", None),
        )
        record(
            "compiler", "error" if resolved.missing else "ok",
            resolved.missing or resolved.summary,
        )
    else:
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
    game = game_directory()
    missing_game = game_missing_message()
    if game is not None:
        record("game", "ok", str(game))
    elif missing_game:
        # An engine-only workspace is complete; the game is optional.
        record("game", "ok", missing_game)
    ok = not any(check["status"] == "error" for check in checks)
    if arguments.json:
        print(json.dumps({"ok": ok, "checks": checks}, indent=2))
    else:
        glyphs = terminal_glyphs()
        ansi = sys.stdout.isatty()
        marks = {"ok": (glyphs["ok"], ANSI_GREEN), "warning": (glyphs["warn"], ANSI_YELLOW),
                 "error": (glyphs["bad"], ANSI_RED)}
        name_width = max(len(check["name"]) for check in checks)
        for check in checks:
            mark, style = marks.get(check["status"], (glyphs["info"], ""))
            # Continuation lines of a detail align under its first line.
            detail = check["detail"].replace("\n", "\n" + " " * (name_width + 6))
            print(f"  {dashboard_style(mark, style, ansi)} "
                  f"{dashboard_style(check['name'].ljust(name_width), ANSI_BOLD, ansi)}  {detail}")
        tally = {status: sum(1 for check in checks if check["status"] == status) for status in marks}
        print("\n  " + "   ".join(
            dashboard_style(f"{marks[status][0]} {count} {label}", marks[status][1] if count else ANSI_DIM, ansi)
            for status, count, label in (("ok", tally["ok"], "ok"),
                                         ("warning", tally["warning"], "warnings"),
                                         ("error", tally["error"], "errors"))))
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
    parser.add_argument(
        "--compiler",
        choices=tuple(key for key, _label in COMPILER_CHOICES),
        default="default",
        help=(
            "host compiler (default, msvc, intel, clang, gcc). "
            "The orchestrator selects the driver and translates optimization, "
            "warnings and the sanitizer into that compiler's flags. "
            "A non-default compiler uses build-workspace-<compiler> unless "
            "--build-dir names a different tree. Intel, Clang and GCC use Ninja."
        ),
    )
    parser.add_argument(
        "--cpu",
        choices=tuple(key for key, _label in CPU_CHOICES),
        default="default",
        help=(
            "instruction set: default baseline, avx2, avx512, or host. "
            "Host is /QxHost or -xHost for Intel and -march=native for Clang and GCC. "
            "It is off unless selected."
        ),
    )
    parser.add_argument(
        "--optimization",
        choices=tuple(key for key, _label in OPTIMIZATION_CHOICES),
        default="default",
        help=(
            "optimization translated for the selected compiler "
            "(default: Debug unoptimized, Release optimized; "
            "none, size, speed, or fast apply to every configuration)"
        ),
    )
    parser.add_argument(
        "--warnings",
        choices=tuple(key for key, _label in WARNING_CHOICES),
        default="default",
        help="warning level translated for the selected compiler (default, strict, quiet)",
    )
    parser.add_argument(
        "--sanitizer",
        choices=tuple(key for key, _label in SANITIZER_CHOICES),
        default="default",
        help="address sanitizer: default (on in Debug), off, or address",
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
        "--no-parallel",
        dest="parallel",
        action="store_const",
        const=None,
        help="disable explicit build parallelism (overrides a profile's --parallel)",
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
            "skip IllumoRuntime and the WASM application packages with "
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
        tracy=False, no_tests=False, no_docs=not documentation_available(), no_tidy=False,
        no_wasm=not WASM_HOST_SUPPORTED,
    )


def _add_clang_ninja_arguments(
    parser: argparse.ArgumentParser, default_dir: Path, label: str,
) -> None:
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=default_dir,
        metavar="PATH",
        help=f"{label} build tree (default: {default_dir})",
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
        "--theme",
        choices=THEME_ORDER,
        default=None,
        help="24-bit color theme (default: ILLUMO_THEME or illumo)",
    )
    menu_parser.add_argument(
        "--snapshot",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    menu_parser.add_argument(
        "--color",
        action="store_true",
        help=argparse.SUPPRESS,
    )

    lightshow_parser = subparsers.add_parser(
        "lightshow",
        aliases=["flair", "spectacle"],
        help="launch the 7-scene 24-bit TrueColor demoscene spectacle (3D Torus, Tunnel, Fireworks, Inferno, CSim Life, Plasma, Hyperwarp)",
    )
    lightshow_parser.add_argument(
        "--scene",
        choices=LIGHTSHOW_SCENE_KEYS,
        default="torus",
        help="starting visualizer scene (default: %(default)s)",
    )
    lightshow_parser.add_argument(
        "--auto",
        action="store_true",
        help="auto-cycle through all 7 scenes and themes (default for 'spectacle')",
    )
    lightshow_parser.add_argument(
        "--theme",
        choices=THEME_ORDER,
        default=None,
        help="24-bit color theme (default: ILLUMO_THEME or illumo)",
    )
    lightshow_parser.add_argument(
        "--frames",
        type=positive_count,
        default=None,
        metavar="N",
        help="stop automatically after N frames",
    )
    lightshow_parser.add_argument(
        "--snapshot",
        action="store_true",
        help="print one rendered frame and exit",
    )
    lightshow_parser.add_argument(
        "--color",
        action="store_true",
        help="force 24-bit ANSI output with --snapshot",
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
    subparsers.add_parser(
        "dist",
        parents=[common],
        help="build and assemble a distribution: the runtime with engine.ilpk "
        "and apps/<name>.ilpk in <build>/dist/<config>, with a shortcut per app "
        "on Windows",
    ).add_argument(
        "--apps",
        nargs="+",
        metavar="NAME",
        help="ship only these applications (space or comma separated, e.g. "
        "game,illed); default: every staged application",
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
        else (WASM_RUNTIME_APPLICATION,)
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
    default_package = DEFAULT_APP if DEFAULT_APP in packages else packages[0]
    play_parser = subparsers.add_parser(
        "play",
        parents=[common],
        help=f"build {WASM_RUNTIME_APPLICATION} and its applications, then run one",
    )
    play_parser.add_argument(
        "--app",
        dest="package",
        # The game stays a valid choice without its repository, so asking for
        # it explains how to add it instead of failing as an unknown name.
        choices=packages if DEFAULT_APP in packages else (*packages, DEFAULT_APP),
        default=default_package,
        help=f"installed application to run (default: {default_package}; "
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

    watch_parser = subparsers.add_parser(
        "watch",
        parents=[common],
        help="build, then rebuild (and re-run one test) whenever a source file changes",
    )
    watch_parser.add_argument("--target", help="rebuild a focused CMake target")
    watch_parser.add_argument("--test", metavar="NAME",
                              help="exact CTest name to run after each successful build")
    watch_parser.add_argument("--interval", type=positive_interval, default=1.0, metavar="SECONDS",
                              help="polling interval (default: %(default)s)")

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
    _add_clang_ninja_arguments(coverage_parser, DEFAULT_COVERAGE_DIRECTORY, "coverage")

    tidy_parser = subparsers.add_parser(
        "tidy", help="configure and run the workspace clang-tidy target"
    )
    _add_clang_ninja_arguments(tidy_parser, DEFAULT_TIDY_DIRECTORY, "clang-tidy")

    docs_parser = subparsers.add_parser(
        "docs", help="build the two documentation PDFs through docs/build.ps1"
    )
    docs_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without executing them",
    )
    version_parser = subparsers.add_parser(
        "version",
        help="show the build version vYY.MM_B, or start a release with --set",
    )
    version_parser.add_argument(
        "--set",
        metavar="YY.MM",
        help="start a release: write VERSION.txt and commit it (build numbers restart at 0)",
    )
    version_parser.add_argument(
        "--no-commit",
        action="store_true",
        help="with --set, write VERSION.txt without committing it",
    )
    version_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="with --set, preview the change without writing or committing",
    )
    version_parser.add_argument(
        "--json",
        action="store_true",
        help="emit every version field as JSON",
    )
    logs_parser = subparsers.add_parser(
        "logs",
        help="inspect or prune recorded dashboard logs under build-orchestrator-logs/",
    )
    logs_parser.add_argument(
        "--prune",
        action="store_true",
        help="delete older run logs and sidecars, keeping the newest --keep runs",
    )
    logs_parser.add_argument(
        "--keep",
        type=non_negative_count,
        default=25,
        metavar="COUNT",
        help="number of newest runs to retain when pruning (default: %(default)s)",
    )
    logs_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="with --prune, report what would be removed without deleting anything",
    )
    logs_parser.add_argument(
        "--json",
        action="store_true",
        help="emit machine-readable JSON",
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
        type=positive_count,
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
        type=positive_count,
        metavar="COUNT",
        help="limit output to the top COUNT largest files",
    )
    file_stats_parser.add_argument(
        "--min-loc",
        type=non_negative_count,
        default=0,
        metavar="LOC",
        help="only include files with at least LOC nonblank lines",
    )
    file_stats_parser.add_argument(
        "--category",
        choices=["production", "tests", "cpp", "shaders", "build", "docs", "config", "all"],
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
        help="filter files to a specific top-level directory (e.g., Illumo, IllEd, IllMeshViewer)",
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


def positive_count(value: str) -> int:
    try:
        count = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("count must be a positive integer") from error
    if count < 1:
        raise argparse.ArgumentTypeError("count must be at least 1")
    return count


def non_negative_count(value: str) -> int:
    try:
        count = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("value must be a non-negative integer") from error
    if count < 0:
        raise argparse.ArgumentTypeError("value must be 0 or greater")
    return count


def choice_label(choices: Sequence[tuple[str, str]], ident: str) -> str:
    for key, label in choices:
        if key == ident:
            return label
    return ident


def flag_text(encoded: str) -> str:
    return encoded.replace("|", " ")


def unsupported_features(compiler: str) -> frozenset[str]:
    return COMPILER_UNSUPPORTED.get(compiler or "default", frozenset())


def omit_unsupported_settings(settings: dict) -> None:
    """Pretend unsupported features were not set, so the command omits them."""
    missing = unsupported_features(settings.get("compiler") or "default")
    if "tidy" in missing:
        settings["no_tidy"] = True
    if "asan" in missing:
        settings["sanitizer"] = "off"


def omit_unsupported_arguments(arguments: argparse.Namespace) -> None:
    compiler = getattr(arguments, "compiler", "default") or "default"
    missing = unsupported_features(compiler)
    if "tidy" in missing and hasattr(arguments, "no_tidy"):
        arguments.no_tidy = True
    if "asan" in missing and hasattr(arguments, "sanitizer"):
        arguments.sanitizer = "off"


def compiler_build_directory(compiler: str) -> str:
    if compiler == "default":
        return str(DEFAULT_BUILD_DIRECTORY)
    return f"build-workspace-{compiler}"


def assigned_build_directory(build_dir: Path | str, compiler: str) -> Path:
    """Keep an explicit tree. The generic default tree is not shared across compilers."""
    path = Path(build_dir)
    if compiler != "default" and path == DEFAULT_BUILD_DIRECTORY:
        return Path(compiler_build_directory(compiler))
    return path


def assigned_generator(generator: str | None, compiler: str) -> str | None:
    if compiler in ("intel", "clang", "gcc") and not generator:
        return "Ninja"
    return generator


def compiler_generator_error(generator: str | None, compiler: str) -> str | None:
    if (
        generator
        and str(generator).startswith("Visual Studio")
        and compiler in ("intel", "clang", "gcc")
    ):
        label = choice_label(COMPILER_CHOICES, compiler)
        return (
            f"{label} is configured with Ninja and that compiler's driver. "
            "The Visual Studio generator cannot take CMAKE_CXX_COMPILER. "
            "Remove --generator, or choose Default or Microsoft MSVC."
        )
    return None


def apply_compiler_selection(arguments: argparse.Namespace, system: str | None = None) -> None:
    """Pin the build tree and generator that the selected compiler requires."""
    if not hasattr(arguments, "compiler"):
        return
    system = system or os.name
    compiler = arguments.compiler or "default"
    known = {key for key, _label in COMPILER_CHOICES}
    if compiler not in known:
        raise BuildError(
            f"Unknown compiler '{compiler}'. "
            f"Choose one of: {', '.join(sorted(known))}."
        )
    arguments.compiler = compiler
    if compiler == "msvc" and system != "nt":
        raise BuildError("The Microsoft compiler is only available on Windows.")
    omit_unsupported_arguments(arguments)
    arguments.build_dir = assigned_build_directory(arguments.build_dir, compiler)
    arguments.generator = assigned_generator(getattr(arguments, "generator", None), compiler)
    error = compiler_generator_error(arguments.generator, compiler)
    if error:
        raise BuildError(error)


def compiler_install_roots() -> tuple[Path, ...]:
    found: list[Path] = []
    for name in ("ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"):
        value = os.environ.get(name)
        if value:
            found.append(Path(value))
    if os.name == "nt":
        found.extend((Path(r"C:\Program Files"), Path(r"C:\Program Files (x86)")))
    unique: list[Path] = []
    seen: set[str] = set()
    for path in found:
        key = os.path.normcase(str(path))
        if key not in seen:
            seen.add(key)
            unique.append(path)
    return tuple(unique)


def compiler_candidates(names: Sequence[str], system: str) -> tuple[Path, ...]:
    """Install locations tried after PATH."""
    candidates: list[Path] = []
    suffix = ".exe" if system == "nt" else ""
    if "icx" in names or "icpx" in names:
        roots: list[Path] = []
        oneapi = os.environ.get("ONEAPI_ROOT")
        if oneapi:
            roots.append(Path(oneapi))
        if system == "nt":
            roots.extend(directory / "Intel" / "oneAPI" for directory in compiler_install_roots())
        else:
            roots.append(Path("/opt/intel/oneapi"))
        for root in roots:
            binary = root / "compiler" / "latest" / "bin"
            for name in names:
                candidates.append(binary / f"{name}{suffix}")
    if system == "nt" and ("clang-cl" in names or "clang" in names or "clang++" in names):
        for directory in compiler_install_roots():
            for name in names:
                candidates.append(directory / "LLVM" / "bin" / f"{name}.exe")
    return tuple(candidates)


def find_executable(names: Sequence[str], extra: Sequence[Path] = ()) -> Path | None:
    for name in names:
        found = shutil.which(name)
        if found:
            return Path(found)
    for path in extra:
        if path.is_file():
            return path
    return None


@dataclass(frozen=True)
class ResolvedCompiler:
    family: str
    language: str
    c: str | None
    cxx: str | None
    located: bool
    summary: str
    missing: str


def missing_compiler_message(family: str, system: str) -> str:
    if family == "msvc":
        return (
            "cl was not found on PATH. Open a Visual Studio developer prompt "
            "for a Ninja build, or leave the generator unset so CMake uses "
            "the Visual Studio toolset."
        )
    if family == "intel":
        tool = "icx" if system == "nt" else "icx and icpx"
        return (
            f"{tool} was not found. Install Intel oneAPI and open its command prompt, "
            "or add the compiler's bin directory to PATH."
        )
    if family == "clang" and system == "nt":
        return (
            "clang-cl was not found. Install LLVM for Windows so clang-cl is on PATH. "
            "That is the MSVC-compatible driver this build uses."
        )
    if family == "clang":
        return "clang and clang++ were not found on PATH."
    if family == "gcc":
        return "gcc and g++ were not found on PATH."
    return f"No {family} compiler was found."


def resolve_compiler(
    family: str,
    *,
    system: str | None = None,
    generator: str | None = None,
    search=None,
) -> ResolvedCompiler:
    """Find the driver for a menu choice and the flag dialect it accepts."""
    system = system or os.name
    family = family or "default"
    generator = generator or ""
    locate = search or find_executable

    def look(names: tuple[str, ...]) -> Path | None:
        return locate(names, compiler_candidates(names, system))

    if family == "default":
        language = "msvc" if system == "nt" else "gnu"
        summary = (
            "CMake's default compiler. On Windows that is the Visual Studio toolset, "
            "and custom options use MSVC flags."
            if system == "nt" else
            "CMake's default compiler. Custom options use GCC and Clang flags."
        )
        return ResolvedCompiler(family, language, None, None, True, summary, "")

    if family == "msvc" and system != "nt":
        message = "The Microsoft compiler is only available on Windows."
        return ResolvedCompiler(family, "msvc", None, None, False, message, message)

    if family == "msvc" and not generator.startswith("Ninja"):
        return ResolvedCompiler(
            family, "msvc", None, None, True,
            "Microsoft cl through the Visual Studio generator. "
            "A Ninja build needs cl on PATH.",
            "",
        )

    if family == "msvc":
        found = look(("cl",))
        path = str(found) if found is not None else "cl"
        missing = "" if found is not None else missing_compiler_message(family, system)
        summary = f"Microsoft cl: {path}" if found is not None else missing
        return ResolvedCompiler(family, "msvc", path, path, found is not None, summary, missing)

    if family == "intel" and system == "nt":
        found = look(("icx",))
        path = str(found) if found is not None else "icx"
        missing = "" if found is not None else missing_compiler_message(family, system)
        summary = f"Intel oneAPI icx: {path}" if found is not None else missing
        return ResolvedCompiler(
            family, "intel-windows", path, path, found is not None, summary, missing,
        )

    if family == "intel":
        c_found = look(("icx",))
        cxx_found = look(("icpx",))
        c_path = str(c_found) if c_found is not None else "icx"
        cxx_path = str(cxx_found) if cxx_found is not None else "icpx"
        located = c_found is not None and cxx_found is not None
        missing = "" if located else missing_compiler_message(family, system)
        summary = f"Intel oneAPI: {c_path} and {cxx_path}" if located else missing
        return ResolvedCompiler(family, "gnu", c_path, cxx_path, located, summary, missing)

    if family == "clang" and system == "nt":
        clang_cl = look(("clang-cl",))
        if clang_cl is not None:
            path = str(clang_cl)
            return ResolvedCompiler(
                family, "msvc", path, path, True, f"LLVM clang-cl: {path}", "",
            )
        c_found = look(("clang",))
        cxx_found = look(("clang++",))
        if c_found is not None and cxx_found is not None:
            return ResolvedCompiler(
                family, "gnu", str(c_found), str(cxx_found), True,
                f"LLVM clang++: {cxx_found}", "",
            )
        missing = missing_compiler_message(family, system)
        return ResolvedCompiler(family, "msvc", "clang-cl", "clang-cl", False, missing, missing)

    if family == "clang":
        c_found = look(("clang",))
        cxx_found = look(("clang++",))
        c_path = str(c_found) if c_found is not None else "clang"
        cxx_path = str(cxx_found) if cxx_found is not None else "clang++"
        located = c_found is not None and cxx_found is not None
        missing = "" if located else missing_compiler_message(family, system)
        summary = f"LLVM clang++: {cxx_path}" if located else missing
        return ResolvedCompiler(family, "gnu", c_path, cxx_path, located, summary, missing)

    if family != "gcc":
        message = f"Unknown compiler '{family}'."
        return ResolvedCompiler(family, "gnu", None, None, False, message, message)

    c_found = look(("gcc",))
    cxx_found = look(("g++",))
    c_path = str(c_found) if c_found is not None else "gcc"
    cxx_path = str(cxx_found) if cxx_found is not None else "g++"
    located = c_found is not None and cxx_found is not None
    missing = "" if located else missing_compiler_message("gcc", system)
    summary = f"GCC: {cxx_path}" if located else missing
    return ResolvedCompiler("gcc", "gnu", c_path, cxx_path, located, summary, missing)


def namespace_from_settings(settings: dict) -> argparse.Namespace:
    return argparse.Namespace(
        compiler=settings.get("compiler", "default"),
        optimization=settings.get("optimization", "default"),
        cpu=settings.get("cpu", "default"),
        warnings=settings.get("warnings", "default"),
        sanitizer=settings.get("sanitizer", "default"),
        generator=settings.get("generator"),
        build_dir=Path(str(settings.get("build_dir", DEFAULT_BUILD_DIRECTORY))),
        fresh=False,
        config=settings.get("config", "Release"),
    )


def compiler_cmake_arguments(
    arguments: argparse.Namespace,
    *,
    system: str | None = None,
    search=None,
) -> list[str]:
    """CMake arguments for the selected compiler. Empty options clear a stale cache."""
    omit_unsupported_arguments(arguments)
    family = getattr(arguments, "compiler", "default") or "default"
    resolved = resolve_compiler(
        family,
        system=system,
        generator=getattr(arguments, "generator", None),
        search=search,
    )
    args: list[str] = []
    if resolved.c and resolved.cxx:
        args.append(f"-DCMAKE_C_COMPILER={resolved.c}")
        args.append(f"-DCMAKE_CXX_COMPILER={resolved.cxx}")
    if (system or os.name) == "nt" and family == "intel":
        manifest_tool = windows_sdk_tool_directory()
        if manifest_tool is not None:
            args.append(f"-DCMAKE_MT={manifest_tool / 'mt.exe'}")
    optimization = getattr(arguments, "optimization", "default") or "default"
    cpu = getattr(arguments, "cpu", "default") or "default"
    warnings = getattr(arguments, "warnings", "default") or "default"
    sanitizer = getattr(arguments, "sanitizer", "default") or "default"
    opt = "" if optimization == "default" else OPTIMIZATION_FLAGS[resolved.language][optimization]
    cpu_flag = cpu_compile_flag(family, resolved.language, cpu)
    warn = "" if warnings == "default" else WARNING_FLAGS[resolved.language][warnings]
    args.append(f"-DILLUMO_ORCHESTRATOR_OPTIMIZATION={opt}")
    args.append(f"-DILLUMO_ORCHESTRATOR_CPU={cpu_flag}")
    args.append(f"-DILLUMO_ORCHESTRATOR_WARNINGS={warn}")
    if sanitizer == "off":
        args.append("-DILLUMO_ENABLE_ASAN=OFF")
    elif sanitizer == "address":
        args.append("-DILLUMO_ENABLE_ASAN=ON")
    return args


def require_selected_compiler(arguments: argparse.Namespace, dry_run: bool) -> None:
    family = getattr(arguments, "compiler", "default") or "default"
    resolved = resolve_compiler(family, generator=getattr(arguments, "generator", None))
    if not resolved.missing:
        return
    if dry_run:
        print(f"warning: {resolved.missing}", file=sys.stderr)
        return
    raise BuildError(resolved.missing)


def configure_command(arguments: argparse.Namespace, cmake: str) -> list[str]:
    omit_unsupported_arguments(arguments)
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
    command.extend(compiler_cmake_arguments(arguments))
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
        + f".\nInteractive applications build as WASM packages ({wasmtime}, {wasi_sdk}). "
        "Run 'python build.py wasm-tools' to download and verify the pinned "
        "toolchain, or pass --no-wasm to build without IllumoRuntime and the apps."
    )


def require_wasm_toolchain(arguments: argparse.Namespace, dry_run: bool) -> None:
    if not wasm_requested(arguments):
        return
    if not WASM_HOST_SUPPORTED:
        raise BuildError(
            "IllumoRuntime and its WASM application packages require Windows x64; "
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
    *,
    clean: bool | None = None,
) -> list[str]:
    build_directory = resolve_build_directory(arguments.build_dir)
    command = [
        cmake,
        "--build",
        str(build_directory),
        "--config",
        arguments.config,
    ]
    if getattr(arguments, "clean", False) if clean is None else clean:
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
    require_selected_compiler(arguments, runner.dry_run)
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
    config_lower = configuration.lower()
    pruned = {"cmakefiles", "testing", "dist", "dist-staging", "wasm-guests"}
    for root_dir, subdirs, files in os.walk(build_directory):
        subdirs[:] = [d for d in subdirs if d.lower() not in pruned]
        for file in files:
            if file.lower() == target_filename:
                found = Path(root_dir) / file
                if any(part.lower() == config_lower for part in found.parts) or len(candidates) <= 2:
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


def distribution_app_names(requested: Sequence[str] | None) -> tuple[str, ...]:
    """The applications `dist --apps` names, validated; empty means all of them.

    Names may be space or comma separated. Each must be one the runtime build
    stages (an unknown one would silently ship a distribution without it).
    """
    names: list[str] = []
    for item in requested or ():
        for name in item.split(","):
            name = name.strip()
            if name and name not in names:
                names.append(name)
    known = tuple(app.name for app in installed_apps())
    unknown = [name for name in names if known and name not in known]
    if unknown:
        raise BuildError(
            f"Unknown application(s) for --apps: {', '.join(unknown)} "
            f"(available: {', '.join(known)})"
        )
    return tuple(names)


def run_distribution(arguments: argparse.Namespace) -> None:
    """Build the runtime and pack its distribution (D-E38).

    The IllumoDistribution target depends on IllumoRuntime (which stages every
    app package), IllumoPack and IllumoVersionInfo, and leaves dist/<config>/
    in the build tree with engine.ilpk and apps/<name>.ilpk.
    """
    if not wasm_requested(arguments):
        raise BuildError(
            "Assembling a distribution requires the WASM runtime; "
            "drop --no-wasm (distributions are Windows x64 only)."
        )
    started = time.monotonic()
    chosen = distribution_app_names(arguments.apps)
    runner = CommandRunner(arguments.dry_run)
    cmake = configure(arguments, runner)
    # Pass ILLUMO_DIST_APPS per-process so it layers over any cached compiler
    # environment (e.g. Intel setvars.bat) without mutating global os.environ.
    if chosen:
        print(f"Distribution apps: {', '.join(chosen)}", flush=True)
    env_overrides: dict[str, str | None] = {
        DISTRIBUTION_APPS_VARIABLE: ",".join(chosen) if chosen else None,
    }
    runner.run(
        build_command(arguments, cmake, "IllumoDistribution"),
        env_overrides=env_overrides,
    )
    build_directory = resolve_build_directory(arguments.build_dir)
    print_build_summary(arguments, "Distribution assembled", started)
    print(f"Distribution: {build_directory / 'dist' / arguments.config}")


def run_ctest_case(
    arguments: argparse.Namespace, cmake: str, runner: CommandRunner, test: dict,
    workspace: WorkspaceProjects | None = None,
) -> bool:
    """Run one exact CTest case that is not owned by a discovered runner.

    Returns False when the case belongs to a discovered runner, which keeps
    its direct --run invocation.
    """
    build_directory = resolve_build_directory(arguments.build_dir)
    if workspace is None:
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
        # name prefixes with discovered runners, so ask CTest first. When CTest
        # identifies a discovered runner (e.g. IllumoPhysicsTests for Illumo.Physics.*),
        # keep that exact runner target directly instead of re-guessing by prefix.
        target: str | None = None
        if not runner.dry_run and arguments.test != PUBLIC_HEADER_SMOKE_TEST:
            listing = ctest_listing(existing_tool("ctest", False), build_directory, arguments.config)
            test = next((item for item in listing if item["name"] == arguments.test), None)
            if test is not None:
                ctest_target = ctest_executable_target(test, build_directory)
                if ctest_target in workspace.test_runners or ctest_target in workspace.smoke_targets:
                    target = ctest_target
                elif run_ctest_case(arguments, cmake, runner, test, workspace):
                    return
        if target is None:
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
        all_targets = [*workspace.test_runners, *workspace.smoke_targets]
        if all_targets:
            runner.run(build_command(arguments, cmake, all_targets))
        for target in workspace.test_runners:
            test_binary = executable_path(
                build_directory,
                arguments.config,
                target,
                runner.dry_run,
                workspace,
            )
            runner.run((str(test_binary), "--list"), test_binary.parent)
        for smoke_target in workspace.smoke_targets:
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
    packages = app_names()
    package = getattr(arguments, "package", None) or (
        DEFAULT_APP if DEFAULT_APP in packages else packages[0])
    missing_game = game_missing_message()
    if package == DEFAULT_APP and DEFAULT_APP not in packages and missing_game:
        raise BuildError(missing_game)
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


WATCH_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl",
    ".vert", ".frag", ".glsl", ".cmake", ".json", ".ilsc",
}
# Top-level scratch and output folders; build*, cmake-build*, archive,
# thirdparty and dot-directories are pruned everywhere.
WATCH_PRUNED_ROOTS = {"docs", "tmp", "scratch", "output", "licenses", "__pycache__"}


def watch_snapshot(root: Path) -> dict[str, int]:
    """Modification times of first-party sources, shaders and CMake files."""
    snapshot: dict[str, int] = {}
    for directory, children, files in os.walk(root):
        relative = Path(directory).relative_to(root)
        children[:] = [
            name for name in children
            if not name.startswith(".") and name != "__pycache__"
            and not (relative == Path(".") and name.lower() in WATCH_PRUNED_ROOTS)
            and not is_excluded_repository_path(relative / name / "entry")
        ]
        for name in files:
            if name == "CMakeLists.txt" or Path(name).suffix.lower() in WATCH_SUFFIXES:
                path = Path(directory) / name
                try:
                    snapshot[(relative / name).as_posix()] = path.stat().st_mtime_ns
                except OSError:
                    continue  # Deleted between listing and stat.
    return snapshot


def watch_changes(before: dict[str, int], after: dict[str, int]) -> list[str]:
    return sorted(path for path in before.keys() | after.keys() if before.get(path) != after.get(path))


def run_watch_cycle(
    runner: CommandRunner, commands: Sequence[Sequence[str]], changed: Sequence[str],
    cycle: int, glyphs: dict, ansi: bool,
) -> bool:
    """Run the build (then the test) once; report the outcome, never raise."""
    heavy = glyphs["heavy"]
    heading = f"{glyphs['refresh']} Cycle {cycle}"
    if changed:
        shown = ", ".join(changed[:4]) + (f" and {len(changed) - 4} more" if len(changed) > 4 else "")
        heading += f": {len(changed)} changed ({shown})"
    print()
    banner_line = f"{heavy * 2} {heading} {heavy * 8}"
    print(_gradient_text_line(banner_line) if ansi else banner_line, flush=True)
    started = time.monotonic()
    ok = True
    for command in commands:
        try:
            runner.run(command)
        except BuildError as error:
            print(str(error), file=sys.stderr, flush=True)
            ok = False
            break
    elapsed = format_duration(time.monotonic() - started)
    mark, style = (glyphs["ok"], ANSI_GREEN) if ok else (glyphs["bad"], ANSI_RED)
    print(dashboard_style(f"{mark} Cycle {cycle} {'passed' if ok else 'failed'} in {elapsed}",
                          ANSI_BOLD + style, ansi), flush=True)
    if ansi:
        sys.stdout.write(f"\x1b]2;Illumo watch | cycle {cycle} {'passed' if ok else 'failed'}\x07"
                         + ("" if ok else "\a"))
        sys.stdout.flush()
    return ok


def run_watch(arguments: argparse.Namespace) -> None:
    """Build once, then rebuild (and re-run --test) whenever a source changes."""
    if arguments.test and getattr(arguments, "no_tests", False):
        raise BuildError("Cannot run --test in watch mode when testing is disabled via --no-tests.")
    runner = CommandRunner(arguments.dry_run)
    cmake = configure(arguments, runner)
    build_directory = resolve_build_directory(arguments.build_dir)
    first_build = build_command(arguments, cmake)
    subsequent_build = build_command(arguments, cmake, clean=False)
    test_command: list[str] | None = None
    if arguments.test:
        test_command = [
            existing_tool("ctest", runner.dry_run), "--test-dir", str(build_directory),
            "-C", arguments.config, "-R", test_name_batches([arguments.test])[0][1],
            "--no-tests=error", "--output-on-failure",
        ]
    first_commands = [first_build, *([test_command] if test_command else [])]
    repeat_commands = [subsequent_build, *([test_command] if test_command else [])]
    if runner.dry_run:
        for command in first_commands:
            runner.run(command)
        return
    glyphs = terminal_glyphs()
    ansi = sys.stdout.isatty()
    snapshot = watch_snapshot(REPOSITORY_ROOT)
    changed: list[str] = []
    cycle = 0
    if ansi:
        sys.stdout.write(ANSI_PUSH_TITLE)
        sys.stdout.flush()
    try:
        while True:
            cycle += 1
            run_watch_cycle(
                runner, first_commands if cycle == 1 else repeat_commands,
                changed, cycle, glyphs, ansi,
            )
            print(dashboard_style(
                f"Watching {len(snapshot):,} files; save one to rebuild (Ctrl+C stops).",
                ANSI_DIM, ansi), flush=True)
            changed = []
            while not changed:
                time.sleep(arguments.interval)
                current = watch_snapshot(REPOSITORY_ROOT)
                if watch_changes(snapshot, current):
                    # Editors and generators write in bursts; let them settle.
                    time.sleep(0.3)
                    current = watch_snapshot(REPOSITORY_ROOT)
                    changed = watch_changes(snapshot, current)
                    snapshot = current
    except KeyboardInterrupt:
        print("\n" + dashboard_style(f"Watch stopped after {cycle} cycle{'s' if cycle != 1 else ''}.",
                                     ANSI_DIM, ansi), flush=True)
    finally:
        if ansi:
            sys.stdout.write(ANSI_POP_TITLE)
            sys.stdout.flush()


def positive_interval(value: str) -> float:
    try:
        seconds = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("interval must be a number of seconds") from error
    if not 0.1 <= seconds <= 60:
        raise argparse.ArgumentTypeError("interval must be between 0.1 and 60 seconds")
    return seconds


def run_wasm_tools(arguments: argparse.Namespace) -> None:
    if not WASM_HOST_SUPPORTED:
        raise BuildError("The pinned Wasmtime and WASI SDK toolchain is Windows x64 only.")
    runner = CommandRunner(arguments.dry_run)
    missing = missing_wasm_tools(DEFAULT_WASM_TOOLS_DIRECTORY)
    wasmtime, wasi_sdk = wasm_toolchain_packages()
    if not missing:
        print(f"WASM toolchain ready: {wasmtime} + {wasi_sdk} in {DEFAULT_WASM_TOOLS_DIRECTORY}")
        return
    powershell = find_powershell(runner.dry_run, WASM_BOOTSTRAP_SCRIPT.name)
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
    print("WASM toolchain ready. 'python build.py play' builds and launches an application.")


def _run_clang_ninja_target(
    arguments: argparse.Namespace,
    *,
    target: str,
    coverage: bool,
    tidy: bool,
    extra_tools: Sequence[str] = (),
) -> None:
    runner = CommandRunner(arguments.dry_run)
    cmake = existing_tool("cmake", runner.dry_run)
    for tool in ("ninja", "clang", "clang++", *extra_tools):
        existing_tool(tool, runner.dry_run)
    if tidy and not runner.dry_run:
        tidy_path = shutil.which("clang-tidy")
        tidy_dir = Path(tidy_path).parent if tidy_path else None
        has_runner = bool(
            shutil.which("run-clang-tidy")
            or shutil.which("run-clang-tidy.py")
            or (tidy_dir and (
                (tidy_dir / "run-clang-tidy").is_file()
                or (tidy_dir / "run-clang-tidy.py").is_file()
            ))
        )
        if not has_runner:
            raise BuildError(
                "Required tool 'run-clang-tidy' (or 'run-clang-tidy.py') was not found on PATH "
                "or beside clang-tidy; IllumoTidy requires it."
            )
    check_args = argparse.Namespace(
        build_dir=arguments.build_dir,
        generator="Ninja",
        architecture=None,
        compiler="clang",
        fresh=getattr(arguments, "fresh", False),
    )
    validate_build_settings(check_args)
    build_directory = resolve_build_directory(arguments.build_dir)
    configure_cmd = [
        cmake,
        "-S",
        str(SOURCE_DIRECTORY),
        "-B",
        str(build_directory),
        *(["--fresh"] if getattr(arguments, "fresh", False) else []),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER=clang++",
        "-DILLUMO_BUILD_DOCUMENTATION=OFF",
        "-DILLUMO_ENABLE_TRACY=OFF",
        f"-DILLUMO_ENABLE_COVERAGE={'ON' if coverage else 'OFF'}",
        f"-DILLUMO_ENABLE_CLANG_TIDY={'ON' if tidy else 'OFF'}",
    ]
    configure_cmd.extend(arguments.cmake_arg)
    runner.run(configure_cmd)

    build_cmd = [
        cmake,
        "--build",
        str(build_directory),
        "--target",
        target,
    ]
    if arguments.parallel is not None:
        build_cmd.append("--parallel")
        if arguments.parallel > 0:
            build_cmd.append(str(arguments.parallel))
    if arguments.verbose:
        build_cmd.append("--verbose")
    runner.run(build_cmd)


def run_coverage(arguments: argparse.Namespace) -> None:
    _run_clang_ninja_target(
        arguments,
        target="IllumoCoverage",
        coverage=True,
        tidy=False,
        extra_tools=("llvm-profdata", "llvm-cov"),
    )


def run_tidy(arguments: argparse.Namespace) -> None:
    _run_clang_ninja_target(
        arguments,
        target="IllumoTidy",
        coverage=False,
        tidy=True,
        extra_tools=("clang-tidy",),
    )


def run_docs(arguments: argparse.Namespace) -> None:
    docs_script = REPOSITORY_ROOT / "docs" / "build.ps1"
    if not docs_script.is_file():
        raise BuildError(
            f"Documentation script not found at {docs_script}. "
            "Generated workspaces omit the engine LaTeX documentation sources."
        )
    runner = CommandRunner(arguments.dry_run)
    powershell = find_powershell(runner.dry_run, "docs/build.ps1")
    runner.run(
        (
            powershell,
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(docs_script),
        )
    )


def prune_orchestrator_logs(
    keep: int, directory: Path | None = None, dry_run: bool = False,
) -> dict[str, object]:
    """List or prune older run logs and their sidecar files beyond the newest `keep` runs."""
    log_dir = directory or (REPOSITORY_ROOT / "build-orchestrator-logs")
    if not log_dir.is_dir():
        return {"directory": str(log_dir), "total_runs": 0, "kept_runs": 0,
                "removed_runs": 0, "removed_files": [], "reclaimed_bytes": 0, "dry_run": dry_run}
    runs = run_history(log_dir)
    stale = runs[keep:]
    removed_files: list[str] = []
    reclaimed_bytes = 0
    if stale:
        stale_logs = {Path(run["log"]) for run in stale}
        stale_prefixes = tuple(path.stem + "." for path in stale_logs)
        candidates = sorted(
            entry for entry in log_dir.iterdir()
            if entry.is_file() and (entry in stale_logs or entry.name.startswith(stale_prefixes))
        )
        for path in candidates:
            try:
                reclaimed_bytes += path.stat().st_size
            except OSError:
                pass
            removed_files.append(path.name)
            if not dry_run:
                path.unlink(missing_ok=True)
    return {
        "directory": str(log_dir),
        "total_runs": len(runs),
        "kept_runs": min(len(runs), keep),
        "removed_runs": len(stale),
        "removed_files": removed_files,
        "reclaimed_bytes": reclaimed_bytes,
        "dry_run": dry_run,
    }


def run_logs(arguments: argparse.Namespace) -> None:
    log_dir = REPOSITORY_ROOT / "build-orchestrator-logs"
    if arguments.prune:
        report = prune_orchestrator_logs(arguments.keep, log_dir, dry_run=arguments.dry_run)
        if arguments.json:
            print(json.dumps(report, indent=2))
            return
        verb = "Would remove" if arguments.dry_run else "Removed"
        print(
            f"{verb} {report['removed_runs']} run(s) ({len(report['removed_files'])} files, "
            f"{format_size(report['reclaimed_bytes'])}); kept {report['kept_runs']} of {report['total_runs']}."
        )
        return
    runs = run_history(log_dir) if log_dir.is_dir() else []
    total_bytes = sum(
        entry.stat().st_size for entry in log_dir.iterdir() if entry.is_file()
    ) if log_dir.is_dir() else 0
    if arguments.json:
        print(json.dumps({
            "directory": str(log_dir),
            "runs": len(runs),
            "total_bytes": total_bytes,
            "recent": [{"log": Path(r["log"]).name, "action": r.get("action"),
                        "status": r.get("status"), "elapsed": r.get("elapsed")}
                       for r in runs[:20]],
        }, indent=2))
        return
    print(f"Orchestrator logs: {log_dir} ({len(runs)} runs, {format_size(total_bytes)})")
    glyphs = terminal_glyphs()
    for run in runs[:20]:
        print("  " + run_history_label(run, glyphs))
    if len(runs) > 20:
        print(f"  ... and {len(runs) - 20} older run(s). Use 'python build.py logs --prune --keep 25' to clean up.")


def run_source_file_statistics(
    arguments: argparse.Namespace,
    statistics: RepositoryStatistics | None = None,
) -> None:
    if statistics is None:
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
            files_source=statistics.repository_files_source,
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
            run_source_file_statistics(arguments, statistics=statistics)


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


def _run_interactive_dashboard(theme_name: str | None = None) -> int:
    try:
        return run_dashboard(theme_name=theme_name)
    except KeyboardInterrupt:
        print("\nBuild console closed.", file=sys.stderr)
        return 130
    except BuildError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.exit_code


def main(arguments: Sequence[str] | None = None) -> int:
    workspace = discover_workspace_projects(REPOSITORY_ROOT)
    parser = create_parser(workspace)
    command_line = sys.argv[1:] if arguments is None else list(arguments)
    if not command_line:
        if sys.stdin.isatty() and sys.stdout.isatty():
            return _run_interactive_dashboard()
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
        if parsed.command in COMPILER_COMMANDS:
            apply_compiler_selection(parsed)
    except BuildError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.exit_code

    if parsed.command == "menu":
        if parsed.snapshot:
            snap_state = DashboardState(theme=resolve_theme(parsed.theme).key)
            print(
                render_dashboard(
                    snap_state,
                    96,
                    ansi=bool(parsed.color),
                    terminal_rows=36 if parsed.color else None,
                    frame=12 if parsed.color else None,
                )
            )
            return 0
        return _run_interactive_dashboard(theme_name=parsed.theme)

    actions = {
        "_toolbox": run_toolbox_request,
        "profiles": run_profiles,
        "profile-save": run_profile_save,
        "doctor": run_doctor,
        "configure": run_configure,
        "build": run_build,
        "dist": run_distribution,
        "test": run_tests,
        "run": run_application,
        "play": run_application,
        "wasm-tools": run_wasm_tools,
        "watch": run_watch,
        "coverage": run_coverage,
        "tidy": run_tidy,
        "docs": run_docs,
        "version": run_version,
        "logs": run_logs,
        "lightshow": run_lightshow_cli,
        "flair": run_lightshow_cli,
        "spectacle": run_lightshow_cli,
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
