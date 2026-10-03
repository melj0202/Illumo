# Performance sweep, tier 1 (execution plan)

Tier 2 performance plan (`.agent/PLANS.md`). Status: implemented
(2026-10-03); decisions D-P35, D-P36 and D-E40.

## Objective and end state

The 2026-10-03 read-only performance sweep ranked four items as the largest
remaining wins. This plan delivers them:

1. **Compiled-code cache.** A launch whose WASM modules were compiled before
   skips Cranelift entirely: the isolated compiler's artifact is kept on disk
   and reused. Success: a warm package launch is measurably faster than a cold
   one (`--bench-frames 1 --bench-warmup 1` wall time), and cold behaviour,
   limits and failure reporting are unchanged.
2. **One compile per lane module.** Simulation lanes that share a worker
   module compile it once per process instead of once per lane. Success: one
   compiler process per distinct module and engine options, whatever the lane
   count.
3. **Extended-range and weighted-kernel evaluation.** Larger-than-Life counts
   neighbours from row prefix sums instead of testing every tap, and Lenia
   accumulates taps over contiguous rows with an exact accumulator. Success:
   bit-identical results and a large per-generation speedup in
   `IllumoGame.Sim.*` measurements for those families.
4. **Finite torus worlds on the fast paths.** A torus stops bypassing the
   changed-chunk frontier, the dense/halo path and the worker evaluation.
   Success: bit-identical results against the previous toroidal path on
   randomized worlds of many sizes (including 1x1 and 1xN chunk tori), a
   settled torus evaluates no chunks, and dense torus soups step about as fast
   as the same soup on the infinite canvas.

## Evidence

- `WasmInstance::load` always runs `compileWasmIsolated`
  (`Illumo/Source/Wasm/WasmInstance.cpp`), which launches
  `IllumoWasmCompiler.exe` for a full Cranelift compile; nothing is cached.
  The game package compiles a 6.8 MB guest plus, per lane, a 4.0 MB worker.
  Baseline: `IllumoRuntime --bench-frames 1 --bench-warmup 1` of the game
  takes about 1.7 s wall time (four runs: 1680-1750 ms).
- `WasmGameServices::ensureLaneWorkers` creates up to eight `WasmWorker`s,
  each calling `WasmInstance::load` on the same bytes.
- `SparseCellGrid::evaluateIsolatedTarget` tests every extended-range offset
  per cell (120-440 taps for the shipped radii) and gathers every Lenia tap
  with a 64-bit multiply-add (about 530-700 taps per cell).
- `SparseCellGrid::advanceImpl` returns `advanceToroidal` before the frontier
  check; `advanceToroidal` enrolls candidates per neighbour contribution with
  a hash probe each and has no frontier, dense path or memo.

## Scope and non-goals

In scope: the four items above, their tests, and the documentation they
change. Not in scope: the other sweep findings (bit-parallel Life, lane
rebalancing, canvas ring buffer, IllEd scaling, backend work); sharing one
Wasmtime engine across lanes (artifact sharing captures the compile cost
without coupling store lifetimes). Overlapping the cold compile with the splash
was first listed here as a follow-up; it proved small (a compile thread plus a
deferred start) and is part of item 1 (see Deviations).

## Constraints

- Guests stay untrusted: a cached artifact is only reused for byte-identical
  modules (SHA-256 of the module) under identical engine options and Wasmtime
  version, and is integrity-checked before deserialization. Wasmtime itself
  also refuses artifacts built for another version or configuration.
- Cache failures never fail a launch: any read, verify or deserialize problem
  falls back to compiling, and a write problem only skips caching.
- No exceptions, no new third-party dependency (SHA-256 is implemented in
  Foundation), explicit ownership: callers pass the cache; native tests that
  pass none keep today's behaviour (including the compiler-limit tests).
- Ruleset results stay bit-identical on every platform and lane.

## Design

### 1-2. `WasmModuleCache`

A host-side object (`Illumo/Include/Illumo/Wasm/WasmModuleCache.h`) owned by
the runtime launch and passed to `WasmProgram`, which hands it to its guests,
mod and lane workers. `WasmInstance::load(bytes, cache)` asks it for an
artifact:

- In memory: key = SHA-256(module) + engine options mask. Concurrent lookups
  of the same key wait for the one compile in flight (lanes load on their own
  threads), so N lanes cost one compile.
- On disk: `<runtime>/cache/wasm/<key hex>.cwasm` beside the default storage
  root, with a header (magic, format version, options, Wasmtime version,
  module digest, artifact size, artifact digest). A mismatch or corruption is
  treated as a miss and the entry is rewritten. Files are written to a
  temporary name and renamed.
- Captures and benchmarks use the cache too; tests that need a cold compile
  pass no cache or a fresh directory.

Threat model: the cache directory is as trusted as the runtime directory a
user already executes from; anyone able to write there can replace the
runtime itself. The digest guards against corruption and stale files, not a
same-user attacker.

### 3. Extended-range and weighted kernels

- Extended range: when `getExtendedCountedState` is the same for every state
  (Larger-than-Life), the plan builds per-row spans of the shape (centre
  included) and each target window gets row prefix sums of
  `cell == countedState`; a cell's count is one subtraction per span, minus
  the centre when excluded. Shapes whose rows are not contiguous, and
  cell-dependent counted states (long-range cyclic, radius 2-3), keep the tap
  loop.
- Weighted kernel: taps loop outside, the 16 cells of each target row inside,
  over contiguous levels, so the compiler vectorizes it (wasm SIMD128 is
  enabled). The accumulator is 32-bit when the plan proves
  `sum(weights) * max(level)` fits, else 64-bit; both are exact.

