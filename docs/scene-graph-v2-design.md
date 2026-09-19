# Scene graph v2 design

**Status:** Owner-authorized implementation, 2026-09-17. Baseline is
`release/v26.09` at `0bd6e38a`. Phases 0-5 and 7a are implemented. Phases 6
and 7b retain their measurement gates; phase 8 remains separately authorized.
The implementation record in section 12 and the companion plan resolve source
assumptions below and report validation limits. The owner subsequently authorized
committing and pushing the verified update to origin on a feature branch.

**Supersedes:** `docs/scene-graph-v1-design.md` (D-E8, D-E11) where that
document fixes node storage, dirty propagation, extraction, attachment
multiplicity, and the main-thread extraction model. The v1 document remains
the record of why the persistent hierarchy exists and why it is separate from
the per-frame `Rendering::Scene` list; that separation is preserved.

**Accepted decisions:** D-E12 (scene graph v2 storage, identity, queries, and
snapshot extraction) and D-R25 (renderer consumes an extracted scene snapshot
instead of traversing the graph three times per frame). Both decisions are recorded in
`docs/latex/sections/09-design-decision-log.tex`.

**Guidance:** Root and subsystem guidance now describe the approved v2
boundary. Main-thread ownership and the exclusions for ECS, persistence, UI,
and update callbacks remain in force.

---

## 1. Objective and measurable end state

Replace the v1 node storage, dirty model, and extraction path with a compiled,
data-oriented scene graph that scales to six-figure node counts, gives
consumers stable identity and incremental mutation, and produces an immutable
per-frame snapshot as the single seam between scene state and rendering.

The end state is measurable when all of the following hold on the baseline
hardware and are recorded in the validation section:

| # | Condition | v1 baseline |
|---|---|---|
| E1 | `setLocalTransform` is O(1) regardless of subtree size | O(subtree), with one heap allocation per call |
| E2 | World transform resolution for `n` dirty nodes is one forward pass over a contiguous index array with no explicit stack | stack of handles into scattered slots |
| E3 | A frame performs exactly one graph traversal, not three | three (`CollectShadowCasters`, `AppendShadowCommands`, `AppendCommands`) |
| E4 | Per-frame steady-state heap allocation in the scene subsystem is zero | per-call `std::vector` in `markSubtreeDirty` and `destroyNode`; per-node child vectors on structural change |
| E5 | `getWorldBounds(node)` is O(depth) | calls whole-graph `updateWorldTransforms()` |
| E6 | Attachment local bounds are queried once per bounds revision, not once per node per pass | up to three virtual calls per node per frame |
| E7 | A node carries a stable name, arbitrary user data, and zero or more attachments | one borrowed attachment, no identity, no payload |
| E8 | IllEd performs no full graph rebuild during editing | `m_graph.clear()` + rebuild at 12 call sites, including drag and nudge paths |
| E9 | Extraction output is an immutable snapshot that the renderer consumes without touching graph storage | renderer calls into live graph state through virtual attachment callbacks |
| E10 | Parallel transform, bounds, and culling produce bit-identical results to the serial path | no parallel path |
| E11 | Emission order is byte-identical to v1 pre-order for an identical construction sequence | — |

E11 is the parity gate. Every phase must keep it, because deterministic
pre-order emission is a published contract in `docs/packages/scene.md` and in
the scene subsystem guidance.

---

## 2. V1 baseline evidence

All references are to `release/v26.09`.

### 2.1 Storage is array-of-structs with a heap allocation per node

`SceneGraph::Impl::NodeSlot` (`Illumo/Source/Scene/SceneGraph.cpp`) holds a
generation, liveness flag, parent handle (16 bytes), a
`std::vector<SceneNodeHandle> children` (24 bytes plus a heap block), a local
`Matrix4` (64 bytes), a cached world `Matrix4` (64 bytes), three flags, and an
attachment pointer. That is roughly 190 bytes per node plus one allocation for
every node that acquires a child. Traversal pushes handles onto a stack and
dereferences into `slots[handle.slot]`, so the access pattern is
handle-indirect and scattered even when the graph was built in order.

Sibling removal is `std::find` plus `vector::erase` (`Impl::eraseHandle`),
which is O(children) per detach and shifts the tail.

### 2.2 Dirty propagation walks the subtree on every edit

`SceneGraph::setLocalTransform` calls `Impl::markSubtreeDirty`, which
allocates a local `std::vector<SceneNodeHandle> pending` and walks the entire
subtree setting `transformDirty`. Animating a root with five thousand
descendants therefore costs five thousand writes and at least one heap
allocation per frame, before any transform is actually recomputed.
`SceneGraph::destroyNode` allocates two more local vectors per call.

### 2.3 Every frame traverses the graph three times

`Renderer::RenderScene` (`Illumo/Source/Rendering/Renderer.cpp:1129`, `:1143`,
`:1198`) calls `CollectShadowCasters`, `AppendShadowCommands`, and
`AppendCommands` on each World drawable. `SceneGraph` implements all three by
calling `Impl::visitRenderAttachments`, which each time re-walks the graph,
re-resolves dirty transforms, re-evaluates effective enabled and visible
state, calls the virtual `getSceneLocalBounds`, and re-runs the eight-corner
`AxisAlignedBounds3::transformed`. The culling work is duplicated between the
shadow-depth and color passes, and the collect pass repeats the traversal a
third time for no spatial benefit.

