# Third-party notices

Illumo includes and uses third-party software and font files. Those components
remain the property of their respective copyright holders and are distributed
under their own licenses. Illumo's license, if any, does not replace or modify
those licenses.

The full license texts are kept with the corresponding source or asset in this
repository. A normal CMake build also copies this notice and the license files
for components used by the application into a `licenses/` directory beside the
IllumoGame executable.

## Components used by Illumo

| Component | Use | License selected for Illumo | Copyright / required acknowledgement | Full license text |
|---|---|---|---|---|
| [FreeType 2.13.3](https://freetype.org/) | TrueType font loading | FreeType License (FTL) | Portions of this software are copyright (C) 2024 The FreeType Project (www.freetype.org). All rights reserved. This software is based in part on the work of the FreeType Team. | [`LICENSE.TXT`](Illumo/thirdparty/freetype-2.13.3/LICENSE.TXT), [`FTL.TXT`](Illumo/thirdparty/freetype-2.13.3/docs/FTL.TXT) |
| [GLEW 2.1.0](https://github.com/nigels-com/glew) | OpenGL extension loading | BSD-style license, with bundled MIT notices | Copyright notices for Milan Ikits, Marcelo E. Magallon, Lev Povalahev, Brian Paul, and The Khronos Group are reproduced in the license file. | [`LICENSE.txt`](Illumo/thirdparty/glew-2.1.0/LICENSE.txt) |
| [GLFW 3.4](https://www.glfw.org/) | Windowing and input | zlib/libpng license | Copyright (c) 2002-2006 Marcus Geelnard and Copyright (c) 2006-2019 Camilla Löwy. | [`LICENSE.md`](Illumo/thirdparty/glfw-3.4/LICENSE.md) |
| [GLM 1.0.0](https://github.com/g-truc/glm) | Vector and matrix math | MIT License | Copyright (c) 2005 - G-Truc Creation. | [`copying.txt`](Illumo/thirdparty/glm/copying.txt) |
| [JSON for Modern C++ 3.12.0](https://github.com/nlohmann/json) | Environment-variable persistence | MIT License | Copyright (c) 2013-2025 Niels Lohmann. | [`LICENSE.MIT`](Illumo/thirdparty/json/LICENSE.MIT) |
| [stb](https://github.com/nothings/stb) (`stb_easy_font` 1.1 and `stb_image` 2.30) | Debug text geometry and image loading | MIT License | Copyright (c) 2017 Sean Barrett. | [`LICENSE`](Illumo/thirdparty/stb/LICENSE); the same notice is retained at the end of each header |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | Wavefront OBJ mesh loading | MIT License | Copyright (c) 2012-Present Syoyo Fujita and many contributors. | [`LICENSE`](Illumo/thirdparty/tinyobjloader/LICENSE) |
| [Tracy Profiler 0.13.1](https://github.com/wolfpld/tracy) | Debug profiling instrumentation | BSD 3-Clause License | Copyright (c) 2017-2025 Bartosz Taudul. | [`LICENSE`](Illumo/thirdparty/tracy-0.13.1/LICENSE) |
| [Handjet](https://github.com/rosettatype/Handjet/) | Application TrueType font | SIL Open Font License 1.1 | Copyright 2018 The Handjet Project Authors. | [`OFL.txt`](Illumo/Assets/Fonts/Handjet/OFL.txt) |

The repository also contains `stb_image_resize2` 2.10 and `stb_truetype` 1.26.
They are not referenced by the current Illumo targets, but are covered by the
same stb MIT notice and retain the complete dual-license text in each header.

## Vendored source not linked by the current targets

These source trees are present in `Illumo/thirdparty/`, but the current
`Illumo/CMakeLists.txt` does not compile or link them directly:

| Component | License / notice retained in the source tree |
|---|---|
| [Independent JPEG Group JPEG 9f](https://www.ijg.org/) | [`README`](Illumo/thirdparty/jpeg-9f/README), including its full Legal Issues section. If IJG code is added to an executable, its documentation must state: "This software is based in part on the work of the Independent JPEG Group." |
| [libpng 1.6.44](http://www.libpng.org/pub/png/libpng.html) | [`LICENSE`](Illumo/thirdparty/libpng-1.6.44/LICENSE) |

Keeping these packages in the repository constitutes source redistribution, so
their upstream license and notice files must remain in their directories even
while they are unused.

## Platform and configure-time dependencies

OpenGL and platform libraries such as the Windows SDK frameworks or Linux GTK
are supplied by the operating system, toolchain, or build environment; they are
not vendored in this repository. FreeType can also discover optional system
libraries during configuration. Anyone packaging a binary with additional
redistributed runtime libraries must include the notices required by the exact
libraries resolved in that build.

This file is a good-faith inventory, not legal advice. When adding, updating,
or enabling a third-party component, update this file and preserve its upstream
license before distributing source or binaries.
