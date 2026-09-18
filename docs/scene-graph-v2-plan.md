# Scene graph v2 execution plan

## Execution record (2026-09-17)

The owner authorized implementation from both supplied documents in this task.
The starting tree is clean at the specified baseline `0bd6e38a`. The historical
proposal status in the design is superseded by that request. The owner subsequently
authorized committing and pushing the verified update to origin on a feature
branch. Phase 8 remains excluded; phases 6 and 7b retain their measurement
gates. Phase 7a is scheduled. This record is maintained by the lead implementer.

Execution starts with a v1 Release build, oracle/golden fixtures and baseline
measurements, followed by storage/bounds, public API/snapshot, editor migration,
query acceleration, measured gate decisions, and final verification. Existing
formats, token/backend interfaces, and product/engine boundaries stay intact.

Resolved source discrepancies:

- Shadow relevance depends on the Renderer light fit, which is available only
  after every drawable has collected casters. Extract once before collection;
  evaluate the fitted shadow predicate against snapshot values in the depth
  pass. Do not guess shadow relevance early or discard off-camera casters.
- The v1 matrix setter preserves arbitrary matrices; the design's claim that
  it already decomposes is incorrect. The authorized v2 TRS setter explicitly
  introduces the documented lossy matrix conversion. Test that limitation.
- Picking currently performs an exact transformed local-box test. Preserve it
  as a narrow-phase query after the graph's world-AABB broad phase so rotated
  objects and singular transforms retain editor behavior.

Implemented phases: 0, 1, 2, 3, 4, 5, and 7a. Conditional phases 6 and 7b
remain closed for the demonstrated editor workload; phase 8 is excluded.
Implementation, canonical documentation, durable guidance, and D-E12/D-R25
are synchronized in this update.

### Boundaries and acceptance

| Requirement | Implementation / evidence |
|---|---|
| E1/E2 | O(1) slot dirty mark and watermark; iterative compiled preorder resolves parent-first. 1k/10k/100k and depth 1/8/64 benchmarks. |
| E3/E9 | SceneGraphDrawable takes one immutable view per renderer frame; collection/depth/color consume it. Two ring slots, renderer lifetime identity, per-callback expiration validation, and direct-path containment switch. |
| E4 | Zero heap allocations over 100 warmed 2,048-node animated frames, including long-name lookup. Structural growth and first index construction may allocate. |
| E5/E6 | Ancestor-only world getters, revision-cached attachment boxes, dirty world-bound refresh, and backward subtree refit. Production MeshVisual skips duplicate scene camera bounds queries. Revision zero remains explicitly uncacheable. |
| E7 | Descriptor, primary TRS/batch setters, interned nonunique names, opaque uint64 payload, ordered multiple borrowed attachments, handle queries, ordered iteration, overflow-signaled change journal. |
| E8 | EditorDocument is the mutation gateway and runtime graph owner; recipes stay editor-owned. Visual and node identities survive recolor/extent/transform/reparent. Overflow resyncs bindings. Serialization exports graph order and preserves .ilsc v1. |
| E10 | Not activated: parallel scene stages remain gated. Generic WorkerPool passes zero/one/four-worker range and drain tests. No TSan evidence on this Windows toolchain. |
| E11 | Original construction/mutation emission golden is unchanged and passes both v1 and v2. |
| Oracle | 1,000,000 operations, seed 0x154917; independent identities/hierarchy/TRS model; exact float-bit world matrices, attachment bounds, effective order, compiled snapshot contents, and indexed/linear ray/overlap parity after every edit. |
| Failure recovery | One-shot allocation failure at each of 13 compiled-array and 3 BVH allocation sites preserves fallback results and permits recovery. |
| Lifetimes | Snapshot slot reuse, detach/destruction, external content invalidation, graph teardown, attempted mutation during extraction, and mutation during emission are covered. |

### Measurements

Windows x64, Intel Core i7-10700K at nominal 3.80 GHz, Visual Studio 18 / MSVC
19.51.36257, SDK 10.0.28000, Release /O2. Measurements are CPU wall time on a
shared desktop, not GPU timing or a frame-rate guarantee. The final stress
samples overlapped sanitizer/coverage runs and are reported as such.

The original tree was copied read-only with `git archive` to an ignored build
folder, and the same 2,000-cube `.ilsc` save/load benchmark was built there.
No project scene files are shipped in this checkout; the fixture is generated,
serialized and reloaded through the actual codec, not user-authored content.

