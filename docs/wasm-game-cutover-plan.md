# IllumoGame WASM cutover execution plan

Status: authorized 2026-09-22; milestones 1-5 complete (see §8).
Parent design: [wasm-game-runtime-design.md](wasm-game-runtime-design.md).
First-slice evidence: [wasm-game-runtime-plan.md](wasm-game-runtime-plan.md)
(2026-09-22 ledger entry).

## 1. Objective and end state

The complete IllumoGame product (main menu, new-simulation and configuration
menus, canvas, editor, console commands, save/load, clipboard, ruleset
workshop, exit confirmation and the 3D render test mode) executes inside
`IllumoGame.wasm`. One generic native executable, `IllumoRuntime.exe`, hosts
it. No native IllumoGame executable is built. Done when:

1. The default Windows build produces `IllumoRuntime.exe` and an IllumoGame
   package directory (`game.json`, `IllumoGame.wasm`, the worker module and
   catalogs), and no `IllumoGame.exe`.
2. Launching `IllumoRuntime.exe` with no arguments runs the package: main
   menu, play, edit, save/load, workshop, settings and exit, all from WASM.
3. An end-to-end CTest case drives the real package through host console
   trampolines (`play`, edits, `step`, `save`) and decodes the guest's save
   natively against a reference oracle.
4. The 3D test mode renders through a versioned frame-schema extension.
5. The full Release build and workspace CTest pass; documentation and
   guidance name the runtime and package instead of `IllumoGame.exe`.

## 2. Decisions (user, 2026-09-22)

- **Guest-side engine.** A guest-local `IllumoContext` composed inside the
  wasm runs the existing `MainMenuModule`/`CellGameModule` and menus. This
  replaces the earlier plan's rejection of compiling `CellGameModule` into the
  reactor. That rejection assumed native services; here every context member
  is a guest object, and nothing about `IllumoContext` crosses the ABI.
- **Remove native `IllumoGame.exe`.** `IllumoGameCore` stays as the native
  test oracle for `IllumoGameTests`.
- **Extend the frame ABI** for the 3D test mode (lit meshes, shared shadow).
- **Windows-only runtime** for now; Linux keeps engine/tools, no game.
- Runtime name `IllumoRuntime.exe`. `Illumo.exe` would collide with the
  `Illumo` library target's outputs.

## 3. Current evidence (inventory, 2026-09-22)

- The game uses 9 `IllumoContext` members (`assetManager` unused). Console use
  is `isOpen` plus `log*`; registry use is `RegisterCommand`/`UnregisterCommand`.
- Blocking platform calls: 5 native file dialogs, 2 OS clipboard calls,
  `IllumoCodec` file I/O, `RuleCatalogLoader` path I/O and executable-relative
  catalog lookup, and `EnvVars::save`.
- Threads: `SimulationRunner` (thread in constructor, blocking wait, about 20
  drain sites). `SparseWorkerPool` already has a serial twin.
- 3D test mode: procedural `MeshVisual` geometry in a `SceneGraph`, built-in
  `LitMesh`/`ShadowDepth` styles, perspective camera, no file assets.
- Guest SDK already provides async files, dialogs, clipboard, console
  trampolines, display, environment, fonts, input and recording backend.

## 4. Design

### 4.1 Product platform seam (IllumoGame, shared source)

`CSimPlatform` is a product-level interface with completion callbacks:
choose load/save location, read/write a location's bytes, read/write
clipboard text, and user-catalog read/write. `CSimPlatform::current()` is
defined per link. The native TU (tests) completes synchronously with the
existing `SaveLoad`/`Clipboard`/`AtomicFile` code. The guest TU completes on a
later update through `GuestDialog`/`GuestFiles`/`GuestClipboard`. A location is
a path natively and a storage name or selected-file capability name in the
guest. Game code calls the seam instead of platform APIs, so native tests keep
exercising the same control flow.

### 4.2 Simulation runner

Under `ILLUMO_SERIAL_GUEST`, `SimulationRunner` has no thread. `start()`
executes the shared generation body immediately and the result is taken on
the next `try/waitAndTakeCompleted`. Publication, mirror deltas and drain
semantics are unchanged. Offloading to the `CSW1` worker store is a follow-up
performance milestone (§6) that needs asynchronous drains.

