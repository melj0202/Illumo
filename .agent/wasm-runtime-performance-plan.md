# WASM Runtime Performance Execution Plan

## Objective and measurable end state

Recover the frame rate and simulation throughput lost when every Illumo
application moved into a WASM package hosted by `IllumoRuntime`.

End state (design §13 gates, `docs/wasm-game-runtime-design.md`):

- IllumoGame achieved TPS is at least 80% of the native production runner
  (threaded pools) on the frozen benchmark suite;
- p95 frame work regresses by no more than 25% against the same baseline;
- no lost input or publication and no unbounded wait;
- IllEd and IllMeshViewer steady-state frames re-upload no unchanged dynamic
  geometry;
- a Debug-configuration developer can play without the sanitizer profile's
  explicit guest bounds checks (`dev` profile or `ILLUMO_ENABLE_ASAN=OFF`).

## Current-state evidence

- Debug is a sanitizer profile: `/Od`, `/fsanitize=address`
  (`cmake/IllumoBuild.cmake`), and `WasmEngineConfig.h` disables Wasmtime
  signal-based traps under `_DEBUG`, so every guest load and store carries an
  explicit bounds check. Debug defines `TRACY_ENABLE` without on-demand mode.
- Fuel metering is enabled for every store (`WasmEngineConfig.h`) and reset
  per call; epoch interruption already bounds each call by its deadline.
- `ILLUMO_SERIAL_GUEST` links `SimulationRunnerSerial.cpp` and
  `SparseWorkerPoolSerial.cpp`; each generation runs inside
  `illumo_guest_update` on one core. Milestone 6 of
  `docs/wasm-game-cutover-plan.md` (worker-store offload) is open.
  `WasmWorker`, `CSimWorkerGuest.wasm` and the CSW1 protocol exist but are
  not staged or granted.
- Guest `GuestRecordingBackend` retains only static meshes of at least 64 KiB;
  `UpdateBuffer` permanently inlines a mesh. All UI/text/GameVisual geometry
  is re-extracted, serialized, decoded and re-uploaded every frame. Host inline
  slots are keyed by batch index and are replaced when layouts shift.
- Frame bytes cross roughly eight copies (byte-wise writer, envelope re-copy,
  host copy plus assign, element-wise decode, per-float repack).
- No Tracy zones or counters exist in `Illumo/Source/Wasm`.

## Scope and non-goals

In scope, in order: measurement (Phase 0), build/engine configuration
(Phase 1), per-manifest fuel metering (Phase 2), frame-path copy and retention
fixes including frame schema v4 (Phase 3), and N-lane worker-store simulation
offload with retire-on-drain (Phase 4).

Non-goals: compiled-artifact caching; shared-memory guest pthreads; moving
kernels native; new retained UI trees or render graphs; Linux runtime support.

## Constraints and invariants

- `IllumoRuntime` and `Illumo/Source/Wasm` stay product-agnostic.
- Frame schema, capability, manifest and import changes land with decoder and
  deny tests. Older frame versions keep decoding.
- Compiled artifacts and the host engine use identical settings; mismatches
  fail closed at deserialization.
- Mods remain fuel-metered. Runtime ceilings bound every manifest request.
- Simulation results remain deterministic and hash-identical to native.
- Save format v4 and v3/v2/legacy reads are unchanged.
- Token payload pointers stay valid until synchronous submission returns.

## Proposed design and alternatives

See the approved plan phases below. Alternatives considered: pending-operation
drains (rejected in favour of retiring the one in-flight generation, approved
by the user); single worker first (the user asked for N lanes from the start;
one lane is the degenerate case); keeping `_DEBUG` as the traps key (rejected:
the non-ASan compiler helper would disagree with an ASan host).

### Phase 0: measurement

Host Tracy zones and `WasmFrameStats` counters (`wasm_stats`), guest timings
(`guest_stats`), runtime `--bench-frames/--bench-warmup/--bench-script` JSON
mode, benchmark cases labelled `IllumoBenchmark`.

### Phase 1: engine options and a usable Debug

`ILLUMO_ENABLE_ASAN` option; `WasmEngineOptions {meterFuel, explicitBounds}`
passed to the out-of-process compiler; explicit bounds keyed on ASan presence;
`build.py` `dev` (RelWithDebInfo) and `debug-noasan` profiles.

