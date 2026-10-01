# Hot-loop allocation removal (execution plan)

Tier 2 performance plan (`.agent/PLANS.md`). Status: complete (2026-09-25).

## Objective and end state

Warmed steady-state frames and simulation generations perform no avoidable
heap allocation on the paths below, and focused tests fail if they regress.
Success: allocation-count gates pass for a warmed package frame exchange and
a warmed lane generation; `--bench-frames` / `IllumoGame.Wasm.PackageBench`
show no regression and ideally lower `wasmMs` exchange times.

## Evidence (2026-09-25 audit)

The core loops are already retained and allocation-free: `SparseCellGrid`
stepping (D-P9 through D-P29), `CommandQueue`, GL submit, `MeshVisual` and
`GameVisual` draw loops, `SceneGraph` snapshots and BVH
(`Illumo/Tests/TestSceneAllocations.cpp`). Tracy memory tracking already
exists (`Illumo/Source/Services/DebugAlloc.cpp`).

What remains follows four patterns:

1. Retained buffers are moved out (`take()`, `std::move(m_frame)`,
   `x = std::move(y)`), discarding capacity every frame. Worst on the WASM
   frame exchange: the frame payload exists as about four guest copies and
   three host copies, each freshly allocated.
2. UI text and fields rebuilt from strings every frame even when unchanged.
3. `const std::string&` lookups called with literals (`getVar`,
   `isActionActive`), which allocate on wasm32 past about 10 characters.
4. Queues rebuilt every frame (input queues, command-name sync).

Also found: `Renderer::frameArena` (8 KiB `ArenaAlloc`) returns `nullptr`
for the immediate-draw list above 1,024 drawables.

## Decision: no central memory manager

A process-wide manager owning every allocator was considered and rejected.
The remaining allocations come from discarded capacity and redundant
rebuilds; routing them through handles would keep the same churn. Each WASM
guest owns its own linear memory under a manifest budget, so the host cannot
own guest memory. `IllumoContext` stays a non-owning pointer bag and
ownership stays local, as the architecture requires.

## Scope and non-goals

In scope, in order:

1. WASM frame exchange: retained host request/reply buffers and one reused
   guest transfer allocation; retained guest payload/packet writers; a guest
   frame recorded in place and swapped instead of moved; host decode and
   accept into retained scratch swapped with the live frame.
2. Simulation lanes and delta application: retained patch, halo and reply
   scratch; recycled nodes in `applyDeltaToMap` / `synchronizeInactiveMap`.
3. UI rebuild only on change (IllEd inspector, CSim hints and inspector,
   console ghost/hints); `GameVisual::addText` moves instead of copying.
4. `std::string_view` lookups for `getVar` and `isActionActive`.
5. `frameArena` sizing bug.
6. Allocation gates for a warmed package frame and a warmed lane generation.

Non-goals: wire-format or ABI changes, new exports, allocator replacement
inside guests, generic allocator frameworks, and debug-only overlays.

## Constraints

- No frame schema, envelope, capability or import change. The host may call
  `illumo_guest_alloc` less often; the export contract is unchanged.
- Decoding stays transactional: the live frame and renderer state change only
  after complete validation. Scratch contents after a failure are
  unspecified and never observed.
- Resource lifetimes stay the same: swapped-out scratch must release its
  texture and mesh references at the point the old code destroyed them.
- Retained scratch must not grow without bound; it is bounded by the
  existing frame limits.

## Verification

- Release build; `ctest -L IllumoWorkspace`.
- `IllumoGame.Wasm.PackageBench` and `--bench-frames` before and after.
- New allocation-count gates (milestone 6).
- clang-format on touched files.

## Validation log

- Baseline: `build-workspace` Release package tests were stale (built
  before the settings redesign changed `Display.h`); rebuilt before
  measuring. Clean baseline: 648/648 workspace tests pass.
- Milestone 1 (WASM frame exchange) done. Guest: retained payload and
  response writers and recorded frame in `Exports.cpp`;
  `GuestApplication::recordFrame` records in place;
  `GuestRecordingBackend::takeFrame(GuestFrame&)` swaps and recycles
  texture-pixel and mesh-write buffers through a bounded spare pool;
  `GuestServiceQueue` keeps its writer. Host: `WasmGuest` keeps its request
  writer, reply buffer and one guest transfer buffer (at most 1 MiB);
  `GuestFrame::decode` decodes in place, reusing batches, texture writes and
  mesh writes; `WasmFrameRenderer::accept` swaps retained scratch with the
  live frame and releases resource references at the same point as before;
  the service exchanges keep their writers.
- New gate `IllumoGame.Wasm.PackageFrameAllocations` (IllumoWorkspace),
  host allocations over 120 warmed frames of `bench-sparse64`:
  HEAD 10,560 paused / 10,807 running (about 88 per frame); after
  milestone 1, 0 paused / 3 running. The running residue is high-water
  growth of texture-write buffers for larger dirty rectangles.