### 2.4 There is no bounds cache and no spatial structure

Culling is linear in node count and unconditional per pass.
`SceneGraph::getWorldBounds` calls the whole-graph `Impl::updateWorldTransforms`
to answer a query about one node, even though `getWorldTransform` already has
the correct O(depth) ancestor-chain walk. That is a defect, not just a design
limit.

The D-E11 record already states the honest result: a 5,000-node microbenchmark
cut attachment callbacks from 5,000 to 500 but measured *slower* bounded
traversal (750 µs versus 598 µs) because the callback was trivial. The cost
being measured there is traversal and bookkeeping, not emission. That is
direct evidence that the traversal itself is the thing to fix first, and that
a spatial index is not yet justified by measurement.

### 2.5 One attachment, no identity, no payload — consumers keep a shadow model

`IllEd` is the only substantial consumer and it demonstrates the gap:

- `EditorDocument` owns the authoritative hierarchy, its own cycle rejection
  (`wouldCreateCycle`, `isDescendant`), its own world-matrix composition
  (`worldMatrix`), and its own picking (`pick`, `pickRay`). The scene graph
  provides all of these primitives but cannot hold the editor's node identity,
  so the editor cannot use them.
- `EditorModule` keeps `std::unordered_map<std::string, SceneNodeHandle>
  m_handles` and `std::vector<std::unique_ptr<EditorAttachment>>
  m_attachments` as side tables.
- `EditorModule::rebuildGraph()` calls `m_graph.clear()` and reconstructs
  every node and every attachment from scratch. Its parent-resolution loop
  repeats full passes until parents resolve, so it is worst case O(n²). It is
  called from 12 sites, including per-edit paths (`EditorModule.cpp:613`,
  `:620`, `:1168`, `:1323`, `:1345`, `:1371`). Changing one node's colour
  rebuilds the whole graph and every `MeshVisual`.

`IllumoGame`'s `render3dTest` diagnostic uses three nodes and is not a design
constraint.

### 2.6 Mutation and extraction are the same pass

`Impl::renderTraversalActive` rejects every structural, state, and attachment
mutation while attachments are being visited. This is a correct guard for the
current design, but it exists only because the renderer walks live graph
storage. It also means an attachment cannot react to what it sees.

### 2.7 Illumo has no general worker pool, and the existing one cannot move

`SparseWorkerPool` lives in `IllumoGame/Source/Game/` and cannot be relocated
as it stands. Its entire public API is three cellular-automata entry points
typed on `SparseCellGrid` internals — `evaluate`, `evaluateCandidates`, and
`prepareCandidates`, taking `ChunkAddress`, `SparseCellGrid::TargetResult`,
`SparseCellGrid::CandidateScratchChunk`, and
`SparseCellGrid::CandidateWorkRange` — and it is declared
`friend class SparseWorkerPool` inside `SparseCellGrid`
(`SparseCellGrid.h:576`). Moving the class would drag a game-domain type into
the library and break the rule that Illumo does not depend on Game.

What *is* general is its private half: worker lifecycle (`ensureWorkerCount`),
slot claiming, the work-item counter, and the generation-stamped
mutex/condition-variable handshake in `workerLoop`. That mechanism is proven in
this codebase and is the right model to reimplement generically.

`AssetManager` runs a single dedicated decode thread and is the only other
threading precedent in the library.

---

## 3. Scope and non-goals

### 3.1 In scope

- Structure-of-arrays authoritative node store with intrusive sibling lists.
- A derived **compiled layer**: a pre-order-contiguous index array with
  subtree sizes, parent indices, and depth bands, rebuilt lazily on structural
  revision change.
- O(1) local mutation with a dirty watermark instead of subtree marking.
- Cached node world bounds and a linear backward-pass subtree bounds refit.
- Hierarchical range culling, with an optional flat BVH gated on measurement.
- Single-traversal extraction into an immutable, double-buffered
  `SceneSnapshot` consumed by the renderer for the collect, shadow-depth, and
  color passes.
- Node identity (interned names), per-node user data, and zero-or-more
  attachments per node.
- Graph-side queries: ray cast, bounds query, ordered iteration, and a
  structural change journal for incremental consumer sync.
- Separation of `SceneGraph` from `DrawableBase` via a distinct render adapter.
- A general `WorkerPool` primitive inside Illumo, plus deterministic parallel
  transform, refit, and culling behind measured thresholds.
- IllEd migration off `rebuildGraph`, off the string-to-handle side table, and
  onto graph-side picking.
- Tests, reference-oracle fuzzing, benchmarks, guidance updates, decision-log
  entries D-E12 and D-R25, and regenerated documentation.

### 3.2 Explicit non-goals

- **Moving the OpenGL context off the main thread.** See §7.3. The snapshot is
  a precondition for a render thread; it is not a render thread, and shipping
  one without the other buys nothing.
- No ECS, component registry, or system scheduler. Nodes gain user data and
  attachments, not a general component store.
