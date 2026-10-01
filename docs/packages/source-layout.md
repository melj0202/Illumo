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
| `Illumo/Source/Engine/` | Application runner, host and frame phases, context, `DebugOverlay` implementation |
| `Illumo/Source/Scene/` | Persistent scene nodes, hierarchy, transform cache, and render extraction |
| `Illumo/Source/Content/` | Optional `Illumo::Content` layer: virtual paths, `illumo.json`, `.ilpk` archives, virtual file tree, `.ilsc` format 2, `SceneInstance`, `ProgramScene` and `SceneDirector` (headers in `Illumo/Include/Illumo/Content/`) |
| `Illumo/tools/IllumoPack.cpp` | Host tool that packs a package directory into an `.ilpk` or verifies one |
| `Illumo/Source/Foundation/` | Build metadata and generic implementation support |
| `Illumo/Source/Services/` | Logging, environment, input, SysCmdLine, generic console, allocators |
| `Illumo/Source/Rendering/` | Renderer plus private window/OpenGL implementation |
| `Illumo/Source/Platform/` | OS entry points, native save/load dialogs, and clipboard text |
| `Illumo/TestSupport/Include/` | Test-only `Illumo::TestSupport` API |
| `Illumo/Tests/` | `Illumo.*` library cases (`Tests/Content/` holds `Illumo.Content.*` and the archive fixture) |
| `IllumoGame/Source/Game/` | CA defaults, the title and canvas scenes (`TitleScene`, `CanvasScene`, `CSimScenes`), simulation, presentation, editor, persistence |
| `IllumoGame/Source/Rulesets/` | CA transitions and palettes |
| `IllumoGame/Tests/` | `IllumoGame.*` product cases |
| `IllEd/Source/` | The editor scene (`EditorScene`), document model over its `SceneInstance`, history, menu bar and dock panels |
| `IllEd/Assets/` | Editor UI atlas and other product runtime files |
| `IllEd/Tests/` | `IllEd.*` product cases |
| `IllMeshViewer/Source/` | The viewer scene (`MeshViewerScene`), camera, configuration, input, menu bar and Info/Display panels |
| `IllMeshViewer/Tests/` | `IllMeshViewer.*` product cases |
| `Playground/` | The sample game for scene behaviours (D-E35): `Playground.wasm` (a `GuestPlayProgram`), its `behaviours.json`, demo scene and `Playground.Wasm.*` package tests |

`Illumo/Shader`, `Illumo/Assets`, notices, dependencies, and licenses remain
library-owned. `IllumoGame/envvars.json`, `IllEd/envvars.json`, and
`IllMeshViewer/envvars.json` (and `Playground/envvars.json`) are product-owned, as are each product's
`illumo.json` package manifest (which replaced `app.json`). Historical
material under `archive/` is not built.
