# Contributing to the Illumo workspace

## Code style

1. Avoid `auto`; use explicit types.
2. Avoid namespaces.
3. Do not add recursive code.
4. Match the surrounding C++23 style and keep ownership explicit.
5. **No exceptions (D-F3).** The host and every guest build with C++
   exceptions off; never write `try`, `catch` or `throw`. Report failures
   by return value (`bool` plus an error string, an empty handle, a sticky
   `GuestWireWriter::failed()`). Use the non-throwing forms of standard
   facilities: `Foundation/ParseNumber.h` instead of `std::sto*`,
   `std::error_code` overloads of `std::filesystem`,
   `nlohmann::json::parse(text, nullptr, false)` with type checks before
   `get`, `Platform/PathText.h` for path text. A programming error or running
   out of memory or threads ends the process with `illumoFatal`
   (`Foundation/Fatal.h`). The `Illumo.Build.NoExceptions` test enforces the
   first rule.
6. **Code Formatting:** The project uses `clang-format` based on Mozilla style (80-column limit, 2-space indent). You must run `clang-format` on your changes before finalizing them.
7. **Static analysis:** First-party C++ is linted by `clang-tidy` during the
   normal build (`ILLUMO_ENABLE_CLANG_TIDY` defaults to ON). The check set is
   `.clang-tidy`; diagnostics are errors. Disable with
   `-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`.
   `python build.py tidy` runs the batch `IllumoTidy` target. Do not lint
   vendored or generated files. Checks that conflict with house style
   (including `modernize-use-auto`) or that would require unrelated mass
   cleanup stay disabled in `.clang-tidy`.
8. **Naming Conventions:** Enforce the following conventions on new/modified code:
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
  build metadata, OS entry points, native dialogs, generic services, and the
  frame phases, between which `RuntimeShell` runs one program (D-E31); it must
  not depend on Game or Rulesets.
- IllumoGame owns only CA configuration, Game, Rulesets, and its scene program
  (a `GuestProgram` with `TitleScene` and `CanvasScene`).
- IllEd owns the world-editor document, history, tools, toolbar, and its scene
  program (a `GuestProgram` with one `EditorScene`). It must not depend on Game
  or Rulesets.
- `Illumo::Content` owns the `.ilsc` scene format and codec (`SceneDocument`,
  `IlscCodec`, `SceneInstance`), program scenes (`ProgramScene`,
  `SceneDirector`), `illumo.json` package manifests, `.ilpk`
  archives, and the virtual file tree. Core `Illumo` never includes
  `<Illumo/Content/...>`, and Content never depends on Wasm or a product.
- IllumoGame consumes supported headers through `<Illumo/...>`; OpenGL
  implementation headers and TestSupport are not production API.
- Game and rules code do not issue raw OpenGL calls.
- Production rendering uses `RenderCommand` tokens through `IBackend`.
- Keep `DebugOverlay` out of Release compilation. `RuntimeShell` runs it in
  Debug and RelWithDebInfo and drops it if it fails to start; each product is
  one WASM program that manages its own scenes.
- The approved `SceneGraph` v2 owns retained SoA hierarchy, compiled transforms
  and bounds, identity/journal state, and a derived query index. Its separate
  drawable consumes immutable snapshots in the frame list. Keep graph state,
  borrowed attachment lifetime, editor policy, and token/backend execution
  separate. ECS components, retained UI, persistence formats, update callbacks,
  and render-thread payload recording remain outside this boundary.
- Do not introduce a render graph, additional graphics backend, or compute
  backend solely for architectural completeness.

## Documentation

All first-party documentation belongs under `docs/`. Update
`architecture-consensus.md` and the relevant LaTeX chapter when behavior changes.
Append a formal decision in `latex/sections/09-design-decision-log.tex` when a
closed architecture or product-policy decision changes.