- No serialization format. `.ilsc` stays editor-owned; the graph never
  serializes itself.
- No prefabs, animation system, scripting, physics, networking, or retained UI
  tree.
- No render graph, no additional graphics backend, no material system.
- No change to `RenderCommand`, `CommandQueue`, backend handle types, the
  shared directional-shadow policy, `SparseCellGrid`, `CanvasView`, or ruleset
  code.
- No change to the role of `Rendering::Scene` as the per-frame layered
  drawable list.

---

## 4. Constraints and invariants that must remain true

Carried forward from v1 and the repository contract:

- Illumo does not depend on Game, Rulesets, or IllEd.
- Scene emits only backend-neutral tokens through `Renderer` and imports no
  OpenGL or game-domain type.
- Handles remain graph-ID plus slot plus generation. Node addresses are never
  exposed. Slot zero and generation zero stay invalid, and a released slot
  advances its generation before reuse.
- Foreign, stale, and cyclic operations are rejected with no partial mutation.
- Root and sibling insertion order defines emission order; reparenting appends
  to the destination's tail.
- Destroying a node destroys its complete subtree; no child promotion.
- Local enabled or visible state suppresses the whole subtree.
- Unknown, invalid, or non-finite bounds fail open. Culling an attachment never
  suppresses traversal of its children.
- No recursion anywhere in the subsystem. No `auto`, no namespaces. Mozilla
  `clang-format` on every modified file. Owning types declare or delete copy
  and move deliberately.
- Token payload pointers stay valid until synchronous submission returns.

New invariants introduced by v2:

- The compiled layer is **derived state**. It is reconstructible from the
  authoritative store alone, and every query must be answerable without it
  (at worse asymptotic cost) so a failed or deferred compile is never a
  correctness problem.
- A snapshot is immutable once published. Its lifetime is owned by the graph's
  ring, not by the renderer.
- Any parallel path must be bit-identical to its serial path. Where that
  cannot be proven, the parallel path does not ship.

---

## 5. Proposed design

### 5.1 Two layers

```text
authoritative store (mutation)        compiled layer (derived, lazy)
  slot arrays, generations              preorder[]  -> slot
  parentSlot / firstChild / nextSibling slotToIndex[] -> index
  lastChild                             parentIndex[], subtreeSize[], depth[]
  localTrs, localBoundsCache            worldMatrix[], worldBounds[]
  flags, nameId, userData               subtreeBounds[], levelBands[]
  attachment ranges                     structuralRevision stamp
                                                |
                                                v
                                        SceneSnapshot ring (immutable)
                                                |
                                                v
                                        SceneGraphDrawable -> Renderer tokens
```

### 5.2 Authoritative store

Parallel arrays indexed by slot, all `std::vector<T>` grown together:

| Array | Type | Note |
|---|---|---|
| `generation` | `uint32_t` | unchanged semantics |
| `flags` | `uint32_t` | alive, enabled, visible, localDirty, boundsDirty, hasAttachments |
| `parentSlot`, `firstChildSlot`, `nextSiblingSlot`, `lastChildSlot` | `uint32_t` | intrusive sibling list |
| `localTrs` | `Transform3D` | 40 bytes, authoritative |
| `localBoundsCache` | `AxisAlignedBounds3` + `uint32_t` revision | filled on demand from the attachment |
| `nameId` | `uint32_t` | index into an interning table |
| `userData` | `uint64_t` | opaque consumer tag |
| `attachmentFirst`, `attachmentCount` | `uint32_t` | range into a chunked attachment array |

The intrusive sibling list replaces the per-node `std::vector<SceneNodeHandle>`
and removes both the per-node allocation and the O(children) `std::find` in
detach. `lastChildSlot` preserves the documented append-at-tail order in O(1).

`Transform3D` becomes the authoritative local value rather than `Matrix4`. It
is smaller, it is what editors and animation actually manipulate, and it makes
`getLocalTransform` lossless. The matrix setter stays as a convenience that
decomposes through the existing `Transform3D::fromMatrix`, and is documented as
lossy for shear — which is already true in v1's round trip, just undocumented.

### 5.3 The compiled layer, and why pre-order

Compilation produces `preorder[]`, a permutation of live slots in depth-first
pre-order, plus `parentIndex[]`, `subtreeSize[]`, `depth[]`, and the inverse
`slotToIndex[]`.

Pre-order has two properties that do all the work:

1. **A parent's index is always less than every descendant's index.** So world
   transforms resolve in a single forward loop with no stack and no recursion:

   ```cpp
   for (size_t i = 0; i < count; ++i) {
     const uint32_t p = parentIndex[i];
     worldMatrix[i] = (p == kNoParent) ? localMatrix[i]
                                       : worldMatrix[p] * localMatrix[i];
   }
   ```

   The same shape computes effective enabled and visible state as a forward
   prefix, which removes the `ancestorsEnabled` / `ancestorsVisible` fields
   that v1 carries on its traversal stack.

2. **A subtree is the contiguous range `[i, i + subtreeSize[i])`.** Subtree
   destroy, subtree disable, and hierarchical cull-skip all become range
   operations. A culled subtree advances `i += subtreeSize[i]` instead of
   pushing children.

