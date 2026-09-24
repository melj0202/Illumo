# Scene subsystem guidance

This file specializes the repository `AGENTS.md` for `Illumo/Source/Scene/`.

## Ownership and invariants

- `SceneGraph` owns SoA slots, intrusive hierarchy, authoritative TRS, compiled
  preorder/bounds/query caches, interned names, a bounded journal, and two
  snapshot buffers. It is not a drawable, ECS, serializer, or update framework.
- Expose graph-ID-plus-slot-plus-generation handles, never node addresses.
  Zero is invalid; destruction invalidates every descendant before reuse.
- Reject stale, foreign, and cyclic relationships without partial mutation.
  Preserve root/sibling insertion order; reparenting appends to its destination
  unless `setParent(node, parent, insertBefore)` names the sibling to precede
  (D-E26), which is the only ordering call.
- Keep all graph walks iterative. Transform setters mark one slot and lower a
  watermark; getters must not consume dirtiness required by compiled updates.
- Names need not be unique; lookup returns first preorder match. Payloads are
  opaque uint64 values. Multiple attachments preserve insertion order.
- Borrow attachments. Their owner detaches/invalidateSnapshots before changing
  or retiring content and keeps emitted token payloads alive until submission.
- Nonzero bounds revisions cache local bounds; zero requests every-extraction
  polling. Transform each attachment box before unioning node/subtree boxes.
  Unknown/invalid bounds fail open during rendering. Geometry queries exclude
  unknown bounds and preserve preorder ties.
- Derived compilation or BVH allocation failure falls back to authoritative
  traversal or linear queries. A cache failure must not change results.
- Reject mutation during extraction/bounds callbacks. Ordinary edits after
  publication leave snapshot values unchanged. Adding an attachment affects the
  next extraction without retiring existing captured items. Attachment removal,
  replacement, explicit content invalidation, destruction, clear, and ring-slot
  reuse expire affected views. Validate before each callback.
- SceneGraphDrawable alone bridges snapshots to DrawableBase. Keep graph state
  separate from token/backend execution. No OpenGL or product-domain imports.
- Shared shadow fitting occurs after collection, so camera rejection cannot
  discard potential off-camera casters. Test fitted shadow relevance against
  snapshot values during depth emission.
- Mutation, extraction, queries, and callbacks remain main-thread affine.
  Parallel stages are measurement-gated; they may not call virtual attachments,
  mutate structure, or change deterministic emission order.

## Documentation and verification

See `docs/scene-graph-v2-design.md`, `docs/scene-graph-v2-plan.md`, D-E12/D-R25,
`docs/architecture-consensus.md`, and `docs/packages/scene.md`. Preserve the
v1 emission golden; verify independent oracle matrices/bounds/order, indexed
query parity, snapshot expiration, allocation failures, and consumer behavior.
