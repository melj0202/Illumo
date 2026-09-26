# Scene programs: one program per package, scenes instead of modules

**Status:** accepted 2026-09-26. The owner approved proceeding ("go for it"),
so the recommended answers to O1-O7 stand; each can still be revisited before
the milestone that depends on it. Work is on branch `scene-programs`.
**Tracker:** `docs/scene-programs-plan.md`.
**Baseline:** `28b10125` on `host-render-world` (host rendering complete,
including M9 and the UI performance work).
**Supersedes, on acceptance:**
- D-004 (module frame hooks): replaced by scene hooks.
- D-E1 (module registration lives in App): there is nothing left to register.
- D-E5 (`IllumoContext` frozen for the "two shipped modules").
- D-E9 (DebugModule is a global overlay): the overlay stays, but not as a
  module.

D-B1 is kept: debug tools stay out of Release.
**New decisions (to record at M8):**
- D-E31: programs own scenes; the module abstraction is removed.
- D-R30: one render world per scene (frame schema v8).

---

## 1. Problem

`IModule` was the unit of product composition when products were native
executables. Since the WASM cutover (D-E21 onward), a product is a WASM
program run by `IllumoRuntime`, and the module layer no longer fits.

**On the host, module machinery serves one real module.** `Illumo` is a
module host: required and optional registrations, rollback, transitions, and
the `IModuleHost` interface (`Illumo/Source/Engine/Illumo.cpp`, 771 lines).
It hosts only:
- `RuntimeModule`, which wraps `WasmGameModule`
  (`Illumo/Source/Wasm/RuntimeApplication.cpp:307`);
- `DebugModule`, in Debug and RelWithDebInfo builds only.

The native executables are gone: `build.py doctor` reports `IllumoGame.exe`,
`IllEd.exe` and `IllMeshViewer.exe` as stale outputs. Their product
definitions (`IllumoGameApplication.cpp`, `IllEdApplication.cpp`,
`IllMeshViewerApplication.cpp`) and the native `Templates/SpinningCube`
module are still compiled.

**Inside a guest, modules are the product structure, and they fit badly.**
`GuestModuleApplication` (`IllumoGuest/Source/ModuleApplication.cpp`) runs
one `IModule` at a time and replaces it on `RequestTransition`. CSim swaps
`MainMenuModule` (1,783 lines) and `CellGameModule` (5,559 lines). Each swap
destroys everything the old module built, which causes three problems:
- **Shared things are built twice.** The title screen and the canvas each own
  a `ConfigurationMenu`. Each reimplements `currentConfiguration` and
  `applyConfiguration` and registers and unregisters its own console
  commands.
- **Returning is a cold start.** Leaving the canvas for the title screen
  destroys the world, the camera, the simulation runner and every retained
  visual. The user can't go back to the canvas they left. The host re-creates
  every visual and world object it just released.
- **"Scene" means three things,** none of them the one the owner means:
  - the engine `Scene`, a per-frame drawable list (D-E4);
  - `SceneInstance` / `.ilsc`, content scenes with a render world (D-E25,
    D-E30);
  - the product's screens, which are modules.

A module is also the wrong unit for the host render world. There is one
`GuestRenderWorld` per guest, and every scene of every module shares it
(`IRenderWorld.h`: "one guest world serves every scene of its modules"). A
screen kept alive in the background would keep drawing its world instances.

## 2. Owner decisions (2026-09-26)

| # | Question | Decision |
|---|---|---|
| S1 | Scope | Host and guests. `IModule`, `IModuleHost` and the module runner are deleted, along with the native product leftovers. |
| S2 | What a scene is | Program-owned code with its own logic and UI, backed by a `SceneInstance` (optionally loaded from a package `.ilsc`). |
| S3 | Leaving a scene | The program keeps scenes it may return to and resumes them. |
| S4 | Process | Design and plan first, then milestone by milestone. |

Open questions for the owner are in section 11.

## 3. End state

