# Game subsystem guidance

This file specializes the repository `AGENTS.md` for `IllumoGame/Source/Game/`.

## Scope and boundaries

Game owns cellular-automata state, editing, camera-facing presentation,
simulation timing, ruleset selection, and save/load orchestration. It may use
Rulesets, Services interfaces, Foundation values, and backend-neutral Rendering
types. It must not depend on OpenGL or native platform APIs.

IllumoGame's only application seam is a declarative engine-facing definition:
CA defaults, CA-specific CLI metadata, and the required `CellGameModule`
factory. Do not add a process entry point, frame loop, logger lifetime,
SysCmdLine implementation, BuildInfo, native SDK code, or platform
implementation under IllumoGame.

## Domain and presentation invariants

- `SparseCellGrid` is production state: signed 64-bit world coordinates,
  sparse non-background 16x16 chunks, infinite non-toroidal or finite
  toroidal evolution, and a revision that changes only when contents change.
- Finite topology is configured in whole chunks. Positive width and height
  wrap both axes; `0 x 0` selects the infinite canvas. Mixed zero/positive
  dimensions are invalid, and topology changes start a fresh world.
- Preserve deterministic transitions across direct, candidate, halo, frontier,
  serial, and worker-pool paths. A failed advance must not partially publish a
  generation.
- Worker pools are grid-owned implementation details. Bound work, join before
  destruction, and do not expose partially written state to the frame thread.
- `CanvasView` is a bounded world-space view over the sparse domain. It owns a
  reusable RGB texture and quad; visible sampling and overview capping are
  presentation limits, never simulation-domain limits.
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
- Editor patterns (RLE/plaintext/stamps/clipboard) are a side path. World saves
  stay sparse version 3. Finite worlds skip out-of-bounds stamp cells.

## Persistence and compatibility

Writes use sparse format version 3; reads accept versions 3 and 2 plus the prior
dense format. Validate headers, topology, dimensions, rulesets, coordinates,
counts, and cell states before replacing live state. Loading must be
transactional. Format,
endianness, numeric-range, or replacement-policy changes require an explicit
compatibility plan and tests with fixtures.

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