### Phase 2: fuel optional per manifest

`WasmLimits::meterFuel`; extracted testable manifest decoder with
`"metering": "fuel"|"epoch"` and worker fields; first-party manifests become
epoch-only; mods stay metered.

### Phase 3: frame path

Retained exchange buffers and payload spans, bulk wire I/O, retained frame
capacity, raw-layout inline vertices, dynamic retained meshes with ranged
`meshWrites`, one streaming host mesh per layout, cached exports, cached
memory in imports, typed service processing, command-registry revisions.

### Phase 4: N-lane simulation offload

Job lanes in the host, same-frame job submission, worker interrupts,
`SparseCellGrid::advancePartition`, CSW v3 (multipart sync, lane advance,
changed-chunk replies, undo for retire), guest `SimulationRunnerGuest`,
retire-on-drain in `CellGameModule`, worker staging.

## Public contracts, compatibility, migration

- Compiler helper command line gains an engine-options argument (private).
- `WasmLimits` gains `meterFuel` (defaults to metered).
- Manifest gains `metering`, `workers`, `workerMemoryMiB`,
  `workerDeadlineMilliseconds`; absent fields keep current behaviour.
- Frame schema v4 (Phase 3), CSW protocol v3 (Phase 4): new versions, older
  frame versions still decode.
- Decision-log entries D-E15 (engine modes), D-E16 (frame v4), D-E17 (worker
  offload with retire-on-drain).

## Ownership, lifetime, threading

Each `WasmInstance` owns its engine, store and epoch ticker, so metered mods
and epoch-only packages coexist. Worker lanes own one instance each on a
host thread; only bounded bytes cross. Control-store callbacks remain
main-thread affine. Retiring a generation never touches the published grid.

## Milestones and validation strategy

Each phase: Release build, `ctest -L IllumoWorkspace`, exact new cases,
clang-format, targeted tidy, Debug ASan run of `Illumo.Wasm.*`, capture
verification when rendering changes, benchmark matrix re-run, ledger update.

## Rollback and containment

- Phase 1/2: revert manifests to `fuel` metering; `ILLUMO_ENABLE_ASAN`
  defaults ON.
- Phase 3: the host still accepts v1-v3 frames; the guest SDK can be pinned to
  v3 emission.
- Phase 4: without the Jobs grant or the staged worker the guest runner falls
  back to serial generations.

## Open questions

- Whether epoch-only packages need a lower deadline ceiling once generations
  leave the frame.
- Whether 4-bit chunk packing is needed to meet delta-volume budgets.

## Validation ledger

| Date | Phase | Result |
|---|---|---|
| 2026-09-22 | Plan | Approved; implementation started |
| 2026-09-22 | 0 | Instrumentation, `--bench-frames`, `IllumoGame.Sim.RunnerBench`, `IllumoGame.Wasm.PackageBench` build in Release |

### Baseline (before Phase 1)

Hardware: Intel i7-10700K (8C/16T), Windows 11 Pro 10.0.26200, MSVC
(Visual Studio 18 2026), Wasmtime 48.0.2, wasi-sdk 34.0, guest `-O3`.
Worlds from `IllumoGame/Tests/BenchWorlds.h` (Game of Life, seeded).
WASM package bench: headless MockBackend, `tps 1000`, 120 timed frames,
fixed dt 1/60, fuel metering on (2e9 per call).

| World | Native production gps | Native 1-worker gps | WASM Release TPS | WASM update p50/p95 ms | Guest update p50 ms | Frame+accept p50 ms |
|---|---|---|---|---|---|---|
| dense32 (1121 chunks) | 994 | 251 | 138 (13.9%) | 6.99 / 9.06 | 6.17 | 0.13 + 0.03 |
| dense64 (4285 chunks) | 502 | 166 | 47 (9.4%) | 20.30 / 28.52 | 17.76 | 0.08 + 0.02 |
| sparse128 (2199 chunks) | 1481 | 903 | 325 (22.0%) | 2.88 / 3.72 | 2.88 | 0.07 + 0.01 |

Observations: in Release the host frame exchange is about 0.1-0.2 ms, so
simulation inside `illumo_guest_update` dominates. Canvas uploads are about
61 KiB/frame.

