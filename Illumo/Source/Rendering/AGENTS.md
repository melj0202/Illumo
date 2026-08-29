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
- `CommandQueue` has a fixed 2,048-command capacity. Preserve deterministic
  order and explicit overflow behavior; never write past capacity or silently
  claim a dropped frame was complete.
- `Scene` is a non-owning ordered drawable list rebuilt each frame. It does not
  own drawables and must not become a retained scene graph or ECS.
- The separate `SceneGraph` may enter `Scene` as one drawable. Its borrowed
  `ISceneRenderAttachment` values receive resolved world transforms and emit
  tokens only; do not make Rendering own graph nodes or graph lifetime.
- Resource handles are backend-neutral identifiers. The owning backend
  registry controls concrete resource lifetime; enrollment is rare and
  per-frame work emits commands rather than recreating resources.
- Keep coordinate space and layer explicit. Overlay chrome uses a screen ortho
  `uMVP`; world objects use the camera view-projection. Do not mix those
  matrices or texture-space sampling implicitly.
- Renderer and backend calls are main-thread affine with the active graphics
  context unless an authorized design introduces synchronization.

## Compatibility and errors

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
