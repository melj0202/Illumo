# Source layout

The repository root is the canonical CMake workspace. `IllumoGame`, `IllEd`,
and `IllMeshViewer` depend on the static `Illumo` library; the library has no
dependency on these products or their domain code.

| Path | Role |
|---|---|
| `Illumo/Include/Illumo/` | Supported public headers consumed as `<Illumo/...>` |
| `Illumo/Source/Engine/` | Application runner, host, module lifetime, context, Debug module implementation |
| `Illumo/Source/Scene/` | Persistent scene nodes, hierarchy, transform cache, and render extraction |
| `Illumo/Source/Foundation/` | Build metadata and generic implementation support |
| `Illumo/Source/Services/` | Logging, environment, input, SysCmdLine, generic console, allocators |
| `Illumo/Source/Rendering/` | Renderer plus private window/OpenGL implementation |
| `Illumo/Source/Platform/` | OS entry points, native save/load dialogs, and clipboard text |
| `Illumo/TestSupport/Include/` | Test-only `Illumo::TestSupport` API |
| `Illumo/Tests/` | `Illumo.*` library cases |
| `IllumoGame/Source/Game/` | CA defaults/CLI metadata/module factory, simulation, presentation, editor, persistence |
| `IllumoGame/Source/Rulesets/` | CA transitions and palettes |
| `IllumoGame/Tests/` | `IllumoGame.*` product cases |
| `IllEd/Source/` | World-editor module factory, document model, `.ilsc` codec, toolbar |
| `IllEd/Assets/` | Editor UI atlas and other product runtime files |
| `IllEd/Tests/` | `IllEd.*` product cases |
| `IllMeshViewer/Source/` | Mesh-viewer module factory, camera, configuration, input, and UI |
| `IllMeshViewer/Tests/` | `IllMeshViewer.*` product cases |

`Illumo/Shader`, `Illumo/Assets`, notices, dependencies, and licenses remain
library-owned. `IllumoGame/envvars.json`, `IllEd/envvars.json`, and
`IllMeshViewer/envvars.json` are product-owned. Historical material under
`archive/` is not built.
