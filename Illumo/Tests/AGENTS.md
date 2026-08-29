# Illumo library tests guidance

This file specializes the repository `AGENTS.md` for `Illumo/Tests/`.

Library tests own the generic host lifecycle, services, rendering contracts,
primitive UI, allocators, persistent scene hierarchy, and public-header
consumption. Register exact `Illumo.<area>.<case>` names and keep the runner's
`--list`, exact `--run`, and CTest discovery synchronized. Do not add Game,
Rulesets, simulator defaults, or product command policy to this runner.

SceneGraph cases are `Illumo.SceneGraph.HandlesAndLifetime`,
`Illumo.SceneGraph.HierarchyAndTransforms`, and
`Illumo.SceneGraph.RenderExtraction`. MeshVisual cases are
`Illumo.MeshVisual.DynamicMeshReuse`, `Illumo.MeshVisual.SpriteAndCube`,
`Illumo.MeshVisual.Billboard`, `Illumo.MeshVisual.SceneAttachment`, and
`Illumo.MeshVisual.NewPrimitives`.

Use `Illumo::TestSupport` for MockBackend and test-only fixtures. Tests must be
headless, deterministic, and isolated under `build/Testing/Illumo/`.

```powershell
cmake --build build --config Release --target IllumoTests
ctest --test-dir build -C Release -L Illumo --output-on-failure
build/Illumo/Release/IllumoTests.exe --list
build/Illumo/Release/IllumoTests.exe --run <exact-test-name>
```
