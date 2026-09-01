# IllMeshViewer guidance

This file specializes the repository `AGENTS.md` for `IllMeshViewer/Source/`.

## Product identity

IllMeshViewer is an Illumo application: it uses the same runner, platform entry,
services, renderer, and `CreateIllumoApplication` seam as IllumoGame and IllEd.
Its job is to display a single 3D mesh (.obj) on a 3D reference grid with standard
orbit, pan, zoom, and rotate camera controls.

## Scope and boundaries

- Depend only on `Illumo::Illumo`. Do not link Game, Rulesets, IllumoGameCore,
  or IllEdCore.
- Load meshes through `MeshLoader::loadFromFile` or `MeshLoader::loadFromMemory`.
- Keep UI primitive-composed through `GameVisual` and `GuiKit`. No retained widget tree.
- Persist lighting and shadows in EnvVars (`lightingEnabled`, `lightDir*`,
  `lightColor*`, `ambientColor*`, `shadowsEnabled`, `shadowMapSize`,
  `shadowRadius`, `lightDistance`, `shadowBias`, `shadowSlopeScale`,
  `shadowNormalOffset`, `shadowPcf`) and apply them through `MeshVisual`
  setters. Do not have `MeshVisual` read EnvVars, and do not bind EnvVars
  directly as shader uniforms.
- Main-thread affine.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
