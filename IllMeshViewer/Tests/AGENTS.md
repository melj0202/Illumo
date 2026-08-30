# IllMeshViewer Tests Guidance

This directory contains the headless automated test suite for IllMeshViewer.

## Invariants

- Tests must run headlessly with `MockBackend` and `NullRenderWindow`.
- Follow the CTest single-case discovery pattern via `TestRegistry`.
- Verify camera math, config defaults, UI layout/interaction, and module lifecycle.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
