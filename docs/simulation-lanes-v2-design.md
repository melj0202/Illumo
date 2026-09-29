# Simulation lanes v2: design proposal

Status: **complete 2026-09-28** (Tier 3, `.agent/PLANS.md`); decision D-E34.
Supersedes the lane scheduling parts of D-E17 and D-E29.

## 1. Problem

Tracy captures (2026-09-28, `docs/tracy-profiling.md`) of the IllumoGame
package on a dense random world (about 260 chunks, 8 lanes, `tps 1000`,
vsync 60) measured:

| Measure | Value |
|---|---|
| Achieved TPS | 30 (requested 1000) |
| Lane work per generation, all lanes | 1.8 ms (slowest lane 0.7-0.9 ms) |
| Control-store merge | 0.36 ms |
| Lane round trip | 33 ms = two frames |
| Frames that dropped overdue steps | 92% |
| Lane load | two lanes about 0.9 ms, four lanes about 14 us |

Four structural causes, all in the current design:

1. **One-frame host lag.** `WasmProgram::update` exchanges services with the
   guest and only then calls `WasmGameServices::process`, which harvests
   finished lane replies into completions delivered at the *next* exchange.
   A waiting guest queues nothing, so no `ServicesPending` pass runs and the
   replies wait a frame. Every lane generation costs two frames.
2. **One generation per round trip.** A lane holds a one-chunk-line halo,
   exact for exactly one step, so every generation needs the control store
   to merge all lanes and relay halos. With the guest seeing replies once per
   frame, lanes can never exceed one generation per frame (60 TPS at vsync),
   and serial mode is also limited to one generation per frame
   (`CanvasScene::Normal`, one `start` per frame, overdue steps dropped).
3. **Interleaved fixed bands.** Chunk rows are dealt round-robin to lanes in
   bands of 8 rows (128 cells). Worlds shorter than about 64 chunk rows leave
   most lanes idle; a world 14 rows tall uses two of eight lanes.
4. **Policy.** Serial to lanes at a serial median above 1 ms, back below
   0.5 ms of summed lane work, measured per generation rather than by
   throughput. It keeps lanes where serial would be faster, and
   `laneWorkMetric` is never reset, so re-entry can flap. D-E17 and the
   performance plan still say 4 ms.

Headless `PackageBench` hid causes 1 and 2 because it runs frames back to
back at about 800 FPS, where two frames are about 2.5 ms.

## 2. Goals and measurable end state

On the Tracy capture scenario above and the `PackageBench` worlds (dense32,
dense64, sparse128), Release, 8 lanes:

- vsync 60, `tps 1000`: achieved TPS at least **600** on the 260-chunk world
  and at least **10x** today's lane TPS on dense32 and dense64, limited by
  compute rather than frame rate;
- uncapped: lane TPS at least today's, and at least 60% of native production
  (the open D-E17 gate is 80%);
- lane utilisation: slowest lane at most 1.5x the mean on every bench world;
- frame work p95 no worse than today (generations stay off the frame);
- serial mode reaches more than one generation per frame on small worlds;
- bit-exact results: every published state equals the native serial
  reference (LaneParity, GamePackageLanes); no host allocation regression.

## 3. Non-goals

Shared-memory guest threads; native (non-WASM) kernels; GPU simulation;
changing rule semantics or save formats; lane-to-lane channels that bypass
the control store; Linux runtime support.

## 4. Proposed design

### 4.1 Same-frame completion delivery (host, product-agnostic)

`WasmGameServices` gains `harvest()`, which moves finished worker and lane
results into the pending completions. `WasmProgram::update` calls it before
the `Services` exchange, so a reply that finished while the previous frame
rendered is visible to this frame's update. No ABI or schema change; the
existing `ServicesPending` pass still starts jobs queued by the update in
the same frame. Alone this halves lane latency (30 to 60 TPS at vsync).

### 4.2 Multi-generation blocks (temporal blocking)

A lane advances **K generations per job** instead of one. Its halo is **H
chunk lines** deep on each side (H = 2 by default, 32 cells). Wrong data
enters from the halo's outer edge at the rule radius r per step, so owned
cells stay exact while `K * r <= 16 * H`. The coordinator chooses
`K = min(steps due, Kmax, floor(16 * H / r))`; Life-like rules allow 32
steps per block, radius-16 rules (Lenia) two.