### 4.3 Guest-side engine (Illumo-owned SDK, generic)

`GuestModuleApplication` (IllumoGuest) is a `GuestApplication` that composes:

- a `GuestSnapshotWindow`
- `InputManager` via `GuestInputProvider`
- storage-backed `GuestEnvironment` plus display synchronization
- `Camera`
- a `Renderer` on `GuestRecordingBackend`
- `GuestFontProvider`
- a `Scene`
- a local `CommandRegistry` mirrored to host console trampolines
- a guest `CommandLine` whose log lines forward to the host console and whose
  `isOpen` mirrors the host overlay
- a module host that applies transitions at frame boundaries

It renders each scene layer in order: World maps to `GuestLayer::World`,
UI/Debug to `GuestLayer::Ui`. Close requests from product code set a new
update-response flag bit. The host treats that bit as a window close request
and still asks the guest through `illumo_guest_close`. Products override only
bootstrap readiness and the first module.

### 4.4 IllumoGame guest application

It loads storage `envvars.json`, applies `IllumoGameConfig` defaults,
bootstraps catalogs (`CSimCatalogBootstrap`), installs the guest
`CSimPlatform`, then starts `MainMenuModule`. User catalog overlays and saves
live in the storage root.

### 4.5 Runtime and package

`IllumoRuntime.exe` (renamed player) reads `<exe>/game/game.json` when no
`--game` is given. `game.json` is a bounded generic manifest: module and worker
file names plus requested memory/fuel/deadline, clamped to host ceilings.
Storage defaults to `<exe>/storage`. CLI flags still override. The default
Windows configure enables the runtime.

### 4.6 Frame ABI extension (3D)

The frame version gains a lit-mesh batch style carrying world vertices with
normals, a model matrix and shadow flags. The host submits these through the
built-in `LitMesh` style as one world drawable that also registers as a shadow
caster. Validation and fuzz tests cover the new records. Version 1 packets
stay valid.

## 5. Constraints

- Illumo host code gains no Game/Rulesets include and no CA opcode.
- The guest never receives host paths. The WASI linker stays disabled.
- Native `IllumoGameTests` remain green on the shared sources.
- Save format v4 plus v3/v2/dense reads are unchanged.
- No `auto`, namespaces or recursion. Mozilla clang-format. Tidy stays clean.

## 6. Milestones

1. Platform seam and serial runner; native tests green.
2. Guest-side engine SDK and `IllumoGame.wasm` reaching the main menu.
3. Runtime rename, manifest, close flag, default package, CMake cutover
   (native exe removed); end-to-end console-driven test.
4. Frame ABI 3D extension and the render test mode.
5. Documentation, guidance and README sync; full validation.
6. (Follow-up) Worker-store simulation offload with asynchronous drains; perf
   gates. Done 2026-09-22 as simulation lanes with retire-on-drain (D-E17);
   see the ledger below and `.agent/wasm-runtime-performance-plan.md`.

## 7. Risks and containment

- Serial generations run inside the control call. Large worlds need a larger
  per-call budget until milestone 6. The manifest records measured budgets.
- Host console built-ins (`set`/`get`/`toggle`) address runtime settings, not
  guest settings; the game's own commands and menus are trampolined.
- Each milestone keeps unrelated native consumers building. The first-slice
  control guest is superseded by the package and removed with its test once
  the end-to-end test replaces it.

## 8. Validation ledger

2026-09-22, milestones 1-5 complete:

- **M1 platform seam.** `CSimPlatform` (native oracle, guest adapter), the
  portable `RuleCatalogOverlay`, and a serial `SimulationRunner` sharing one
  generation body. All 181 native `IllumoGame.*` tests passed unchanged.
- **M2 guest engine.** `GuestModuleApplication` runs the unchanged
  `MainMenuModule`/`CellGameModule`/menus/workshop in `IllumoGame.wasm`
  (5.5 MB). SDK fixes made along the way:
  - `IBackend::BeginLayer` layer tagging.
  - A texture replacement supersedes a pending acquisition. Without this,
    `CanvasView` growth during first acquisition dropped every canvas frame.
  - The console forwards up to 16 requests in flight.
  - The host rejects guest registrations of host-owned command names.
  - A rejected frame is logged and dropped rather than retiring the store.
  - The `randomize` command seeds from the monotonic clock, because the
    sandbox denies `random_get`.