- **Host:** `IllumoRuntime` runs exactly one WASM program. Its frame is fixed
  code in one place:
  1. input
  2. global hotkeys
  3. the debug overlay (non-Release)
  4. the program
  5. render

  There is no module registry, no `IModuleHost`, and no
  `createRequiredModule`. `WasmGameModule` becomes `WasmProgram`, a concrete
  class. `DebugModule` becomes `DebugOverlay`, a concrete class compiled only
  with `ILLUMO_ENABLE_DEBUG_TOOLS`.
- **Guest:** a product derives from `GuestProgram` (renamed from
  `GuestModuleApplication`).
  - The program lives as long as the WASM store and owns what every scene
    shares: settings, fonts, sounds, catalogs, program-wide UI and commands.
  - It holds named `ProgramScene`s in a `SceneDirector`, and one of them is
    active.
  - Switching scenes happens at a frame boundary and keeps the scene being
    left, unless the program releases it.
- **Scene:** a `ProgramScene` owns:
  - its `SceneInstance` (empty unless it loads a document);
  - its camera;
  - its render world (on the host, one per scene);
  - its 2D visuals and its commands.

  Only the active scene updates and receives input. A kept scene is frozen.
- **CSim:**
  - One program, `CSimProgram`, with two scenes: `TitleScene` and
    `CanvasScene`.
  - There is one Settings menu, owned by the program.
  - Returning to the title keeps the canvas; creating or loading a simulation
    replaces it.
- **IllEd and IllMeshViewer:** one program with one scene each.
- **Naming:** "scene" means `ProgramScene` together with its content. The
  engine's per-frame drawable list is renamed if the owner agrees (O1).

## 4. Non-goals

- **No scripting or data-driven behaviour.** Scenes are C++. An `.ilsc` file
  supplies content, not logic.
- **No scene graph of scenes.** No nesting and no stack semantics beyond
  "active" plus "kept". Overlays (Settings, dialogs) stay plain objects owned
  by the program or a scene.
- **No change to the WASM capability model, the file protocol or the ABI**,
  beyond the frame schema v8 world addressing in section 6.5.
- **Crossfades are not in the first cut.** The design allows them later (O4).
- **No native product executables.** The removal is final.

## 5. Invariants that remain true

- Engine code never includes product headers (the spirit of D-E1).
- Debug tools are absent from Release builds (D-B1).
- Guests keep game state and the host keeps render objects (D-E30, D-R28,
  D-R29). A kept scene's host objects persist, unchanged and undrawn.
- Frames are recorded once per host frame. Key and character queues stay
  per-frame and are drained on a scene switch, as on a module switch today,
  so a click does not bleed into the next scene.
- Allocation gates keep their baselines. A steady frame allocates nothing in
  `game.Update`, whether scenes are kept or not.
- Failure is loud. A scene that fails to start reports it, and the program
  decides whether to fall back or close. A failed first scene closes the
  product, as a failed required module does today.

## 6. Design

### 6.1 Objects

| Object | Where | Owns | Lifetime |
|---|---|---|---|
| `WasmProgram` | host, `Illumo/Wasm` | the guest store, services, frame renderer, per-scene worlds | the process |
| `RuntimeShell` | host, `RuntimeApplication.cpp` | `Illumo` services, `WasmProgram`, `DebugOverlay`, capture and bench | the process |
| `GuestProgram` | guest SDK, `IllumoGuest` | guest services, `IllumoContext`, the `SceneDirector`, program UI | the WASM store |
| `SceneDirector` | `Illumo::Content` (host), `IllumoGuestContent` (guest) | named scenes, each scene's saved camera and world, the active scene, a pending switch | the program |
| `ProgramScene` | `Illumo::Content`, `IllumoGuestContent` | `SceneInstance`, render world binding, scene-scoped commands, product state | created and released by the program |

`SceneDirector` and `ProgramScene` live in the Content library. A scene owns a
`SceneInstance`, and core `Illumo` never includes `<Illumo/Content/...>`. Both
host and guest builds compile them, so native product tests (`IllumoGameTests`)
can run a real director without a WASM store, and no mock host is needed.
`GuestProgram` composes a director and feeds it its context.

### 6.2 `ProgramScene`

