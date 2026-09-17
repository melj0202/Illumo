# Rendering subsystem guidance

This file specializes the repository `AGENTS.md` for
`Illumo/Source/Rendering/` and applies to all rendering children.

## Scope and boundaries

Rendering defines backend-neutral command tokens, queueing, handles, assets,
drawables, camera/style values, and the backend contract. Concrete OpenGL
execution belongs only in `OpenGL/`; headless semantic execution belongs in
`Mock/`. Game and Services consume this boundary without importing GL types.

## Required invariants

- Production drawables implement `AppendCommands(Renderer*)`. Immediate
  `Draw()` is a compatibility fallback for tests or incomplete stubs, not a
  second production path.
- `Renderer` depends on `IBackend`, not `GLBackend` or OpenGL headers. Backend
  creation is composed outside this directory's neutral core.
- `RenderCommand` is a tagged value contract. Every token kind must be handled
  consistently by real and mock backends or rejected explicitly.
- Pointer payloads for mesh, texture, uniform, or text updates are borrowed.
  Their storage must remain valid and unchanged until synchronous
  `SubmitCommandQueue` returns.
- `CommandQueue` reserves 2,048 commands and grows to a configurable 65,536
  default ceiling. Preserve deterministic
  order and explicit overflow behavior; never write past capacity or silently
  claim a dropped frame was complete.
- `Scene` is a non-owning ordered drawable list rebuilt each frame. It does not
  own drawables and must not become a retained scene graph or ECS.
- Directional shadows are one Renderer-owned pass before color rendering. The
  first camera-visible World caster selects the light; the Renderer retains
  caster descriptors, extrudes the camera frustum toward that light by the
  configured caster distance, fits the shared map to intersecting bounds, and
  rejects depth emission outside the relevant volume or fitted light frustum.
  Invalid camera reconstruction fails open to all casters. Drawables and
  SceneGraph attachments must not allocate private per-object shadow maps or
  clear depth once per object.
- Camera and shadow bounds tests are Renderer-owned and conservative. Invalid
  frusta, matrices, or bounds disable the corresponding rejection rather than
  risking missing geometry.
- The separate `SceneGraph` may enter `Scene` as one drawable. Its borrowed
  `ISceneRenderAttachment` values receive resolved world transforms and emit
  tokens only; do not make Rendering own graph nodes or graph lifetime.
- Resource handles are backend-neutral identifiers. The owning backend
  registry controls concrete resource lifetime; enrollment is rare and
  per-frame work emits commands rather than recreating resources.
- `AssetManager` reference-counts immutable static mesh assets and owns their
  backend-handle lifecycle. `MeshVisual` mesh-asset bindings are non-owning;
  the acquiring owner must retain the manager reference until every visual is
  detached. Procedural dynamic geometry remains visual-owned.
- Keep coordinate space and layer explicit. Overlay chrome uses a screen ortho
  `uMVP`; world objects use the camera view-projection. Do not mix those
  matrices or texture-space sampling implicitly.
- Renderer and backend calls are main-thread affine with the active graphics
  context unless an authorized design introduces synchronization.

## Compatibility and errors

Standalone capture is an authorized composition entry through `FrameCapture`.
Keep it main-thread-affine and separate from the application host. Its producer
owns content through synchronous submission and destroys renderer-bound content
before returning. Strict capture rejects immediate fallback; render attachments
must report required-resource emission failures with `Renderer::reportFrameError`.
Readback occurs before swap and preserves pixel-pack state; diagnostics survive
queue reset. Do not imply that a successful submission proves useful pixels.

Treat command layout, handle semantics, ordering, capacity, blend/state
behavior, and shader-visible data as cross-backend contracts. Validate sizes,
handles, and resource types before concrete API calls. Startup or submission
failure must be observable and must not leave a partially usable backend.

## Documentation and verification

- `docs/packages/source-layout.md`
- `docs/latex/sections/05-rendering-current.tex`
- renderer decisions in `docs/architecture-consensus.md`
- MockBackend, renderer end-to-end, UI token, GameVisual, and MeshVisual tests

Use MockBackend for deterministic contract tests. Use a live OpenGL smoke for
context, shader, state, upload, and visual behavior; headless success is not
pixel validation. Update this file only for durable Rendering contracts.
