# Source layout

The repository root is the canonical CMake workspace. `IllumoGame`, `IllEd`,
and `IllMeshViewer` depend on the static `Illumo` library; the library has no
dependency on these products or their domain code. The optional
`IllumoContent` library (`Illumo::Content`) sits above it; core `Illumo` never
includes `<Illumo/Content/...>`, and guests compile its serial-safe subset as
`IllumoGuestContent`.

| Path | Role |
|---|---|
| `Illumo/Include/Illumo/` | Supported public headers consumed as `<Illumo/...>` |
| `Illumo/Source/Engine/` | Application runner, host, module lifetime, context, Debug module implementation |
| `Illumo/Source/Scene/` | Persistent scene nodes, hierarchy, transform cache, and render extraction |
| `Illumo/Source/Content/` | Optional `Illumo::Content` layer: virtual paths, `illumo.json`, `.ilpk` archives, virtual file tree, `.ilsc` format 2, `SceneInstance` (headers in `Illumo/Include/Illumo/Content/`) |
| `Illumo/tools/IllumoPack.cpp` | Host tool that packs a package directory into an `.ilpk` or verifies one |
| `Illumo/Source/Foundation/` | Build metadata and generic implementation support |
| `Illumo/Source/Services/` | Logging, environment, input, SysCmdLine, generic console, allocators |
| `Illumo/Source/Rendering/` | Renderer plus private window/OpenGL implementation |
| `Illumo/Source/Platform/` | OS entry points, native save/load dialogs, and clipboard text |
| `Illumo/TestSupport/Include/` | Test-only `Illumo::TestSupport` API |
| `Illumo/Tests/` | `Illumo.*` library cases (`Tests/Content/` holds `Illumo.Content.*` and the archive fixture) |
| `IllumoGame/Source/Game/` | CA defaults/CLI metadata/module factory, simulation, presentation, editor, persistence |
| `IllumoGame/Source/Rulesets/` | CA transitions and palettes |
| `IllumoGame/Tests/` | `IllumoGame.*` product cases |
| `IllEd/Source/` | World-editor module factory, document model over `SceneInstance`, history, menu bar and dock panels |
| `IllEd/Assets/` | Editor UI atlas and other product runtime files |
| `IllEd/Tests/` | `IllEd.*` product cases |
| `IllMeshViewer/Source/` | Mesh-viewer module factory, camera, configuration, input, menu bar and Info/Display panels |
| `IllMeshViewer/Tests/` | `IllMeshViewer.*` product cases |

`Illumo/Shader`, `Illumo/Assets`, notices, dependencies, and licenses remain
library-owned. `IllumoGame/envvars.json`, `IllEd/envvars.json`, and
`IllMeshViewer/envvars.json` are product-owned, as are each product's
`illumo.json` package manifest (which replaced `app.json`). Historical
material under `archive/` is not built.