| Workload | v1 | v2 |
|---|---:|---:|
| 2,000-cube recolor, 30 commands | 4,964 us | 2.06 us |
| Same document, 30 exact editor picks (including initial index build) | 5,820 us | 96-100 us |
| Root transform setter, 1k / 10k / 100k descendants | 7.28 / 45.47 / 1,801 us | 0.014 / 0.015 / 0.019 us |
| Full transform resolve, 100k nodes | 5.04 ms | 4.98 ms final quiet sample; 7.54 ms under concurrent validation |

The transform-setter improvement does not imply faster full TRS recomposition.
The depth 1/8/64 harness measured 1,000 reparent operations at 15-25 us and
100k-node recompilation at 7.35-9.00 ms under concurrent final validation.

| Nodes | First BVH query, including build | Warm indexed query | Warm linear query | Planar grid build | Grid +Z ray |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 647 us | 5.12 us | 19.37 us | 161 us | 0.025 us |
| 10,000 | 7.61 ms | 51.17 us | 220.35 us | 1.73 ms | 0.036 us |
| 100,000 | 105.29 ms | 718.41 us | 2,464.82 us | 12.33 ms | 0.045 us |

On the codec-loaded 2,000-cube document the grid built in 702 us and queried
in 0.25 us versus a 34.21 us warm graph query. The prototype inserts every
covered XY bucket and performs exact bounds hit tests; results match graph
queries. It only handles the benchmark's vertical rays and omits revision
polling, so these are not interchangeable production API timings. Uniform
planar content strongly favors a grid. The production BVH is retained for
arbitrary 3D rays, overlap queries, scale variation, and bounded index storage
without voxel duplication. It is isolated behind private SceneQueryIndex so
this choice can change independently. Cold 100k rebuild cost and O(n) revision
polling are explicit limitations, not hidden by the warm numbers.

At 100k nodes and 10/50/100 percent visibility, earlier extraction samples
were 3.85/4.46/5.56 ms static and 15.59/16.09/17.20 ms fully dynamic.
Transform-only stages were about 5.0 ms and bounds plus extraction about
10.5-12.0 ms. With concurrent sanitizer/coverage load, final samples rose to
5.03-8.08 ms static and 20.83-24.10 ms fully dynamic; bounds plus extraction
reached 18.03 ms. A final repeat after the heavy validations completed restored
static costs to 3.82/4.48/5.42 ms and fully dynamic costs to
15.71/16.21/17.02 ms; transforms were 5.02-5.05 ms and bounds plus extraction
10.64-12.00 ms. Snapshot items occupy 120 bytes, about 24 MB for two full
100k-item buffers, excluding graph/index storage. Shadow preservation means
low camera visibility does not reduce retained item count.

**Gate decisions.** Use 16.67 ms as a provisional 60 Hz CPU frame budget, not a
promised product target. The demonstrated editor fixture has 2,000 nodes;
a single-node drag leaves 99.95 percent of transforms static. Its recolor and
pick costs do not establish a serial scene stage over budget. The synthetic
100k fully dynamic stress case approaches/exceeds the whole budget and needs
an agreed realistic scene/workload before claiming 60 Hz. In the final quiet
100k sample, neither measured serial stage exceeds 16.67 ms; their combined
cost can consume the entire budget. Consequently phase 6 is not activated for
this milestone under the plan's individual-stage gate. Phase 7b's
parallel-cull gate has not opened either. High static fraction is recorded as
motivation to revisit it; no observed live user editing-session distribution
is claimed. These gates can be reopened without changing public graph,
snapshot, or query interfaces.

The 4,096-entry journal retained all 1,000 unconsumed drag records in the editor
fixture. A deliberate 4,200-record burst verifies overflow resynchronization
without replacing node or visual identities. Consumers normally advance their
cursor every edit/frame; an arbitrarily large import/delete can exceed the ring
and intentionally take the resync path.

### Verification record

- Baseline Release workspace: 432/432; original oracle/golden and capture
  fixtures passed before replacing storage.
- Final full Release build and explicit labeled CTest: 447/447 passed.
- Independent million-operation oracle and all new API/lifetime/failure cases
  pass in Release. The containment-switch fixture explicitly sets and restores
  its persisted value; repeated exact runs pass.