Level-ordered storage has property 1 but not property 2, so pre-order is the
better base layout here; depth bands are kept as a *separate* permutation used
only by the parallel path (§5.8), which keeps emission order untouched.

**Structural edits do not pay for the layout.** They bump
`structuralRevision`; compilation is a single O(n) pass run at most once per
frame, before the transform update. Transform-only edits never trigger it.
This is exactly IllEd's pattern: a burst of structural edits between frames,
coalesced into one rebuild. If profiling later shows recompilation dominating
under heavy per-frame reparenting, the fallback is a gap-buffer compiled array
that patches ranges in place; that is an optimization of a derived structure
and needs no contract change.

### 5.4 Dirty tracking without subtree marking

Because a parent's compiled index precedes its descendants', v1's
`markSubtreeDirty` is unnecessary. `setLocalTransform` sets the node's
`localDirty` bit and lowers a `lowestDirtyIndex` watermark — O(1), no
allocation, no subtree walk. The update pass runs from the watermark forward
and maintains a `recomputed` bit array; a node recomputes when its own bit is
set or its parent's `recomputed` bit is set. That is E1 and E2.

Two consequences worth stating:

- The watermark is conservative: one edit near the root scans the tail of the
  array. The scan is a linear pass over contiguous `uint32` and bitset data,
  which is cheap relative to v1's pointer-chasing subtree walk, but it is not
  free. For workloads with a single deep edit in a huge graph, a dirty-range
  list (min/max per frame, or a small sorted set of ranges) is the escape
  hatch and is a pure optimization of the same contract.
- `getWorldTransform` on a single node keeps v1's O(depth) ancestor-chain walk
  against the authoritative store, so it stays correct and cheap even before a
  compile. `getWorldBounds` is fixed to use that same path (E5).

### 5.5 Bounds

- Attachment local bounds are cached per node with an attachment-supplied
  revision. The virtual `getSceneLocalBounds` is called when the revision
  changes, not once per node per pass (E6). `ISceneRenderAttachment` gains a
  `getSceneBoundsRevision()` with a default that forces a re-query, so
  existing implementations stay correct without changes.
- Node world bounds are computed in the same forward pass as world transforms.
- **Subtree world bounds** are computed by a single *backward* loop over the
  compiled array. Children always follow their parent, so iterating from the
  end unions each node into its parent with no stack and no recursion:

  ```cpp
  for (size_t i = count; i-- > 0; ) {
    const uint32_t p = parentIndex[i];
    if (p != kNoParent) { subtreeBounds[p].include(subtreeBounds[i]); }
  }
  ```

  AABB union is `min`/`max`, which is exactly associative in IEEE-754, so this
  pass is bit-deterministic under any decomposition — which is what makes the
  parallel version in §5.8 safe.

### 5.6 Culling and the spatial index

Hierarchical range culling falls out of §5.5 for free: if `subtreeBounds[i]`
misses the frustum, skip `subtreeSize[i]` entries. Its honest limitation is
that authored hierarchies are usually *not* spatially coherent — a root with
five thousand scattered children has one subtree box the size of the world,
and hierarchical culling degenerates to linear. Phase 2 ships it anyway
because it costs nothing beyond the refit already required for parent world
bounds.

The spatial index is a separate question, and it has **two distinct consumers
with opposite cost profiles**. Conflating them is what makes "do we need a
BVH?" hard to answer.

#### 5.6.1 What D-E11's benchmark does and does not establish

The v1 record measured 750 µs bounded versus 598 µs unbounded traversal at
5,000 nodes with a deliberately trivial callback. Read correctly, that is a
measurement of **culling overhead**, roughly 30 ns per node, in a setup where
the emission savings were near zero by construction. It does *not* show that
culling is worthless — with a real `MeshVisual` emitting tokens, rejecting 90%
of nodes obviously wins. Any claim that "culling is not the bottleneck" is
over-reading it, and this document previously did exactly that.

What the number is genuinely useful for is extrapolation: 30 ns per node is
~3 ms at 100k nodes, which is a fifth of a 60 Hz frame and not ignorable.

#### 5.6.2 Per-frame culling: the index is closer to a wash than it looks

After Phase 2 the per-node test changes character. World bounds are already
resolved into a contiguous `worldBounds[]` array, so the test is six plane
evaluations against a cached AABB with no virtual call and no eight-corner
transform. Order-of-magnitude estimates, not measurements:

| Path | 100k nodes |
|---|---|
| Scalar frustum test over cached AABBs | ~2–3 ms |
| SoA / SIMD-friendly layout | ~0.5–1 ms |
| Above, across four workers (Phase 6) | ~0.2 ms |
| BVH traversal | ~0.05 ms, **plus refit** |

The decisive point is the last row's tail. A BVH's advantage is sublinear
*traversal*, but its refit is linear — updating leaf boxes and propagating up
internal nodes is another full pass over the same data, comparable in cost to
the §5.5 backward pass we already pay. In a scene where everything moves every
frame, that refit eats most of what the traversal saves. The BVH wins
decisively only when a large fraction of the scene is *static between frames*,
because then both its refit and its test are skipped for that fraction.