- `PackageBench` (single runs, noisy): guest frame exchange p50 fell from
  0.090-0.153 ms to 0.077-0.128 ms and accept p50 from 0.010-0.018 ms to
  0.007-0.016 ms. Frames are simulation-bound, so FPS changes are within
  run-to-run noise without lanes and 6-19% higher with 8 lanes.
- Milestone 2 (lanes and delta application) done. `SparseCellGrid`: delta
  application, inactive-map catch-up, full-map copies and elementary-row
  removal recycle chunk nodes through the existing pool; `applyChunkPatches`
  keeps a retained delta and indexes it with the retained patch address set
  instead of a per-call `unordered_map`. Lane worker: retained request,
  reply, writer and patch scratch; sorted retained address lists replace the
  per-generation `unordered_set` and halo `unordered_map`; after a
  non-elementary advance it captures the generation delta and calls
  `rememberInactiveGenerationDelta`, exactly as `SimulationRunner` does, so the
  next patch catches the inactive map up from the journal instead of copying
  every chunk. Coordinator: retained Advance request, writer, poll buffer
  and reply (swapped into the lane), merged-change drain and result delta
  (swapped with the caller's); a slot-retaining request FIFO replaces the
  per-lane `std::deque` (MSVC allocates one block per element).
  `GuestSimulationLanes::submit` writes the lane prefix in place and moves
  the request into the service record through the new
  `GuestServiceQueue::tryEnqueue`, which leaves a refused payload untouched.
  `WasmWorker` keeps its request buffer across jobs and one guest transfer
  buffer (at most 1 MiB).
- New gate `IllumoGame.Wasm.LaneAllocations` (IllumoWorkspace), 4 loopback
  lanes on a 12x12-chunk torus, allocations on the control thread over 60
  warmed generations: HEAD 40,009 (about 667 per generation); after
  milestone 2, 6. `LaneParity`, `LaneProtocol` and `GamePackageLanes` pass.
- Milestone 3 (UI rebuilds) done. `GameVisual::clearPrimitives` keeps up to
  256 cleared text strings and `addText` builds in place from them (it used
  to copy each string twice); geometry rebuilds sort a retained item list
  with `std::sort` (the (draw order, unique sequence) key is a strict total
  order, so the result equals the former `stable_sort` without its
  temporary buffer). CSim's edit-hint footer is laid out only when its
  inputs change (window size, UI scale, selection, buffer, ruleset
  revision, default font); `CellContext::getRuleSetRevision` is new. IllEd's
  inspector rebuilds its fields only when the document's scene generation
  or revision, or the selection, changes (`EditorDocument::sceneGeneration`
  is new). The console already recomposes only when dirty (a few times a
  second while open) and is left as a follow-up; the CSim inspector's values
  change every generation while running, so it relies on the `GameVisual`
  string reuse instead of a cache.
- Milestone 4 (string lookups) done. `IEnvVars::getVar` takes
  `std::string_view` and the settings map (`EnvVarMap`) hashes
  transparently; `InputManager::isActionActive` takes `std::string_view`
  over a transparent `InputContext::ActionMap`. Literal lookups no longer
  build temporary strings.
- Milestone 5 done. `Renderer::frameArena` (fixed 8 KiB `ArenaAlloc`, which
  returned `nullptr` for the immediate-draw list above 1,024 drawables) is
  replaced by a retained `std::vector` with the same per-scene cap.
- Milestone 6 done: gates `IllumoGame.Wasm.PackageFrameAllocations`,
  `IllumoGame.Wasm.LaneAllocations` and `Illumo.GameVisual.SteadyTextAllocations`
  (0 over 100 rebuilds of 32 labels).
- Final verification (clean Release rebuild after an out-of-memory build
  left stale objects): 651/651 IllumoWorkspace tests pass; no compiler
  warnings in changed files. `python build.py tidy` checked every native
  first-party translation unit and reported only three pre-existing
  diagnostics in untouched files (`SparseWorkerPool.h` duplicate include,
  `WinSystemInfo.cpp` cast through `void*`, `TestRuleSets.cpp` integer
  division); guest-only sources such as `IllumoGuest/Source/Exports.cpp`
  are not in the native tidy tree. `PackageBench` with 8 lanes, baseline
  to final FPS: 535 to 566, 184 to 212, 700 to 759, 1223 to 1408, 1798 to
  2075; serial runs unchanged within noise.
- Benchmarks on this machine vary by about 20% between identical runs
  (other applications use significant CPU) and the build intermittently
  fails to create compiler processes; wall-clock deltas under that noise
  are not claimed. Allocation counts from the gates are deterministic.
- Observed, not changed: `Illumo.Wasm.Bench.PanelSurface` reports
  `frameBytes` 92160 on HEAD source; earlier runs showed 74112. The bench
  reads and writes `envvars.json` in its working directory, so its layout
  depends on that file.
