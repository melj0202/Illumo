# Game subsystem guidance

This file specializes the repository `AGENTS.md` for `IllumoGame/Source/Game/`.

## Scope and boundaries

Game owns cellular-automata state, editing, camera-facing presentation,
simulation timing, ruleset selection, and save/load orchestration. It may use
Rulesets, Services interfaces, Foundation values, and backend-neutral Rendering
types. It must not depend on OpenGL or native platform APIs.

IllumoGame ships only as the `IllumoGame.wasm` package hosted by
`IllumoRuntime`. Its application seam is `IllumoGame/Source/Wasm/
GameApplication.cpp`, a `GuestModuleApplication` that bootstraps settings and
catalogs, installs the guest `CSimPlatform`, and starts `MainMenuModule`. The
same Game sources also build natively into `IllumoGameCore`, which is only the
test oracle; `IllumoGameApplication.cpp` remains for those tests. Do not add a
process entry point, frame loop, logger lifetime, SysCmdLine implementation,
BuildInfo, native SDK code, or platform implementation under IllumoGame, and
do not reintroduce a native game executable.

Game code performs dialogs, file transfers, clipboard access and user-catalog
writes only through `CSimPlatform`. Completions may run before the request
returns (native oracle, `CSimPlatformNative.cpp`) or on a later update (guest,
`Wasm/GuestPlatform.cpp`); guard callbacks with the module lifetime token and
never assume either timing. Locations are opaque: a path natively, a storage
name or `selected:` capability in the guest. Do not call `SaveLoad`,
`Clipboard`, `AtomicFile`, `std::filesystem`, or `std::ifstream` from shared
Game sources, and do not use entropy sources (`std::random_device`); the
sandbox grants bounded clocks only.

Natively, `RuleCatalogLoader` owns catalog file reads, first-valid base-pair
lookup across executable/current/product directories, and the
working-directory `families.user.json` and `rulesets.user.json` overlays. In
the package, `CSimCatalogBootstrap` reads the packaged pair and storage
overlays. Both stage overlays through the portable `RuleCatalogOverlay`.
`RuleSetRegistry` starts empty and owns text validation and data-backed
factories; Rulesets has no filesystem or native platform dependencies.

`SimulationRunner` has a native worker-thread implementation and a guest
implementation (`Wasm/SimulationRunnerGuest.cpp`, `ILLUMO_SERIAL_GUEST`) that
runs the shared generation body serially or fans it out to simulation lanes
through `CSimPlatform::simulationLanes()` (`Wasm/SimulationLanes.*`). Keep
publication and mirror-delta semantics identical across both. A drain
consumes the outstanding generation when `canBlock()` is true and retires it
(discarding at most one generation) when lanes make it false; never wait for
a lane inside one frame. Lanes must produce exactly the serial generation:
change partitioning, halos or `SparseCellGrid::applyChunkPatches` only with
`IllumoGame.Wasm.LaneParity` green.

## Domain and presentation invariants

- `SparseCellGrid` is production state: signed 64-bit world coordinates,
  sparse non-background 16x16 chunks, infinite non-toroidal or finite
  toroidal evolution, and a revision that changes only when contents change.
- Finite topology is configured in whole chunks. Positive width and height
  wrap both axes; `0 x 0` selects the infinite canvas. Mixed zero/positive
  dimensions are invalid, and topology changes start a fresh world.
- Preserve deterministic transitions across direct, candidate, halo, frontier,
  serial, and worker-pool paths. Counting-dense target centers skip
  neighbor-count scratch preparation and evaluate as halo.
  Dense-majority and large frontiers skip candidate scratch entirely. A failed
  advance must not partially publish a generation. Changed-chunk tracking
  retains at most 16,384 addresses; the frontier versus complete work
  comparison still selects evaluation, and only tracking overflow or
  allocation failure invalidates the journal. Journals of at least 2,048
  presentation chunks capture a lightweight replacement marker instead of
  per-chunk payloads.
- Full-state, extended-range, and directional von Neumann models use isolated
  serial correctness kernels and the same transactional inactive-map
  publication. Directional neighbors are ordered north, east, south, west.
- Worker pools are grid-owned implementation details. Bound work, join before
  destruction, and do not expose partially written state to the frame thread.