- Clang-tidy: all 27 changed translation units pass the repository check set
  (including native test tools), and the
  final containment fixture was rechecked after its configuration fix.
  Full configured compilation still encounters the three specified pre-existing
  `modernize-use-nullptr` errors in unchanged GLDevice.cpp (368/380/397).
  Coverage uses tidy-off after recording that blocker. Existing constructor
  order/unused declarations remain unchanged.
- Coverage: final Debug/Clang run passes 449/449 in 464.32 seconds and the
  repository's 85 percent gate reports 86.04 percent production lines. LLVM
  reports 317 functions with mismatched profile data when combining the four
  runners; the percentage carries that reporting limitation. No stronger
  function-level coverage claim is made.
- Debug MSVC AddressSanitizer: 64/64 focused correctness cases pass in 28.73
  seconds, including the worker pool, scene/mesh lifetime, editor changes, and
  Debug-only simulator scene adapter. The separate million-operation MSVC ASan
  stress run was interrupted after about 20 minutes, with no sanitizer report
  observed; it is not claimed complete. The full corpus passes Release and
  Debug/Clang coverage. Two non-gating 100k performance fixtures exceeded their
  120-second ASan limits; performance is measured in Release.
- Real OpenGL capture: all 11 verifier invocations pass. direct.png, scene.png,
  and occlusion.png are byte-identical to v1. Additional GPU checks pass with
  shadowed direct/snapshot images identical and shadows demonstrably changing
  pixels. Backend failure/lifecycle checks report zero failures.
- Native IllEd harness passes 64-level hierarchy movement, recolor, reparent,
  deletion, per-frame single extraction, stable identities, close/cancel,
  failed-save recovery, save, discard, and teardown on the real Windows/GL path.
- Documentation builds; updated scene chapter and both decision pages were
  rendered and visually checked. All modified C++/headers pass clang-format
  --dry-run --Werror; git diff --check and the final source/artifact review are
  clean. Generated PDFs/build outputs are not tracked changes.

**Limits:** native editor checks are scripted, not hands-on visual acceptance
of dragging/animation feel. TSan and non-Windows runtime validation were not
available. No claim of 100k fully dynamic 60 Hz, fully sublinear public picking,
or representative user-authored `.ilsc` workload is made.

---

Companion to `docs/scene-graph-v2-design.md`. That document holds the
architecture, evidence, and rationale; this one holds the ordered work, the
per-phase exit criteria, and what each phase owes the documentation tree.

Baseline: `release/v26.09` at `0bd6e38a`. The supplied phase/tag sequencing
below is retained as historical guidance. The owner's subsequent publication
request covers committing and pushing the completed update, without phase tags
or merges.

**Guidance.** At the baseline, root and Scene `AGENTS.md` described v1's
restricted storage and attachment boundary. Both now reflect the authorized
v2 contracts. Main-thread ownership and the exclusions for update frameworks,
ECS, scene serialization, and retained UI remain in force.

---

## Standing rules for every phase

- No recursion, no `auto`, no namespaces. Mozilla `clang-format` on every
  modified C++ file. `git diff --check` clean.
- Emission-order golden test passes unchanged. This is the parity gate and it
  never moves.
- Phase lands as one merge, with its documentation and guidance updates inside
  that merge.
- Record verification results in the design document's validation section as
  each phase completes, in the style of the D-E8 and D-E11 records.
- Pre-existing `modernize-use-nullptr` diagnostics in `GLDevice.cpp` stay
  pre-existing. Do not opportunistically fix unrelated code.
- Benchmarks are recorded with hardware, compiler, and build configuration.
  A phase does not land without its numbers, even when the numbers are
  non-gating.

---

## Phase 0 — foundations

**Goal:** the tools that make phases 1–6 verifiable, with no behaviour change.

