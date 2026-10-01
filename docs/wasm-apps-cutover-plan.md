# Illumo applications as WASM packages: cutover plan

Status: authorized 2026-09-22; milestones 1-7 implemented (see §7).
Predecessor: [wasm-game-cutover-plan.md](wasm-game-cutover-plan.md). IllumoGame
already runs entirely as `IllumoGame.wasm` inside `IllumoRuntime.exe`.

## 1. Objective and end state

Every interactive Illumo client program runs as a WASM package in the one
generic native host, `IllumoRuntime.exe`. Done when:

1. The default Windows build produces `IllumoRuntime.exe` and three packages:
   - `apps/game/` (IllumoGame);
   - `apps/illed/` (IllEd);
   - `apps/meshviewer/` (IllMeshViewer).

   Each package holds an `app.json` manifest, its module and its data. The
   build produces no `IllEd.exe`, `IllMeshViewer.exe` or `IllumoCapture.exe`.
2. `IllumoRuntime.exe` alone plays the game. `--app illed` and
   `--app meshviewer` select the other packages, and `--open <file>` hands a
   user-chosen document to the package.
3. `IllumoRuntime.exe --capture out.png` renders any package to a PNG and
   exits. It replaces the IllumoCapture tool.
4. Frame schema v3 adds:
   - retained host meshes, so large models are not re-sent every frame;
   - a cubemap skybox;
   - an explicit alpha-blend flag.
5. The native `IllEdCore` and `IllMeshViewerCore` test suites stay green, and
   package end-to-end CTest cases drive each package through the host.

## 2. Decisions (user, 2026-09-22)

- **IllumoCapture.** It becomes a runtime capture mode. The executable is
  removed. `FrameCapture` and `IllumoCaptureGpuTests` stay as native engine GPU
  checks, so they keep testing the real OpenGL path.
- **Launching.** One runtime and an `--app <name>` flag. Packages live in
  `apps/<name>/`, and the default app is `game`.
- **Frame format.** Version 3 adds retained meshes, a cubemap skybox and a
  blend flag. Motion blur and MSAA stay host policy.
- **Native executables.** IllEd and IllMeshViewer native executables are
  removed. Their Core libraries remain as native test oracles, following the
  `IllumoGameCore` pattern.

## 3. Evidence (inventories, 2026-09-22)

**IllEd** (6.2k lines):

- Uses scene, window, camera, renderer, input, console and settings, plus an
  optional `assetManager` for the UI atlas.
- Save, open and close-confirm are synchronous (`SaveLoad` dialogs,
  `std::ifstream`, `AtomicFile`), and a Save writes back to the path that was
  opened.
- Geometry is procedural lines and triangles with CPU picking. There are no
  shaders, framebuffers or mesh files.
- The gizmo planes are translucent but unblended.

**IllMeshViewer** (1.7k lines):

- `Start` requires `assetManager`, which is null in the guest, so the guest
  currently fails to start.
- Meshes are OBJ files loaded synchronously by path through tinyobjloader.
- The skybox comes from a 2048x1536 PNG cross decoded to a cubemap. It has no
  frame representation, and drawing it drops the whole frame.
- Lit meshes, shadows, grid, axes, wireframe and bounds already map to frame
  v2, but frame v2 re-sends all geometry every frame.

**IllumoCapture** is a CLI wrapper around the engine's `FrameCapture` with a
fixture:

- It compiles custom GLSL, renders offscreen, writes a PNG and prints JSON.
- `tools/verify_capture.py` drives it.

## 4. Design

### 4.1 Runtime: applications, launch files and capture

- **Package layout.**
  - `<exe>/apps/<name>/app.json` describes a package:
    - `id`, `module`, optional `worker`, `title`;
    - requested `memoryMiB`, `fuelPerCall` and `deadlineMilliseconds`,
      clamped to host ceilings.
  - `--app <name>` selects `apps/<name>`, and `--package <dir>` still
    overrides. The window title comes from the manifest.
  - Storage stays `storage/<id>`.