So the real gate variable is not "is culling expensive" — it is **what
fraction of nodes are transform-static frame to frame**. And v2 already knows
that exactly: it is the complement of the dirty set the watermark tracks.

This points at a two-tier structure rather than one BVH:

- **Static tier.** Nodes untouched since the last structural or transform
  revision live in a BVH that is refit only when membership changes.
- **Dynamic tier.** Dirty nodes are culled by a flat SIMD linear pass over
  their compiled indices, which is where linear is genuinely competitive.

Membership migration between tiers is driven by the same dirty bits, so this
costs no new tracking machinery. That is a better fit for this codebase than a
single monolithic acceleration structure, and it is the recommended shape when
the culling gate opens.

#### 5.6.3 Interactive queries: the stronger case, and the one to build first

`raycast` and `queryBounds` (§5.9) have the opposite profile. A single ray
against 100k AABBs is ~100k slab tests, order 2–3 ms — **per mouse-move
event**, potentially several times per frame, on the latency-sensitive path
that decides whether the editor feels responsive. A BVH takes that to
microseconds. And picking runs against a quiescent scene, so a build-on-demand
index that is valid until the next structural or transform revision is both
cheap and trivially correct.

The query path therefore justifies a spatial index more strongly than culling
does, at lower risk, and it is the one to build first. This reverses the
earlier ordering in this document, which gated the index entirely behind a
culling measurement it could not have passed.

#### 5.6.4 Resulting position

- Phase 2: hierarchical range culling, free.
- Phase 7a (scheduled, not gated): binned-SAH BVH over `worldBounds[]` serving
  `raycast` and `queryBounds`, built lazily and invalidated by revision
  change. Justified by interactive latency, not by frame time.
- Phase 7b (gated on measurement): extend the same structure to per-frame
  culling as the static tier of the two-tier scheme in §5.6.2. The gate is
  the static fraction plus the measured cost of the Phase 6 parallel linear
  cull — if parallel linear culling lands near 0.2 ms at the largest realistic
  node count, 7b is not worth its refit complexity, and saying so is a result.

A loose uniform grid remains the cheaper alternative for uniformly distributed
editor content and should be benchmarked against the BVH in 7a rather than
assumed away.

### 5.7 Extraction snapshot and the renderer seam

Extraction publishes once per renderer frame into two graph-owned reusable
snapshot buffers. The current public records are:

```cpp
struct SceneRenderItem {
  Matrix4 worldTransform;
  AxisAlignedBounds3 worldBounds;
  ISceneRenderAttachment* attachment;
  SceneNodeHandle node;
  bool boundsValid;
  bool cameraVisible;
};
struct SceneSnapshot {
  uint64_t graphId, frameSerial, structuralRevision, publication;
  std::vector<SceneRenderItem> items;
};
```

Items retain compiled preorder and include camera-invisible potential shadow
casters. SceneGraphDrawable borrows the graph and validates one view for
collection, depth and color. Color uses cameraVisible; depth evaluates snapshot
bounds against the Renderer-owned fitted light volume after caster collection.
Shadow relevance is not an extraction-time flag. Snapshot buffer capacity is
reused; memory depends on actual record size and retained item capacity in both
buffers rather than a culled-only or triple-buffer estimate.

The extraction guard also protects direct emission, bounds polling and queries.
Ordinary transform/hierarchy changes and attachment additions preserve captured
values; removal, replacement, explicit content invalidation, graph destruction
and ring-slot reuse expire views. Check validity before every attachment callback.

### 5.8 Parallelism, and what it actually requires

Prerequisite: a `WorkerPool` in `Illumo/Services/` (§2.7). `SparseWorkerPool`
cannot be moved, because its public API is typed on `SparseCellGrid` internals
and it is a friend of that class. The new pool reimplements its *mechanism* —
worker lifecycle, slot claiming, work-item counter, generation-stamped
handshake — with a generic range-submit-and-join surface. Re-expressing
`SparseWorkerPool` as a thin adapter over the library pool is a worthwhile
follow-up, not a prerequisite; see the plan document for why it should not be
bundled into this milestone.

Three parallel stages, each gated by a measured node-count threshold and each
required to be bit-identical to its serial path (E10):

1. **Transform update.** The forward loop has a parent-before-child dependency,
   so parallelism is by depth band: all nodes at depth *d* are independent once
   depth *d−1* is complete. This uses the separate depth-band permutation from
   §5.3 and writes into `worldMatrix[]` indexed by compiled index, so results
   land in pre-order regardless of worker scheduling. Bit-identical because
   each node's matrix product is computed by exactly one worker from
   already-final inputs.
2. **Bounds refit.** Same banding in reverse. Bit-identical because `min`/`max`
   union is exactly associative (§5.5).
3. **Culling.** Embarrassingly parallel over item ranges into per-worker
   bitsets, then a serial compaction in compiled order. Determinism comes from
   the compaction, not from the workers.

Below threshold, all three run serially. The threshold is measured, not
guessed; the benchmark in §9 produces it.

### 5.9 Identity, payload, attachments, and queries

- **Names.** An interning table maps `std::string` to `uint32_t nameId`.
  `findByName` returns the first node in compiled order. Names are optional and
  non-unique by default; uniqueness is a consumer policy.
