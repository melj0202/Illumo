# Illumo Scene

`SceneGraph` is Illumo's persistent, product-agnostic world hierarchy. It owns
node storage and exposes graph-ID-plus-slot-plus-generation
`SceneNodeHandle` values rather than node pointers.

Each node has one parent, ordered children, a local transform, a cached world
transform, local enabled/visible flags, and at most one borrowed
`ISceneRenderAttachment`. Reparenting rejects cycles, subtree destruction
invalidates every affected handle, and root/sibling insertion order defines
deterministic iterative traversal.

The graph is also one token-path `DrawableBase`. When it is placed in the World
layer of the per-frame `Rendering::Scene`, it resolves dirty transforms and
calls visible, enabled attachments in hierarchy pre-order. Attachments receive
the node world matrix and append commands through `Renderer`; the graph owns
neither attachments nor backend resources. An attachment may optionally report
finite local `AxisAlignedBounds3`. The graph exposes the corresponding world
bounds and skips color emission only when a valid bound is wholly outside the
active camera frustum. Unknown or malformed bounds fail open, and a culled
parent attachment never prunes child traversal.

One attachment can emit multiple visual items; child nodes provide independent
transforms and subtree state. Its iterative render stack is retained private
scratch that grows with the graph; v1 does not cache a flattened render list or
subtree bounds.

V1 is main-thread affine and deliberately excludes ECS components, update
callbacks, serialization, prefabs, spatial indices, physics, scripting, and
retained UI. IllEd is the first product consumer: it rebuilds a graph from an
editor-owned `.ilsc` document (D-E10) and never asks SceneGraph to serialize
itself. IllumoGame never uses the graph for CA cell storage. Its opt-in
`render3dTest` diagnostic separately attaches world `MeshVisual` hosts to scene
nodes. The complete contract and rollout boundary are in
`../scene-graph-v1-design.md`; formal decisions D-E8, D-E10, D-E11, and D-R21
record the hierarchy, interchange file, bounded extraction, and world look.
