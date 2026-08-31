# 2026-08-30 workspace clang-tidy

Added a repeatable first-party `clang-tidy` gate.

- `.clang-tidy` is the check set. `modernize-use-auto` is disabled because
  house style forbids `auto` (D-008). Diagnostics are errors.
- `ILLUMO_ENABLE_CLANG_TIDY` defaults to ON. First-party targets get
  `CXX_CLANG_TIDY` and are linted during the ordinary compile (D-T3).
  Disable with `-DILLUMO_ENABLE_CLANG_TIDY=OFF` or
  `python build.py build --no-tidy`.
- Workspace `IllumoTidy` reads `compile_commands.json` and runs LLVM
  `run-clang-tidy` on Illumo, IllumoGame, IllEd, and IllMeshViewer sources
  under Include/Source/Tests/TestSupport. Vendored `thirdparty` translation
  units, including TracyClient, are excluded.
- `python build.py tidy` configures `build-workspace-tidy` with Ninja and
  Clang for the batch target. Combined coverage remains a separate tree and
  keeps tidy off (D-T1).
- The first-pass check set is curated so the existing tree is a real gate:
  reserved MacroDefs identifiers, override/default-ctor churn, empty-catch
  and similar existing-style findings stay disabled in `.clang-tidy`.
- Two in-scope cleanups landed with the gate: `WinSaveLoad` uses `nullptr`,
  and unused scene-graph test locals were dropped.

Formal decisions: D-T2 (batch target), D-T3 (default compile-time run).
