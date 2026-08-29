# Scene graph v1 design and execution plan

**Status:** Implemented library slice; unused by the current IllumoGame CA path.

**Supersedes:** D-E4 only where it treated every retained scene hierarchy as
out of scope. The existing per-frame `Scene` drawable list remains intact and
continues to implement D-E4's render-extraction role.

## Objective and measurable end state

Add a product-agnostic persistent scene hierarchy to the `Illumo` static
library for future downstream projects. The hierarchy must provide stable
generational node identity, explicit ownership, deterministic parent/child
behavior, cached world transforms with dirty propagation, subtree
enabled/visible state, and a token-render attachment contract.

The end state is measurable when:

- consumers can create, reparent, inspect, and destroy nodes without exposed
  node pointers;
- stale handles are rejected after node destruction and slot reuse;
- cycles are rejected and failed operations leave the graph unchanged;
- local transforms resolve to deterministic parent-composed world transforms;
- disabled or invisible subtrees do not emit render attachment commands;
- render attachments receive their node's resolved world transform in stable
  hierarchy order through the existing renderer token path;
- the current IllumoGame canvas, UI, renderer, module lifecycle, and frame list
  compile and behave without migration.

## Current-state evidence and problem

The live `Illumo/Include/Illumo/Rendering/Scene.h` type is a small, non-owning
layered frame list. `Illumo::render` clears it, modules contribute drawable
pointers, and `Renderer::RenderScene` walks those pointers and submits tokens.
That remains an appropriate render-extraction boundary.

The archived `archive/dead-engine/SceneObject.h` is not suitable for reuse. It
exposes raw parent/child pointers, does not own nodes, does not prevent cycles,
does not propagate transforms, and is not integrated with token rendering.

The earlier Illumo/IllumoGame design explicitly deferred a scene graph because
the cellular-automata product did not need one. The owner has now clarified a
broader objective: Illumo is intended to become a reusable foundation for
future projects, with the simulator becoming a downstream consumer. This
authorization changes the product direction while preserving the current
simulator's efficient canvas representation.

## Scope and non-goals

### In scope

- Public `SceneNodeHandle` and `SceneGraph` contracts under
  `Illumo/Include/Illumo/Scene/`, plus the narrow renderer-owned
  `ISceneRenderAttachment` contract under `Illumo/Rendering/`.
- Private node storage and traversal under `Illumo/Source/Scene/`.
- Scene-owned node lifetime with borrowed render attachments.
- Parent/child hierarchy, root ordering, cycle rejection, subtree destruction,
  local/world transforms, dirty propagation, enabled/visible subtree state,
  and deterministic pre-order token emission.
- Headless contract tests, public-header consumption, CMake wiring, package
  maps, canonical architecture, formal decision record, and durable guidance.

### Explicit non-goals

- No ECS, component registry, serialization, prefabs, editor object model,
  physics, animation system, scripting, networking, or persistence format.
- No frustum culling, bounds hierarchy, octree/BVH, render graph, offscreen
  pass, material system, model loader, or new graphics backend.
- No change to `IModule`, `IllumoContext`, `RenderCommand`, backend handles,
  CSim simulation storage, `CanvasView`, or UI ownership.
  `Renderer::FrameContext` and `GameVisual` reuse of that context are
  supporting token-path seams so attachments can share the active window and
  world MVP.
- No rename or breaking removal of the existing `Rendering::Scene`-path type in
  this milestone. Documentation calls it the frame render list where ambiguity
  matters.
- No automatic ownership of render attachments. A node borrows its attachment,
  and the consumer must detach or keep it alive while the node can render.

## Constraints and invariants

- Illumo remains independent of Game and Rulesets.
- The scene subsystem is backend-neutral and emits only through `Renderer`.
- Runtime scene mutation and token emission are main-thread affine in v1.
- Traversal and subtree operations are iterative; repository policy forbids
  recursion.
- Node slot zero and generation zero are invalid. Releasing a live node advances
  its generation before the slot can be reused.
- A node has at most one parent and one borrowed render attachment.
- Handles carry graph identity in addition to slot and generation so a live
  handle from another graph is rejected deterministically.
- Sibling/root insertion order defines deterministic pre-order rendering.
- Destroying a node destroys its complete subtree, invalidating every handle in
  that subtree. No child promotion occurs implicitly.
- Reparenting appends the moved node after the destination's existing children,
  or after existing roots when detached.
- Existing frame-list layers remain the composition boundary. A `SceneGraph`
  is normally added as one World drawable; current UI is not migrated into it.

## Proposed design

