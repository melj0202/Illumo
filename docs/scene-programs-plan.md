# Scene programs: execution tracker

Design and decisions: `docs/scene-programs-design.md`, accepted 2026-09-26
with the recommended answers to O1-O7. Branch `scene-programs` from
`28b10125`. Stop for owner review after M2.

## Status

| M | State | Notes |
|---|---|---|
| M0 | done | Baseline in design section 13; `@begin` bench directive |
| M1 | done | `ProgramScene`, `SceneDirector` in `Illumo::Content`; 7 tests |
| M2 | done, awaiting review | `GuestProgram`; viewer and IllEd are scenes; CSim through a temporary adapter |
| M3 | not started | Frame schema v8: a world per scene |
| M4 | not started | CSim: `CSimProgram`, `TitleScene`, `CanvasScene` |
| M5 | not started | Host: modules removed |
| M6 | not started | Leftovers, template, optional `Scene` rename |
| M7 | not started | Optional crossfade |
| M8 | not started | Decisions and documentation |

## Prerequisite (done)

The open `host-render-world` work landed as `3b6df22c` (M9 docs) and
`28b10125` (UI performance); `scene-programs` branches from there. Never stage
the untracked `*.csim` saves or the deleted `.grok` workflow.

## M0: baseline

Objective: numbers to compare against, measured before any code change.

- Release `IllumoRuntime`, the owner's host settings, uncapped storage copies:
  - CSim: title-to-canvas and canvas-to-title times, from key press to first
    settled frame.
  - Memory (private bytes) on the title screen, on the canvas, and after a
    return to the title screen.
  - Bench medians for the paused canvas, settled menu, settings overlay and
    workshop, using the interleaved A/B scripts from the UI performance work.
- Allocation gate baselines (game paused 0 and running 1; IllEd and viewer).
- Screenshot references for each app's first frame and CSim's two screens.

Exit: numbers recorded in design section 13 (validation results).

## M1: engine scenes

Objective: `ProgramScene` and `SceneDirector` exist in `Illumo::Content` (a
scene owns a `SceneInstance`, which core `Illumo` may not include), built into
host and guest content libraries. No product uses them yet.

- `Illumo/Include/Illumo/Content/ProgramScene.h` and `SceneDirector.h`, plus
  sources, following design 6.2 and 6.3: `Cut` and `Cover` switching,
  keep-alive, failed-start fallback, input drain, pass reset, and the context
  camera and world following the active scene.
- `IllumoContext::scenes` added. `moduleHost` stays until M5.
- Tests (`Illumo/Tests/Content/TestSceneDirector.cpp`):
  - first switch starts and enters;
  - leaving keeps, and resuming enters without restarting;
  - release stops and destroys, and is refused for the active scene;
  - a failed start re-enters the previous scene, or asks to close when there
    is none;
  - the switch is deferred to the frame boundary;
  - key and character queues are drained;
  - commands registered in `enter` disappear after `leave`;
  - the context camera tracks the active scene.

Exit: full suite green, no product changes.

## M2: guest programs

Objective: guests run scenes. The two single-screen apps prove the SDK.

- `IllumoGuest`: `GuestModuleApplication` becomes `GuestProgram` (design
  6.4), with `createScenes`, `updateProgram` and `dispatchProgram`.
  `IModuleHost` is dropped from the guest.
- IllMeshViewer: `ViewerApplication` becomes `ViewerProgram`, and
  `MeshViewerModule` becomes `ViewerScene` (content is the viewed document).
- IllEd: `EditorApplication` becomes `EditorProgram`, and `EditorModule`
  becomes `EditorScene` (content is the edited document).
- CSim, temporarily: `GameApplication` becomes a `GuestProgram` whose two
  scenes wrap today's modules through a short-lived adapter (removed in M4),
  so every package keeps running.
- Package tests (`IllEd.Wasm.Package`, the viewer package, game package) and
  gates unchanged and green.

Exit: full suite green; the three apps run; screenshots match M0. **Stop for
owner review.**

## M3: a world per scene (frame schema v8)

Objective: design 6.5.

- `IllumoGuest/Frame.h` v8:
  - `SelectWorld`, `DestroyWorld` and `SetWorldCamera` world operations;
  - the `World` composition entry names a world;
  - `GuestFrameLimits::worlds = 8`;
  - v7 decoding kept.
- Host (`WasmFrameRenderer`, `WasmVisuals` world path, `RenderWorld`):
  - worlds keyed by id per guest, with leases per world;
  - each world drawn with its own camera;
  - `DestroyWorld` releases on acceptance.
- Guest: `GuestRenderWorld` per scene over one operation queue; the recorder
  composes the active scene's world; `SceneDirector` binds worlds on switch.
- Tests: v8 round trips; a v7 frame from a baseline package still draws; two
  worlds with one composed draw only that one; `DestroyWorld` frees; the
  world limit is enforced; allocation gates unchanged.

Exit: full suite green, viewer and IllEd benches within noise of M0.

## M4: CSim program and scenes

Objective: design 6.8 for CSim.

