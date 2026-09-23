# Rendering primitives guidance

This file specializes `Illumo/Source/Rendering/AGENTS.md` for
`Illumo/Source/Rendering/Primitives/`.

## Scope and boundaries

Primitives provides small backend-neutral visual values and composed
drawables, including the game chrome path. It translates geometry, color,
layer, and coordinate-space intent into ordinary renderer tokens. It is not a
retained widget toolkit or a second renderer.

## Required invariants

- Keep primitive descriptions value-oriented, explicitly typed, and free of
  OpenGL types or resource ownership.
- Make screen-space versus world-space placement and render layer explicit at
  construction and command emission boundaries.
- Compose overlay chrome through `GameVisual` value primitives. Compose world
  objects through `MeshVisual` (mesh + style + optional texture, optional
  billboard) on a `SceneGraph` node or as one World drawable. Both hosts emit
  the `WorldLook` `uMVP` contract; do not add an immediate-mode side channel.
- A `MeshVisual` may borrow one immutable mesh-asset handle managed by
  `AssetManager`; it must not destroy that handle. Keep procedural dynamic
  batches visual-owned and preserve their reuse behavior.
- `MeshVisual` bounds cover every current geometry source immediately after
  mutation, before GPU preparation. Bounds must remain conservative under the
  local model matrix; billboard sprites use a view-independent enclosure and
  unreliable data fails open. Preserve frame-serial motion history so a
  culled visual re-enters with its current MVP as its previous MVP.
- Batch compatible geometry into bounded reusable storage and emit one upload
  and draw per batch where the existing contract allows it. Preserve order and
  clipping; do not trade correctness for fewer commands.
- Any vertex, index, uniform, or text bytes referenced by appended commands
  must outlive queue submission. Reallocation after pointer capture is a
  lifetime defect.
- Keep shared themes and style data value-only. Ownership, input behavior, and
  domain commands stay with their existing App/Game/Services owners.
- `ShapeKind::GradientQuad` carries one color per vertex through the ordinary
  shape vertex format and shader. Its quad must be convex and fan-ordered
  ((0,1,2) then (2,3,0)); colors interpolate per triangle, so use two-color
  axis gradients or radial fans rather than four unrelated corner colors.
  Keep `rect` as the bounding box and `color` as the first vertex color so
  bounds, culling, and pivot code treat it like any other shape.
- Do not grow this package into a widget tree, scene graph, layout framework,
  or generalized UI architecture without explicit design approval and a real
  consumer.

## Documentation and verification

Use `docs/packages/source-layout.md`, the rendering chapter,
`TestGameVisual.cpp`, and UI token tests. Assert geometry, layer, coordinate
space, token ordering, batch bounds, and payload values with MockBackend. Use a
live visual smoke for layout, clipping, blending, text, and resize behavior.

Update this file only for durable primitive-composition rules.

Scene-bound geometry/model mutations must advance the attachment bounds
revision. Scene emission relies on the graph's camera acceptance; direct
MeshVisual drawables retain their own camera test. Owners invalidate snapshots
before changing borrowed content used by already extracted items.
