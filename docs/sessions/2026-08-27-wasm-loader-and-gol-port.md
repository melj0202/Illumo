# Wasmtime loader, two guest ABIs, and the GoL ruleset port

Status: working notes from the 2026-08-26/27 illumo work room. Feature work is
on `script-host` at `90e10d26`; `main` is still `6e2f04b2`. Do not treat this
file as a closed architecture decision. Fold into
`docs/architecture-consensus.md` and the formal decision log only after the
host API is actually the host.

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

`ScriptHost` is load (path plus a per-store callback list) / `invoke` i32.
No `TransitionTable`, no ruleset-change fence, no `Ruleset`/`Editor` kinds even
as an internal tag, no "product guest" type, no `next_state` in engine headers.
The host does not name `next_state`. CSim does, at `setRuleSet`.

A global `registerCallback` on the plugin is one ABI: after
`registerEngineCallbacks`, a CSim image can import `sg_create`. Callbacks go
with the path, per store.

Guest still gets packed slot+generation handles, never a `SceneGraph*`.
`createWasmtimeScriptSandbox()` stays in Source, not `Include/Illumo`.

`startModules` may load `IllumoEngineScript.wasm`. It must not know
`IllumoGameScript.wasm` exists. IllumoGame's `setRuleSet` is what finds that
image, names `next_state`, drains, and bakes the table. Bake and the nine
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
  path. Native stepper stays; per-cell wasm is off the table.

## IllumoGame-to-script slice

The ask was to move game code into scripting. The live slice is rulesets only.
`CellGameModule` is not becoming a `.wasm`.

Stay native in IllumoGame:

- app definition, module shell, menus
- `SparseCellGrid`, `SimulationRunner`
- `CanvasView` and the PBO upload path
- drain, bake, and trap registration for `next_state`

Stay native in Illumo:

- wasm load (path + per-store callback list) / invoke i32
- SceneGraph trap implementations (Source, not public headers)

Do not retarget existing IllumoGame `.cpp` to `wasm32`. Those TUs include the
grid and the renderer. A guest that includes them is a DLL again, even through
wasmtime. New guest TUs see only the trap header.

Guest exports `next_state` / `next_elementary` / `eval_cell`. IllumoGame
drains, bakes the 256x9 `TransitionTable` (about 2.3KB) once at `setRuleSet`,
and the existing native stepper runs. Calling the guest per cell per tick is
how you drop TPS. Do not call the guest from `SimulationRunner`.

Do not port `RuleSet::canvas` or dense `CellGrid` into the guest. That path
still compiles and it is the wrong engine.

Editor and save/load are a later product image with drain-gated callbacks, not
this slice. Menus stay native until a ruleset guest actually ticks.

## Still open on `script-host`

As of `90e10d26`, kinds are gone and the CSim guest bakes at `setRuleSet`.
Before this is the host:

- Callbacks per `load`, not a global `registerCallback` on the plugin.
- `SceneGraph*` (`engineGraph`) and `createWasmtimeScriptSandbox()` out of
  `Include/Illumo`.

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
- Updating canonical architecture docs once `script-host` is actually the host
