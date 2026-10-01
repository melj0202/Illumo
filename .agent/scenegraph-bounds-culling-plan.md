# SceneGraph bounds, frustum culling, and relevant shadows

## Objective and measurable end state

Give the persistent `SceneGraph` a conservative, attachment-authored bounds
contract so it can expose per-node world bounds and skip off-frustum color
attachments. Restrict the shared directional-shadow pass to casters that can
affect the camera-visible region while keeping the existing iterative node
storage, token renderer, and single shared shadow map.

The change is complete when bounded attachments outside the camera frustum emit
no color tokens, unknown bounds fail open, child traversal is independent of a
parent attachment's visibility, direct and graph-attached meshes use the same
shadow relevance policy, and focused plus full Release verification passes.

## Current-state evidence

- `SceneGraph` owns generational node slots, ordered child vectors, cached world
  transforms, and a retained iterative traversal stack, but no bounds.
- `ISceneRenderAttachment` receives a node world matrix and has color, caster
  collection, and shadow-depth hooks but no spatial query.
- `MeshData`, managed `MeshAssetInfo`, and `MeshVisual` shadow collection already
  retain local triangle/asset bounds; lines and sprites are not included.
- `Renderer::FrameContext` captures the active world MVP once per frame. The
  shared shadow projection is currently fitted to every registered caster.
- `docs/output/illumo.pdf` is absent; Markdown and LaTeX sources are authoritative
  and will be updated before regenerating the PDF.

## Scope and explicit non-goals

In scope:

- a public finite axis-aligned 3D bounds value and transform helpers;
- optional attachment-local bounds and a SceneGraph world-bounds query;
- linear camera-frustum culling during color extraction;
- complete conservative `MeshVisual` bounds, including billboards;
- a bounded relevant-caster volume and prepared light-frustum depth culling;
- motion-history handling across culled frames;
- tests, benchmark evidence, canonical documentation, decision D-E11, durable
  guidance, and regenerated PDFs.

Out of scope:

- BVH, octree, subtree bounds cache, occlusion queries, or GPU culling;
- IllEd picking migration, `.ilsc` changes, ECS work, retained UI, or a render
  graph;
- a new light object, multiple shadow maps, cascades, or another backend.

## Constraints and invariants

- Keep `SceneGraph` main-thread affine, handle-based, iterative, and separate
  from the per-frame `Rendering::Scene` list.
- Unknown, invalid, non-finite, or unavailable bounds render conservatively.
- Culling an attachment never suppresses its children.
- Preserve enabled/visible subtree semantics and deterministic surviving order.
- Direct World meshes and SceneGraph attachments share renderer-owned shadow
  selection and one depth target.
- Preserve existing `lightDistance`; the new caster horizon defaults to 100
  world units and is independently configurable.
- Token payload lifetime and synchronous submission rules remain unchanged.

## Proposed design and alternatives

Add `AxisAlignedBounds3` as a small public value with validation, expansion,
intersection, and conservative eight-corner transformation. Add a default
`ISceneRenderAttachment::getSceneLocalBounds` that returns false. `SceneGraph`
queries it on demand, transforms it by the resolved node world matrix, exposes
`getWorldBounds`, and asks `Renderer` whether the world box is camera-visible.
No persistent bounds cache is needed for the selected O(n) design.

`MeshVisual` maintains CPU-side aggregate bounds for procedural lines,
triangles, sprites, and its managed mesh asset. Its reported node-local box
includes `modelMatrix`; billboards use a view-independent enclosing cube around
their quad. `EditorAttachment` forwards the query; skyboxes and other
attachments retain the default unbounded behavior.

`Renderer` extracts frustum planes and world corners from the active MVP. It
retains caster descriptors until shadow preparation, chooses the first
camera-visible caster's direction and 100-unit-default caster distance,
extrudes the camera-frustum half-spaces toward the light, filters descriptors
against that conservative swept volume, and fits the shared light projection
to the survivors. Its world AABB is only an early rejection. Depth emission
also tests the prepared caster volume and light frustum. Invalid camera
reconstruction falls back to the existing all-caster aggregation.

A renderer frame serial lets `MeshVisual` recognize that its previous color MVP
is stale after a culled frame and use the current MVP on re-entry.

Rejected alternatives:

- Explicit node-authored bounds duplicate attachment geometry and require a
  synchronization protocol.
- Subtree bounds or a spatial index add invalidation complexity without a
  demonstrated node-count requirement.
