#!/usr/bin/env python3
"""Front end for the Illumo library, IllumoGame, and IllEd workspace."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from typing import Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parent
SOURCE_DIRECTORY = REPOSITORY_ROOT
DEFAULT_BUILD_DIRECTORY = Path("build-workspace")
DEFAULT_COVERAGE_DIRECTORY = Path("build-workspace-coverage")
PUBLIC_HEADER_SMOKE_TEST = "Illumo.PublicHeaders.ConsumerSmoke"

ANSI_RESET = "\x1b[0m"
ANSI_BOLD = "\x1b[1m"
ANSI_DIM = "\x1b[2m"
ANSI_CYAN = "\x1b[38;5;45m"
ANSI_GREEN = "\x1b[38;5;82m"
ANSI_YELLOW = "\x1b[38;5;220m"
ANSI_BLUE = "\x1b[38;5;111m"
ANSI_REVERSE = "\x1b[7m"
ANSI_CLEAR = "\x1b[2J\x1b[H"
ANSI_ENTER_SCREEN = "\x1b[?1049h"
ANSI_LEAVE_SCREEN = "\x1b[?1049l"
ANSI_HIDE_CURSOR = "\x1b[?25l"
ANSI_SHOW_CURSOR = "\x1b[?25h"

DASHBOARD_CONFIGURATIONS = ("Release", "Debug", "RelWithDebInfo")
DASHBOARD_APPLICATIONS = ("IllumoGame", "IllEd")
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
    ("setting", "Documentation", "documentation"),
    ("setting", "Tracy profiling", "tracy"),
    ("setting", "Parallel build", "parallel"),
    ("action", "Build everything", "build"),
    ("action", "Build application", "build_app"),
    ("action", "Run headless tests", "test"),
    ("action", "Build and run application", "run"),
    ("action", "Run existing build", "launch"),
    ("action", "Repository statistics", "stats"),
    ("action", "Build documentation", "docs"),
    ("action", "Run LLVM coverage", "coverage"),
    ("action", "Exit", "quit"),
)
DASHBOARD_DESCRIPTIONS = {
    "build": "applications, tests, and optional PDFs",
    "build_app": "focused target for selected application",
    "test": "all three isolated test runners",
    "run": "build, then launch selected application",
    "launch": "skip configure and build",
    "stats": "Git state, files, and first-party LOC",
    "docs": "illumo.pdf and architecture-map.pdf",
    "coverage": "Ninja, Clang, and the 85% gate",
    "quit": "return to the shell",
}


class BuildError(RuntimeError):
    """A user-facing build orchestration failure."""

    def __init__(self, message: str, exit_code: int = 1) -> None:
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class LineStatistics:
    label: str
    files: int = 0
    physical_lines: int = 0
    loc: int = 0


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
    documentation_enabled: bool = True
    tracy_enabled: bool = False
    parallel_index: int = 0
    status: str = "Ready"
    status_kind: str = "normal"

    @property
    def configuration(self) -> str:
        return DASHBOARD_CONFIGURATIONS[self.configuration_index]

    @property
    def application(self) -> str:
        return DASHBOARD_APPLICATIONS[self.application_index]

    @property
    def parallel_value(self) -> int | None:
        return DASHBOARD_PARALLEL_OPTIONS[self.parallel_index][1]


class DashboardTerminal:
    """Own the alternate screen and raw keyboard mode while the menu is open."""

    def __init__(self) -> None:
        self.input_fd: int | None = None
        self.original_attributes: object | None = None

    def enter(self) -> None:
        enable_virtual_terminal_processing()
        if os.name != "nt":
            import termios
            import tty

            self.input_fd = sys.stdin.fileno()
            self.original_attributes = termios.tcgetattr(self.input_fd)
            tty.setraw(self.input_fd)
        sys.stdout.write(ANSI_ENTER_SCREEN + ANSI_HIDE_CURSOR)
        sys.stdout.flush()

    def leave(self) -> None:
        if os.name != "nt" and self.original_attributes is not None:
            import termios

            termios.tcsetattr(
                self.input_fd, termios.TCSADRAIN, self.original_attributes
            )
            self.original_attributes = None
            self.input_fd = None
        sys.stdout.write(ANSI_RESET + ANSI_SHOW_CURSOR + ANSI_LEAVE_SCREEN)
        sys.stdout.flush()


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


def dashboard_style(text: str, style: str, ansi: bool) -> str:
    if not ansi or not style:
        return text
    return f"{style}{text}{ANSI_RESET}"


def dashboard_value(state: DashboardState, key: str) -> str:
    if key == "configuration":
        return state.configuration
    if key == "application":
        return state.application
    if key == "documentation":
        return "On" if state.documentation_enabled else "Off"
    if key == "tracy":
        return "On" if state.tracy_enabled else "Off"
    if key == "parallel":
        return DASHBOARD_PARALLEL_OPTIONS[state.parallel_index][0]
    return ""


def render_dashboard(
    state: DashboardState, terminal_width: int, ansi: bool = True
) -> str:
    width = max(54, min(94, terminal_width - 2))
    inner_width = width - 2
    lines: list[str] = []
    encoding = sys.stdout.encoding or "utf-8"
    try:
        "╭─╮│├┤╰╯▶‹›↑↓←→".encode(encoding)
        glyphs = {
            "top_left": "╭",
            "top_right": "╮",
            "middle_left": "├",
            "middle_right": "┤",
            "bottom_left": "╰",
            "bottom_right": "╯",
            "horizontal": "─",
            "vertical": "│",
            "marker": "▶",
            "left": "‹",
            "right": "›",
            "help": "↑↓ navigate   ←→ change   Enter select   q quit",
        }
    except UnicodeEncodeError:
        glyphs = {
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

    def border(left: str, fill: str, right: str) -> None:
        lines.append(left + (fill * inner_width) + right)

    def content(
        value: str = "", style: str = "", align: str = "left"
    ) -> None:
        if len(value) > inner_width - 2:
            value = value[: inner_width - 5] + "..."
        if align == "center":
            padded = value.center(inner_width)
        else:
            padded = (" " + value).ljust(inner_width)
        lines.append(
            glyphs["vertical"]
            + dashboard_style(padded, style, ansi)
            + glyphs["vertical"]
        )

    border(glyphs["top_left"], glyphs["horizontal"], glyphs["top_right"])
    content("ILLUMO WORKSPACE BUILD CONSOLE", ANSI_BOLD + ANSI_CYAN, "center")
    content(
        "CMake orchestration without the ceremony",
        ANSI_DIM,
        "center",
    )
    border(
        glyphs["middle_left"],
        glyphs["horizontal"],
        glyphs["middle_right"],
    )
    last_kind = None
    for index, (kind, label, key) in enumerate(DASHBOARD_ITEMS):
        if kind != last_kind:
            if last_kind is not None:
                border(
                    glyphs["middle_left"],
                    glyphs["horizontal"],
                    glyphs["middle_right"],
                )
            section_title = "Settings" if kind == "setting" else "Actions"
            content(section_title, ANSI_BOLD + ANSI_BLUE)
            last_kind = kind

        marker = glyphs["marker"] if index == state.selected else " "
        if kind == "setting":
            value = dashboard_value(state, key)
            available = max(
                1, inner_width - len(label) - len(value) - 8
            )
            raw = (
                f" {marker} {label}{' ' * available}"
                f"{glyphs['left']} {value} {glyphs['right']} "
            )
        else:
            if key == "build_app":
                description = f"focused {state.application} target"
            elif key == "run":
                description = f"build, then launch {state.application}"
            elif key == "launch":
                description = f"launch existing {state.application}"
            else:
                description = DASHBOARD_DESCRIPTIONS[key]
            if inner_width >= 76:
                available = max(1, inner_width - len(label) - len(description) - 7)
                raw = f" {marker} {label}{' ' * available}{description} "
            else:
                raw = f" {marker} {label} "
        raw = raw[:inner_width].ljust(inner_width)
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
    content(glyphs["help"], ANSI_DIM)
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


def read_dashboard_key() -> str:
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
        return {
            "\r": "enter",
            "q": "quit",
            "Q": "quit",
            "j": "down",
            "k": "up",
            "h": "left",
            "l": "right",
        }.get(character, "unknown")

    character = sys.stdin.read(1)
    if character == "\x03":
        raise KeyboardInterrupt
    if character == "\x1b":
        import select

        sequence = ""
        while len(sequence) < 2 and select.select([sys.stdin], [], [], 0.03)[0]:
            sequence += sys.stdin.read(1)
        return {
            "[A": "up",
            "[B": "down",
            "[C": "right",
            "[D": "left",
        }.get(sequence, "quit")
    return {
        "\r": "enter",
        "\n": "enter",
        "q": "quit",
        "Q": "quit",
        "j": "down",
        "k": "up",
        "h": "left",
        "l": "right",
    }.get(character, "unknown")


def adjust_dashboard_setting(state: DashboardState, direction: int) -> None:
    key = DASHBOARD_ITEMS[state.selected][2]
    if key == "configuration":
        state.configuration_index = (
            state.configuration_index + direction
        ) % len(DASHBOARD_CONFIGURATIONS)
    elif key == "application":
        state.application_index = (
            state.application_index + direction
        ) % len(DASHBOARD_APPLICATIONS)
    elif key == "documentation":
        state.documentation_enabled = not state.documentation_enabled
    elif key == "tracy":
        state.tracy_enabled = not state.tracy_enabled
    elif key == "parallel":
        state.parallel_index = (state.parallel_index + direction) % len(
            DASHBOARD_PARALLEL_OPTIONS
        )


def dashboard_parallel_arguments(state: DashboardState) -> list[str]:
    parallel = state.parallel_value
    if parallel is None:
        return []
    if parallel == 0:
        return ["--parallel"]
    return ["--parallel", str(parallel)]


def dashboard_common_arguments(state: DashboardState) -> list[str]:
    arguments = ["--config", state.configuration]
    if not state.documentation_enabled:
        arguments.append("--no-docs")
    if state.tracy_enabled:
        arguments.append("--tracy")
    arguments.extend(dashboard_parallel_arguments(state))
    return arguments


def dashboard_action_arguments(
    state: DashboardState, action: str
) -> list[str]:
    if action == "build":
        return ["build", *dashboard_common_arguments(state)]
    if action == "build_app":
        return [
            "build",
            *dashboard_common_arguments(state),
            "--target",
            state.application,
        ]
    if action == "test":
        return ["test", *dashboard_common_arguments(state)]
    if action == "run":
        return [
            "run",
            "--app",
            state.application,
            *dashboard_common_arguments(state),
        ]
    if action == "launch":
        return [
            "run",
            "--app",
            state.application,
            "--config",
            state.configuration,
            "--no-build",
        ]
    if action == "stats":
        return ["stats"]
    if action == "docs":
        return ["docs"]
    if action == "coverage":
        return ["coverage", *dashboard_parallel_arguments(state)]
    raise BuildError(f"Unknown dashboard action: {action}")


def execute_dashboard_action(
    state: DashboardState,
    action: str,
    terminal: DashboardTerminal,
) -> None:
    arguments = dashboard_action_arguments(state, action)
    command = [sys.executable, str(Path(__file__).resolve()), *arguments]
    terminal.leave()
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
    terminal.enter()
    try:
        while True:
            terminal_width = shutil.get_terminal_size((96, 30)).columns
            sys.stdout.write(
                ANSI_CLEAR + render_dashboard(state, terminal_width)
            )
            sys.stdout.flush()
            key = read_dashboard_key()
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
        print(f"> {format_command(command)}", flush=True)
        if self.dry_run:
            return

        result = subprocess.run(
            list(command),
            cwd=working_directory,
            check=False,
        )
        if result.returncode != 0:
            raise BuildError(
                f"Command failed with exit code {result.returncode}: "
                f"{format_command(command)}",
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
    output = git_output(root, ("status", "--porcelain=v1", "--untracked-files=all"))
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
        counts[category]["files"] += 1
        counts[category]["physical_lines"] += len(lines)
        counts[category]["loc"] += sum(1 for line in lines if line.strip())

    categories = tuple(
        LineStatistics(label, **counts[label]) for label in category_order
    )
    branch_output = git_output(root, ("rev-parse", "--abbrev-ref", "HEAD"))
    commit_output = git_output(root, ("rev-parse", "--short=10", "HEAD"))
    subject_output = git_output(root, ("log", "-1", "--format=%s"))
    return RepositoryStatistics(
        root=root,
        branch=branch_output.strip() if branch_output else None,
        commit=commit_output.strip() if commit_output else None,
        subject=subject_output.strip() if subject_output else None,
        worktree=worktree_statistics(root),
        repository_files=len(files),
        repository_files_source=files_source,
        categories=categories,
    )


def repository_statistics_json(statistics: RepositoryStatistics) -> str:
    worktree = None
    if statistics.worktree is not None:
        worktree = {
            "staged": statistics.worktree.staged,
            "modified": statistics.worktree.modified,
            "untracked": statistics.worktree.untracked,
            "conflicted": statistics.worktree.conflicted,
        }
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
        "first_party": {
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
        },
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


def resolve_build_directory(value: Path) -> Path:
    if value.is_absolute():
        return value.resolve()
    return (REPOSITORY_ROOT / value).resolve()


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
    parser.add_argument(
        "--no-docs",
        action="store_true",
        help="disable the optional IllumoDocs target for this build tree",
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


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Configure, build, test, run, and measure the Illumo workspace. "
            "Running without a command opens the "
            "interactive build console in a terminal."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    common = argparse.ArgumentParser(add_help=False)
    add_common_build_arguments(common)

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
        "test", parents=[common], help="build and run the headless tests"
    )
    test_mode = test_parser.add_mutually_exclusive_group()
    test_mode.add_argument(
        "--test", metavar="NAME", help="run one exact test from any runner"
    )
    test_mode.add_argument(
        "--list-tests",
        action="store_true",
        help="list exact Illumo.*, IllumoGame.*, and IllEd.* case names",
    )

    run_parser = subparsers.add_parser(
        "run", parents=[common], help="build and launch an Illumo application"
    )
    run_parser.add_argument(
        "--app",
        choices=("IllumoGame", "IllEd"),
        default="IllumoGame",
        help="application to launch (default: IllumoGame)",
    )
    run_parser.add_argument(
        "--target",
        dest="app",
        choices=("IllumoGame", "IllEd"),
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
    try:
        count = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("job count must be an integer") from error
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
    if arguments.generator:
        command.extend(("-G", arguments.generator))
    if arguments.architecture:
        command.extend(("-A", arguments.architecture))

    docs_enabled = "OFF" if arguments.no_docs else "ON"
    tracy_enabled = "ON" if arguments.tracy else "OFF"
    command.extend(
        (
            f"-DCMAKE_BUILD_TYPE={arguments.config}",
            f"-DILLUMO_BUILD_DOCUMENTATION={docs_enabled}",
            f"-DILLUMO_ENABLE_TRACY={tracy_enabled}",
            "-DILLUMO_ENABLE_COVERAGE=OFF",
        )
    )
    command.extend(arguments.cmake_arg)
    return command


def build_command(
    arguments: argparse.Namespace,
    cmake: str,
    target: str | None = None,
) -> list[str]:
    build_directory = resolve_build_directory(arguments.build_dir)
    command = [
        cmake,
        "--build",
        str(build_directory),
        "--config",
        arguments.config,
    ]
    selected_target = (
        target if target is not None else getattr(arguments, "target", None)
    )
    if selected_target:
        command.extend(("--target", selected_target))
    if arguments.parallel is not None:
        command.append("--parallel")
        if arguments.parallel > 0:
            command.append(str(arguments.parallel))
    if arguments.verbose:
        command.append("--verbose")
    return command


def configure(arguments: argparse.Namespace, runner: CommandRunner) -> str:
    cmake = existing_tool("cmake", runner.dry_run)
    validate_workspace_build_directory(
        resolve_build_directory(arguments.build_dir)
    )
    runner.run(configure_command(arguments, cmake))
    return cmake


def executable_path(
    build_directory: Path,
    configuration: str,
    name: str,
    dry_run: bool,
) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    if name.startswith("IllumoGame"):
        project_directory = "IllumoGame"
    elif name.startswith("IllEd"):
        project_directory = "IllEd"
    else:
        project_directory = "Illumo"
    candidates = (
        build_directory / project_directory / configuration / f"{name}{suffix}",
        build_directory / project_directory / f"{name}{suffix}",
    )
    if dry_run:
        return candidates[0]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    rendered = " or ".join(str(candidate) for candidate in candidates)
    raise BuildError(f"Expected executable was not produced at {rendered}")


def exact_test_target(test_name: str) -> str:
    if test_name == PUBLIC_HEADER_SMOKE_TEST:
        return "IllumoPublicHeaderSmoke"
    if test_name.startswith("IllumoGame."):
        return "IllumoGameTests"
    if test_name.startswith("IllEd."):
        return "IllEdTests"
    if test_name.startswith("Illumo."):
        return "IllumoTests"
    raise BuildError(
        "Exact tests must start with 'Illumo.', 'IllumoGame.', or 'IllEd.'."
    )


def run_configure(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    configure(arguments, runner)


def run_build(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    cmake = configure(arguments, runner)
    runner.run(build_command(arguments, cmake))


def run_tests(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    exact_target = (
        exact_test_target(arguments.test) if arguments.test else None
    )
    cmake = configure(arguments, runner)
    build_directory = resolve_build_directory(arguments.build_dir)

    if arguments.test:
        target = exact_target
        if target is None:
            raise BuildError("Exact test target was not resolved.")
        runner.run(build_command(arguments, cmake, target))
        test_binary = executable_path(
            build_directory,
            arguments.config,
            target,
            runner.dry_run,
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
        if target != "IllumoPublicHeaderSmoke":
            test_command = (str(test_binary), "--run", arguments.test)
        runner.run(test_command, case_directory)
        return

    if arguments.list_tests:
        for target in ("IllumoTests", "IllumoGameTests", "IllEdTests"):
            runner.run(build_command(arguments, cmake, target))
            test_binary = executable_path(
                build_directory,
                arguments.config,
                target,
                runner.dry_run,
            )
            runner.run((str(test_binary), "--list"), test_binary.parent)
        runner.run(
            build_command(arguments, cmake, "IllumoPublicHeaderSmoke")
        )
        print(PUBLIC_HEADER_SMOKE_TEST, flush=True)
        return

    for target in (
        "IllumoTestsDiscover",
        "IllumoGameTestsDiscover",
        "IllEdTestsDiscover",
        "IllumoPublicHeaderSmoke",
    ):
        runner.run(build_command(arguments, cmake, target))

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


def run_application(arguments: argparse.Namespace) -> None:
    runner = CommandRunner(arguments.dry_run)
    app_name = getattr(arguments, "app", "IllumoGame") or "IllumoGame"
    if not arguments.no_build:
        cmake = configure(arguments, runner)
        runner.run(build_command(arguments, cmake, app_name))

    build_directory = resolve_build_directory(arguments.build_dir)
    if arguments.no_build:
        validate_workspace_build_directory(build_directory)
    application = executable_path(
        build_directory,
        arguments.config,
        app_name,
        runner.dry_run,
    )
    app_arguments = list(arguments.app_arguments)
    if app_arguments and app_arguments[0] == "--":
        app_arguments.pop(0)
    runner.run((str(application), *app_arguments), application.parent)


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


def run_repository_statistics(arguments: argparse.Namespace) -> None:
    statistics = collect_repository_statistics(REPOSITORY_ROOT)
    if arguments.json:
        print(repository_statistics_json(statistics))
    else:
        print_repository_statistics(statistics)


def main(arguments: Sequence[str] | None = None) -> int:
    parser = create_parser()
    command_line = sys.argv[1:] if arguments is None else list(arguments)
    if not command_line:
        if sys.stdin.isatty() and sys.stdout.isatty():
            try:
                return run_dashboard()
            except KeyboardInterrupt:
                print("\nBuild console closed.", file=sys.stderr)
                return 130
        command_line = ["build"]
    parsed = parser.parse_args(normalize_arguments(command_line))

    if parsed.command == "menu":
        if parsed.snapshot:
            print(render_dashboard(DashboardState(), 96, ansi=False))
            return 0
        try:
            return run_dashboard()
        except KeyboardInterrupt:
            print("\nBuild console closed.", file=sys.stderr)
            return 130

    actions = {
        "configure": run_configure,
        "build": run_build,
        "test": run_tests,
        "run": run_application,
        "coverage": run_coverage,
        "docs": run_docs,
        "stats": run_repository_statistics,
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