```cpp
class ProgramScene
{
public:
  virtual ~ProgramScene();

  // Once, the first time the scene is entered. False fails the switch.
  virtual bool start(IllumoContext& context) = 0;
  // Each time the scene becomes active, including right after start.
  // Register console commands and bind input contexts here.
  virtual void enter() {}
  // Each time it stops being active. It is kept (frozen) until released:
  // withdraw commands, park workers, keep everything else.
  virtual void leave() {}
  // Active scene only.
  virtual void update(double elapsed) = 0;
  virtual void dispatch(Scene& frame) = 0;
  // Once, when the program releases the scene (or the program stops).
  virtual void stop() = 0;
  // Close negotiation, as IModule::OnCloseRequested today.
  virtual bool closeRequested() { return true; }
  // Options for the scene's SceneInstance (editors want pick proxies).
  virtual SceneInstanceOptions contentOptions() const { return {}; }

  SceneInstance& content();   // from start on
  IRenderWorld* world() const; // this scene's world, or nullptr

protected:
  IllumoContext& context();
  // A console command that lives while the scene is active: withdrawn
  // when it leaves or stops. Call from enter().
  void command(name, function, usage, description, completions);
  // Updates the content and adds its drawable and sky to the world layer.
  void dispatchContent(Scene& frame);
};
```

The hooks are, deliberately, close to today's `IModule` hooks. What changes
is ownership:
- The program owns scenes and can hold several.
- Starting is separate from entering, and leaving is separate from stopping.
- Content, camera state, world and commands belong to the scene, not to the
  process.

There is one `Camera` per program, because visuals and the renderer hold
pointers to it. Each scene still has its own camera state: the director
saves the camera when a scene leaves and restores it when the scene
re-enters, and a scene starts from the camera the director was created with.
A frozen scene never runs, so one live camera is enough. Crossfades would need
two (O4).

A scene that loads content calls `content().load(document, root, error)` in
`start`. The document may come from a package `.ilsc` through the asset cache
the program already preloads (`packageAssets`).

### 6.3 `SceneDirector` and switching

```cpp
class SceneDirector
{
public:
  SceneDirector(IllumoContext& context, ISceneWorlds* worlds = nullptr);
  // Takes ownership; the scene is not started until first entered.
  bool add(std::string name, std::unique_ptr<ProgramScene> scene);
  // Applied at the next frame boundary. Replaces any pending request.
  bool switchTo(std::string_view name, SceneSwitch how = SceneSwitch::Cut);
  bool covering() const;   // a Cover switch waits for coverComplete()
  void coverComplete();
  // Stops and destroys a kept scene; refused for the active one and the
  // target of a pending switch.
  bool release(std::string_view name);
  // Frame boundary; false when a first scene failed and the program closes.
  bool applyPending();
  void update(double elapsed);  // active scene only
  void dispatch(Scene& frame);  // active scene only
  bool closeRequested();
  void stopAll() noexcept;      // also run by the destructor
};

// Per-scene render worlds where the host keeps them (frame schema v8).
class ISceneWorlds
{
public:
  virtual IRenderWorld* create() = 0;
  virtual void activate(IRenderWorld* world) = 0;
  virtual void destroy(IRenderWorld* world) = 0;
};
```

