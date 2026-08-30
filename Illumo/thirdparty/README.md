# Third-party source inventory

The complete project notice is [`../../THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md).
Do not remove the license files stored beside these source trees.

## Used by the current Illumo targets

| Directory | Component | License file |
|---|---|---|
| `freetype-2.13.3/` | FreeType 2.13.3 | `LICENSE.TXT` and `docs/FTL.TXT` |
| `glew-2.1.0/` | GLEW 2.1.0 | `LICENSE.txt` |
| `glfw-3.4/` | GLFW 3.4 | `LICENSE.md` |
| `glm/` | GLM 1.0.0 | `copying.txt` |
| `json/` | JSON for Modern C++ 3.12.0 | `LICENSE.MIT` |
| `stb/` | stb headers (`stb_easy_font` and `stb_image` are active) | `LICENSE` and the notice at the end of each header |
| `tinyobjloader/` | tinyobjloader header | `LICENSE` |
| `tracy-0.13.1/` | Tracy Profiler 0.13.1 | `LICENSE` |

## Present but not linked by the current targets

| Directory | Component | License file |
|---|---|---|
| `jpeg-9f/` | Independent JPEG Group JPEG 9f | `README` (`LEGAL ISSUES`) |
| `libpng-1.6.44/` | libpng 1.6.44 | `LICENSE` |

`stb_image_resize2.h` and `stb_truetype.h` are also present but currently
unused. Their full dual-license notices remain embedded in the headers, and
this repository selects the MIT alternative recorded in `stb/LICENSE`.