- **User data.** One `uint64_t` per node, opaque to the graph. IllEd stores its
  document index or id hash there and deletes `m_handles`.
- **Multiple attachments.** A chunked attachment array with a per-node range.
  Ordering within a node is insertion order, so emission stays deterministic.
  Ownership is unchanged: attachments are borrowed, never deleted by the graph.
- **Queries.** `raycast(origin, direction, SceneRayHit*)` and
  `queryBounds(const AxisAlignedBounds3&, ...)` run against `worldBounds[]` —
  linear today, accelerated by the BVH when its gate opens. This is what lets
  `EditorDocument::pickRay` delegate instead of duplicating.
- **Change journal.** A bounded ring of `{kind, handle}` records plus a
  `structuralRevision` counter, so a consumer can sync incrementally and fall
  back to a full resync on overflow. This is the mechanism that removes E8's
  rebuilds. It is a journal, not an observer/callback framework — no
  reentrancy, no user code running inside graph mutation.

---

## 6. Alternatives considered

| Alternative | Verdict |
|---|---|
| Keep AoS slots, just add a bounds cache and single-pass extraction | Rejected as the end state, but it is essentially Phase 2 of this plan and captures a large share of the win. Kept as the fallback if Phase 3+ is descoped. |
| Level-ordered (breadth-first) compiled array | Rejected as the primary layout: it gives parent-before-child but not subtree contiguity, and it breaks the documented pre-order emission contract. Retained as the secondary permutation for parallel banding only. |
| Keep a flat array permanently sorted and patch it in place on every structural edit | Rejected for now: worst-case O(n) memmove per edit, versus one O(n) compile per frame for any number of edits. Revisit only if per-frame reparenting churn is measured. |
| Full ECS with transform/hierarchy components | Rejected. It solves a different problem, it is explicitly out of scope in the repository charter, and nothing in the two current consumers needs it. |
| Observer callbacks instead of a change journal | Rejected. Callbacks reintroduce reentrancy during mutation, which is the exact hazard `renderTraversalActive` exists to prevent. |
| Snapshot carrying recorded command payloads instead of attachment pointers | Deferred, not rejected. It is the real precondition for a render thread (§7.3), and it is the natural Phase 8 if the GL-context decision is ever taken. |
| Build one BVH serving both culling and queries | Rejected. The two consumers have opposite static/dynamic profiles (§5.6). Queries want a build-once structure over a quiescent scene; per-frame culling of dynamic nodes wants no tree at all. A two-tier split follows the dirty set the graph already tracks. |
| Gate the whole spatial index behind a culling-cost measurement | Rejected on review. That was this document's earlier position and it over-read D-E11's microbenchmark, which measures culling *overhead*, not culling *value*. It also ignored the interactive query path, where the case is strongest. The query index is now scheduled; only the culling tier stays gated. |

---

## 7. Compatibility, ownership, lifetime, threading, platform

### 7.1 Public contract changes

This milestone is authorized to break the v1 API. Changes:

- `createNode(SceneNodeHandle parent)` becomes
  `createNode(const SceneNodeDesc&)`; the old form remains as an inline
  convenience because it is unambiguous.
- `setLocalTransform(Matrix4)` stays but is documented as decomposing;
  `Transform3D` is the primary form. Batch setters
  `setLocalTransforms(const SceneNodeHandle*, const Transform3D*, size_t)` are
  added for animation-style updates.
- `setRenderAttachment` is replaced by `addAttachment` / `removeAttachment` /
  `getAttachmentCount` / `getAttachment(node, index)`. A single-attachment
  inline shim keeps the common case one line.
- `SceneGraph` no longer derives from `DrawableBase`. Consumers add a
  `SceneGraphDrawable` to the frame list instead. This is the only change that
  forces every consumer to edit a line.
- `ISceneRenderAttachment` gains `getSceneBoundsRevision()` with a
  conservative default, so existing attachments compile and behave unchanged.

Migration cost: `IllEd` (the real work, and a net deletion), `IllumoGame`'s
`render3dTest` (three nodes), `Illumo/Tests/TestSceneGraph.cpp`,
`Illumo/Tests/PublicHeaderSmoke.cpp`, `TestMeshVisual.cpp`,
`TestFrameCapture.cpp`, and the IllEd test fixtures.

### 7.2 Ownership and lifetime

Unchanged in principle: the graph owns nodes, transforms, compiled state, and
snapshots; consumers own attachments and must keep them alive while reachable
from any *live snapshot*, which is a slightly longer window than v1's "while
reachable by traversal". Detaching an attachment must therefore either
invalidate outstanding snapshots or be deferred to the next extraction; the
plan takes the first option, since a snapshot is only one frame long.

### 7.3 Threading — the part that needs to be said plainly

You asked for an extractable snapshot for a render thread. The snapshot is
achievable and worth building. The render thread is not achievable inside this
milestone, for two reasons that are in the current code:

1. `AGENTS.md` declares window, input, module, rendering, and OpenGL work
   main-thread affine, and `Renderer`/`GLBackend` are built that way. Moving
   the GL context to another thread is a separate, larger architecture change
   with its own decision record, and it is the thing that actually gates any
   frame-rate benefit.