1. `Illumo/Include/Illumo/Services/WorkerPool.h` +
   `Illumo/Source/Services/WorkerPool.cpp`. Fixed worker count, submit-range
   plus join, no allocation in steady state, explicit start/stop tied to the
   owner's lifetime.

   **Reimplement, do not move.** `SparseWorkerPool`'s entire public surface is
   three CA entry points typed on `SparseCellGrid` internals (`ChunkAddress`,
   `TargetResult`, `CandidateScratchChunk`, `CandidateWorkRange`), and it is
   `friend class SparseWorkerPool` inside `SparseCellGrid`
   (`SparseCellGrid.h:576`). Relocating it would pull a game-domain type into
   the library. Port the *mechanism* instead — `ensureWorkerCount`,
   `claimWorkerSlot`, the work-item counter, and the generation-stamped
   mutex/condvar handshake in `workerLoop` — behind a generic range interface.
   Read `SparseWorkerPool.cpp` closely while doing it; its failure modes are
   already solved there and are not obvious.

   **Do not refactor `SparseCellGrid` onto the new pool in this milestone.**
   Re-expressing `SparseWorkerPool` as a thin adapter over the library pool is
   the right end state — one tested threading primitive instead of two — but
   it touches the hottest code in the repository across seven construction
   sites in a 4,600-line file, to serve a scene-graph milestone that gains
   nothing from it. Schedule it as a follow-up once the library pool has run
   under the scene workload for a release. Record it as a known duplication in
   the meantime rather than leaving it undiscussed.
2. Scene benchmark harness registered as `Illumo.SceneGraph.Bench.*`,
   non-gating by default, parameterized on node count and depth.
3. Reference-oracle fixture in `Illumo/Tests/`: a naive graph (parent
   pointers, recompute-everything, no caches) plus a randomized script runner
   that drives both implementations and compares world transforms, world
   bounds, effective enabled/visible, and emission order after every step.
4. Emission-order golden test: fixed construction and mutation script, recorded
   attachment call sequence, byte-comparison.

**Exit:** oracle and golden test both reproduce current v1 behaviour exactly
on the existing corpus. `IllumoWorkspace` label green. Nothing in
`Illumo/Source/Scene/` changed.

**Owes:** `Illumo/Source/Services/AGENTS.md` gains the worker-pool contract.

---

## Phase 1 — storage and transform core

**Goal:** replace the internals. Public API unchanged, so no consumer edits.

1. Replace `Impl::NodeSlot` with parallel slot arrays (generation, flags,
   parent/firstChild/nextSibling/lastChild, `localTrs`, name id, user data,
   attachment range).
2. Intrusive sibling lists. Delete `Impl::eraseHandle` and its `std::find`.
   Verify `lastChildSlot` keeps append-at-tail ordering.
3. Compiled layer: `preorder[]`, `slotToIndex[]`, `parentIndex[]`,
   `subtreeSize[]`, `depth[]`, rebuilt when `structuralRevision` changes, at
   most once per frame, before the transform update.
4. Watermark dirty: `setLocalTransform` sets one bit and lowers
   `lowestDirtyIndex`. Delete `markSubtreeDirty` entirely.
5. Flat forward transform update; effective enabled/visible as a forward
   prefix. Delete the `ancestorsEnabled` / `ancestorsVisible` fields from the
   traversal state.
6. Retained scratch everywhere. Delete the per-call `std::vector` allocations
   in `destroyNode` and the old dirty path.

**Exit criteria:** E1 (O(1) local set), E2 (single forward pass), E4 (zero
steady-state allocation), E11 (emission order identical). Oracle fuzzer green
over at least 10^6 randomized operations. Full Release build and
`IllumoWorkspace` label green. Benchmarks recorded against the v1 baseline.

**Risk:** highest-subtlety phase. The compiled-index and watermark logic is
where this design can be quietly wrong; that is exactly what the oracle exists
for. Do not proceed on focused tests alone.

**Owes:** decision D-E12, `docs/architecture-consensus.md` scene section,
`docs/packages/scene.md`, `Illumo/Source/Scene/AGENTS.md`, the LaTeX chapter,
and the root `AGENTS.md` scene paragraphs.

---

## Phase 2 — bounds and hierarchical culling

1. `ISceneRenderAttachment::getSceneBoundsRevision()` with a default that
   forces re-query, so existing attachments are unaffected.
2. Per-node local bounds cache keyed on that revision.
3. Node world bounds in the forward pass; subtree world bounds in a single
   backward pass over the compiled array.
4. Hierarchical range culling: a missed subtree box advances `i +=
   subtreeSize[i]`.
5. Fix `getWorldBounds` to use the O(depth) ancestor walk instead of
   `updateWorldTransforms()`.

**Exit:** E5, E6. Culling results identical to v1 on the D-E11 corpus
(including the fail-open cases: unknown, invalid, non-finite bounds, and the
rule that a culled parent never prunes children). Benchmarks at 10%, 50%, and
100% visibility recorded — these become the Phase 7 gate inputs.

---

## Phase 3 — public API v2

