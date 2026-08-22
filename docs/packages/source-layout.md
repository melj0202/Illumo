# Source layout

The repository root is the canonical CMake workspace. `IllumoGame` depends on
the static `Illumo` library; the library has no Game or Rulesets dependency.

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

`Illumo/Shader`, `Illumo/Assets`, notices, dependencies, and licenses remain
library-owned. `IllumoGame/envvars.json` is product-owned. Historical material
under `archive/` is not built.
