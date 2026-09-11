"""Real GPU capture acceptance checks. Requires Python and Pillow; never a headless gate."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from PIL import Image


def verify(executable: Path, directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)

    def run(*args: str, success: bool = True) -> dict:
        completed = subprocess.run(
            [str(executable), *args], cwd=directory, capture_output=True,
            text=True, timeout=30,
        )
        result = json.loads(completed.stdout)
        assert (completed.returncode == 0) == success, (completed.returncode, result, completed.stderr)
        assert result["success"] == success, result
        (directory / f"result-{run.count}.json").write_text(
            json.dumps({"result": result, "stderr": completed.stderr}, indent=2), encoding="utf-8"
        )
        run.count += 1
        return result

    run.count = 0
    for mode in ("direct", "scene"):
        run("--output", f"{mode}.png", "--mode", mode, "--width", "321", "--height", "241")
    with Image.open(directory / "direct.png") as direct, Image.open(directory / "scene.png") as scene:
        assert direct.size == scene.size == (321, 241)
        assert direct.tobytes() == scene.tobytes(), "Scene and direct producers differ"
        background = direct.getpixel((0, 0))
        visible = sum(direct.getpixel((x, y)) != background
                      for y in range(direct.height) for x in range(direct.width))
        assert visible > 2000, f"Expected visible fixture, got {visible} pixels"

    # Front red triangle submitted before farther blue triangle. A fresh-context
    # depth-cache defect makes the last blue triangle overwrite the front one.
    obj = directory / "occlusion.obj"
    obj.write_text(
        "v -1 -1 1 1 0 0\nv 1 -1 1 1 0 0\nv 0 1 1 1 0 0\n"
        "v -1 -1 0 0 0 1\nv 1 -1 0 0 0 1\nv 0 1 0 0 0 1\n"
        "f 1 2 3\nf 4 5 6\n", encoding="ascii"
    )
    inspected = run("--inspect-mesh", str(obj))
    assert inspected["backend"] is None and inspected["vertices"] == 6
    run("--mesh", str(obj), "--output", "occlusion.png", "--rotation", "0",
        "--eye", "0", "0", "5", "--width", "128", "--height", "128")
    with Image.open(directory / "occlusion.png") as image:
        red, green, blue, _ = image.getpixel((64, 64))
        assert red > 200 and green < 10 and blue < 10, "First-frame depth test failed"

    run("--width", "0", "--output", "bad-size.png", success=False)
    run("--backend", "unsupported", "--output", "bad-backend.png", success=False)
    run("--mesh", "missing.obj", "--output", "missing-mesh.png", success=False)
    run("--vertex-shader", "missing.glsl", "--output", "missing-shader.png", success=False)
    (directory / "invalid.glsl").write_text("#version 330 core\ninvalid shader\n", encoding="ascii")
    run("--fragment-shader", "invalid.glsl", "--output", "bad-shader.png", success=False)
    run("--output", "absent-parent/frame.png", success=False)
    original = (directory / "direct.png").read_bytes()
    run("--output", "direct.png", success=False)
    assert (directory / "direct.png").read_bytes() == original
    assert not list(directory.glob("*.partial")), "Staging artifacts leaked"
    assert not (directory / "envvars.json").exists(), "Capture started persistent configuration"
    for name in ("bad-size.png", "bad-backend.png", "missing-mesh.png", "missing-shader.png", "bad-shader.png"):
        assert not (directory / name).exists(), f"Failure published {name}"
    print(f"Capture GPU checks passed ({run.count} invocations); artifacts: {directory}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    destination = args.output_dir or Path(tempfile.mkdtemp(prefix="illumo-capture-proof-"))
    verify(args.executable.resolve(), destination.resolve())
