# IllMeshViewer guidance

This file specializes the repository `AGENTS.md` for `IllMeshViewer/Source/`.

## Product identity

IllMeshViewer is an Illumo application. Its job is to display a single 3D mesh
(.obj) or an `.ilsc` format 2 scene on a 3D reference grid with standard
orbit, pan, zoom, and rotate camera controls.

It ships only as the `IllMeshViewer.wasm` package (`apps/meshviewer/`,
manifest `IllMeshViewer/illumo.json`, `launchAccess: "read"`, private storage
`storage/meshviewer/`) run by `IllumoRuntime --app meshviewer [--open
model.obj]`, like IllumoGame and IllEd. There is no native
`IllMeshViewer.exe`. `Wasm/ViewerApplication.cpp` is the guest entry
(`GuestModuleApplication`); it preloads `Assets/Skybox/skybox-daylight.png`
from the package (`packageAssets()`) and hands the `--open` launch file to
the platform seam. `IllMeshViewerCore` stays a native library for the
`IllMeshViewerTests` oracle suite.

## Scope and boundaries

- Depend only on `Illumo::Illumo` and `Illumo::Content` (the guest links
  `IllumoGuestContent`). Do not link Game, Rulesets, IllumoGameCore, or
  IllEdCore.
- Scenes load through `SceneInstance`, never a private reader:
  `loadSceneLocation` reads the text, `collectSceneFetches` lists its assets,
  `MeshViewerPlatform::fetchAssets` makes them readable (the guest's
  `GuestSceneFetches`, OBJ material libraries included), then
  `loadSceneFromText` instantiates. Relative references resolve against the
  scene's package root: the mount holding a `vfs:` location, or `/local` for
  a dialog pick. `viewer_open <virtual path>` opens a scene or mesh from the
  file tree (for example a mod in `/packages/<id>`). A scene's environment
  sky replaces the default skybox; opening a mesh closes the scene and vice
  versa.
- Loose `.obj` files keep the dedicated mesh path (centered, normalized, one
  retained `MeshVisual`) rather than becoming a temporary `/local` scene, so
  mesh metadata, wireframe triangles and the retained-mesh contract stay as
  they are.
- Load meshes through `MeshLoader::loadFromFile` or `MeshLoader::loadFromMemory`.
  Opening a mesh reads its bytes through `MeshViewerPlatform`
  (`MeshViewerPlatform.h`; `MeshViewerPlatformNative.cpp` for the native
  oracle, `Wasm/ViewerApplication.cpp` for the package) and parses them with
  `MeshLoader::loadFromMemory`. Completions may arrive synchronously or on a
  later update; locations are opaque and only a display label is shown.
- In the package, large static meshes reach the host as retained host meshes
  and the skybox as a host cubemap (frame schema v3); motion blur and MSAA
  stay host policy.
- Keep UI primitive-composed through `GameVisual` in the plain tool look
  (`GuiToolStyle`, D-UI7). No retained widget tree.
- `MeshViewerUi` is only the main-window chrome: File and View menus, the
  empty-state card and toasts inside the dock's centre rectangle, and the
  status bar. The Info and Display panels (`MeshViewerPanels`) live in a
  `GuiPanelDock` (right column) owned by `MeshViewerModule`
  (`MeshViewerModulePanels.cpp`); each draws into the `GuiPanelPlacement` it
  is given and reads its pointer through `GuiPanelPointer`, docked or
  detached. The layout is saved in `panelLayout`. Do not restate UI-scale
  conversion or press-edge bookkeeping.
- The Display panel edits nothing itself: it returns edits (a display
  toggle, or an env var and its value) and the module applies them, so the
  env vars below stay the only source of those settings. Display toggles
  are stored in `showGrid`, `showAxes`, `showSkybox` and `showWireframe`. A
  camera drag belongs to the viewport only when it began there.
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
  `IllMeshViewer.Wasm.ScenePackage` mounts a content package and opens its
  scene through `viewer_open`; the scene's mesh is fetched and retained.
- Follow `docs/contributing.md`: avoid `auto`, avoid namespaces, keep ownership
  explicit, and format with Mozilla-style `clang-format`.
