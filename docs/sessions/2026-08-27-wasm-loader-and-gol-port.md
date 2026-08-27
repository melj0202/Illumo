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
- Do not load it from `CellGameModule::Start`. That guest was `next_state`
  with empty imports; the engine table is `sg_*`.
- `setRuleSet` is the wrong hook for add-objects-and-draw.
- C++ rulesets still run the sim until the ruleset guest is the live path.

## IllumoGame-to-script slice

The ask was to move game code into scripting. The live slice is rulesets only.
`CellGameModule` is not becoming a `.wasm`.

Stay native:

- app definition, module shell, menus
- `SparseCellGrid`, `SimulationRunner`
- `CanvasView` and the PBO upload path
- trap implementations on the host

Do not retarget existing IllumoGame `.cpp` to `wasm32`. Those TUs include the
grid and the renderer. A guest that includes them is a DLL again, even through
wasmtime. New guest TUs see only the trap header.

Guest exports `next_state` / `next_elementary` / `eval_cell`. Host drains,
bakes the 256x9 `TransitionTable` (about 2.3KB) once at ruleset change, and the
existing stepper runs. Calling the guest per cell per tick is how you drop TPS.

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