- `CanvasView` is a bounded world-space view over the sparse domain. It owns a
  reusable RGB texture and quad; visible sampling and overview capping are
  presentation limits, never simulation-domain limits. Aligned cache-origin
  shifts copy retained CPU texels and resample only newly exposed strips. LOD,
  resize, palette, revision gap, torus wrap, and non-aligned jumps still refill.
  Exact-cell LOD fades changing texels; density overviews snap. Dense visible
  revisions that dirty a quarter of the cache resample the complete cache.
  Changed-bin marking stops once that threshold is reached. Overview resamples
  walk occupied cells and snap-convert the sampled RGB in one pass.
- A finite torus presents only its centered canonical rectangle. Cells outside
  that rectangle remain background-colored; wrapping is a simulation-domain
  rule and must not tile the finite world across the camera view.
- Keep simulation state independent from fading, camera, texture dimensions,
  and upload strategy. Game code emits rendering tokens and never calls GL.
- Legacy `CellGrid` and `Canvas` exist for compatibility tests. Do not revive
  them as a parallel production runtime without explicit migration approval.

## Ownership, input, and errors

- `CellContext` owns its game objects. Make copy/move behavior explicit and
  keep module callback registrations within the context/module lifetime.
- Editing, camera mutation, view sampling, and module callbacks are main-thread
  affine. Validate coordinate conversions before narrowing floating-point or
  arithmetic results to signed world coordinates.
- Treat allocation, worker, parse, and I/O failure as observable failure.
  Never consume simulation debt or report a successful command after an
  unpublished generation or failed save/load.
- Keep console commands domain-owned and register them through
  `CommandRegistry`; usage, descriptions, validation, and completion data move
  with the command.
- Product input (menu, settings, confirm dialogs, camera, editor) yields while
  `CommandLine` is open. Do not drain `KeyCode::Grave`; `DebugModule` owns the
  global console toggle.
- F1 settings and F2 Ruleset Workshop remain separate. F1 and New Simulation
  select an explicit family/ruleset pair; each ruleset is bound to exactly one
  family. The family owns state count, names, and colors; the rule owns
  transition behavior. F2 stages these definitions separately, validates the
  pair, drains the simulation, persists a custom family before its referencing
  rule, then replaces the active pair and refreshes presentation. Reject family
  schema changes that invalidate live cells. Both menus honor
  `reducedUiMotion`; F2 wheel input scrolls visible rows without moving
  keyboard selection.
- Menu screens take their motion, fitted virtual space, row windows, and
  pointer edges from `Illumo/Gui/GuiMenuShell` (`GuiEasing`, `GuiSpring`,
  `GuiSpringArray`, `GuiMenuAnimator`, `GuiPanelLayout`, `GuiPointerTracker`)
  and their chrome from `GuiKit`'s glass helpers. Do not restate easing curves,
  spring integration, animation timings, UI-scale fitting, scroll clamping, or
  press-edge bookkeeping in a screen; add a new one by composing the shell and
  supplying only that screen's rows, layout constants, and drawing. Feedback
  animation never delays an action, and animated offsets never move a hit
  area: rows lean in horizontally only and lift at most a couple of pixels.
- The title screen's ambient world is presentation only. It must never write
  the player's ruleset preference (restore `FamilyString`, `RuleSetString` and
  `ModeString` around its `CellContext`), and it must refresh its canvas
  targets after each advance (`rebuildTargetsFromGrid`).
- Editor patterns (RLE/plaintext/stamps/clipboard) are a side path. World saves
  stay sparse; version 4 records family and ruleset IDs, version 3 derives family
  from its rule ID, and version 2/dense legacy readers remain compatible. Finite
  worlds skip out-of-bounds stamp cells.

## Persistence and compatibility

Writes use sparse format version 4; reads accept versions 4, 3, and 2 plus the
prior dense format. Validate headers, topology, dimensions, the family/ruleset
pair, coordinates, counts, and family-defined cell states before replacing
live state. Loading must be transactional. Format, endianness, numeric-range,
or replacement-policy changes require an explicit compatibility plan and tests
with fixtures. Custom families and rules remain external catalog entries
referenced by stable IDs; their catalogs must load before a world that uses
them can be restored.

## Documentation and verification

- `docs/packages/game.md`
- `docs/latex/sections/07-game-and-rules.tex`
- `docs/architecture-consensus.md`
- Domain, boundary, simulation, canvas, and CellGameModule exact tests
- `IllumoGame.Sim.MicroBench` and `IllumoGame.Sim.SparseMicroBench` for measured
  performance claims

Add regression tests for changed evolution, persistence, camera/edit, and
presentation behavior. Benchmark only representative Release workloads and
record machine, toolchain, inputs, and limitations. Update this file only for
durable Game contracts.