- The lane snapshots its halo, runs `advance` K times over owned plus halo
  chunks, and replies with the owned chunks changed in any of the K steps
  (a union journal; `buildPatchDelta` drops ones that ended unchanged).
- The coordinator merges the replies once per block. The block publishes
  one `SparseGenerationDelta` covering K generations, and the generation
  counter advances by K.
- The next block's halo updates are the neighbours' net changes; the lane
  restores its block-start halo snapshot and applies them (the existing
  restore-then-patch mechanism, generalised).
- Speculation: when a block's replies arrive, block B+1 is queued before B
  is merged and published, so lanes compute while the control store merges
  and the frame renders. Steady state is one block per frame.
- Elementary 1D keeps K = 1 in the first version (its source-row handoff
  is per generation); it still gains from 4.1 and 4.3.
- CSL1 version 3: `SyncBegin` carries H and the partition; `Advance` carries
  K; replies report K. Version 2 is refused (lanes and control store ship in
  one package, so no bridge is needed).

Fixed-step accounting: `CanvasScene` asks for the whole steps due (bounded
by the block limit) instead of one and dropping the rest. Debt is still
dropped when lanes cannot keep up, and a drain still retires the in-flight
block without touching the published grid (up to K unpublished generations
are discarded, as one is today).

### 4.3 Contiguous, weighted partition

Each lane owns one contiguous run of chunk rows (columns for elementary
rules), chosen so every lane has about the same number of stored chunks;
the torus wraps as today. That minimises halo traffic (two halo runs per
lane instead of two per band) and balances small worlds. Boundaries are
recomputed at every resync and when measured imbalance persists (slowest
lane above 1.5x the mean for 30 blocks, at most once every 2 s); a
rebalance is a resync.

### 4.4 Cheaper control-store merge

Under lanes the control grid never advances, yet `applyGenerationDelta`
keeps its inactive map current and the spare grid is mirrored every
generation. Keep only the published map current under lanes and rebuild the
inactive map lazily on the switch back to serial. With blocks, the remaining
merge runs once per K generations.

### 4.5 Throughput-based policy

Estimate what each mode delivers per frame: serial runs up to N generations
inside the update within a budget (default 4 ms of the frame), and lanes run
one block per frame. Choose the mode with the higher estimated TPS toward
the requested rate, with hysteresis; reset both metrics on every switch.
Serial mode gains multi-generation frames as part of this. Fix the doc drift
in D-E17 and the performance plan.

## 5. Alternatives considered

- **Only fix the host lag (4.1).** Caps lanes at 60 TPS at vsync; not
  maximum performance, but kept as the first milestone.
- **Several generations in flight with one-step halos.** Each generation
  still needs every neighbour's result through the control store, so
  in-flight generations would serialise on the same round trip.
- **Host-relayed halo exchange between lanes.** Needs the host to understand
  CSL1 or a generic lane-to-lane channel (a new ABI surface, and it breaks
  the product-agnostic host rule), and still costs a hop per generation.
  Temporal blocking gets K generations per hop with no new host surface.
- **Smaller interleaved bands.** Balances load but multiplies halo traffic,
  and conflicts with deeper halos for blocking.
- **Parallel merge in lanes.** Useful later; blocking amortises the merge by
  K first.

## 6. Contracts and compatibility

- Guest ABI, frame schema and capabilities: unchanged. The host change is an
  ordering change inside `WasmProgram` and `WasmGameServices`.
- CSL1 goes to version 3 (package-internal); CSW1 is untouched.
- `SimulationRunner`: a start requests up to N generations; a completion
  reports how many it published. The native runner keeps N = 1 unless the
  policy work extends it.
- Save format, rule semantics, determinism: unchanged.
- Generation publication: at more than one generation per frame the view
  shows every K-th generation. Today those generations are dropped instead.

## 7. Ownership, threading, memory

Unchanged ownership: one store per lane on its own host thread, bytes-only
messages, control-store callbacks on the main thread, one block per lane in
flight. Lane memory grows with halo depth: each lane holds its run plus two
H-line halos. Halo traffic per block is the neighbours' net changes, so per
generation it drops by about K.

## 8. Milestones

