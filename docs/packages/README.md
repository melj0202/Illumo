# Source-package maps

These files summarize current ownership across the sibling Illumo and
IllumoGame projects. They are documentation, not build inputs.

| File | Area |
|---|---|
| `source-layout.md` | Workspace, library, product, and test trees |
| `app.md` | Illumo application definition, generic runner, and process loop |
| `engine.md` | Illumo host, context, modules, and failure semantics |
| `scene.md` | Persistent nodes, hierarchy, transforms, and render attachments |
| `game.md` | IllumoGame canvas, simulation, editing, and persistence |
| `illed.md` | IllEd world editor, SceneGraph documents, and `.ilsc` |
| `rendering.md` | Public renderer boundary and private OpenGL implementation |
| `services.md` | Generic Illumo services versus IllumoGame policy |
| `foundation.md` | Dependency-light public utilities |
| `assets.md` | Illumo runtime files and product configuration staging |
| `platform.md` | Illumo platform contract and port map |
| `platform-linux.md` | Linux scaffold status |
| `platform-macos.md` | macOS scaffold status |
| `tests.md` | Split test ownership and aggregate workflow |

The canonical architecture remains `../architecture-consensus.md`. Operational
rules live in the root and nested `AGENTS.md` hierarchy.