### Public contracts

`SceneNodeHandle` is a strong graph-ID-plus-slot-plus-generation value with
equality and an `isValid` check. It does not expose a node address. Each graph
receives a process-unique nonzero ID at construction, allowing operations to
reject foreign handles even when their slot and generation happen to match.

`ISceneRenderAttachment` is a non-owning token-emission contract:

```cpp
virtual void appendSceneCommands(Renderer* renderer,
                                 const Matrix4& worldTransform) = 0;
```

It intentionally has no immediate-mode fallback. Implementations append
backend-neutral commands and may use the supplied world transform directly or
compose it with camera state. The attachment must outlive any render traversal
that can reach it.

`MeshVisual` implements this attachment contract as the world mesh/sprite
consumer (D-R21; formerly `DebugDraw3D`). Direct drawing uses identity node
world; scene attachment composes `cameraVP * nodeWorld * local` (billboard
optional) without mutating the drawable. Overlay UI is not migrated into the
graph.

`SceneGraph` owns nodes and derives from `DrawableBase`. Its
`AppendCommands(Renderer*)` updates dirty world transforms and visits effective
enabled/visible nodes in deterministic hierarchy pre-order. Each attachment is
called with the resolved world matrix. `AppendCommands` always handles the
token path and therefore returns true; a hidden graph emits nothing.

The graph exposes creation, subtree destruction, validity, parent/child
inspection, reparenting, local/world transform access, enabled/visible state,
render attachment access, node count, explicit world-transform update, and
token emission. Copy and move are deleted because handles identify storage
owned by one graph instance.

### Private storage and algorithms

The implementation uses an indexed slot vector with slot zero reserved. Each
slot stores generation, liveness, parent handle, ordered child handles, local
and cached world matrices, dirty state, local enabled/visible flags, and a
borrowed attachment pointer. A free-slot stack reuses released slots only after
generation advancement. A separate ordered root list preserves deterministic
root traversal independently of slot reuse.

Reparenting validates both handles, walks the proposed parent's ancestor chain
to reject cycles, removes the node from its old sibling/root list, appends it to
the destination list, and iteratively marks its subtree dirty.

World-transform update performs an iterative root-to-leaf traversal. A root's
world matrix is its local matrix. A child recomputes as `parentWorld * local`
when either it or an ancestor is dirty. Cached matrices remain queryable after
an explicit update; `getWorldTransform` performs that update before returning.

Subtree destruction first detaches the root from its parent/root list, gathers
the subtree iteratively in pre-order, then releases it in reverse order so
children are invalidated before parents. Every released slot clears borrowed
pointers and advances its generation.

### Ownership, lifetime, threading, errors, and platforms

- `SceneGraph` owns node slots, hierarchy links, and cached transforms.
- Consumers own `ISceneRenderAttachment` implementations. The graph never
  deletes an attachment.
- Handles are graph-local. A process-unique graph ID makes foreign handles fail
  validation before slot/generation lookup.
- All methods are main-thread affine. No internal locks are introduced.
- Storage and hierarchy mutation is rejected while render attachments are
  being visited. Graph-level drawable visibility may change for the next
  extraction without invalidating traversal storage.
- Invalid/stale handles, cycles, invalid parent handles, and out-of-range child
  access return false or an invalid handle without partial mutation.
- The implementation is standard C++23 plus the already-public GLM-backed
  `Matrix4`; it adds no platform-specific code or dependency.

## Alternatives considered

- **Replace the frame list with a graph:** rejected because persistent world
  organization and per-frame render extraction solve different problems.
- **Restore archived `SceneObject`:** rejected because raw pointers, ambiguous
  ownership, recursive search, and absent renderer integration violate the new
  contract.
- **Use `shared_ptr` nodes:** rejected because ownership cycles and distributed
  lifetime obscure scene authority. Generational handles make ownership and
  stale-reference behavior explicit.
- **Add an ECS first:** rejected because hierarchy and transform propagation do
  not require a general component architecture.
- **Store `DrawableBase*` directly:** rejected because existing drawables have
  no contract for receiving the graph-resolved world transform.
- **Add culling/spatial acceleration now:** deferred until a consuming project
  supplies bounds, camera, and workload requirements.

## Compatibility and migration

This milestone is additive. Existing consumers need no source changes. The
existing frame-list `Scene`, `IModule::DispatchDrawables`, and renderer API keep
their names and behavior. Future projects may instantiate a `SceneGraph`, add
it to the World frame-list layer, and attach token emitters to nodes.

