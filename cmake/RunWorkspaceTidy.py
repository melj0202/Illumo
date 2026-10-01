"""Select workspace-owned compilation entries and run LLVM's batch driver."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def select_entries(entries: list[dict], source_dir: Path, binary_dir: Path) -> list[dict]:
    source_dir = source_dir.resolve()
    binary_dir = binary_dir.resolve()
    selected = []
    for entry in entries:
        directory = Path(entry["directory"])
        if not directory.is_absolute():
            directory = binary_dir / directory
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        source = source.resolve()
        if not source.is_relative_to(source_dir) or source.is_relative_to(binary_dir):
            continue
        relative = source.relative_to(source_dir)
        if any(part.casefold() == "thirdparty" for part in relative.parts):
            continue
        # Keep each compilation variant; LLVM can apply all commands for a file.
        selected.append({**entry, "directory": str(directory.resolve()), "file": str(source)})
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--run-clang-tidy", required=True)
    parser.add_argument("--python", required=True)
    parser.add_argument("--jobs", type=int, required=True)
    args = parser.parse_args()
    database = args.binary_dir / "compile_commands.json"
    entries = select_entries(json.loads(database.read_text(encoding="utf-8")),
                             args.source_dir, args.binary_dir)
    if not entries:
        parser.error(f"No first-party compilation entries in {database}")
    selected_dir = args.binary_dir / "tidy-compile-commands"
    selected_dir.mkdir(parents=True, exist_ok=True)
    selected_database = selected_dir / "compile_commands.json"
    selected_database.write_text(json.dumps(entries, indent=2), encoding="utf-8")
    sources = {entry["file"] for entry in entries}
    print(f"Running clang-tidy on {len(sources)} first-party source files "
          f"({len(entries)} compilation entries, {args.jobs} jobs)", flush=True)
    print(f"Selected compile database: {selected_database}", flush=True)
    return subprocess.run(
        [args.python, args.run_clang_tidy, "-p", str(selected_dir),
         "-clang-tidy-binary", args.clang_tidy,
         "-config-file", str(args.source_dir / ".clang-tidy"),
         "-j", str(args.jobs), "-quiet", "-hide-progress"],
        cwd=args.source_dir,
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
