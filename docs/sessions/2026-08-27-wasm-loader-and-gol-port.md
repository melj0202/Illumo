# Wasmtime loader, two guest ABIs, and the GoL ruleset port

Status: working notes from the 2026-08-26/27 illumo work room. The wasmtime
loader is implemented but uncommitted. Do not treat this file as a closed
architecture decision. Keep these constraints in mind for the
IllumoGame-to-script move. Fold into `docs/architecture-consensus.md` and the
formal decision log only after the loader lands.

## Locked runtime

Loader, not wasm2c. wasm2c can compile a first-party guest later as a ship
path. It cannot load a user `.wasm` at `Start` unless Illumo becomes a
compiler, which was refused.

Wasmtime sits behind `IScriptSandboxPlugin` so public headers stay RLBox-free.
Fuel and a store `ResourceLimiter` refill on every guest invoke: `on_start`,
`on_update`, and `invokeI32I32`. Setting them only on `createSandbox` still
lets a guest loop freeze the tick.

No WASI. Missing images fail-closed. A missing engine script stays quiet.
The engine owns the host at initialize; stores wait for load.

## Illumo is the host, CSim is the sim

Illumo is the renderer and game host. IllumoGame (CSim) is the cell simulator.
Illumo must not be aware of anything related to the cell simulator. This has
to hold in the host API, not just headers.

`ScriptHost` is load / `invoke` i32 / `register_callback` by name, plus
SceneGraph traps. No `TransitionTable`, no ruleset-change fence, no
"product guest" type, no `next_state` in engine headers.

`startModules` may load `IllumoEngineScript.wasm`. It must not know
`IllumoGameScript.wasm` exists. IllumoGame's `setRuleSet` is what finds that
image, registers `next_state`, drains, and bakes the table. Bake and the nine
rules stay in IllumoGame.

## Two ABIs, two images

Do not merge these tables. Widening one guest to satisfy both is how the
sandbox becomes a DLL again.

Engine guest (`IllumoEngineScript.wasm`):

- Loaded at `startModules` on whatever module is running, including the menu.
- Exports `on_start` / `on_update`.
- Imports `sg_create`, destroy, parent, transform, enabled, visible, and
  `sg_attach_draw` (host-owned DebugDraw3D tokens).
- Packed slot+generation to the guest. Graph id stays on the host.
- Never `Renderer*`, never GL, never a scene pointer.
- No `play_sound` or effects traps until a real host exists. There is no
  audio service under `Illumo/Source` (Engine, Scene, Rendering, Services).

Product guest (`IllumoGameScript.wasm`):

- GoL / ruleset / later editor traps. Sample product image, not the engine API.
- IllumoGame loads it from `setRuleSet`. Do not load it from engine
  `startModules` or from `CellGameModule::Start` as an engine concern.
- Engine `setRuleSet` is the wrong hook for add-objects-and-draw. Game
  `setRuleSet` is the bake point for the nine rulesets.
- Native C++ rulesets still run the sim until the ruleset guest is the live
  path.

## IllumoGame-to-script slice

The ask was to move game code into scripting. The live slice is rulesets only.
`CellGameModule` is not becoming a `.wasm`.

Stay native in IllumoGame:

- app definition, module shell, menus
- `SparseCellGrid`, `SimulationRunner`
- `CanvasView` and the PBO upload path
- drain, bake, and trap registration for `next_state`

Stay native in Illumo:

- wasm load / invoke i32 / register_callback by name
- SceneGraph trap implementations

Do not retarget existing IllumoGame `.cpp` to `wasm32`. Those TUs include the
grid and the renderer. A guest that includes them is a DLL again, even through
wasmtime. New guest TUs see only the trap header.

Guest exports `next_state` / `next_elementary` / `eval_cell`. IllumoGame
drains, bakes the 256x9 `TransitionTable` (about 2.3KB) once at `setRuleSet`,
and the existing native stepper runs. Calling the guest per cell per tick is
how you drop TPS. Per-cell wasm is off the table. Do not call the guest from
`SimulationRunner`.

Do not port `RuleSet::canvas` or dense `CellGrid` into the guest. That path
still compiles and it is the wrong engine.

Editor and save/load are a later product image with drain-gated callbacks, not
this slice. Menus stay native until a ruleset guest actually ticks.

## Performance cliffs to keep

Live TPS is `SparseCellGrid::evaluateCandidateChunk`, not
`RuleSet::calcGeneration`. That loop is already `transitions[cell * 9 + n]`.
Bake the table at ruleset change. Do not call the guest per cell per tick.

While the port is open:

- `SimulationRunner` falls back to a full `copyStateFrom` when the mirror
  delta misses.
- The changed-chunk frontier gives up at 16,384 tracked chunks and evaluates
  everything.
- Presentation is one `CanvasView` texture. Fade is per-texel. Dirty uploads
  merge into at most eight rectangles. Uploads through 64KB are direct; PBO
  only kicks in above that.

## Deferred

- wasm2c as a later ship path for a first-party engine sample
- Audio and particle/effects traps (need a host first)
- Editor / save-load product guest
- Updating canonical architecture docs once the uncommitted loader work lands
