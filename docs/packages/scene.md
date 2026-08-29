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
neither attachments nor backend resources. Its iterative render stack is
retained private scratch that grows with the graph; v1 does not cache a flattened
render list.

V1 is main-thread affine and deliberately excludes ECS components, update
callbacks, serialization, prefabs, bounds/culling, physics, scripting, and
retained UI. The current IllumoGame cellular-automata path does not instantiate
a graph except the opt-in `render3dTest` diagnostic, which attaches `MeshVisual`
hosts to scene nodes. The complete contract and rollout boundary are in
`../scene-graph-v1-design.md`; formal decisions D-E8 and D-R21 record the
architecture.