Debug (ASan host, explicit guest bounds checks, fuel on): dense32 98 TPS,
dense64 36, sparse128 199; host frame accept 0.5 ms versus 0.03 ms in Release.

Corrected native baseline (`IllumoGame.Sim.RunnerBench` now drives the real
`SimulationRunner` publication cycle: two grids, mirror deltas, and
`advanceFrom` after full replacements):

| World | Native production gps | Native 1-worker gps |
|---|---|---|
| dense32 | 743 | 229 |
| dense64 | 321 | 58 |
| sparse128 | 869 | 619 |

Live runtime (`--bench-frames 300`, Release, vsync 60, idle screens):

| App / screen | Module update p50 ms | Frame bytes | Guest frame ms | Accept ms | Inline vertex bytes |
|---|---|---|---|---|---|
| game main menu | 2.75 | 417,996 | 1.86 | 0.57 | 230,592 |
| IllEd launch scene | 0.51 | 65,948 | 0.32 | 0.11 | 38,944 |
| mesh viewer | 0.35 | 40,216 | 0.19 | 0.07 | 26,944 |

### Phases 1-2 (engine options, ASan-keyed traps, epoch metering)

`Illumo.Wasm.EngineModes`, `Illumo.Wasm.Manifest`, `Illumo.Wasm.Epoch`
(now genuinely fuel-free) and `Illumo.Wasm.Fuel` pass; full Release build
with its 490-test workspace run passes. Package bench, Release:

| World | WASM epoch TPS | WASM fuel TPS | Epoch vs native 1-worker | Epoch vs native production |
|---|---|---|---|---|
| dense32 | 147 | 141 | 64% | 20% |
| dense64 | 49 | 47 | 84% | 15% |
| sparse128 | 371 | 356 | 60% | 43% |

Fuel metering costs only 3-7% here. The guest kernel runs within 1.2-1.7x of
native single-threaded; the dominant gap is lost parallelism and in-frame
execution (Phase 4).

### Phase 3 (frame schema v4, dynamic retained meshes, per-style slot pools)

Live runtime, same command as the baseline (`--bench-frames 300
--bench-warmup 120`, Release, vsync 60, idle screens), re-run after Phase 4:

| App / screen | Module update p50/p95 ms | Frame bytes | Guest frame ms | Accept ms | Inline vertex bytes | Mesh write bytes | Retained / batches |
|---|---|---|---|---|---|---|---|
| game main menu | 0.65 / 0.76 (was 2.75) | 228,592 (was 417,996) | 0.41 | 0.05 | 0 | 223,240 | 15 / 15 |
| IllEd launch scene | 0.24 / 0.26 (was 0.51) | 16,232 (was 65,948) | 0.14 | 0.02 | 0 | 6,208 | 63 / 63 |
| mesh viewer | 0.16 / 0.18 (was 0.35) | 3,260 (was 40,216) | 0.07 | 0.01 | 96 | 0 | 17 / 18 |

The menu still rewrites most of its geometry each frame because its content
animates; unchanged spans are no longer sent. Host slot replacements are
lifetime counts and stop growing in steady state.
`Illumo.Wasm.FrameValidation`, `RetainedResources`, `FrameRendering`,
`FrameFailures` and `SdkContract` cover v4 and dynamic meshes;
`tools/verify_capture.py` passes (7 invocations, real GPU).

### Phase 4 (N-lane simulation offload)

Headless `IllumoGame.Wasm.PackageBench`, Release, epoch metering, 2-second
timed windows after a lane warm-up, 8 lanes granted:

| World | Lanes TPS | Lanes FPS | Serial TPS (= FPS) | Native production gps | Lanes vs native |
|---|---|---|---|---|---|
| dense32 | 238-244 | ~780 | 177 | 743 | ~32% |
| dense64 | ~60 | ~205 | 48 | 321 | ~19% |
| sparse128 | 270-290 | 930-990 | 312 | 869 | ~32% |

Live GPU runtime with lanes (`--bench-script` loading the bench worlds):
vsync holds 60 FPS on dense32 (module update p50/p95 1.56/2.76 ms) and dense64
(1.97/9.78 ms); uncapped, dense32 reaches 657 FPS and dense64 221 FPS. Frame
rate no longer tracks generation cost.

