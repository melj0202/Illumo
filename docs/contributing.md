# Contributing to the Illumo workspace

## Code style

1. Avoid `auto`; use explicit types.
2. Avoid namespaces.
3. Do not add recursive code.
4. Match the surrounding C++23 style and keep ownership explicit.
5. **Code Formatting:** The project uses `clang-format` based on Mozilla style (80-column limit, 2-space indent). You must run `clang-format` on your changes before finalizing them.
6. **Static analysis:** First-party C++ is linted by `clang-tidy` during the
   normal build (`ILLUMO_ENABLE_CLANG_TIDY` defaults to ON). The check set is
   `.clang-tidy`; diagnostics are errors. Disable with
   `-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`.
   `python build.py tidy` runs the batch `IllumoTidy` target. Do not lint
   vendored or generated files. Checks that conflict with house style
   (including `modernize-use-auto`) or that would require unrelated mass
   cleanup stay disabled in `.clang-tidy`.
7. **Naming Conventions:** Enforce the following conventions on new/modified code:
   - **Classes and structs:** `PascalCase` (e.g., `RenderQueue`)
   - **Enums and Enum values:** `PascalCase` (e.g., `BlendMode`, `BlendMode::AlphaBlend`)
   - **Functions:** `camelCase` (e.g., `submitCommand()`)
   - **Local variables & Parameters:** `camelCase` (e.g., `commandCount`, `framebufferWidth`)
   - **Private members:** `m_camelCase` (e.g., `m_framebufferWidth`)
   - **Constants:** `kPascalCase` (e.g., `kMaximumLights`)
   - **Namespaces:** `lowercase` or `snake_case` (e.g., `csim::render`)
   - **Files:** Match the primary type (e.g., `RenderQueue.hpp`, `RenderQueue.cpp`)
   - **Template parameters:** Short `PascalCase` (e.g., `T`, `Allocator`)

## Dependencies

Do not add a third-party dependency without author approval. Propose dependency
changes separately so their maintenance, license, and build impact can be
reviewed.

## Architecture boundaries

- Illumo owns the application runner, process loop, system command-line parser,
  build metadata, OS entry points, native dialogs, generic services, and module
  lifetime; it must not depend on Game or Rulesets.
- IllumoGame owns only CA configuration/CLI metadata, Game, Rulesets, and its
  required game-module factory.
- IllEd owns the world-editor document, `.ilsc` codec, toolbar, and its
  required editor-module factory. It must not depend on Game or Rulesets.
- IllumoGame consumes supported headers through `<Illumo/...>`; OpenGL
  implementation headers and TestSupport are not production API.
- Game and rules code do not issue raw OpenGL calls.
- Production rendering uses `RenderCommand` tokens through `IBackend`.
- Keep `DebugModule` out of Release compilation and register it as optional in
  Debug; each product supplies its own required module.
- The approved `SceneGraph` v1 is the retained world hierarchy. Keep it
  handle-based, backend-neutral, iterative, and separate from the per-frame
  `Rendering::Scene` list. Do not expand it into an ECS, retained UI tree,
  persistence format, culling structure, or update framework without a
  concrete consumer and an authorized design.
- Do not introduce a render graph, additional graphics backend, or compute
  backend solely for architectural completeness.

## Documentation

All first-party documentation belongs under `docs/`. Update
`architecture-consensus.md` and the relevant LaTeX chapter when behavior changes.
Append a formal decision in `latex/sections/09-design-decision-log.tex` when a
closed architecture or product-policy decision changes.
