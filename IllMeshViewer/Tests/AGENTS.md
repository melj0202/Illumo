# IllMeshViewer Tests Guidance

This directory contains the headless automated test suites for IllMeshViewer.
IllMeshViewer ships only as the `IllMeshViewer.wasm` package; the native
`IllMeshViewerTests` runner exercises `IllMeshViewerCore` as a test oracle,
and `Wasm/TestViewerPackage.cpp` (`IllMeshViewerWasmPackageTests`, CTest
`IllMeshViewer.Wasm.Package`) drives the real package through the generic
host: launch mesh as one retained host mesh and the package-preloaded
skybox cubemap.

## Invariants

- Tests must run headlessly with `MockBackend` and `NullRenderWindow`.- Follow the CTest single-case discovery pattern via `TestRegistry`.
- Verify camera math, config defaults, lighting, shadow, and motion-blur
  EnvVars, UI layout/interaction, and module lifecycle.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
