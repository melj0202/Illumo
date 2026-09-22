"""Real GPU capture acceptance checks for IllumoRuntime --capture.

Renders every installed application through the runtime's capture mode and
checks the PNGs and JSON results. Requires Python, Pillow and a desktop GPU
session; it is never a headless gate. The native FrameCapture API has its own
explicit check, the IllumoCaptureGpuTests target.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile

from PIL import Image


def torus_obj(path: Path) -> None:
    """A normal-less 96x48 torus (4608 vertices): retained, lit and shadowed."""
    lines = ["# verify_capture torus"]
    around, tube = 96, 48
    for i in range(around):
        a = 2 * math.pi * i / around
        for j in range(tube):
            b = 2 * math.pi * j / tube
            ring = 1.0 + 0.35 * math.cos(b)
            lines.append(f"v {ring * math.cos(a):.5f} {0.35 * math.sin(b):.5f} "
                         f"{ring * math.sin(a):.5f}")
    for i in range(around):
        for j in range(tube):
            a = i * tube + j + 1
            b = ((i + 1) % around) * tube + j + 1
            c = ((i + 1) % around) * tube + (j + 1) % tube + 1
            d = i * tube + (j + 1) % tube + 1
            lines.append(f"f {a} {d} {c} {b}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def luminance_range(image: Image.Image, box: tuple[int, int, int, int]) -> int:
    region = image.convert("L").crop(box)
    low, high = region.getextrema()
    return high - low


def verify(runtime: Path, directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    count = 0

    def run(*args: str, success: bool = True) -> dict:
        nonlocal count
        completed = subprocess.run(
            [str(runtime), "-ww", "640", "-wh", "480", *args], cwd=directory,
            capture_output=True, text=True, timeout=120,
        )
        lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
        assert lines, (completed.returncode, completed.stdout, completed.stderr)
        result = json.loads(lines[-1])
        assert (completed.returncode == 0) == success, (completed.returncode, result)
        assert result["success"] == success, result
        (directory / f"result-{count}.json").write_text(
            json.dumps({"arguments": list(args), "result": result}, indent=2),
            encoding="utf-8",
        )
        count += 1
        return result

    # Every installed application renders a non-uniform frame.
    for application in ("game", "illed"):
        output = directory / f"{application}.png"
        result = run("--app", application, "--capture", str(output))
        assert result["application"] == application and result["error"] == ""
        with Image.open(output) as image:
            assert image.size == (result["width"], result["height"])
            assert luminance_range(image, (0, 0, image.width, image.height)) > 40, \
                f"{application} frame is nearly uniform"

    # The viewer draws a launch mesh through retained geometry with lighting:
    # the torus region must shade smoothly rather than as one flat colour.
    mesh = directory / "torus.obj"
    torus_obj(mesh)
    viewer = directory / "meshviewer.png"
    result = run("--app", "meshviewer", "--open", str(mesh), "--capture",
                 str(viewer), "--capture-frame", "90")
    with Image.open(viewer) as image:
        width, height = image.size
        box = (width * 3 // 8, height * 3 // 8, width * 5 // 8, height * 5 // 8)
        assert luminance_range(image, box) > 60, "Viewer mesh is flat or missing"

    # Refusals publish nothing and never overwrite.
    original = (directory / "game.png").read_bytes()
    run("--app", "game", "--capture", str(directory / "game.png"), success=False)
    assert (directory / "game.png").read_bytes() == original
    run("--app", "absent", "--capture", str(directory / "absent.png"), success=False)
    run("--app", "game", "--capture", str(directory / "frame.bmp"), success=False)
    run("--app", "meshviewer", "--open", str(directory / "missing.obj"),
        "--capture", str(directory / "missing.png"), success=False)
    for name in ("absent.png", "frame.bmp", "missing.png"):
        assert not (directory / name).exists(), f"Failure published {name}"
    assert not list(directory.glob("*.partial")), "Staging artifacts leaked"
    print(f"Runtime capture checks passed ({count} invocations); artifacts: {directory}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path, help="path to IllumoRuntime.exe")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    destination = args.output_dir or Path(tempfile.mkdtemp(prefix="illumo-capture-proof-"))
    verify(args.runtime.resolve(), destination.resolve())
