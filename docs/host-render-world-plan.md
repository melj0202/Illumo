# Host rendering: execution tracker

Design and decisions: `docs/host-render-world-design.md` (authorized
2026-09-26). Branch `host-render-world`. Stop for owner review after M1.

## Status

| M | State | Notes |
|---|---|---|
| M0 | done, `e18d6867` | Results in design section 12 |
| M1 | done, `e18d6867` | RHI instancing, per-frame uniform block, instanced shaders |
| M2 | done, `e18d6867` | `RecordedCommandList`, `ExecuteList`, engine `RenderWorld`, parity and bench |
| M3 | done, `e18d6867` | Schema v6 world operations, `HostRender`, host binding, `GuestRenderWorld` |
| M4 | done, uncommitted | `SceneInstance` on `IRenderWorld`; IllMeshViewer, IllEd, game 3D mode; `hostRenderWorld=0` rollback |
| M5 | done, uncommitted | `VisualStore` over `GameVisual` item edits, opacity, prepare/emit split |
| M6 | done, uncommitted | Frame v7 visual operations and compositions, `WasmVisuals`, font atlases |
| M7 | done, uncommitted | Guest `GameVisual` proxy, compositions from the recorder, churn fallback, `hostVisuals=0` rollback |
| M8-M9 | not started | M8: canvas quad, skybox in the environment, IllEd overlays; M9: retire guest tokens |

## M6: visual operations and composition on the wire (done)

Objective: a guest can create, edit and destroy host `VisualStore` visuals
and say where each one draws, between its remaining batches and its render
world, in the main frame and in surface windows. No product uses it until
M7.

Changes:

1. Frame schema v7 (still gated by `HostRender`), which adds two trailing
   sections after the world operations:
   - **Visual operations** (`GuestVisualOp`): `Create`, `Destroy`, `Set`
     (space, layer, transform, opacity, clip, visibility), `ItemSet` (index
     plus one item record), `ItemRemove` (first, count) and `ItemsClear`.
   - **Item records:** a shape, a sprite with a texture id, or a text run
     with a font id, an optional heavy font id and blend, size, stretch and
     UTF-8 bytes. Each carries its own drawOrder, transform and visibility.
     Custom styles don't travel; items use the built-in Shape and Sprite
     styles.
   - **Compositions:** one per target. Target 0 is the main frame; a
     surface id targets that surface. Each is either `same` or a list of
     entries: `Visual id`, `Batches first count`, or `World`. Entries are
     ordered World layer first. Batch ranges must cover the target's
     batches exactly once, in order.
   - **Budgets:** visual operations share the 65,536 operation quota,
     visuals 4,096, items 65,536 per visual, text 1 MiB per frame, and
     composition entries 16,384 per target.
2. Fonts become guest resources the host can resolve. `LoadFont` already
   hands the guest a host atlas. It gains a `GuestResourceKind::Font` id,
   which text items reference, and the host keeps the `Font` alive while
   referenced.
3. Host binding in `WasmFrameRenderer`:
   - **Validation:** visual operations are planned against the live store
     plus this frame's effects, like world operations. Unknown or duplicate
     ids, bad indices, missing textures or fonts and budget breaches reject
     the frame, and nothing is applied.
   - **Drawing:** with a main composition, the World and UI layer drawables
     walk its entries. Visuals go through `VisualStore::append`, with the
     frame's logical size and guest camera as the frame override. Batch
     ranges draw as today, and `World` places the render world. Without a
     composition, drawing is unchanged.
   - **Surfaces:** a surface with a composition replays its entries
     offscreen. The host marks it changed when its list or a listed
     visual's geometry changed.
4. `GameVisual::prepareFrame` takes an optional frame override (logical
   size and world MVP), so host visuals match what the guest's own
   `GameVisual` would draw.
5. Tests:
   - `Illumo.Wasm.VisualFrameValidation`: round trip, truncation, deny
     cases and quotas; v6 packets stay valid.
   - `Illumo.Wasm.VisualOperations`: host binding, transactional
     rejection, composition order against the world and batches,
     surfaces, `same`, and retirement.
   - SDK contract for encoding.

Exit: the new tests and the full suite pass; v1-v6 guests unchanged.
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
- 2026-09-26: owner said commit and proceed; M0-M3 committed as
  `e18d6867`. M4 implemented: products draw scene meshes as host render
  world instances. IllMeshViewer at 2,000 cubes went from 85 FPS (M0) to
  757, with a 6.5 KB packet. Results, parity and the one deviation are in
  design section 12. The two package tests that counted guest `DrawIndexed`
  calls now count instanced draws inside executed lists.
- 2026-09-26: owner said proceed; M5 implemented. It wraps `GameVisual`
  instead of moving its internals (see design section 12). Full Release suite
  669 of 669 with M4. M6 planned above.
- 2026-09-26: M6 implemented as planned, with two refinements. First,
  compositions carry the logical size that pixel-space visuals lay out in.
  Second, a composition lasts only for its own frame. Results are in design
  section 12.
- 2026-09-26: M7 implemented. Idle IllEd went from 9.25 KB to 0.27 KB per
  frame and IllMeshViewer from 6.55 KB to 0.77 KB. The game menus stay on
  batches through the churn fallback: their chrome animates nearly every item
  each frame. The allocation gate caught decode discarding the v7 buffers every
  frame, and `GameVisual` compaction; both are fixed. Full Release suite 671 of
  671.