The §13 throughput gate (80% of native) is **not met**. The control store's
merge of lane patches (`SparseCellGrid::buildPatchDelta` plus
`applyChunkPatches`) costs about 2 ms per generation in WASM and is serial;
lane compute scales. A fused single-pass `buildPatchDelta` did not move it
(about 0.57 us per chunk natively). The p95 frame-work gate is met: frames no
longer contain generations. sparse128 trades about 10% TPS for 3x frame rate
under lanes, because the adaptive policy keeps lanes while serial generations
exceed 4 ms of frame time.

`IllumoGame.Wasm.LaneParity` (198 rule/topology/lane/band combinations plus
worlds above the 2,048 and 16,384 chunk thresholds) and `LaneProtocol` pass;
`IllumoGame.Wasm.GamePackageLanes` runs the real package with 8 lanes for 62
generations, edits and saves, and its save matches the native reference.

| Date | Phase | Result |
|---|---|---|
| 2026-09-22 | 1-2 | Release workspace 490/490 |
| 2026-09-22 | 3 | Frame tests, SdkContract, capture verification pass |
| 2026-09-22 | 4 | Release workspace 493/493; lane parity and package lane tests pass |
| 2026-09-22 | All | Debug ASan `Wasm\|Runtime` subset: 45/45 pass; LaneParity takes about 390 s there (15 s in Release), so its timeout is now 600 s |

## Deviations from the plan

- Phase 3a/3b (texture-write spans, exchange buffer reuse, bulk wire I/O,
  retained frame capacity), raw-layout inline vertices, and 3d (cached export
  indices, cached memory in imports, typed service processing, command
  revisions) were not implemented. Phase 0 measured the whole host exchange
  at 0.1-0.2 ms per frame in Release, so none could move the gates. The
  per-layout streaming mesh became per-style slot pools, which achieved the
  same goal (no layout-shift replacements) with less change.
- `WasmInstance::interrupt()` was not added. Lane workers stop between jobs;
  a running job is bounded by the worker epoch deadline.
- Phase 4 does not use `advancePartition` over full replicas or CSW v3.
  Lanes hold interleaved row bands (8 chunk rows) plus a one-row halo and
  return owned chunk patches through a new CSL1 protocol that coexists with
  CSW1 in `CSimWorkerGuest.wasm`. This bounds each lane's memory and traffic
  by its region instead of the world, and needs no undo record: a retired
  generation is discarded and the next advance resynchronizes edits.
- Added beyond the plan: speculative pipelining of the next generation from
  lane halos, adaptive deferred or immediate merge (poll interval versus merge
  median), and adaptive switching between serial and lanes (above 4 ms serial
  cost on, below 1 ms lane work off).
- `JobLanes` is answered only after every lane worker has compiled, so the
  guest never submits into a loading worker.

## Completion

Implemented: Phases 0-2 as planned; Phase 3c dynamic retained meshes with
frame schema v4 and per-style host slot pools; Phase 4 as N-lane row-band
offload with retire-on-drain. Decisions D-E15, D-E16 and D-E17 are recorded in
`docs/architecture-consensus.md` and the LaTeX decision log.

Verified: Release build with clang-tidy; Release `IllumoWorkspace` 493/493;
real-GPU capture verification; live runtime benchmarks above; Debug ASan
`Wasm|Runtime` subset, all 45 cases (LaneParity run directly, about 390 s);
`tools/test_build.py` profile tests. `DashboardMouseTests.test_native_console_mouse_and_keyboard_smoke`
fails with WinError 6 with and without these changes (environmental).

Remaining risks:

- Throughput is bounded by the serial control-side merge; the §13 80% gate
  stays open.
- Startup compiles up to eight worker instances in parallel; lanes engage
  only after all are ready, and the game runs serially until then.
- Memory: each lane reserves up to `workerMemoryMiB` (512 MiB) of address
  space; actual use is its bands plus halos.
- Epoch-only packages still hang the window for up to the 10 s deadline on a
  runaway control-store call.

Follow-ups: move the merge's per-chunk mask and journal work into the lanes so
the control store only splices chunks; a compiled-artifact cache for faster
startup; revisit the control-store deadline now that generations can leave
the frame.
