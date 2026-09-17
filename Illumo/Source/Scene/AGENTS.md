# Scene subsystem guidance

This file specializes the repository `AGENTS.md` for
`Illumo/Source/Scene/`.

## Scope and ownership

Scene owns Illumo's persistent product-agnostic node hierarchy. `SceneGraph`
owns node slots, hierarchy links, cached transforms, and node lifetime.
Consumers keep ownership of borrowed `ISceneRenderAttachment` values and must
keep them alive while reachable by render traversal.

## Required invariants

- Expose graph-ID-plus-slot-plus-generation handles, never node addresses.
- Reserve slot and generation zero as invalid; advance a generation before a
  released slot can be reused.
- Reject foreign, stale, and cyclic relationships without partial mutation.
- Preserve deterministic root and sibling insertion order. Reparenting appends
  to the destination order; destruction removes the complete subtree.
- Keep transform propagation, traversal, dirty marking, and destruction
  iterative. Repository policy forbids recursion.
- Local enabled or visible state suppresses the complete subtree during render
  extraction.
- Emit only backend-neutral renderer tokens through
  `ISceneRenderAttachment`; Scene must not import OpenGL or game-domain types.
- Forward collection, shared shadow-depth, and color extraction through the
  same enabled/visible transform traversal so SceneGraph attachments participate
  in the Renderer-owned scene shadow pass without transferring ownership.
- Treat attachment-provided finite local bounds as authoritative and query
  them on demand. Transform them conservatively for world queries and color or
  shadow rejection; unknown, invalid, or malformed bounds fail open. Culling
  an attachment must never suppress traversal of its children.
- Reject node, hierarchy, state, and attachment mutation while render
  attachments are being visited; callbacks must not invalidate traversal
  storage.
- Keep v1 main-thread affine. Thread-safe mutation requires a separately
  authorized synchronization and lifetime design.
- Do not add ECS storage, serialization, update callbacks, retained UI,
  subtree-bound caches, spatial indices, or attachment ownership without a
  concrete consumer and an approved architecture extension.

## Documentation and verification

The contract is recorded in `docs/scene-graph-v1-design.md`, formal decisions
D-E8/D-E11, `docs/architecture-consensus.md`, and `docs/packages/scene.md`. Changes
must exercise handle lifetime, cycles, hierarchy order, transform propagation,
subtree state, destruction, and token extraction.
