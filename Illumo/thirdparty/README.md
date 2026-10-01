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
| `glslang-16.6.0/` | glslang 16.6.0 (Vulkan SDK 1.4.363.0): `glslang/` (GLSL front end, no HLSL), `SPIRV/` (generator, no optimizer) and `LICENSES/`; upstream build files removed, built by `cmake/IllumoVulkanDeps.cmake` | `LICENSE.txt` and `LICENSES/` |
| `json/` | JSON for Modern C++ 3.12.0 | `LICENSE.MIT` |
| `miniaudio-0.11.25/` | miniaudio 0.11.25 (`miniaudio.h` and upstream `README.md`; the implementation is compiled once, by `Source/Audio/AudioDecoder.cpp`) | `LICENSE` |
| `stb/` | stb headers (`stb_image` is active) | `LICENSE` and the notice at the end of each header |
| `tinyobjloader/` | tinyobjloader header | `LICENSE` |
| `tracy-0.14.1/` | Tracy Profiler 0.14.1 | `LICENSE` |
| `vma-3.4.0/` | Vulkan Memory Allocator 3.4.0 (`include/vk_mem_alloc.h`) | `LICENSE.txt` |
| `volk-1.4.363/` | volk (Vulkan SDK 1.4.363.0) | `LICENSE.md` |
| `vulkan-headers-1.4.363/` | Vulkan-Headers (Vulkan SDK 1.4.363.0), `include/` only | `LICENSE.md` and `LICENSES/` |

The four Vulkan components are pinned to one Vulkan SDK release and are updated
together; see `docs/vulkan-backend-plan.md`.

## Present but not linked by the current targets

| Directory | Component | License file |
|---|---|---|
| `jpeg-9f/` | Independent JPEG Group JPEG 9f | `README` (`LEGAL ISSUES`) |
| `libpng-1.6.44/` | libpng 1.6.44 | `LICENSE` |

`stb_image_resize2.h` and `stb_truetype.h` are also present but currently
unused. Their full dual-license notices remain embedded in the headers, and
this repository selects the MIT alternative recorded in `stb/LICENSE`.