- `CSimProgram` owns the catalog, typeface, sounds, one `ConfigurationMenu`
  (through `SimulatorSettingsTarget`), the restart prompt, the software
  pointer and the transition cover.
- `MainMenuModule` becomes `TitleScene`, and `CellGameModule` becomes
  `CanvasScene`. Program-level pieces move out.
- Flow: new and load release any kept canvas, then `Cover`-switch to a fresh
  one. Returning to the menu keeps the canvas. Resume follows O7.
- `leave` parks the `SimulationRunner` and autosave; `enter` resumes them.
  Suspended scenes don't simulate (O3).
- Tests:
  - `TestMainMenuModule` and `TestCellGameModule` fixtures move to
    `SceneDirector`; the mock hosts are deleted;
  - new cases:
    - one Settings instance serves both scenes;
    - a kept canvas resumes with its world, camera and selection;
    - new simulation replaces the kept canvas;
    - leaving drains the runner;
    - a round-trip allocation gate settles to zero.

Exit: full suite green; transition times and memory recorded against M0;
screenshots match M0 apart from animated regions.

## M5: host without modules

Objective: design 6.7.

- `WasmGameModule` becomes `WasmProgram`, and `DebugModule` becomes
  `DebugOverlay`.
- `RuntimeModule` and `Application.cpp`'s runner become `RuntimeShell`, which
  owns the frame order.
- `Illumo` loses the registry, transitions and rollback. `IModule.h`,
  `IModuleHost.h` and `IllumoContext::moduleHost` are deleted.
- `TestIllumoHost`: module-mechanics cases are deleted; frame order, input
  drain, global hotkeys, close negotiation and pipeline cases are rewritten
  against `RuntimeShell`.

Exit: full suite green; `rg "IModule\b|IModuleHost|RequestTransition"`
returns nothing outside `docs/history` and archives.

## M6: leftovers

- Native product definitions deleted, and their config tests moved to the
  programs. The viewer and IllEd ones went in M2; `IllumoGameApplication.cpp`
  remains.
- IllEd: `EditorDocument` borrows `EditorScene::content()` instead of owning
  its `SceneInstance` (it reuses one instance across new, open and undo).
- `Templates/SpinningCube` converted or deleted (O2).
- Engine `Scene` renamed if O1 is accepted, in a mechanical commit of its own.

## M7: crossfade (optional, O4)

- Per-entry opacity in compositions; a camera for world-space 2D visuals per
  scene.
- `SceneSwitch::Crossfade`.

## M8: decisions and documentation

- Decision log:
  - D-E31 (programs own scenes; modules removed);
  - D-R30 (a world per scene, frame v8);
  - D-004, D-E1, D-E5 and D-E9 marked superseded, with D-B1 kept.
- `docs/architecture-consensus.md` (module sections), LaTeX 04 (runtime loop)
  and 07 (game), `docs/packages/engine.md`, `docs/packages/game.md`,
  `docs/wasm-game-runtime-design.md`.
- AGENTS guidance (root, `IllumoGame/Source/Game`, `Illumo/Source/Engine`,
  guest SDK).
- PDF rebuilt through `docs/build.ps1`.

## Log

- 2026-09-26: the owner asked to drop the module abstraction in favour of
  scenes, since products are WASM programs now. The owner chose:
  - host and guests in scope;
  - scenes as code backed by a `SceneInstance`;
  - kept scenes that resume;
  - design first.

  Design and tracker written.
- 2026-09-26: the owner said to commit and go ahead. Pending work committed on
  `host-render-world`; branch `scene-programs` created; the recommended
  answers to O1-O7 stand.
- 2026-09-26, M0: baseline recorded (design section 13). Switching modules
  costs one 4.4-5.2 ms guest update each way, and a kept canvas would cost
  under 10 MB. Bench and capture scripts gained `@begin`, so a switch can be
  timed.
- 2026-09-26, M1: scenes and the director land in `Illumo::Content` rather
  than the engine core, because core `Illumo` may not include Content and a
  scene owns a `SceneInstance`. There is one camera per program; the director
  saves and restores its state per scene, since visuals hold camera pointers.
  Scene commands are scoped through `ProgramScene::command`, and per-scene
  worlds plug in through `ISceneWorlds` (M3). Full suite 679 of 679.
- 2026-09-26, M2:
  - `GuestProgram` runs scenes. IllMeshViewer (content is the opened
    document) and IllEd are single scenes, and CSim's modules run through a
    temporary `ModuleScene` adapter.
  - Parity with M0 holds (design section 13).
  - Found: the canvas inherited the title's 0.5 zoom through the shared
    camera. The adapter preserves this; `CanvasScene` must choose a zoom in
    M4, and `LaunchDirect` opens at 1 today.
  - Deferred: IllEd's document still owns its `SceneInstance`; making it
    borrow `content()` needs `EditorDocument` to reuse one instance across
    new, open and undo. Scheduled with M6.
  - The native viewer and IllEd definitions were deleted early.
  - Full suite 679 of 679. Stopped for owner review.