The only phase that forces consumer edits. Land consumers in the same merge.

1. `SceneNodeDesc` and `createNode(const SceneNodeDesc&)`.
2. `Transform3D` as the authoritative local; `Matrix4` setter documented as
   decomposing. Batch `setLocalTransforms(handles, transforms, count)`.
3. Multi-attachment: `addAttachment` / `removeAttachment` /
   `getAttachmentCount` / `getAttachment`, chunked storage, insertion order
   preserved.
4. Interned names + `findByName`; `uint64_t` user data.
5. Queries: `raycast`, `queryBounds`, ordered iteration that exposes handles
   and values, never node addresses.
6. Change journal: bounded ring of `{kind, handle}` plus `structuralRevision`,
   with an explicit overflow signal meaning "resync fully".
7. Split `SceneGraph` from `DrawableBase`. Introduce `SceneGraphDrawable`.

**Consumer edits:** `IllEd/Source/EditorModule.*`,
`IllumoGame/Source/Game/CellGameModule.cpp` (`render3dTest`, three nodes),
`Illumo/Tests/TestSceneGraph.cpp`, `PublicHeaderSmoke.cpp`,
`TestMeshVisual.cpp`, `TestFrameCapture.cpp`, `IllEd/Tests/*`.

**Exit:** E7. Everything builds and passes with no behaviour change visible to
the user. Public-header smoke exercises every new entry point.

---

## Phase 4 — snapshot extraction (D-R25)

1. `SceneSnapshot` / `SceneRenderItem` types; a two-or-three buffer ring owned
   by the graph, reusing capacity.
2. Single extraction pass computing camera visibility and shadow relevance as
   flags in one go, instead of two separate culling evaluations.
3. `SceneGraphDrawable::CollectShadowCasters` / `AppendShadowCommands` /
   `AppendCommands` iterate the same snapshot with different flag masks.
4. Narrow the `renderTraversalActive` guard to the extraction call.
5. Attachment-detach semantics: detaching invalidates outstanding snapshots.

**Exit:** E3, E9. `FrameCapture` output byte-identical before and after for a
fixed scene. Manual IllEd smoke with shadows on. Containment switch
`sceneSnapshotExtraction` present and tested in both positions.

**Note:** this is the phase whose revert is most awkward, because it changes
the renderer seam. Keep the switch for one release, remove it in the next.

---

## Phase 5 — IllEd migration

The phase that pays for the whole plan on the consumer side.

1. Store the document node id (or its hash) in graph user data; delete
   `std::unordered_map<std::string, SceneNodeHandle> m_handles`.
2. Replace the 12 `rebuildGraph()` call sites with targeted mutations:
   transform edits → `setLocalTransform`; extent/colour edits → attachment
   reconfigure in place, not a `MeshVisual` rebuild; create/destroy →
   `createNode` / `destroySubtree`; reparent → `setParent`.
3. Keep one full-rebuild path for load and new-document only.
4. Delegate `EditorDocument::pickRay` to `SceneGraph::raycast`; delete the
   duplicate world-matrix composition in `EditorDocument::worldMatrix` in
   favour of `getWorldTransform`.
5. Consider whether `EditorDocument`'s `wouldCreateCycle` / `isDescendant` can
   delegate to the graph's own rejection, or must stay because the document is
   authoritative for undo. Decide explicitly; do not leave both.

**Exit:** E8 — no full rebuild during editing. IllEd tests green. Manual smoke:
drag a deep child, recolour, reparent, multi-delete, undo if present. Measure
edit latency on a large `.ilsc` before and after; that number is the headline
result of this plan.

---

## Phase 6 — parallel stages (gated)

Gate: Phase 1/2 benchmarks show a serial stage exceeding the frame budget at a
realistic node count. If they do not, skip and say so in the record.

1. Depth-band permutation (separate from the pre-order array, which is
   untouched).
2. Parallel transform update by band; parallel refit by reverse band; parallel
   cull into per-worker bitsets with serial compaction in compiled order.
3. Thresholds derived from measurement, stored as named constants with the
   measurement cited in a comment.

**Exit:** E10 — 1-worker and N-worker runs produce bit-identical world
matrices, bounds, and emission order across the full fuzz corpus. Measured win
at and above threshold; no regression below it. Sanitizer run (TSan if
available on this toolchain, otherwise document the gap honestly).

---

## Phase 7a — query acceleration (scheduled)