- **M3 cutover.**
  - `IllumoRuntime.exe` with a `game.json` manifest whose budgets are
    clamped to host ceilings, the default package in `game/`, and default
    storage in `storage/<id>`.
  - Update flag bit 0 for product close requests.
  - Launch options are cleared before CLI parsing and after use. The old
    player replayed persisted `--game`/`--storage` values.
  - The native `IllumoGame` executable is removed and the runtime builds by
    default on Windows x64.
  - The first-slice `CSimControlGuest`/`ControlSmoke` and
    `CSimGuestControlCompile` are removed as superseded.
  - `build.py` discovers `IllumoRuntime`.
- **M4 3D frame ABI.**
  - Frame schema v2 adds the world camera, primitive and depth flags, lit
    meshes and shadow casters; v1 still decodes.
  - The guest records its shadow pass against a virtual depth target.
  - The host registers the casters, fits its pass to the guest camera
    (`Renderer::setNextWorldViewProjection`), and draws through `LitMesh`.
- **M5 docs.** README, AGENTS (root and Game), architecture consensus §5.12
  and D-E13, the LaTeX game chapter and decision log. The PDF rebuilt.

Evidence:

- **Release suite:** 482/482 workspace tests.
  - `IllumoGame.Wasm.GamePackage` drives the real package through the
    generic host: menu, keyboard canvas setup, console edit/step/save/load
    matching a native reference hash, canvas render, no dropped frames,
    close. It then drives the 3D mode (4 shadow draws and 4 lit draws over
    two frames).
  - `Illumo.Wasm.FrameValidation` covers v2 round-trips, truncation and
    rejection cases, and v1 compatibility.
- **clang-tidy:** clean for `IllumoGameCore`, `IllumoWasmRendering` and both
  WASM test targets. `RuntimeApplication.cpp` compiles clean.
- **Live GL smoke:** `IllumoRuntime.exe` with no arguments showed the main
  menu, a canvas created with mouse clicks, the glider running after E, and
  the 3D scene with lit cubes.

Not verified, and why:

- **Debug links:** the Debug clang/ASan tidy tree fails to link
  `IllumoRuntime` and `IllumoGameTests` with a duplicate `operator delete`.
  This reproduces at the pre-change baseline, so it is environmental. No
  ASan run of the new code was possible.
- **Interactive coverage:** the workshop import/export dialogs, clipboard
  paste and fullscreen were not exercised live; the native-oracle tests
  cover their shared logic.
- **Linux:** not built (decision: Windows-only runtime).
- **Performance:** not measured. Serial generations run inside the control
  call; the manifest grants 2e10 fuel and a 10 s per-call deadline.

Follow-ups:

- Worker-store simulation offload with asynchronous drains (milestone 6);
  completed below.
- Set the window title from the manifest.
- Package-scoped mapping of the host console's `set`/`get` onto guest
  settings.

2026-09-22, milestone 6 complete (D-E17; execution plan and measurements in
`.agent/wasm-runtime-performance-plan.md`):

- The design's single worker became N lanes: `CSimWorkerGuest.wasm` stores
  owning interleaved eight-row chunk bands with one-row halos (CSL1 protocol
  beside the unchanged CSW1 parity worker), generic `LaneJob`/`JobLanes`
  services, same-frame submission through `GuestUpdateFlags::ServicesPending`,
  and a pipelined control-side merge.
- Drains retire the outstanding generation instead of becoming pending
  operations (the §8 "retire" option, chosen by the user).
- `IllumoGame.Wasm.LaneParity`, `IllumoGame.Wasm.LaneProtocol` and
  `IllumoGame.Wasm.GamePackageLanes` pass; 493/493 Release workspace tests.
- Gates: frames no longer wait for generations (dense 4,285-chunk world at a
  locked 60 FPS versus about 48 serially), but uncapped TPS is 19-32% of the
  native production runner, below the proposed 80% gate. The control-side
  merge (about 2 ms per dense generation) bounds it. Recorded, not waived.