- **Launch files.**
  - `--open <file>` grants that one file to the package as the selection
    named `launch` (writable when the manifest's `launchAccess` is `edit`),
    and describes it in the startup bytes as an `ILS1` record: display label
    (base name only), editability and size.
  - The guest never sees the host path.
  - `GuestModuleApplication` decodes the record and exposes it as
    `launchFile()`.
- **Capture.**
  - `--capture <png>` runs the package until `--capture-frame <n>` (default
    60) has rendered, then reads back the backbuffer after submission and
    writes the PNG with `FrameCapture::savePng`.
  - It prints one JSON result line and exits 0 or 1.
  - `-ww` and `-wh` set the window.

### 4.2 Guest SDK: assets and dialogs

- **Asset sources.**
  - `AssetManager` reads bytes through an `IAssetSource`:
    - `FileAssetSource`, the native default, keeps today's behaviour,
      including hot reload.
    - The guest supplies `GuestPackageAssets`, which serves bytes the package
      preloaded during bootstrap.
  - Images decode with `stbi_load_from_memory` in both cases.
  - Under `ILLUMO_SERIAL_GUEST`, AssetManager has no worker thread. Loads
    complete in `pump()`, and shader and path-based mesh loads report failure
    (shaders are host policy).
  - `MeshLoader` keeps `loadFromMemory` in the guest; `loadFromFile` is native
    only.
- **Preloading.** `GuestModuleApplication::packageAssets()` names the package
  files to preload. The base class reads them before `bootstrap()` and sets
  `context.assetManager`.
- **Editable opens.**
  - `GuestDialogRequest` version 2 adds `edit`: an open whose selection stays
    writable, so the document can be saved in place.
  - Its result adds the display label.
  - Version 1 requests are unchanged.

### 4.3 Frame schema v3

- **Retained meshes.**
  - New services: `CreateMesh` (layout, vertex and index byte sizes),
    `WriteMesh` (chunked ranges of at most 1 MiB) and `ReleaseMesh`.
  - A batch whose `mesh` id is set draws `indexCount` indices from
    `firstIndex` of that host mesh, with no inline geometry.
  - The recorder retains static meshes above 64 KiB. Dynamic and small meshes
    stay inline.
  - Host validation happens when the upload completes: layout stride, finite
    positions and index range. Retained bytes count against the owner's
    resident budget.
- **Cubemaps.**
  - `CreateCubemap` takes six square faces and returns a `Cubemap` resource.
  - Batch style `Skybox` references it and draws inline cube positions with
    the built-in skybox style.
- **Blend.**
  - A batch flag requests alpha blending for Shape and triangle batches.
  - Versions 1 and 2 keep their current defaults.
- **Compatibility.** Versions 1 and 2 still decode.

### 4.4 IllEd package

- `IllEdPlatform` becomes an async seam, as `CSimPlatform` did for the game:
  - choose a load or save location;
  - read or write a location's bytes;
  - with completion callbacks.
- The native TU (tests) completes synchronously with `SaveLoad`, `IlscCodec`
  and `AtomicFile`. The guest TU uses `GuestDialog` and `GuestFiles`.
- The document keeps an opaque location plus a display label instead of a host
  path.
- The confirm-dialog Save, and close vetoes, continue when the write
  completes.
- Package contents: `app.json`, `IllEd.wasm`, `envvars.json`, and
  `Assets/IllEd/editor-ui-atlas.jpg`, which is preloaded.

### 4.5 IllMeshViewer package

- Open is dialog → read the selected bytes → `MeshLoader::loadFromMemory` →
  `acquireMesh(MeshData)`. A launch file follows the same path.
- The skybox cross is preloaded from the package.
- Package contents: `app.json`, `IllMeshViewer.wasm`, `envvars.json`, and
  `Assets/Skybox/skybox-daylight.png`.

### 4.6 Orchestrator and tests

- `build.py` lists the packages. `play --app <name>`, the dashboard app list
  and the post-build summary report every package.
- CTest:
  - `IllEd.Wasm.Package` and `IllMeshViewer.Wasm.Package` drive the real
    packages through `WasmGameModule`: open, edit, save and render, checking
    the recorded frames.
  - `Illumo.Wasm.FrameValidation` gains v3 cases.
  - `Illumo.Runtime.Capture` checks the capture CLI surface (help and invalid
    arguments).

## 5. Constraints

- Illumo host code gains no product includes. The guest never receives host
  paths, and the WASI linker stays disabled.
- The native `IllEdTests` and `IllMeshViewerTests` stay green on the shared
  sources.
- `.ilsc` v1 is unchanged.
- No `auto`, namespaces or recursion. Mozilla clang-format. Tidy stays clean.

## 6. Milestones

1. Runtime: `apps/<name>` with `app.json`, `--app`, `--open` launch files, and
   manifest titles. The game moves to `apps/game`.
2. Guest SDK: the asset source seam, serial AssetManager and MeshLoader,
   package preload, `context.assetManager`, and editable dialog opens.
3. Frame v3: retained meshes, cubemap skybox and blend.
4. IllEd package, async platform seam and package E2E test. Remove
   `IllEd.exe`.
5. IllMeshViewer package and E2E test. Remove `IllMeshViewer.exe`.
6. Runtime capture mode. Remove `IllumoCapture.exe` and retarget
   `verify_capture.py`.
7. `build.py`, README, AGENTS, architecture consensus and LaTeX decision log;
   full validation.

## 7. Validation ledger

2026-09-22, milestones 1-7 complete:

- **M1 runtime.**
  - `apps/<name>/app.json` packages with `--app`, `--open` and manifest
    titles (`IRenderWindow::setTitle`); the game moved to `apps/game`.
  - `--capture` via `Renderer::setBeforePresent` and the new
    `IllumoApplicationDefinition::exitCode`.
  - The runtime works from its own directory wherever it starts, and
    relative options resolve against the invocation directory. Found by the
    capture verifier: engine shaders load by relative path, so a capture
    started elsewhere rendered no lit or skybox frames.
- **M2 guest SDK.**
  - `IAssetSource` with a native `FileAssetSource`, and a serial
    `AssetManager` and `MeshLoader` under `ILLUMO_SERIAL_GUEST`, which is now
    defined for all guest code.
  - `GuestPackageAssets` preloads, `context.assetManager`, per-package
    `envvars.json` defaults in the base class, and editable dialog opens with
    display labels.
  - All 32 WASM cases still passed after the switch, so no guest imports WASI
    file functions.
- **M3 frame v3.**
  - Retained meshes, the cubemap skybox and the blend flag. v1 and v2 still
    decode.
  - `Illumo.Wasm.FrameValidation` gained v3 round-trip, truncation and
    rejection cases.
  - `Illumo.Wasm.RetainedResources` covers in-order upload, escaping-index
    rejection, bounded ranges, release, skybox draws and cubemap misuse.
- **M4 IllEd.**
  - The `IllEdPlatform` seam and the `GuestDocuments` SDK helper.
    `EditorModule` has an async save/open/close-confirm flow that holds input
    while transfers are in flight.
  - `IllEd.exe` removed. All 52 native `IllEd.*` cases pass unchanged.
  - `IllEd.Wasm.Package` loads a launch scene, draws the preloaded atlas,
    pans with a held key and saves in place with Ctrl+S. The native codec
    oracle verifies the saved camera and nodes.
- **M5 IllMeshViewer.**
  - The `MeshViewerPlatform` seam; `IllMeshViewer.exe` removed. All 14
    native cases pass.
  - `IllMeshViewer.Wasm.Package` checks that a 4,608-vertex launch mesh
    becomes one retained static host mesh drawn on later frames, and that
    the preloaded skybox becomes one host cubemap.
  - Found and fixed a pre-existing bug: normal-less OBJ files kept
    `MeshVertex`'s +Z default, so `generateNormalsIfMissing` never ran and
    meshes lit flat (`Illumo.MeshLoader.GeneratesMissingNormals`).
- **M6 capture.**
  - `IllumoCapture` and `CaptureMain.cpp` removed; headless
    `Illumo.Runtime.Help` and `Illumo.Runtime.InvalidCaptureFrame` added.
  - `tools/verify_capture.py` passed 7 real-GPU invocations: game, IllEd and
    a lit retained viewer torus, plus refusals of existing, non-PNG, unknown
    app and missing launch file, none publishing output.
- **M7 orchestrator and docs.**
  - `build.py` has installed-app discovery, `play --app`, and dashboard,
    summary and doctor reporting per app and of stale pre-WASM outputs.
    `tools/test_build.py` passes 89/89 (the native-console case runs in its
    own console).
  - README, AGENTS files, frame-capture, architecture consensus (D-E14), the
    LaTeX chapters and decision log, and the Linux package map are updated.
    The PDF is rebuilt.

Evidence:

- **Release workspace suite:** 486/486, with clang-tidy on for native
  targets and no compiler warnings. `IllEdCloseWindowTests` and
  `IllumoCaptureGpuTests` compile.
- **clang-format:** clean on every changed C++ file.
- **Live captures through `IllumoRuntime.exe`:** the game menu, the IllEd
  editor with atlas icons, and the viewer's shaded torus with sky and grid.

Not verified, and why:

- **Interactive dialogs:** the Open, Save As and edit-grant dialog flows were
  not exercised by hand. Package tests use launch files, and the
  editable-grant host path is covered only through `--open`.
- **Linux:** there is no runtime pin, so there are no runnable apps.
- **Large-mesh performance:** retained uploads are bounded to 8 chunks of
  1 MiB in flight; very large meshes were not measured.

Follow-ups:

- Retire the `LaunchScene` and `LaunchMesh` environment variables in favour
  of `--open`. The native test oracles still read them.
- The native test executables share the build tree's `envvars.json` with the
  runtime, and their fixture window sizes leak into it.