### 4. Torus fast paths

Torus extents are whole chunks, so wrapping is a chunk-address
canonicalization. The infinite paths gain canonical neighbour lookups (halo
rows, candidate sources and targets, frontier expansion), and
`advanceToroidal` is kept only as a test oracle behind a testing override.
Small tori where a chunk neighbours itself are the main risk; a differential
test compares both paths on randomized worlds.

## Milestones

1. Plan (this file).
2. Item 3 with exactness tests; measure.
3. Item 4 with the differential test; measure.
4. Items 1-2: SHA-256, `WasmModuleCache`, wiring, tests; measure.
5. Full Release build, labelled CTest suite, tidy on touched files, docs and
   decision log.

## Verification

- Exact tests: extended-range and Lenia results equal the tap-loop oracle on
  random windows for every shape, radius and centre policy; torus results
  equal the oracle path over many generations and sizes.
- Existing suites: `IllumoGameTests`, lane parity
  (`IllumoGame.Wasm.GamePackageLanes`, `LaneParity`), allocation gates
  (`LaneAllocations`, `PackageFrameAllocations`), `Illumo.Wasm.*`.
- Cache tests: hit, miss, corrupted entry, options mismatch, concurrent
  lookups compile once.
- Measurements: launch wall time cold vs warm; `IllumoGame.Sim.*` benches for
  the kernel and torus changes.

## Validation log

All timings are native Release on the development workstation (32 GB,
Windows 11), not profiled under Tracy.

- **Item 3 (D-P36), 2026-10-03.** `IllumoGame.Rules.WorkerPoolParity`
  informational bench, 192x192 soup, one worker / eight workers, before ->
  after: Lenia Orbium 17.8 / 3.0 -> 5.8 / 1.1 ms per generation; Bosco
  3.4 / 0.7 -> 0.67 / 0.33; Bugsmovie (radius 10) 8.1 / 1.4 -> 0.66 / 0.30;
  QuadLife and HPP unchanged. New `IllumoGame.Rules.ExtendedKernelExactness`
  (every shipped extended-range rule plus 48 synthetic shape, radius and centre
  rules against the dense reference; narrow and wide Lenia accumulators against
  direct evaluation), `LeniaFamily`, `LongRangeCyclicAndTrails` and
  `WorkerPoolParity` pass.
- **Item 4 (D-P35), 2026-10-03.** New `IllumoGame.WorldTopology.SoupBench`:
  512x512 torus Life soup 6.8 ms per generation on the reference path ->
  0.60 ms on the halo paths (the same soup on the infinite canvas: 1.2 ms). New
  `IllumoGame.WorldTopology.HaloParity` (every shipped Moore-count rule, six
  torus sizes from 1x1 to 9x7, two densities, 16 generations with edits,
  serial and pooled evaluation, dual-grid stepping, a forced memo, and a
  settled torus that evaluates no chunks) passes. All 221 native
  `IllumoGame.*` CTest cases pass.
- **Items 1-2 (D-E40), 2026-10-03.** `--bench-frames 1 --bench-warmup 1`
  game launch wall time: 1680-1750 ms before; 1199-1220 ms with an empty cache
  (the eight lanes now share one worker compile); 733-801 ms warm. Stored
  artifacts: 9.0 MB (game), 2.4 MB (worker). New `Illumo.Foundation.Sha256`
  (FIPS 180-4 vectors), `Illumo.Wasm.ModuleCache` (four concurrent loads
  compile once, relaunch reads the entry and skips the compiler limits, options
  key separate entries, a damaged entry is rebuilt, an entry Wasmtime refuses
  is rebuilt, failures are not remembered, pruning keeps the limit) and the
  extended `Illumo.Wasm.RuntimeShellSplash` (the program compiles behind the
  splash and starts after it) pass, with `CompilerLimits`, `Worker`,
  `EngineModes` and `Compatibility`.

- **Milestone 5, 2026-10-03.** Full Release build (guest modules, packages
  and the design-book PDF rebuilt) with the labelled suite: 754/754 pass. The
  first run failed `Illumo.Build.NoExceptions`: an unmatched `[` in a new code
  comment made CMake's line splitting merge the rest of `SparseCellGrid.cpp`
  into one list element, overflowing the scanner's regex; the comment was
  reworded. `python build.py tidy` (clang-tidy 22, 323 files) reports nothing
  in the touched files; it still fails on diagnostics in untouched files
  (Vulkan and D3D12 backends, `EditorInspector.cpp`, `SimulationLanes.cpp`,
  `GpuBackendParity.cpp`, `TestGameVisual.cpp`, and the runtime `.rc` entry),
  left as they were.

## Deviations

- Item 1 also overlaps the cold compile with the splash: `RuntimeShell`
  begins the splash first, `WasmProgram::prepare` compiles the guest and worker
  modules into the cache on a thread, and the program starts between frames
  once its module is ready. The program's own start (which runs its first
  update) therefore happens during the splash rather than before it; it is
  still neither updated nor drawn until the splash ends.
- `WasmLimits` gained the cache pointer (`compiledCode`) instead of a separate
  `load` overload, so guests, mods and lane workers receive it through the
  limits they already copy.

## Remaining risks and follow-ups

- The cache keeps its artifacts in memory for the launch (about 11 MB for the
  game and its worker).
- The other sweep findings remain open: bit-parallel Life-like evaluation,
  Rule 90/184 history cost, lane rebalancing and edit resyncs, the canvas pan
  ring buffer, IllEd scaling at 10k nodes, the Vulkan acquire order and the
  explicit-backend upload paths.
