# Scene graph v2 execution plan

Companion to `docs/scene-graph-v2-design.md`. That document holds the
architecture, evidence, and rationale; this one holds the ordered work, the
per-phase exit criteria, and what each phase owes the documentation tree.

Baseline: `release/v26.09` at `0bd6e38a`. Tag it before Phase 1.

**Guidance.** The root `AGENTS.md` scene paragraphs and
`Illumo/Source/Scene/AGENTS.md` today forbid subtree-bound caches, spatial
indices, update frameworks, multiple attachments, and non-main-thread scene
work. They describe v1's boundary and are the owner's to change. Amend both in
the Phase 1 merge — not for permission, but because the autonomous agents
working in this repository read them and will otherwise keep generating
v1-shaped work.

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

## Phase 7 — spatial index (gated)

Gate: culling exceeds 25% of scene-subsystem frame time at 50k bounded nodes in
the Phase 2 benchmark. D-E11's existing microbenchmark says it currently does
not, so this phase may never open — that is a legitimate outcome, not a
failure.

If it opens: binned-SAH BVH over `worldBounds[]` in compiled order, refit on
transform change, full rebuild on structural revision change or when refit
quality degrades past a surface-area threshold. Evaluate a loose uniform grid
against it with the same benchmark before committing.

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
                                       │                          │
                                       └──────► 7 BVH (gated)     └──► 6 parallel (gated)
                                                                       └──► 8 render thread
                                                                            (separate authorization)
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
- Whether `EditorDocument` remains authoritative for hierarchy after Phase 5,
  or becomes a thin undo/serialization layer over the graph. This is worth
  deciding deliberately, because "both are authoritative" is the state the
  current code is in and is the root cause of `rebuildGraph`.