| # | Milestone | Verification |
|---|---|---|
| M0 | Baseline: Tracy captures (small world, dense32, dense64, sparse128; vsync on and off) and `PackageBench` numbers recorded here | Captures and JSON archived in this document |
| M1 | Same-frame delivery (4.1) | New host test that a completed job is seen by the next update; Tracy round trip about 17 ms; `Illumo.Wasm.*` and package tests |
| M2 | Blocks (4.2), CSL1 v3, runner and `CanvasScene` step accounting | LaneParity for K in {1, 2, 5, 32} across rules, radii and topologies; GamePackageLanes; LaneProtocol v3; LaneAllocations |
| M3 | Contiguous weighted partition and rebalance (4.3) | LaneParity with rebalances mid-run; lane balance in captures |
| M4 | Control-store merge savings (4.4) | Merge zones in Tracy; parity after serial fallback |
| M5 | Policy and serial multi-generation frames (4.5); doc drift fixed | Policy unit tests (no flapping); captures |
| M6 | Docs: D-E34 decision, architecture consensus, LaTeX log, `IllumoGame/Source/Game/AGENTS.md`, `AGENTS.md` canvas truth | Release workspace, Debug ASan `Wasm` subset, clang-tidy |

Each milestone lands only with the Release workspace green and a Tracy
capture recorded in section 10.

## 9. Rollback and containment

M1 is independent and small. M2-M5 sit behind the coordinator: a lane
failure or overflow still falls back to serial, and K = 1 with H = 1
reproduces today's behaviour (a constant kept for bisection until M6).

## 10. Validation ledger

| Date | Milestone | Result |
|---|---|---|
| 2026-09-28 | Evidence | Tracy capture above; this proposal |
| 2026-09-28 | M0 | `PackageBench` baseline below (Release, headless, uncapped, epoch metering) |

M0 baseline, `IllumoGame.Wasm.PackageBench` (i7-10700K, Windows 11):

| World | Serial TPS | 8 lanes TPS (FPS) | Lane round trip p50 | Slowest lane p50 | Merge p50 |
|---|---|---|---|---|---|
| dense16 | 499 | 423 (1270) | 1.61 ms | 0.50 ms | 0.37 ms |
| dense32 | 155 | 207 (642) | 3.78 ms | 1.49 ms | 1.37 ms |
| dense64 | 48 | 64 (210) | 11.6 ms | 3.90 ms | 5.80 ms |
| sparse64 | 987 | 654 (1954) | 1.52 ms | 0.26 ms | 0.44 ms |
| sparse128 | 293 | 244 (759) | 3.83 ms | 0.63 ms | 1.96 ms |

Lanes lose to serial on three of five worlds uncapped, and on dense64 the
control-store merge (5.8 ms) exceeds the slowest lane. Live runtime at vsync
60 (Tracy, 260-chunk world): 30 TPS on lanes, round trip 33 ms.

M1 (same-frame delivery, `WasmGameServices::harvest`, test
`Illumo.Wasm.ServiceHarvest`): the same live capture reaches **60 TPS**
(p50 59.9) with a lane round trip of **15.7 ms** p50, one generation per
frame. Unrelated finding from the same capture: with a display above 60 Hz
the runtime paces to `fps 60` in software with `sleep_for(1 ms)`, which on
Windows can overshoot to a full timer tick, so about 8% of frames took 21-33
ms. Follow-up, outside this effort: a high-resolution waitable timer or
`timeBeginPeriod(1)` in `FramePacer`.

M2 + M3 (blocks, CSL1 v3, contiguous chunk-balanced runs, recut on persistent
imbalance) and M5 (throughput policy, budgeted serial multi-generation
starts), live runtime, vsync 60, `tps 1000`, Tracy captures:

| World | Mode | Achieved TPS p50 | Per round trip | Round trip p50 | Merge p50 | Slowest / mean lane | Frame update p50 |
|---|---|---|---|---|---|---|---|
| 187 chunks (zoom 0.25) | serial, 16 generations per start | 999 | - | - | - | - | 1.07 ms |
| 619 chunks (zoom 0.1) | 8 lanes, 16-generation blocks | 982 | 16 generations | 16.5 ms | 0.22 ms | 1.26 | 0.91 ms |