2. A snapshot holding `ISceneRenderAttachment*` does **not** make cross-thread
   rendering safe. `appendSceneCommands` is consumer code that touches
   consumer-owned state (`EditorAttachment` owns a `MeshVisual` that owns GPU
   handles). Handing that pointer to another thread hands consumer state to
   another thread.

So this document splits the goal:

- **v2a (in scope).** Snapshot of resolved transforms, bounds, flags, and
  attachment pointers. Decouples mutation from emission, collapses three
  traversals into one, enables parallel culling, and is the structural
  precondition for a render thread. Emission stays on the main thread.
- **v2b (designed, not scheduled).** Attachments record their token payloads
  into the snapshot's arena during extraction; the consumer of the snapshot
  submits pure data and touches no consumer object. `Renderer` already owns a
  per-frame arena (`frameArena`) and already requires payload pointers to
  outlive submission, so the ownership shape fits. v2b only pays off together
  with the GL-context move, and should be authorized with it, not before.

Everything in v2a stays main-thread affine except the explicitly bounded
worker-pool stages in §5.8, which run inside a single synchronous call and
join before returning.

### 7.4 Platform

Standard C++23, GLM, and the new `WorkerPool` over `std::thread`. No
platform-specific code. Windows remains the only verified platform.

---

## 8. Ordered milestones

Each phase is independently landable and independently revertible. Phases 1–5
are the core proposal; 6–8 are gated.

| Phase | Content | Gate to proceed |
|---|---|---|
| 0 | `Illumo/Services/WorkerPool`, scene benchmark harness, reference-oracle test fixture (a naive but obviously-correct graph the fuzzer compares against) | Oracle reproduces v1 behaviour exactly on the existing test corpus |
| 1 | SoA store, intrusive sibling lists, compiled pre-order array, watermark dirty, flat transform update. Public API held. | E1, E2, E4, E11 met; full Release CTest green |
| 2 | Bounds revision cache, backward refit, hierarchical range culling, `getWorldBounds` fix | E5, E6 met; culling parity with v1 on the D-E11 corpus |
| 3 | API v2: descriptors, batch setters, multi-attachment, names, user data, queries, change journal; `SceneGraph` split from `DrawableBase` | E7 met; all consumers and tests migrated |
| 4 | Single-pass extraction, snapshot ring, `SceneGraphDrawable`, renderer consumes snapshots (D-R25) | E3, E9 met; frame output byte-identical via `FrameCapture` |
| 5 | IllEd migration: delete `rebuildGraph`, incremental sync via journal, delegate picking to `SceneGraph::raycast` | E8 met; IllEd test suite green; manual editor smoke |
| 6 | Parallel transform, refit, and cull behind measured thresholds | E10 met (1-worker vs N-worker bit-identical); measured win at threshold |
| 7a | BVH (or loose grid) serving `raycast` / `queryBounds`, built lazily, invalidated by revision | Scheduled. Interactive pick latency at 100k nodes, measured before and after |
| 7b | Extend it to per-frame culling as the static tier of the two-tier scheme | Gated: opens only if the Phase 6 parallel linear cull misses budget at the largest realistic node count |
| 8 | v2b recorded-payload snapshot | Opens only with a separate authorized GL-context decision |

Documentation and guidance updates land *with* the phase that changes
behaviour, not at the end: `docs/architecture-consensus.md`,
`docs/packages/scene.md`, `Illumo/Source/Scene/AGENTS.md`, the root
`AGENTS.md` scene paragraphs, the LaTeX chapter, and decision entries D-E12
(Phase 1) and D-R25 (Phase 4).

---

## 9. Verification strategy

**Parity first.** Before Phase 1 changes anything, the existing cases
(`Illumo.SceneGraph.HandlesAndLifetime`, `.HierarchyAndTransforms`,
`.RenderExtraction`, `.BoundsAndCulling`, `.TransformConversions`) are
extended with an *emission-order golden test*: a fixed construction and
mutation script whose recorded attachment call sequence must stay byte-identical
across every phase. That is E11's enforcement mechanism.

**Reference-oracle fuzzing.** The Phase 0 oracle is a deliberately naive
implementation (parent pointers, recompute-everything). A randomized script of
creates, reparents, destroys, transform edits, enable/visible toggles, and
attachment changes runs against both, comparing world transforms, world
bounds, effective state, and emission order after every step. This is the only
practical way to gain confidence in the watermark and compiled-range logic,
which are the two places where this design can be subtly wrong.

**Benchmarks** (Release, recorded with hardware and build configuration), at
1k / 10k / 100k nodes and depths 1 / 8 / 64:

- `setLocalTransform` on a root with a large subtree — expect O(1) versus v1's
  O(subtree).
- Full world-transform resolution, serial and parallel.
- Bounds refit, serial and parallel.
- Culling at 10% / 50% / 100% visibility, scalar and SoA, serial and parallel.
- **Static fraction**: proportion of nodes with clean transforms per frame,
  sampled from a realistic IllEd editing session and from `render3dTest`. This
  is the decisive input for §5.6.2 and nothing currently measures it.