The root guidance and canonical architecture will be updated to distinguish
the persistent `SceneGraph` from the per-frame render list. D-E4 remains useful
history for the removal of the old dead graph but receives a new decision that
authorizes the separate v1 subsystem.

## Ordered implementation milestones

1. Record this design and the pre-change baseline.
2. Add public scene contracts, private graph implementation, and the
   `DebugDraw3D` render-attachment adapter; compile the standalone Illumo
   library.
3. Add deterministic scene graph tests and public-header smoke coverage.
4. Synchronize CMake, README, package maps, canonical architecture, LaTeX,
   formal decisions, and durable guidance.
5. Format modified C++ and run focused contract checks, standalone and workspace
   Release builds/tests, documentation generation, diff checks, and scope
   review. Record exact results and limitations here.

## Verification strategy

- Focused tests cover creation/order, hierarchy/world transforms, reparenting,
  cycle rejection, enabled/visible propagation, render extraction, subtree
  destruction, stale handles, slot reuse, and invalid operations.
- The public-header smoke includes and instantiates the new contracts using only
  `Illumo::Illumo` usage requirements.
- Build the standalone Illumo Release target, then the workspace Release
  targets. Run exact scene tests followed by the `Illumo` and
  `IllumoWorkspace` CTest labels when the checkout permits.
- Run `clang-format` on every modified C++ header/source and `git diff --check`.
- Rebuild `docs/output/illumo.pdf` because canonical and LaTeX sources change.
- No GUI smoke is required: this milestone changes no OpenGL implementation,
  shader, window, input, or native platform behavior. Headless tests prove
  hierarchy and token-call ordering, not pixels.
- Coverage and sanitizers are desirable for the new lifetime code. If the
  current missing TestSupport tree blocks the configured suite/coverage, use a
  disposable standalone contract harness and report the remaining gap without
  restoring unrelated files.

## Rollback and containment

The subsystem is additive and has no production consumer in this milestone.
Containment is therefore straightforward: remove the new Scene public/source
files and their CMake/test/documentation entries. No persisted data, generated
asset, external API implementation, simulator state, or runtime configuration
is migrated.

## Open questions and decisions

No question blocks v1. Multiple attachments per node, culling bounds,
serialization, update callbacks, and renaming the frame list are intentionally
deferred until a real downstream consumer demonstrates their required
contracts.

## Validation record

### Pre-change baseline

- [x] `Illumo::TestSupport` is present and is the registered headless harness
      for `IllumoTests`.
- [x] `TestSceneGraph.cpp` is compiled into `IllumoTests` and registers
      `Illumo.SceneGraph.HandlesAndLifetime`,
      `Illumo.SceneGraph.HierarchyAndTransforms`, and
      `Illumo.SceneGraph.RenderExtraction`.
- [x] Hitch simulation/view work landed separately in `62b28e25`; this slice
      does not compose `SceneGraph` into Engine or IllumoGame.

### Implementation milestones

- [x] Public contracts and private implementation complete.
- [x] Focused scene graph contracts registered through the live TestSupport
      runner.
- [x] Public-header consumer smoke includes Scene headers and creates a node.
- [x] Canonical documentation synchronized; generated PDFs are not regenerated
      for this landing.
- [x] Formatting, diff checks, and final scope review pass.

### Final verification

- `cmake --build build --config Release --target IllumoTests
  IllumoPublicHeaderSmoke IllumoGame`: passed.
- Exact cases passed:
  `Illumo.SceneGraph.HandlesAndLifetime`,
  `Illumo.SceneGraph.HierarchyAndTransforms`,
  `Illumo.SceneGraph.RenderExtraction`,
  `Illumo.Renderer.FrameContext`,
  `Illumo.DebugDraw3D.DynamicMeshReuse`, and
  `Illumo.GameVisual.Shapes`.
- `build/Illumo/Release/IllumoPublicHeaderSmoke.exe`: passed.
- `ctest --test-dir build -C Release -L Illumo --output-on-failure`: 211/211
  passed. The `-L Illumo` regex also selected `IllumoGame` and
  `IllumoWorkspace` labels (96 library + 115 product cases).
- `docs/build.ps1` was not run; generated PDFs were not regenerated.
- Repository-required `clang-format` ran on every modified C++ source/header.

No GUI smoke was run because this additive subsystem has no current product
composition and changes no OpenGL execution, shader, window, input, or native
dialog behavior. No performance claim is made: v1 retains hierarchy and cached
transforms, but visible attachments still traverse and emit draw commands each
frame. A downstream workload should benchmark later culling or extraction
caching proposals before they are added.