Before this effort the same scenarios ran at 30 TPS (two frames per lane
generation, 92% of frames dropping overdue steps). `LaneParity` passes 630
rule/topology/lane-count combinations with blocks up to the halo limit,
forced recuts, edits and retirements, and serial multi-generation starts;
`LaneAllocations` counts 6 heap allocations over 60 warmed blocks (gate 30).

Deviations: M3's partition and rebalance landed with M2, because CSL1 v3
changed the partition anyway. Rebalancing recuts at the next start after 30
consecutive blocks with the slowest lane above 1.5x the mean (and at least
0.5 ms of lane work), at most once per 2 s; recuts otherwise happen at every
resynchronization. The policy threshold compares demand (median requested
generations per start) times cost per generation with the 4 ms serial
budget rather than fixed per-generation costs.

Final `IllumoGame.Wasm.PackageBench` (same tree, machine and settings as
M0; headless, uncapped):

| World | Serial TPS (M0 -> now) | 8 lanes TPS (M0 -> now) | Round trip p50 | Slowest lane p50 | Merge p50 per block | Lanes vs native production |
|---|---|---|---|---|---|---|
| dense16 | 499 -> 617 | 423 -> 1955 | 8.2 ms | 7.6 ms | 0.70 ms | - |
| dense32 | 155 -> 152 | 207 -> 810 | 19.0 ms | 17.9 ms | 2.12 ms | 109% of 743 |
| dense64 | 48 -> 47 | 64 -> 233 | 20.4 ms | 19.4 ms | 2.23 ms | 73% of 321 |
| sparse64 | 987 -> 2986 | 654 -> 3561 | 4.4 ms | 3.8 ms | 0.81 ms | - |
| sparse128 | 293 -> 271 | 244 -> 1185 | 16.1 ms | 14.9 ms | 2.50 ms | 136% of 869 |

Uncapped serial frames now fill the 4 ms budget, so serial FPS falls while
serial TPS rises on small worlds. Lanes are bound by lane compute: the
merge costs about 2 ms per block of up to 32 generations (0.07 ms per
generation, from 5.8 ms per generation on dense64 at M0).

M4 (dropping the control grid's inactive-map maintenance under lanes) was
not implemented: with blocks the whole merge is 1-3% of a block's round trip,
so it could not move any goal, and it would have added a lazy-rebuild path
to the serial fallback. It remains a possible follow-up if lane compute
shrinks enough for the merge to matter again.

M6: Release workspace 693/693 (clang-tidy on every compiled first-party
source), new `Illumo.Wasm.ServiceHarvest`, `LaneParity`, `LaneProtocol`,
`LaneAllocations`, `GamePackageLanes`, `GamePackage`; documentation and
decision D-E34 updated. Not run: the Debug AddressSanitizer build and a
Linux build (no runtime there).

## Completion

Implemented: same-frame completion delivery (M1), multi-generation lane
blocks with CSL1 version 3 and contiguous chunk-balanced partitions with
recuts (M2, M3), the throughput policy with budgeted serial multi-generation
starts (M5) and documentation (M6).

Goals: vsync 60 `tps 1000` reaches 999 TPS (187-chunk world, serial) and 982
TPS (619-chunk world, lanes) against 30 before; uncapped lane TPS rose 3.6-5.4x
and reaches 73-136% of native production; the slowest lane runs 1.26x the
mean on the live world; frame update p95 fell from 2.0 to 1.8 ms; results
stay bit-exact. The 10x-at-vsync goal for dense32/dense64 was not measured
live; only the random worlds above were captured at vsync.

Remaining risks and follow-ups:

- dense64 lanes reach 73% of native production; per-lane kernel speed, not
  scheduling, is now the limit.
- Above the frame rate intermediate generations are not drawn (owner
  decision 1).
- `FramePacer` sleeps with 1 ms granularity requests that Windows can round to
  a timer tick (M1 note above). Resolved 2026-09-28 by D-P34: the runtime
  already held `timeBeginPeriod(1)`, which Windows 11 ignores for occluded or
  minimized windows, so the pacer now waits on a high-resolution waitable
  timer.
- The native runner still runs one generation per start.

## 11. Owner decisions (2026-09-28)

1. Publish K generations at once; the view shows every K-th generation above
   the frame rate.
2. Default halo depth H = 2 chunk lines.
3. Serial multi-generation frames are in scope (M5).
4. All milestones M0-M6 as one effort, with a Tracy capture per milestone.