Not gated. Justified by interactive latency, not frame time: a single
`raycast` against 100k cached AABBs is order 2–3 ms of slab tests, incurred
per mouse-move on the editor's hover and pick path, which is exactly where the
editor either feels responsive or does not.

1. Binned-SAH BVH over `worldBounds[]` in compiled order, built lazily on
   first query and invalidated when the structural or transform revision
   changes. Picking runs against a quiescent scene, so build-once-per-change
   is both cheap and trivially correct.
2. Benchmark a loose uniform grid against it on real `.ilsc` content before
   committing. For uniformly distributed editor geometry the grid may win on
   build cost alone; do not assume the BVH.
3. `raycast` and `queryBounds` route through it; the linear path stays as the
   fallback when the index is stale or absent, so a failed build is a
   performance event, not a correctness one.

**Exit:** pick latency measured before and after at 1k / 10k / 100k. Ray-hit
results bit-identical to the linear path across the fuzz corpus — this is a
correctness gate, not just a speed one, because a wrong pick is worse than a
slow one.

---

## Phase 7b — culling tier (gated)

Gate: the Phase 6 parallel linear cull misses budget at the largest realistic
node count. If parallel SoA culling lands near 0.2 ms at 100k, this phase is
not worth its refit complexity and should be closed with the numbers recorded.

The reason it is gated separately from 7a: a BVH's traversal is sublinear but
its refit is linear, so for fully dynamic scenes it approximately breaks even
against a flat SIMD pass. It wins on the *static* fraction. If it opens, build
the two-tier scheme from design §5.6.2 — static nodes in the 7a structure,
dirty nodes in the flat parallel pass — with tier membership driven by the
dirty bits the graph already maintains, not by new tracking.

Measure the static fraction first (Phase 2 benchmark item). If a realistic
IllEd session turns out to be 95% static between frames, this phase is worth
much more than the frame-time gate alone suggests, and the gate should be
re-read in that light rather than applied mechanically.

---

## Phase 8 — recorded-payload snapshot (separately authorized)

Do not start without an accepted decision to move the OpenGL context off the
main thread. Without that move this phase delivers no benefit, and with it,
this phase is the smaller half of the work. Design sketch is in
`docs/scene-graph-v2-design.md` §7.3 (v2b).

---

## Sequencing summary

```text
0 foundations ──► 1 storage core ──► 2 bounds ──► 3 API v2 ──► 4 snapshot ──► 5 IllEd
                                                                  │             │
                                                                  │             └──► 7a query index
                                                                  │                    (scheduled)
                                                                  └──► 6 parallel (gated)
                                                                         ├──► 7b culling tier (gated)
                                                                         └──► 8 render thread
                                                                              (separate decision)
```

Phases 1 and 2 are the ones that deliver most of the measurable win with the
least risk. If the plan has to stop early, stop after Phase 2 with the API
untouched, or after Phase 5 with the consumer payoff banked. Stopping between
3 and 4 leaves a broken API with no benefit and is the one place not to pause.

---

## Decision-log entries this plan produces

| ID | Phase | Subject |
|---|---|---|
| D-E12 | 1 | Compiled scene graph storage, identity, queries, and mutation model |
| D-R25 | 4 | Renderer consumes an extracted scene snapshot |

Both IDs are unused at the baseline. Confirm again before writing, since other
branches also add decisions.

---

## Known unknowns to resolve during execution

- Watermark versus dirty-range list: the watermark is conservative and may
  scan a long tail after a single near-root edit. Phase 1 benchmarks decide
  whether the range list is needed; do not build it speculatively.
- Snapshot ring depth: two buffers or three. Two is enough while emission is
  main-thread; three only matters for Phase 8.
- Journal capacity (design doc §11 question 3) — measure in Phase 5.
- Static fraction between frames in a real editing session. Unmeasured today,
  and it is the single number that decides Phase 7b. Instrument it in Phase 2
  rather than arguing about it.
- Whether `SparseCellGrid` should adopt the library `WorkerPool` and delete
  `SparseWorkerPool`. Deliberately deferred out of this milestone; revisit
  once the library pool has a release of scene-workload evidence behind it.
- Whether `EditorDocument` remains authoritative for hierarchy after Phase 5,
  or becomes a thin undo/serialization layer over the graph. This is worth
  deciding deliberately, because "both are authoritative" is the state the
  current code is in and is the root cause of `rebuildGraph`.
