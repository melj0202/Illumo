# Host rendering: execution tracker

Design and decisions: `docs/host-render-world-design.md` (authorized
2026-09-26). Branch `host-render-world`. Stop for owner review after M1.

## Status

| M | State | Notes |
|---|---|---|
| M0 | done, uncommitted | Results in design section 12 |
| M1 | done, uncommitted | RHI instancing, per-frame uniform block, instanced shaders |
| M2 | done, uncommitted | `RecordedCommandList`, `ExecuteList`, engine `RenderWorld`, parity and bench |
| M3 | done, uncommitted | Schema v6 world operations, `HostRender`, host binding, `GuestRenderWorld` |
| M4-M9 | not started | M4: `SceneInstance` on `IRenderWorld`; IllMeshViewer, IllEd, game 3D mode |

## M1: RHI instancing and the per-frame uniform block

Objective: the backend-neutral token path can draw many instances of one
mesh in one call, reading per-instance data from a GPU buffer and camera and
shadow state from one per-frame uniform block. Existing drawables are
untouched; nothing uses the new path in products until M2.

Changes:

1. `ResourceHandle.h`: `BufferHandle` (slot + generation, like the others).
2. `IBackend.h`: `BufferUsage { Instance, Uniform }`, and
   `CreateBuffer(usage, capacityBytes)`, `DestroyBuffer` and `IsBufferValid`.
   All three are virtual with safe defaults (invalid/false), so
   `GuestRecordingBackend` and the test backends need no change.
3. `RenderCommand.h`, with new types appended:
   - `WriteBuffer` (handle, offset, size, data borrowed until submission)
   - `BindUniformBuffer` (handle, binding point)
   - `SetInstanceStream` (handle, offset, `InstanceLayout`)
   - `DrawIndexedInstanced` (index count, first index, instance count)

   `InstanceLayout::LitModelTint` is a 144-byte stride: model mat4,
   previous model mat4, tint vec4.
4. `Renderer`:
   - `enrollBuffer`/`destroyBuffer` and the push helpers.
   - A renderer-owned `FrameUniforms` std140 block (view-projection,
     previous view-projection, light space, shadow state). It is written and
     bound at binding 0 once per `RenderScene`, after shadow fitting and
     before the shadow pass.
   - Built-in styles `LitMeshInstanced` and `ShadowDepthInstanced`.
5. OpenGL:
   - A buffer resource table in `GLBackend`.
   - `GLDevice` executes the four tokens with validation. Instance
     attributes use locations 4-12 with divisor 1; mesh attributes keep 0-3.
   - `GLShaderProgram` binds a linked program's `FrameUniforms` block to
     point 0.
6. Shaders: `mesh_lit_instanced_vertex.glsl`, `mesh_lit_instanced_frag.glsl`
   and `shadow_depth_instanced_vertex.glsl`, matching `mesh_lit_*` math.
7. `MockBackend`: buffer resources plus token validation (live handle,
   usage, range).
8. Tests:
   - Headless `Illumo.*` cases for the frame block and instanced token
     validation.
   - A new GPU test executable, which renders the same cubes through
     `LitMesh` and `LitMeshInstanced` offscreen and compares pixels. It is
     labelled `IllumoGpu` rather than `IllumoWorkspace`, because headless
     machines have no GL context.

Exit: the new headless tests pass; the GPU parity test passes on this
machine; the full Release `IllumoWorkspace` suite and package tests are
unchanged.

## Log

- 2026-09-26: M0 landed (see design section 12). M1 started.
- 2026-09-26: M1 implemented as planned, with one deviation. The frame
  block is published lazily: the first `useFrameUniforms()` call in a
  `RenderScene` writes it. Existing token streams, and every test that pins
  them, are unchanged, and frames without instanced draws pay nothing.
  Results are in design section 12.
- 2026-09-26: owner said proceed; M2 implemented. Deviations:
  - Instance and material ids are caller-chosen in the native API too,
    matching the planned ABI.
  - Each bucket has separate color and shadow instance buffers, each
    orphaned per frame, instead of one buffer with two regions.
  - Membership changes patch counts and never re-record; only buffer growth,
    material and environment changes do.
  - A backend reports a failed token inside a list as a frame error and runs
    the rest of the list, rather than skipping it.

  Canonical docs updated: decision log D-E30 and D-R28, pointers on D-R24
  and D-R25, `architecture-consensus.md`, `AGENTS.md`,
  `Illumo/Source/Rendering/AGENTS.md`, LaTeX `05-rendering-current`, PDF
  rebuilt.
- 2026-09-26: owner said proceed; M3 implemented. Deviations and results are
  in design section 12. Docs updated: `architecture-consensus.md` (frame
  schema v6) and the `wasm-game-runtime-design.md` section 7 note.