- Camera-culling every shadow caster loses valid offscreen shadows.
- Testing the old fit-all-caster light frustum cannot materially reduce work
  because those casters define the frustum.

## Public contracts and compatibility

- Add `AxisAlignedBounds3` under the public Foundation headers.
- Add optional `ISceneRenderAttachment::getSceneLocalBounds`.
- Add `SceneGraph::getWorldBounds`.
- Add renderer camera/shadow bounds queries and frame-serial context state.
- Add `MeshVisual::setShadowCasterDistance`/getter and the matching caster
  descriptor field.

All additions are source compatible. Existing attachments remain unbounded and
unculled until they opt in. Persistence and backend contracts do not change.

## Ownership, lifetime, threading, errors, and platforms

Bounds are values; attachments remain consumer-owned and borrowed. Renderer
frusta, caster descriptors, and shadow relevance state are retained per-frame
scratch owned by `Renderer`. No new pointers cross submission. All operations
remain main-thread affine and backend-neutral. Invalid math disables the
optimization for the affected path. Windows remains the only verified runtime.

## Ordered implementation milestones

1. Add the bounds value, attachment query, and SceneGraph world-bounds/culling.
2. Add complete `MeshVisual` bounds and IllEd forwarding.
3. Add renderer frustum state, frame serial, relevant-caster selection, and
   direct/attached depth culling.
4. Add focused bounds, SceneGraph, MeshVisual, renderer-shadow, and motion tests.
5. Synchronize architecture, package, decision, guidance, and LaTeX sources.
6. Format, build, test, benchmark, run static analysis, regenerate and inspect
   PDFs, review the complete diff, and record results here.

## Verification strategy

- Focused exact tests for bounds math, SceneGraph extraction, MeshVisual bounds,
  shared shadows, direct/attached parity, and motion history.
- Full Release workspace build and `IllumoWorkspace` CTest label.
- `clang-format --dry-run --Werror`, `IllumoTidy`, and `git diff --check`.
- Non-gating Release microbenchmark with about 5,000 nodes and most offscreen;
  report traversal time, callbacks, and emitted command reduction.
- Rebuild both generated PDFs and visually inspect the changed architecture and
  decision pages.
- Attempt a live OpenGL smoke if a targetable window is available; otherwise
  report GPU pixels and interactive behavior as unverified.

## Rollback and containment

The feature is additive and has no data migration. It can be removed by
reverting the bounds contract, renderer per-frame relevance state, MeshVisual
implementation, tests, and matching documentation. Existing attachments and
saved scenes require no rollback.

## Open questions and decisions

None. The owner approved attachment-authored bounds, linear traversal, public
world-bounds queries, relevant-caster shadows, a distinct caster horizon, and a
100-world-unit default.

## Validation results

- Focused bounds, SceneGraph, MeshVisual, shadow, motion-history, IllEd
  forwarding, and public-header cases passed. The tests cover point/flat,
  rotated, negatively scaled, malformed, stale/foreign-handle, orthographic,
  perspective, fail-open, parent/child, direct/attached, caster-horizon,
  invalid-camera fallback, and re-entry behavior.
- `cmake --build build --config Release`: passed and built every workspace
  product/test target.
- `ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure`:
  426/426 passed (182 Illumo, 181 IllumoGame, 49 IllEd, 14 IllMeshViewer).
- `IllumoTidy` analyzed 141 first-party sources but remains non-green because
  untouched `Illumo/Source/Rendering/OpenGL/GLDevice.cpp` has three existing
  `modernize-use-nullptr` errors at lines 368, 380, and 397. Direct
  `clang-tidy` on all four changed production translation units passed with no
  user-code diagnostics.
- `docs/build.ps1`: passed; produced an 83-page `illumo.pdf` and an 8-page
  `architecture-map.pdf`. Rendered pages 12, 63, 64, and map page 1 were
  visually inspected successfully.
- Non-gating Release microbenchmark, 5,000 flat nodes with 500 visible:
  bounded traversal took 750 microseconds and invoked/emitted 500
  callbacks/commands; fail-open unbounded traversal took 598 microseconds and
  invoked/emitted 5,000. This proves the intended callback/token reduction but
  not a traversal speedup for trivial attachments.
- The exact interactive plane/shadow smoke could not run because this Codex
  host exposed no native-app surface (`cua.getState` returned no apps and
  native `getApp` was unavailable). The Release capture cases passed, but they
  do not replace visual acceptance of a live window.
