"""Derive the repository's brand assets from the kit in docs/brand.

Writes:
- Illumo/Source/Wasm/IllumoRuntime.ico, IllumoRuntime's Windows icon. Each
  size is stored as a PNG image (supported since Windows Vista). 16 px uses
  the flat favicon art, because the glowing tile blurs at that size; the
  larger sizes use the app icon renders.
- Illumo/Assets/Branding/illumo-splash.png and illumo-badge.png, the engine
  logos GuiEngineSplash and GuiEngineBadge draw (guests read them from
  /engine/Branding). Guest textures have no mipmaps, so these are cropped to
  their artwork and pre-sized near the size they are drawn at. This part
  needs Pillow (pip install pillow).

Run from the repository root:

    python tools/make_brand_assets.py
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRAND = ROOT / "docs" / "brand"
ICON_OUTPUT = ROOT / "Illumo" / "Source" / "Wasm" / "IllumoRuntime.ico"
ICON_SOURCES = [
    BRAND / "favicon" / "favicon-16x16.png",
    BRAND / "icon" / "png" / "illumo-icon-32.png",
    BRAND / "icon" / "png" / "illumo-icon-48.png",
    BRAND / "icon" / "png" / "illumo-icon-64.png",
    BRAND / "icon" / "png" / "illumo-icon-128.png",
    BRAND / "icon" / "png" / "illumo-icon-256.png",
]
ENGINE_BRANDING = ROOT / "Illumo" / "Assets" / "Branding"
# (source, output, output width or None, output height or None). The splash
# logo keeps its glow; the badge is the flat wordmark, legible when small.
ENGINE_IMAGES = [
    (BRAND / "logo" / "illumo-logo-neon@2x.png",
     ENGINE_BRANDING / "illumo-splash.png", 960, None),
    (BRAND / "logo" / "illumo-logo-flat-dark@2x.png",
     ENGINE_BRANDING / "illumo-badge.png", None, 64),
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


def build_engine_images() -> None:
    from PIL import Image

    ENGINE_BRANDING.mkdir(parents=True, exist_ok=True)
    for source, output, width, height in ENGINE_IMAGES:
        image = Image.open(source).convert("RGBA")
        image = image.crop(image.getchannel("A").getbbox())
        if width is None:
            width = round(image.width * height / image.height)
        else:
            height = round(image.height * width / image.width)
        image.resize((width, height), Image.LANCZOS).save(output, optimize=True)
        print(f"wrote {output.relative_to(ROOT)} ({width}x{height})")


def main() -> int:
    ICON_OUTPUT.write_bytes(build_icon(ICON_SOURCES))
    print(f"wrote {ICON_OUTPUT.relative_to(ROOT)}")
    build_engine_images()
    return 0


if __name__ == "__main__":
    sys.exit(main())
