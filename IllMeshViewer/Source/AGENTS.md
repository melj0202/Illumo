# IllMeshViewer guidance

This file specializes the repository `AGENTS.md` for `IllMeshViewer/Source/`.

## Product identity

IllMeshViewer is an Illumo application. Its job is to display a single 3D mesh
(.obj) on a 3D reference grid with standard orbit, pan, zoom, and rotate camera
controls.

It ships only as the `IllMeshViewer.wasm` package (`apps/meshviewer/`,
manifest `IllMeshViewer/app.json`, `launchAccess: "read"`, private storage
`storage/meshviewer/`) run by `IllumoRuntime --app meshviewer [--open
model.obj]`, like IllumoGame and IllEd. There is no native
`IllMeshViewer.exe`. `Wasm/ViewerApplication.cpp` is the guest entry
(`GuestModuleApplication`); it preloads `Assets/Skybox/skybox-daylight.png`
from the package (`packageAssets()`) and hands the `--open` launch file to
the platform seam. `IllMeshViewerCore` stays a native library for the
`IllMeshViewerTests` oracle suite.

## Scope and boundaries

- Depend only on `Illumo::Illumo`. Do not link Game, Rulesets, IllumoGameCore,
  or IllEdCore.
- Load meshes through `MeshLoader::loadFromFile` or `MeshLoader::loadFromMemory`.
  Opening a mesh reads its bytes through `MeshViewerPlatform`
  (`MeshViewerPlatform.h`; `MeshViewerPlatformNative.cpp` for the native
  oracle, `Wasm/ViewerApplication.cpp` for the package) and parses them with
  `MeshLoader::loadFromMemory`. Completions may arrive synchronously or on a
  later update; locations are opaque and only a display label is shown.
- In the package, large static meshes reach the host as retained host meshes
  and the skybox as a host cubemap (frame schema v3); motion blur and MSAA
  stay host policy.
- Keep UI primitive-composed through `GameVisual` and `GuiKit`. No retained widget tree.
- `MeshViewerUi` resolves its viewport with `GuiPanelLayout::viewport` and
  tracks the pointer with `GuiPointerTracker` from `Illumo/Gui/GuiMenuShell`.
  Do not restate UI-scale conversion or press-edge bookkeeping.
- Persist lighting, shadows, and motion blur in EnvVars (`lightingEnabled`,
  `lightDir*`, `lightColor*`, `ambientColor*`, `shadowsEnabled`,
  `shadowMapSize`, `shadowRadius`, `lightDistance`, `shadowBias`,
  `shadowSlopeScale`, `shadowNormalOffset`, `shadowPcf`,
  `motionBlurEnabled`, `motionBlurAmount`, `motionBlurMax`) and apply them
  through `MeshVisual` setters. Do not have `MeshVisual` read EnvVars, and
  do not bind EnvVars directly as shader uniforms.
- Main-thread affine.
- `IllMeshViewer.Wasm.Package` (`IllMeshViewer/Tests/Wasm/TestViewerPackage.cpp`)
  drives the real package through the generic host: launch mesh as one
  retained host mesh, and the package-preloaded skybox cubemap.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