- **Interactive pick latency**: single `raycast` against 1k / 10k / 100k
  bounded nodes, which is the Phase 7a justification.
- Structural churn: 1k reparents per frame, measuring recompilation.
- Full frame extraction versus v1's three traversals.

The benchmarks are the gate inputs for Phases 6 and 7b. They are non-gating for
correctness but a phase does not land without its numbers recorded.

**Standard repository requirements**, per phase: `clang-format` on every
modified file, `git diff --check`, focused exact tests, full Release build and
the `IllumoWorkspace` CTest label, `IllumoTidy` on changed translation units
(the three pre-existing `modernize-use-nullptr` diagnostics in `GLDevice.cpp`
remain pre-existing and are not opportunistically fixed), coverage against the
85% gate, and `docs/build.ps1` when LaTeX sources change.

**Frame parity.** Phase 4 is verified with `FrameCapture`: identical scenes
must produce identical captures before and after the snapshot seam.

**Manual smoke.** Phases 4, 5, and 6 change what reaches the screen or when,
so each needs a live IllEd smoke with shadows enabled. Headless tests prove
ordering and lifetime, not pixels.

---

## 10. Rollback and containment

`release/v26.09` at `0bd6e38a` is the rollback point and should be tagged
before Phase 1. Each phase is a separate merge with its own revert.

Phases 1 and 2 are internal and revert cleanly. Phase 3 breaks the API, so its
revert also reverts consumer migrations — it should land as one merge
containing both. Phase 4 is the highest-risk revert because it changes the
renderer seam; containment is an environment-variable switch
(`sceneSnapshotExtraction`, default on after validation) that restores direct
traversal for one release, removed in the following one. Phases 6 and 7 are
behind thresholds and can be disabled by setting the threshold beyond reach
without reverting code.

No persisted data, asset, or file format changes, so there is no data
migration to unwind.

---

## 11. Open questions

1. **Name uniqueness.** Should the graph enforce unique names, or stay
   non-unique with `findByName` returning first-in-order? Recommendation:
   non-unique, because uniqueness is an editor policy and enforcement costs a
   hash map on every rename.
2. **User data width.** One `uint64_t`, or a small fixed byte blob? One word
   covers both current consumers; a blob invites the graph to become a
   component store.
3. **Journal capacity.** What ring size before a consumer is forced into full
   resync? IllEd's worst realistic burst is a multi-select delete; 4,096
   records is a starting guess that Phase 5 should measure.
4. **`Transform3D` as authoritative.** This makes non-uniform-scale-plus-shear
   local matrices unrepresentable. Inspection found that v1 actually preserves arbitrary matrix input. The
   authorized v2 conversion is a deliberate compatibility change for those
   matrices, not a pre-existing limitation; the TRS limitation is now explicit.
5. **Phase 8 authorization.** Whether the GL-context move is ever on the table
   determines whether v2b's design constraints should influence Phase 4's
   snapshot layout now. Cheap to accommodate now; expensive to retrofit.


## 12. Implementation record and validation (2026-09-17)

The supplied proposal and execution sequence above remain design rationale.
Current implementation details and evidence are in `scene-graph-v2-plan.md`.
The following resolved discrepancies take precedence over earlier sketches:

- The shared shadow light fit is unavailable during caster collection. The
  immutable snapshot records camera visibility and retains potential casters;
  the adapter tests shadow relevance against snapshot bounds in the depth pass.
  Camera-rejected subtree ranges avoid camera tests, but retain shadow items.
- Intrusive storage includes `previousSibling` for constant-time detach.
  Attachment storage is a reusable chunked intrusive pool. Names are nonunique,
  interned strings; unique-name lookup is allocation-free and duplicates retain
  first-preorder semantics. Payload width is uint64, journal capacity is 4,096,
  and the snapshot ring has two slots with explicit expiring views.
- Graph state is the runtime authority. EditorDocument is the edit gateway and
  owns serialization recipes, stable file IDs, picking proxies, and graph;
  EditorModule owns persistent render bindings and consumes the change journal.
  `.ilsc` remains version 1 and exports graph order. Exact editor picking stays
  a local-box narrow phase after graph queries.
- A bounds revision of zero opts out of caching. Indexed queries poll revisions
  before querying, so total public query work remains O(n) despite faster BVH
  intersection. This avoids silently requiring notifications from old callers.
- Matrix setters decompose to TRS; unlike the proposal's assumption, v1 retained
  arbitrary matrices. Shear/projective inputs are intentionally lossy in v2.
- WorkerPool is generic and tested. Scene stages remain serial unless their
  measurement gate opens. The CA-specific SparseWorkerPool remains unchanged;
  consolidating it is a separate follow-up after scene workload experience.
- Implementation and verification preceded Git publication. The owner then
  explicitly authorized committing and pushing the completed update. The
  supplied phase-merge/tag advice remains historical guidance; phase tags and
  merges are outside that publication request.

Verification includes the independent million-operation oracle, unchanged
emission golden, indexed/linear parity, borrowed snapshot lifetime tests,
allocation injection and recovery, zero-allocation warmed animation, editor
identity/order tests, and real OpenGL baseline image comparison. See the plan
for exact results, measurements, gate decisions, and remaining manual limits.
