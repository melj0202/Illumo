# Illumo Scene

`SceneGraph` owns persistent nodes in parallel slot arrays and exposes only
`SceneNodeHandle` identities (graph ID, slot, generation). Intrusive ordered
parent/child/sibling links are authoritative. A structural revision lazily
compiles preorder, parent indices, subtree ranges, and depths. Local TRS edits
set one dirty bit and lower a watermark; one forward pass resolves affected
world transforms. Authoritative world queries walk only ancestors and do not
consume dirty state. Matrix setters explicitly decompose to TRS; shear and
projective input are lossy.

Nodes carry nonunique interned names, an opaque uint64 payload, enabled/visible
state, and an ordered list of borrowed attachments. Names return the first
preorder match. A 4,096-entry sequence journal lets consumers update bindings;
an expired cursor signals a full resynchronization. No node addresses, file
format, component system, or application update callbacks enter this API.

Nonzero attachment bounds revisions cache local AABBs; zero means uncacheable.
World boxes transform each attachment before union, and a backward pass refits
subtree boxes. Unknown/invalid bounds fail open during rendering. Hidden
subtrees skip their compiled range. Camera-missed bounded subtrees skip camera
tests but retain snapshot entries for potentially relevant off-camera shadows.
`raycast`, ordered ray candidates, and `queryBounds` use a lazy binned-SAH BVH;
failed builds use the equivalent linear path. Warm queries still poll attachment
revisions in O(n); the index accelerates intersection work, not that polling.

`SceneGraph` no longer derives from `DrawableBase`. `SceneGraphDrawable` borrows
it and publishes one `SceneSnapshotView` per renderer frame, keyed by renderer
lifetime and frame serial. Collection, depth, and color consume immutable world
matrices, bounds, handles, and borrowed attachment pointers. The graph retains
two reusable snapshot buffers. Views expire on slot reuse, graph teardown, or
attachment invalidation/destruction. The adapter checks validity before every
callback and reports an expired frame rather than calling retired content.
Ordinary transform/state edits after extraction affect the next snapshot.

The Renderer fits the shared directional light after caster collection, so
shadow relevance is computed from snapshot bounds in the depth pass. This is
an intentional adjustment to the original proposal's early shadow flag; it
preserves the established shared-shadow policy and off-camera casters. Owners
must detach or invalidate snapshots before changing/freeing borrowed content,
and emitted token payloads must still outlive synchronous submission. Mutation
is rejected inside extraction/bounds callbacks. All scene work stays on the
main thread. The `sceneSnapshotExtraction` environment value defaults to on;
zero temporarily selects guarded direct traversal for containment.

Sibling order is part of the graph contract: `setParent(node, parent,
insertBefore)` places a node immediately before a current child of `parent`
(null appends, even under the same parent), with the same cycle rules and
Reparented record as `setParent`; an unchanged position records nothing.
`getPreviousSibling` complements `getNextSibling`. This narrow addition serves
hierarchy reordering and exact sibling restore on undo; nothing else in the v2
contract changed.

The graph still never serializes itself. `Illumo::Content` owns `.ilsc`
format 2 and `SceneInstance`, the one scene loader, which builds graphs only
through this public API: stable file IDs become graph names (an id-to-record
map lives in the instance), attachments persist across in-place edits, and
`document()` exports hierarchy order from the graph (see `content.md`). IllEd's
`EditorDocument` wraps one `SceneInstance` and is its edit gateway; picking
uses `SceneInstance::pickRay` over attachment local bounds with rotation and
effective visibility. IllumoGame's `render3dTest` scene
(`Scenes/render3d-test.ilsc`) and IllMeshViewer's scenes load the same way;
CA storage and primitive-composed UI stay separate. Generic `WorkerPool` is
available without importing CA policy; scene parallelism and a separate
culling index remain measurement-gated.

See `../scene-graph-v2-design.md`, `../scene-graph-v2-plan.md`, and decisions
D-E12/D-R25 for implementation, benchmark gates, and verification. The v1
design remains historical context for D-E8/D-E11. The sibling-order API and
the move of `.ilsc` into Content are recorded in
`../content-packages-and-scenes-design.md` (section 7) and its plan (M3);
`Illumo.SceneGraph.SiblingInsertBefore` covers the API.
