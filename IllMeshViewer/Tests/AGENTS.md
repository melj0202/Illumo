# IllMeshViewer Tests Guidance

This directory contains the headless automated test suites for IllMeshViewer.
IllMeshViewer ships only as the `IllMeshViewer.wasm` package; the native
`IllMeshViewerTests` runner exercises `IllMeshViewerCore` as a test oracle,
and `Wasm/TestViewerPackage.cpp` (`IllMeshViewerWasmPackageTests`, CTest
`IllMeshViewer.Wasm.Package`) drives the real package through the generic
host: launch mesh as one retained host mesh and the skybox cubemap preloaded
from `/engine`. The package carries no skybox (D-E38), so these tests mount
the engine's `Illumo/Assets` at `/engine` beside the package at `/app`, as the
runtime does.

## Invariants

- Tests must run headlessly with `MockBackend` and `NullRenderWindow`.
- Follow the CTest single-case discovery pattern via `TestRegistry`.
- Verify camera math, config defaults, lighting, shadow, and motion-blur
  EnvVars, the menus, the Info and Display panels docked and detached
  (through `FakePanelSurfaces`), and the scene lifecycle through a
  `SceneDirector`.
- `EnvVars` loads and saves `envvars.json` in the working directory, so scene
  fixtures give it a fresh file in the temp directory: display toggles,
  Display panel edits and panel layouts must never leak between cases.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
