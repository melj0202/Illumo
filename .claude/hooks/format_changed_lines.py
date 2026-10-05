"""PostToolUse hook: clang-format only the lines of an edited C/C++ file that
differ from HEAD, so formatting never churns untouched code.

Reads the hook payload on stdin. Untracked files are formatted whole. Always
exits 0; when it changes the file it tells Claude to re-read it.
"""

import json
import os
import re
import shutil
import subprocess
import sys

EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"}
SKIPPED_PARTS = {"thirdparty", "archive"}
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def run(arguments, cwd):
    return subprocess.run(arguments, cwd=cwd, capture_output=True, text=True)


def changed_ranges(path, root):
    """Return clang-format --lines ranges, [] for no changes, None for untracked."""
    tracked = run(["git", "ls-files", "--error-unmatch", "--", path], root)
    if tracked.returncode != 0:
        return None
    diff = run(["git", "diff", "-U0", "--no-color", "--no-ext-diff", "HEAD", "--", path], root)
    ranges = []
    for line in diff.stdout.splitlines():
        match = HUNK.match(line)
        if not match:
            continue
        start = int(match.group(1))
        count = 1 if match.group(2) is None else int(match.group(2))
        if count > 0:
            ranges.append(f"--lines={start}:{start + count - 1}")
    return ranges


def main():
    payload = json.load(sys.stdin)
    path = (payload.get("tool_input") or {}).get("file_path") or ""
    if os.path.splitext(path)[1].lower() not in EXTENSIONS or not os.path.isfile(path):
        return
    clang_format = shutil.which("clang-format")
    top = run(["git", "rev-parse", "--show-toplevel"], os.path.dirname(path))
    if clang_format is None or top.returncode != 0:
        return
    root = top.stdout.strip()
    # Vendored, historical and build trees are never ours to format.
    directories = os.path.relpath(path, root).lower().replace("\\", "/").split("/")[:-1]
    if any(d in SKIPPED_PARTS or d == "build" or d.startswith("build-") for d in directories):
        return

    ranges = changed_ranges(path, root)
    if ranges == []:
        return
    with open(path, "rb") as source:
        before = source.read()
    run([clang_format, "-i", "--style=file", *(ranges or []), path], root)
    with open(path, "rb") as source:
        after = source.read()
    if after != before:
        scope = "the whole new file" if ranges is None else "its changed lines"
        print(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "PostToolUse",
                "additionalContext": f"clang-format reformatted {scope} in {path}; re-read it before editing it again.",
            }
        }))


if __name__ == "__main__":
    main()