A switch at the frame boundary runs these steps in order:
1. Drain the key and character queues.
2. `ResetDefaultPasses` and clear the frame's drawables.
3. `leave()` the active scene, withdraw its commands and save its camera.
4. `start()` the target if it hasn't started. First the director:
   - creates its content;
   - gives it its world (its own through `ISceneWorlds`, otherwise the
     context's shared one);
   - resets the camera to the initial one.
5. Restore the target's saved camera. `IllumoContext::renderWorld` becomes the
   target's world and is activated.
6. `enter()` the target.

If `start` fails (or throws, after which the scene is stopped), the
director re-enters the previous scene and logs an error. If there is no previous scene (the program's first switch), it asks
the program to close.

`SceneSwitch` values:
- **`Cut`:** switch at the next frame boundary.
- **`Cover`:** the program draws its transition cover (CSim's darken)
  and the switch happens when the cover reports full. The outgoing scene
  keeps updating until then, exactly as CSim's canvas return does today.
- **`Crossfade`:** reserved (O4).

### 6.4 `GuestProgram`

`GuestModuleApplication` loses `IModuleHost`, `createFirstModule`,
`RequestTransition`, `m_module` and `m_pending`, and gains:
- `SceneDirector& scenes()`.
- `virtual bool createScenes(SceneDirector&)`. It runs once after bootstrap,
  adds the program's scenes and switches to the first one.
- Program-level update and dispatch around the active scene:
  1. `updateProgram(elapsed)` before the scene: program UI that has input
     first, such as an open Settings menu.
  2. The active scene's `update(elapsed)`.
  3. `updateOverlay(elapsed)` after the scene: the software pointer and the
     transition cover.

  Dispatch follows the same order: `dispatchProgram`, then the scene, then
  `dispatchOverlay`, so program UI draws on top.

The phases (Settings, Bootstrap, Running, Stopped), service pumping, console
forwarding, frame recording and capability grants are unchanged. The
`hostRenderWorld=0` and `hostVisuals=0` switches move from `startModule` to
program start.

### 6.5 Content and render worlds: frame schema v8

A kept scene must neither draw nor lose its host objects, so each scene gets
its own world on the host.

- **World addressing:** a new world operation `SelectWorld(worldId)` sets the
  target of the world operations after it in the frame. Worlds are created
  implicitly on first selection and destroyed with `DestroyWorld(worldId)`.
  Material and instance ids stay unique across a guest's worlds, so a lease
  names one object.
- **Camera per world:** `SetWorldCamera(matrix)` stores a camera with the
  selected world. The frame-level camera (v2) stays for world-layer batches.
- **Composition:** the `World` composition entry uses `first` as a world id
  (v7 ignored it; v8 requires a live world). The host draws that world with
  its own camera.
- **Limits:** at most 8 worlds per guest (`GuestFrameLimits::worlds`). An
  undrawn world costs host memory (materials, instances, recorded lists) and
  nothing per frame.
- **Guest side:** `GuestRenderWorld` becomes a per-scene object sharing the
  backend's operation queue. The director tells the backend which world is
  active, and the recorder composes only the active world. A kept world's
  queued operations are flushed before the switch.
- **Compatibility:** the host keeps decoding v7 frames as "world 1, frame
  camera", so an older package keeps working until it is rebuilt. Guests
  built from this repo emit v8.
- **Rollback:** `hostRenderWorld=0` still returns scenes to `MeshVisual`s.

The alternative (one world, where scenes hide their instances when left)
needs no ABI change. It costs a visibility sweep per switch and rules out
drawing two scenes' worlds at once (O5).

### 6.6 Input, commands and `IllumoContext`

- **Input:** only the program's pre-scene UI and the active scene see input.
  A kept scene sees none, and resuming does not replay input it missed.
- **Commands:**
  - Program commands register once. CSim's candidates are `sound`,
    `version`, `quit` and the rule catalog commands, whichever the owner
    wants global.
  - Scene commands register in `enter` and unregister in `leave`, so a kept
    scene's commands disappear from the console.
  - `GuestProgram::synchronizeCommands` already forwards the changes.
- **`IllumoContext`:**
  - `moduleHost` is removed and `scenes` (`SceneDirector*`) is added.
  - `camera` and `renderWorld` follow the active scene.
  - The rest of the bag is unchanged.
  - D-E5's per-module field audit becomes a per-scene audit in `start`.

### 6.7 Host: `RuntimeShell`, `WasmProgram`, `DebugOverlay`

- `Illumo` keeps service ownership, initialisation, host defaults, the scene
  pipeline (motion blur), global hotkeys (F11, F3, F5), close handling and
  `render()`. It loses `addModule`, `startModules`, `RequestTransition`,
  `RegisteredModule` and rollback.
- `RuntimeShell` (in `RuntimeApplication.cpp`, replacing `RuntimeModule`)
  owns the frame order from section 3. It also owns the capture and bench
  hooks that `RuntimeModule` holds today.
- `WasmGameModule` becomes `WasmProgram`: the same members, with
  `start/update/dispatch/stop/closeRequested` in place of the `IModule`
  overrides.
- `DebugModule` becomes `DebugOverlay`. Its code (console routing, detached
  console, profiler and file tree overlays, renderer demo) is unchanged, and
  so is its position before the program in update and after it in dispatch.
- `IllumoApplicationDefinition` loses `createRequiredModule`. The generic
  runner in `Application.cpp` becomes the runtime's own loop, since the
  runtime is the only host executable. The platform entry calls the runtime
  directly.

### 6.8 Products

**CSim** (`IllumoGame/Source/Wasm/GameApplication.cpp` becomes
`CSimProgram`):
- **The program owns:**
  - the catalog, the typeface and the sound bank (as today);
  - one `ConfigurationMenu`;
  - the restart prompt;
  - the software pointer;
  - the transition cover.
- **Settings and the active scene:** the menu reads and applies through a
  small interface the active scene implements (`SimulatorSettingsTarget`):
  - the title scene edits the defaults for the next world;
  - the canvas scene applies to the live world.

  This replaces today's two copies of `currentConfiguration` and
  `applyConfiguration`.
- **`TitleScene`** (from `MainMenuModule`) keeps:
  - the background Immigration world (a `CellContext`);
  - the title UI layers;
  - the New Simulation menu.
- **`CanvasScene`** (from `CellGameModule`) keeps everything else, including
  the Ruleset Workshop, the paint drawer, the inspector and autosave. Leaving
  it parks the `SimulationRunner` (drain, no new work) and pauses autosave.
  Entering resumes both.
- **Flow:**
  - Creating a simulation from the title screen releases any kept canvas,
    adds a new `CanvasScene` and switches to it with `Cover`.
  - Loading a simulation does the same with the load path.
  - Returning to the menu from the canvas switches to `TitleScene` and keeps
    the canvas.
  - Whether the title then offers to resume that canvas is a product
    question (O7).
- **Direct launch** (`LaunchDirect`) starts on the canvas.

**IllEd:** `EditorApplication` becomes `EditorProgram`, with one
`EditorScene` from `EditorModule`. The edited document becomes the scene's
`SceneInstance`.

**IllMeshViewer:** `ViewerApplication` becomes `ViewerProgram`, with one
`ViewerScene` from `MeshViewerModule`. The viewed document becomes the
scene's content.

**Removed:**
- `IllumoGame/Source/Game/IllumoGameApplication.cpp`,
  `IllEd/Source/IllEdApplication.cpp` and
  `IllMeshViewer/Source/IllMeshViewerApplication.cpp`, with the parts of
  `TestIllumoGameConfig`, `TestIllEdConfig` and `TestRuleCatalogLoader` that
  test them. Their `applyDefaults` checks move to the programs.
- `Templates/SpinningCube`: converted to a minimal WASM scene program, or
  deleted (O2).

### 6.9 Ownership, lifetime, errors, threading

- Scenes are main-thread objects, like `SceneInstance`.
- The director owns scenes through `std::unique_ptr`. When the program
  stops, the director leaves and stops the active scene, then stops the kept
  ones newest first. A scene that never started is never stopped.
- A scene must not hold pointers into another scene. Anything shared goes
  through the program.
- A switch requested during `update` applies at the next frame boundary,
  never in the middle of a frame.
- Worker-backed state (CSim's simulation runner) is parked in `leave` and
  must be idle before `stop` runs.
- Destroying a scene destroys its world through `DestroyWorld`, and its host
  objects are released when that frame is accepted.

## 7. Compatibility and migration

- The host decodes v7 and v8 frames. Other ABI surfaces are unchanged.
- Each milestone lands green on the full Release suite, the allocation gates
  and the three apps. There is no runtime switch between modules and scenes;
  rollback is reverting a milestone.
- Saved games, settings files and package layouts are unaffected.
  `LaunchDirect`, `--open` and capture and bench scripts keep their meaning.

## 8. Alternatives considered

- **Keep modules and add keep-alive to them.** This fixes cold returns but
  keeps two host-level concepts (module and program) for one thing, the
  duplicated Settings, and the confusion about what a scene is.
- **Scenes as data (`.ilsc` plus behaviour bindings).** Flexible, but it needs
  a behaviour-binding or scripting design first. Rejected for now (S2 chose
  code scenes backed by content).
- **Program-only, no scene type.** Each product would hand-roll screen
  switching inside one class. That is simple for single-screen apps but
  pushes CSim back to one giant class.
- **One shared world with visibility toggling.** See section 6.5 and O5.

## 9. Milestones

These are summarised here; the tracker has the detail.

| M | Content |
|---|---|
| M0 | Baseline: transition timings, memory, allocation gates, bench medians; commit or shelve the open host-render-world work; new branch. |
| M1 | Engine `ProgramScene` and `SceneDirector`, with native tests (lifecycle, keep and resume, failed start falls back, input drain, command withdrawal). |
| M2 | Guest SDK `GuestProgram` over the director; IllMeshViewer and IllEd ported (one scene each). `GuestModuleApplication` removed. |
| M3 | Frame schema v8 world addressing and per-world camera; per-scene `GuestRenderWorld`; host worlds per guest; v7 decode kept. |
| M4 | CSim: `CSimProgram`, `TitleScene`, `CanvasScene`, one Settings menu, kept canvas, runner parking; test fixtures moved off mock module hosts. |
| M5 | Host: `WasmProgram`, `DebugOverlay`, `RuntimeShell`; `IModule`, `IModuleHost` and the module registry deleted; `TestIllumoHost` rewritten. |
| M6 | Leftovers: native product definitions, SpinningCube (O2), the engine `Scene` rename (O1) if accepted. |
| M7 | Optional: crossfade (O4). |
| M8 | Decisions D-E31 and D-R30 recorded; D-004, D-E1, D-E5 and D-E9 marked superseded; consensus, LaTeX 04 and 07, package docs, AGENTS guidance, PDF. |

## 10. Verification

- **Full Release suite:** `ctest -L "IllumoWorkspace|IllumoGpu"`, green at
  every milestone. The host settings file is restored after each run.
- **Allocation gates:** `IllumoGame.Wasm.PackageFrameAllocations` at its
  paused 0 and running 1 baseline. IllEd and viewer gates unchanged. A new
  gate covers a title-to-canvas-to-title round trip settling back to zero.
- **Transitions:** return-to-title and resume timings against M0; memory
  with a kept canvas.
- **Parity:** screenshot parity of each app's first frame and of CSim's two
  scenes against M0, allowing only animated regions to differ.
- **Guests:** v8 frames decode on the host; v7 frames from a baseline
  package still run.

## 11. Open questions for the owner

| # | Question | Recommendation |
|---|---|---|
| O1 | Rename the engine `Scene` (per-frame drawable list) so "scene" is unambiguous? 181 references, mechanical. | Yes, to `DrawList`, in M6. |
| O2 | `Templates/SpinningCube`: convert to a minimal WASM scene program, or delete? | Convert; it becomes the template for new programs. |
| O3 | Does a kept canvas keep simulating in the background? | No, it freezes. Resuming continues where it left off, and no worker runs unseen. |
| O4 | Crossfades between scenes? They need per-entry opacity in compositions and a camera per world-space 2D visual set (CSim's canvas cells use the frame camera). | Later (M7); ship `Cut` and `Cover` first. |
| O5 | One world per scene (frame v8) or one shared world with visibility toggling? | One per scene: no sweeps, and it is what crossfades need. |
| O6 | Keep `IllumoContext` as the service bag (minus `moduleHost`, plus `scenes`) or introduce a new context type? | Keep it; the change is two fields. |
| O7 | CSim: should the title screen offer to resume a kept canvas? | Yes, as a "Resume" row shown only when one is kept, but it is a product call. |

## 12. Risks

- **`CellGameModule` is 5,559 lines** with its own transition and cover
  logic. Splitting program state (Settings, restart) out of it is the largest
  edit. M4 keeps the class and renames it into a scene first, then moves
  shared pieces out.
- **Test churn:**
  - `TestCellGameModule` and `TestMainMenuModule` build around mock module
    hosts (`CanvasReturnHost`, `MockModuleHost`) and move to a real director.
  - `TestIllumoHost` (1,199 lines) mostly tests module mechanics that
    disappear. Its frame-order, input-drain and close-negotiation cases are
    kept.
- **Kept scenes and workers:** a parked `SimulationRunner` must hold no lane
  work. M4 adds a test that leaving drains the runner.
- **Memory:** a kept canvas holds its grid, visuals and host world, which is
  bounded by the world limit. M0 measures the baseline.
- **Uncommitted work on the current branch** (M9 docs, UI performance) must
  land or be shelved before M1, so the refactor starts from a clean baseline.

## 13. Validation results

### M0 (2026-09-26, branch `scene-programs` at `51a3b635`)

Release `IllumoRuntime`, the owner's host settings (1552x877, MSAA 4, vsync
off, uncapped), CSim storage with the frame cap off.

- **Harness:** a bench or capture script may now contain `@begin`. Counting
  (warm-up, timed or capture frames) starts there while the lines after it
  keep running, so a benchmark can time a transition.
- **Transition hitch, 5 runs each:** a 3,000-frame window opened just before
  the switch; frame interval maximum, with the host's longest guest update in
  brackets.

  | Switch | Worst frame | Guest update |
  |---|---|---|
  | Title to canvas (Create) | 5.1-5.4 ms | 4.4-4.9 ms |
  | Canvas to title (`menu`) | 5.2-5.8 ms | 4.4-5.2 ms |

  The p50 frame is 0.5-0.75 ms. Nearly all of each hitch is the module
  change inside the guest update: the old module's `Exit` and the new one's
  `Start`, including the title's background world.
- **Memory** (private bytes, one run; working set in brackets):

  | Point | Private | Working set |
  |---|---|---|
  | Title screen | 326 MB | 221 MB |
  | Canvas | 331 MB | 224 MB |
  | Back on the title | 322 MB | 219 MB |

  WASM linear memory never shrinks, so the return figure is the high-water
  mark less what the host released. The canvas costs under 10 MB, so keeping
  one alive is cheap.
- **Frame benches** (the interleaved A/B from the UI performance work, 5 runs,
  medians, this build):

  | Scene | FPS | Guest frame |
  |---|---|---|
  | Paused canvas | 1,852 | 0.07 ms |
  | Settled main menu | 899 | 0.15 ms |
  | New Simulation | 1,041 | 0.30 ms |
  | Settings overlay | 645 | 0.32 ms |
  | Ruleset Workshop | 1,508 | 0.17 ms |

- **Allocation gates:** `IllumoGame.Wasm.PackageFrameAllocations` (paused 0,
  running 1), `IllumoGame.Wasm.LaneAllocations`, `SteadyTextAllocations` and
  the scene graph gates all pass (17 of 17 `Alloc` tests).
- **Screenshots:** references of IllEd and IllMeshViewer at frame 600, and
  of CSim's settled title and paused canvas at frame 4,000. They are kept
  with the session's measurement files, not in the repository.

### M1 (2026-09-26, branch `scene-programs`)

- `Illumo/Include/Illumo/Content/ProgramScene.h` and `SceneDirector.h`,
  compiled into `Illumo::Content` and `IllumoGuestContent`.
- `IllumoContext::scenes` added; `moduleHost` stays until M5.
- `Illumo/Tests/Content/TestSceneDirector.cpp` covers:
  - **Lifecycle:** add, switch at the boundary, keep and resume, release.
  - **Failed starts:** fallback to the previous scene, a start that throws,
    and closing when the first scene fails.
  - **Cover switches.**
  - **Input and commands:** input drained on a switch; scene commands
    withdrawn and registered again; close negotiation.
  - **Cameras:** a camera per scene through save and restore.
  - **Worlds:** per-scene worlds through `ISceneWorlds`, or the shared world
    without one.
  - **Shutdown order.**
- Full Release suite 679 of 679 (672 plus the 7 new cases).
