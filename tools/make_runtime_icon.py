"""Assemble IllumoRuntime's Windows icon from the brand kit PNGs.

The ICO stores each size as a PNG image (supported since Windows Vista).
16 px uses the flat favicon art, because the glowing tile blurs at that size;
the larger sizes use the app icon renders. Run from the repository root:

    python tools/make_runtime_icon.py
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRAND = ROOT / "docs" / "brand"
OUTPUT = ROOT / "Illumo" / "Source" / "Wasm" / "IllumoRuntime.ico"
SOURCES = [
    BRAND / "favicon" / "favicon-16x16.png",
    BRAND / "icon" / "png" / "illumo-icon-32.png",
    BRAND / "icon" / "png" / "illumo-icon-48.png",
    BRAND / "icon" / "png" / "illumo-icon-64.png",
    BRAND / "icon" / "png" / "illumo-icon-128.png",
    BRAND / "icon" / "png" / "illumo-icon-256.png",
]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(data: bytes, path: Path) -> tuple[int, int]:
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        raise ValueError(f"{path} is not a PNG file")
    width, height = struct.unpack(">II", data[16:24])
    if width != height or width > 256:
        raise ValueError(f"{path} is {width}x{height}; icons must be square, at most 256")
    return width, height


def build_icon(sources: list[Path]) -> bytes:
    images = [path.read_bytes() for path in sources]
    header = struct.pack("<HHH", 0, 1, len(images))
    entries = b""
    offset = 6 + 16 * len(images)
    for path, data in zip(sources, images):
        width, height = png_size(data, path)
        # A width or height of 256 is stored as 0.
        entries += struct.pack("<BBBBHHII", width % 256, height % 256, 0, 0, 1,
                               32, len(data), offset)
        offset += len(data)
    return header + entries + b"".join(images)


def main() -> int:
    OUTPUT.write_bytes(build_icon(SOURCES))
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
