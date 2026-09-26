# Host rendering: host-owned render objects, instancing and recorded command lists

**Status:** authorized by the owner 2026-09-26 (Tier 3). M0-M7 done; M8
(canvas, skybox and overlay migration) next. Baseline `release/v26.09` at `bb113a9c`.
**Tracker:** `docs/host-render-world-plan.md`.
**Supersedes (on acceptance):** D-R24 (instance aggregation deferred), the
recorded-payload clause of D-R25, and the guest-side presentation model of
`docs/wasm-game-runtime-design.md` section 7 (lines 312-322: guests
tessellate, order and batch their own draws). Guests keep presentation
*decisions*.
**Precedent:** D-E14 (retained static meshes), D-E16 (retained dynamic
meshes), D-E28 (capability pattern), D-R27 (surface `same` revisions).
**New decisions (on acceptance):**
- D-E30: guest/host render ownership, frame schema v6 and the `HostRender`
  capability.
- D-R28: engine `RenderWorld`, instancing and recorded lists.
- D-R29: host-retained 2D visuals and composition lists.

---

## 1. Problem

Guests rebuild and copy their whole world every frame. Measured on
2026-09-26, Release `IllumoRuntime` at `bb113a9c`, IllMeshViewer, a grid of
`.ilsc` primitive cubes all in view, shadows on, vsync off, 1,500 timed
frames after 200 warm-up (`--bench-frames`), medians:

| Cubes | FPS | Frame | Guest frame | Host accept | Packet | Batches |
|---|---|---|---|---|---|---|
| 0 | 1,769 | 0.56 ms | 0.11 ms | 0.01 ms | 8 KB | 51 |
| 500 | 159 | 6.2 ms | 4.0 ms | 1.1 ms | 865 KB | 549 |
| 2,000 | 41 | 23.9 ms | 15.9 ms | 4.7 ms | 3.4 MB | 2,049 |
| 8,000 | — | — | 13.6 ms | — | 0.1 KB | 0 (nothing drawn) |

At 2,000 cubes about 20.6 of 23.9 ms is guest recording plus host
validation and copying. Host token building, GL submission and presentation
together take about 3 ms.

Causes, from the current code:

- Every cube already shares one guest mesh (`SceneInstance::primitiveMesh`,
  `Illumo/Source/Content/SceneInstance.cpp:600-613`). It is 1,296 B of
  vertices, below `RetainedMeshBytes` (64 KiB,
  `IllumoGuest/Include/IllumoGuest/RecordingBackend.h:106`). So
  `GuestRecordingBackend` inlines the whole mesh into every draw
  (`IllumoGuest/Source/RecordingBackend.cpp:446-460, 1038-1122`).
- Each lit object costs about 22 guest tokens (`MeshVisual.cpp:1143-1253`)
  plus 3 more for the shadow pass. The guest encodes them into a batch, and
  the host validates, re-uploads and re-translates them into about 22 host
  tokens (`Illumo/Source/Wasm/WasmFrameRenderer.cpp:947-966, 1017-1070`).
  Every token does a `glUniform*` call with a string-hashed location lookup
  (`GLShaderProgram.h:39-56`).
- Three failure modes appear with large worlds:
  1. At about 2,600-3,000 lit objects the guest's 65,536-token queue
     overflows. `takeFrame` throws and the guest sends an empty frame, logged
     once as "Frame dropped" (`IllumoGuest/Source/ModuleApplication.cpp:334-344`).
     This is the 8,000-cube row above.
  2. A frame with more than 4,096 batches is rejected by the host decoder
     (`Frame.h:603`). `WasmGameModule::fail` then **retires the whole guest**
     (`WasmGameModule.cpp:311-335, 457-458`). Retiring a guest that sends a
     malformed or over-quota packet is intended. But the guest SDK never
     checked the quota before sending, so a legitimately large world could
     close its app.
  3. Every `MeshVisual` registers its own shadow caster, and the guest
     silently keeps only the first 256 (`RecordingBackend.cpp:89-91`).

The current architecture can't meet the owner's rule, stated 2026-09-26:
**a guest manages its own game state and makes presentation decisions; every
render and media object lives on the native host, and the guest manages
those objects by calling the host.** Audio already follows it (D-E28), and
meshes and textures partly do (D-E14, D-E16). World objects, materials,
lights and per-object draw state still travel as copies every frame.

## 2. Owner decisions

| # | Question | Recommendation | Owner answer |
|---|---|---|---|
| O1 | Guest/host render ownership rule (section 1) | Adopt as a closed decision (D-E30) | Stated by owner 2026-09-26 |
| O2 | Scope: world objects only, with 2D/UI in a later design (Stage B) | Stage it | **Everything in one design** (2026-09-26). 2D and UI are section 6.9 |
| O3 | Land milestone M0 (quick fixes, no ABI change) first | Yes | **Yes** (2026-09-26) |
| O4 | Recorded lists copied into the frame queue as spans, or executed directly | Span copy | **Execute directly** (2026-09-26). See section 6.5 |
| O5 | Immediate-mode product UI: a guest `GameVisual` proxy diffs rebuilds, or products move to a retained UI API | Proxy diffing, with no product changes | **Proxy diffing** (2026-09-26) |
| O6 | Authorize this design (M1-M9) | Yes | **Authorized** (2026-09-26). Stop for review after M1 |

## 3. End state

- A product-agnostic engine component, `RenderWorld` (Illumo core,
  `Illumo/Include/Illumo/Rendering/RenderWorld.h`). It owns persistent world
  render objects: instances, materials and one environment. It draws them
  through the existing token path as one World-layer drawable.
- Native consumers (test oracles, tools) use `RenderWorld` directly. Each
  WASM guest gets its own `RenderWorld` on the host. The guest drives it
  through frame schema v6 operations behind a new `HostRender` capability,
  using the same `IRenderWorld` interface in guest and native code.
- Identical mesh+material instances draw as one instanced draw per pass.
  Per-instance data lives in host memory and is streamed to the GPU from
  there, never from the guest.
- Each draw bucket's command tokens are recorded once, when its membership
  or material changes. Each frame the host culls, writes the visible
  instance stream, patches two fields per draw (instance count, stream
  buffer) and executes the recorded lists in place through `ExecuteList`.
- 2D and UI (shapes, sprites, text, the CA canvas quad, cursors, overlays,
  panel surfaces) are host-retained **visuals** (section 6.9). A guest
  visual is a proxy that sends item changes, not geometry. The host does
  text layout from its own fonts, tessellation, painter ordering and
  batching, with each visual's batches in a recorded list.
- Per frame, a guest with a static world and static UI sends only its
  camera and an unchanged composition marker. Moving objects send their
  transforms, and animated UI sends the items or visual properties that
  changed.
- In-tree guests emit no `RenderCommand` tokens and no batches. The batch
  path remains for frame versions 1-5 and older packages.
- Target at 2,000 cubes: guest frame under 0.5 ms and packet under 16 KB
  when static; host world emission under 0.3 ms; frame time bound by the
  GPU. Large worlds (at least 50,000 instances) draw correctly under
  explicit, reported budgets, never silently. Static IllEd or IllMeshViewer
  UI sends under 1 KB per frame.

## 4. Non-goals

- Moving product presentation *decisions* to the host. Layout, hit
  testing, animation clocks and springs, CA fade and LOD, and painter order
  stay in the guest. The host executes them.
- Complex text shaping (kerning, ligatures, non-ASCII). Text stays ASCII
  32-126 as today, just laid out on the host.
- Rewriting product UI code into a retained style. Guest proxies diff
  immediate-mode rebuilds (section 6.9, O5).
- A host copy of the guest's hierarchy. `SceneGraph` stays in the guest as
  product state; the host world is flat (D-E8 rationale, and
  `wasm-game-runtime-design.md:342-346`).
- GPU-driven rendering (indirect draws, compute culling). The context is
  OpenGL 3.3 core (`RenderWindow.cpp:77-79`).
- Moving rendering off the main thread, or the GL context move (D-R25).
- Automatic instancing of arbitrary `MeshVisual` token streams inside
  `Renderer`.
- Transparent-object sorting beyond today's behavior (section 6.6).

## 5. Invariants that remain true

- Drawables emit `RenderCommand` tokens; only `IBackend` touches GL. Game
  and ruleset code never issue GL calls.
- Resources use generational slot handles. A recorded list that names a
  retired handle fails backend validation instead of touching stale memory.
- Every pointer a token carries stays valid until synchronous submission
  returns. Recorded lists own their payload storage, which outlives
  submission.
- `CommandQueue` keeps its 2,048 reserve, configurable 65,536 ceiling and
  high-water/rejection metrics. Recorded lists get their own ceiling and
  metrics (section 6.5), so no limit is removed and no rejection is hidden.
- One shared directional shadow pass per frame, fitted after caster
  collection. No per-object shadow framebuffers.
- `Rendering::Scene` remains the non-owning per-frame drawable list. The
  `RenderWorld` drawable is re-added each frame like `SceneGraphDrawable`.
  Only its contents persist.
- Frame acceptance stays transactional. A trap or rejected packet discards
  every operation in it, with no partial commit.
- The host stays product-agnostic, and guests address only their own ids.
- Rendering, scene mutation and GL work stay main-thread affine.

## 6. Design

### 6.1 Objects

| Object | Owner | Contents | Lifetime |
|---|---|---|---|
| Mesh | host (exists, D-E14/D-E16) | vertices, indices, local bounds, style | create, write, release |
| Material (new) | host | style (LitMesh first), base texture or none, tint, receive/cast shadow, blend, depth test | create, update, destroy |
| Instance (new) | host | mesh + index range, material, world matrix, tint override, visible, layer mask | create, update, destroy |
| Environment (new, one per world) | host | sun direction/color/intensity, ambient, shadow settings, skybox cubemap and tint | set |
| Camera | guest value | view and projection, sent per frame | per frame |

Instance and material ids are **guest-chosen** 32-bit values inside
host-granted ranges, validated on the host, as audio ids already are
(D-E28). So a guest can create and draw an object in the same frame with no
completion round trip. Meshes and textures keep their current
request/completion flow, because their bytes stream asynchronously.

### 6.2 ABI: frame schema v6 and the `HostRender` capability

- New capability `GuestCapability::HostRender`, bit 12, bumping
  `KnownCapabilities` (`IllumoGuest/Include/IllumoGuest/Protocol.h:14-34,
  69`). It covers world objects (6.3) and 2D visuals (6.9). The host grants
  it whenever it renders. Guests without it keep the batch path.
- `GuestFrame::Version = 6` appends one ordered **operations** section after
  surfaces (`Frame.h:155-160, 690-721`), followed by the composition lists
  of section 6.9. World operations:
  - `MaterialCreate/Update/Destroy`
  - `InstanceCreate/Update/Destroy`
  - `InstanceTransforms`, a packed run of id + 3x4 matrix for the common
    moving-objects case
  - `EnvironmentSet`

  The v2 camera record stays and becomes the world camera. The visual
  operations are listed in 6.9.
- Validation happens before anything is applied: known ids, live mesh and
  material references, finite matrices, index ranges inside the mesh, and
  per-frame and total budgets. A rejected section rejects the frame.
- Budgets, host-granted and reported like other limits: instances (default
  ceiling 262,144), materials (4,096), operations per frame (65,536), world
  operation bytes per frame (8 MiB).
- The host keeps retiring a guest whose packet is malformed or over quota.
  The guest SDK checks quotas before sending (M0), so a well-behaved guest
  drops and reports an over-quota frame instead of being retired.
- The v1-v5 decoders and the batch path stay. During migration, batches and
  operations may share a frame: world batches draw after the `RenderWorld`
  drawable (section 6.6), and UI batches draw in their composition position
  (section 6.9).
- New decoder and deny tests cover every operation, as `AGENTS.md` requires
  for schema changes.

### 6.3 Engine `RenderWorld`

- **Storage.** Flat structure-of-arrays with a slot and generation per
  instance: world matrix, previous world matrix (for motion blur), tint,
  flags, world AABB and bucket index. Updating an instance recomputes its
  AABB from the mesh's local bounds and marks its bucket dirty.
- **Buckets.** A bucket is keyed by (mesh, index range, material). Opaque
  lit instances with equal keys share a bucket.
- **Per frame** (`AppendCommands` / `AppendShadowCommands` /
  shadow-caster collection):
  1. Collect shadow casters from caster-flagged instances (fixes failure
     mode 3). Bounds come from the instance AABBs, so fitting sees every
     caster.
  2. Cull each bucket against the camera frustum, and separately against
     shadow relevance (`isShadowCasterRelevant`), into two visible-index
     lists. This is a linear loop over the SoA AABBs. A BVH is added only if
     measurement shows the loop matters; `SceneGraph`'s derived BVH is the
     reuse candidate.
  3. Write visible instances' per-instance records into that bucket's
     streaming buffer, orphaned each frame. Each record holds the model,
     previous model and tint, 144 B.
  4. Patch each bucket's recorded list (instance count, stream binding) and
     append one `ExecuteList` token per list.
- **Instance data never comes from the guest per frame.** Transform updates
  land in host SoA, and the GPU stream is written from host memory.

### 6.4 Instancing and RHI additions (OpenGL 3.3 core)

- A new `DrawIndexedInstanced` token (index count, first index, instance
  count), executed with `glDrawElementsInstanced`. The existing
  `DrawInstanced` is non-indexed only (`GLDevice.cpp:396-409`).
- A new `SetInstanceStream` token (buffer handle, layout). It binds a
  per-instance vertex buffer onto the bound mesh's VAO with
  `glVertexAttribDivisor(1)`: four vec4s for the model matrix, four for the
  previous model matrix, and one tint. Without base-instance support (GL
  4.2), each bucket owns its stream buffer and draws from offset 0.
- A new streaming buffer resource (`InstanceBufferHandle`, generational)
  with orphan-and-write updates through `UpdateBuffer`.
- **A per-frame uniform block.** `Renderer` owns one std140 uniform buffer
  bound at binding 0. It holds view-projection, previous view-projection,
  the light-space matrix, light direction and color, ambient, shadow bias,
  slope, normal offset, PCF, the active flag and the motion-blur
  parameters. It is written once per frame after shadow fitting. The shadow
  map binds to a fixed texture unit once per frame. Recorded lists therefore
  never contain camera- or light-dependent values. That is exactly what
  makes recording possible (these were the invalidating values found in
  `Renderer.cpp:294-461` and `MeshVisual.cpp:1103-1104`).
- New shader variants `lit_instanced` and `shadow_depth_instanced`. They
  read the model from attributes and the camera from the block, and match
  the current lit shader's math for pixel parity.
- `MockBackend` gains the same tokens and resources, with validation, so
  headless tests cover them.

### 6.5 Recorded command lists

- `RecordedCommandList` is a host-owned, move-only token vector plus an arena for
  `Mat4` payloads (`RenderCommand.h:137`). It carries a revision.
- Each bucket records `SetPipelineState`, `SetShader`, `SetMesh`, material
  uniforms and textures, `SetInstanceStream` and `DrawIndexedInstanced`
  once. It re-records when any of these change:
  - bucket membership or material
  - mesh replacement or release
  - shader reload (`AssetManager` hot reload in Debug)
  - backend reset
- Invalidation is by revision stamp and checked before every copy.
- **Direct execution (O4).** A new `ExecuteList` token carries a pointer to
  a host-owned `RecordedCommandList`. The frame queue holds one such token
  per list, in draw order. The backend executes that list's tokens in
  place, with the same validation and state caching as queued tokens. The
  recorded tokens are never copied. The list outlives submission, so the
  pointer-lifetime invariant holds.
- **Ordering.** A list runs exactly where its `ExecuteList` token sits, so
  layer and painter order are unchanged. Lists can't contain `ExecuteList`
  tokens (no nesting, consistent with the no-recursion rule).
- **Ceiling and metrics.** Each list has its own recorded-token ceiling
  (default 65,536) that applies when it is recorded. Recording past it
  fails that list and reports it, never silently. Frame statistics gain
  "recorded lists executed" and "recorded tokens executed", next to the
  queue's high-water and rejection counts. The queue keeps its 2,048
  reserve, 65,536 ceiling and metrics unchanged for per-frame tokens.
- **Errors.** A token that fails backend validation inside a list reports
  through the frame error with the list's identity, and the rest of that
  list is skipped for the frame. The list is then marked for re-recording.
- **Per-frame patching.** Before submission, the owner writes each draw's
  instance count and stream binding into the list in place. The list is
  host-owned and main-thread only, and no other state changes.
- To the owner's proposal ("build the queue once on add/remove"): token
  recording, uniform packing and per-object emission happen once on add,
  remove or change. Each frame the host still culls, writes the visible
  instance stream, patches counts and issues the GL draws. The GL draws
  can't be avoided, because GL keeps no draw list across frames.

### 6.6 Ordering, blending and overlays

- Opaque buckets draw in bucket order, which doesn't affect correctness.
- Blended materials skip instancing. They draw as single-instance buckets
  in creation order, matching today's per-draw order.
- The guest's remaining world batches (IllEd grid, gizmo and selection
  overlays) draw after the `RenderWorld` drawable in the same World layer,
  keeping today's depth and overlay behavior.

### 6.7 Guest SDK and product integration

- `IRenderWorld` (engine header, shared by native and guest code):
  `createMaterial`, `updateMaterial`, `destroyMaterial`, `createInstance`,
  `setTransform`, `setTint`, `setVisible`, `destroyInstance` and
  `setEnvironment`. The native implementation is `RenderWorld`. The guest
  implementation `GuestRenderWorld` queues operations into the next frame.
- `IllumoContext` gains a non-owning `renderWorld` pointer, frozen after
  startup like its other pointers. 2D visuals need no new context member,
  because the guest `GameVisual` proxy reaches the host through the SDK.
- `SceneInstance` (Content) creates instances and materials through
  `IRenderWorld` when one is available, instead of one `MeshVisual` per
  primitive or mesh component (`SceneInstance.cpp:703-730`). It pushes only
  transforms whose `SceneGraph` revision changed. `SceneGraph` stays the
  product hierarchy and the pick/raycast structure, with IllEd pick proxies
  unchanged. Without `IRenderWorld`, the existing `MeshVisual` path remains
  for tests and fallback.
- `GuestRecordingBackend` keeps serving UI and remaining batches. M0 also
  retains every shaped static mesh regardless of size.

### 6.8 Ownership, lifetime, errors and threading

- Each guest's `RenderWorld` is created on start and destroyed when the
  guest retires. Retirement revokes the world immediately. GPU destruction
  waits for frame leases, as retained meshes do today
  (`WasmResourceTable.h:71-78`).
- Instances borrow meshes and materials by reference count. Releasing a
  mesh that instances still use is rejected (`Busy`), not deferred silently.
- All work is main-thread. No new threads.
- Linux compiles the same code (GL 3.3), within the existing
  support-claim limits.

### 6.9 2D and UI: host-retained visuals

**Today.** From the 2026-09-26 survey:
- Fonts are already rasterized on the host. `LoadFont` returns a host atlas
  texture plus glyph tables (`WasmRenderServices.cpp:246-303`).
- Everything else runs in the guest:
  - `GameVisual` item storage, (`drawOrder`, sequence) sorting,
    tessellation and adjacent-batch merging (`GameVisual.cpp:1060-1175`)
  - ASCII text layout, one quad per glyph (`GameVisual.cpp:960-1045`)
  - per-batch tokens every frame (`GameVisual.cpp:1389-1410`)
- Tool UIs clear and recompose every frame, for example
  `EditorModulePanels.cpp:208-218` and `EditorInspector.cpp:1084-1091`.
- Game UI animates continuously: `GuiMenuShell` springs, and the canvas
  chrome idle breath from `bd6bde94`.
- The recorder byte-diffs mesh updates, so unchanged rebuilds cost guest CPU
  and batch records but no geometry. IllEd sends 89 batches, 14 KB per
  frame, while idle.

**Objects.**

| Object | Contents | Lifetime |
|---|---|---|
| Visual (new) | layer (World/Ui), space (Pixels/World), `Transform2D` + scale, opacity, clip rect, surface target | create, set properties, destroy |
| Item (new) | inside a visual: filled or outline rect, line, ellipse, triangle, gradient quad/rect/triangle, sprite (texture + uv or region), text run (font id, string, size, color, weight blend, stretch); drawOrder; per-item `Transform2D`, tint, opacity | set by index, remove range, clear |
| Composition list (new) | per layer and per surface: the ordered visual ids drawn this frame, interleaved with world/`RenderWorld` entries | per frame, or `same` |
| Font | existing host atlas + glyph table | existing `LoadFont` |
| Texture | existing, including CA canvas dirty-rect writes | existing |

**Host side.**
- `GameVisual`'s item store, sort, tessellation, text layout and batching
  move behind an engine `VisualStore`. It is the same code, compiled into
  the host and driven by operations instead of method calls.
- Each visual rebuilds its dynamic geometry only when its items change,
  exactly as `GameVisual` does today (dirty flag, `GameVisual.cpp:1315-1343`).
- Each visual records its batches into a `RecordedCommandList` (6.5), and
  `ExecuteList` runs it in composition order. The only per-frame patch is
  the visual's matrix: projection × visual transform × opacity uniform.
  So moving, fading or tilting a whole panel costs one property operation
  and no re-tessellation.
- Text layout uses `FontLayout` and `FontWeightRamp`. That code is already
  compiled into both the guest and the native build, so host placement
  matches the guest's `measureText` exactly, and guests keep their glyph
  tables for measuring and hit testing.

**Guest side (O5).**
- `GameVisual` in the guest SDK becomes a proxy with the same public API.
  Products and `GuiKit`, `GLString`, `SplashText`, `CommandLine`, cursors
  and overlays don't change.
- The proxy keeps a shadow of the items last sent. When a visual is cleared
  and recomposed (immediate-mode UI), it compares items by position and
  content, then sends `ItemSet` only for changed indices, `ItemRemove` for a
  shortened tail, and nothing when identical.
- Guest CPU still runs layout each frame. That is a presentation decision
  and stays guest-side. Tessellation, text quads, sorting, batching and
  token recording leave the guest.

**Visual operations** (in the v6 operations section):
- `VisualCreate/Destroy`
- `VisualSet` (transform, scale, opacity, clip, space, layer, surface)
- `ItemSet` (index + item record)
- `ItemRemove` (range)
- `ItemsClear`

Budgets: visuals (4,096), items per visual (65,536, matching `GameVisual`'s
quad ceiling, `GameVisual.h:24-25`), text bytes per frame (1 MiB) and
operations per frame (shared with world operations).

**Composition.**
- Painter order across visuals is today's per-frame `Scene` order
  (`DispatchDrawables`). The guest keeps building its `Scene` every frame.
  Proxies and the `RenderWorld` proxy enter it as usual.
- At submission the guest emits one composition list per layer and per
  surface: visual ids in order, plus markers for the world and any
  remaining batch ranges. If the list is unchanged since the last frame it
  sends `same`, the same revision scheme as v5 surfaces
  (`PanelSurfaces.cpp:398-436`).
- The host replays composition lists in order through `ExecuteList`.

**Panel surfaces (D-UI7).**
- A surface's content becomes a composition list targeting that surface.
- The host marks a surface changed when its list or any listed visual
  changed, then replays offscreen as `WasmPanelWindows` does today
  (`WasmPanelWindows.cpp:318-369`). The guest no longer records or
  byte-compares surface batches.

**CA canvas (`CanvasView`).**
- The texture and its dirty-rect writes already follow the ownership rule:
  they are resource updates, not per-frame copies. They stay as they are.
- The canvas quad becomes a sprite item in a World-space visual that
  references the canvas texture.
- Fade and LOD decisions stay in the guest (presentation decisions).
- The skybox becomes part of the world environment (6.1).

**Remaining guest drawables.** IllEd grid, gizmo and selection overlays,
`ProfilerOverlay` and `FileTreeOverlay` are `GameVisual` or `MeshVisual`
users. They migrate to visuals and to `RenderWorld` instances with dynamic
meshes. After that, `GuestRecordingBackend` serves only packages built
against frame versions 1-5.

## 7. Compatibility and migration

- Hosts keep accepting frame versions 1-6. In-tree guests move to v6
  milestone by milestone, and the batch path remains for UI and old guests.
- `.ilsc`, `.ilpk` and save formats don't change.
- Native test oracles (`IllEdCore`, `IllMeshViewerCore`) gain a
  `RenderWorld` option. Their existing `MeshVisual` tests keep running
  until M9 migrates them.
- Native `GameVisual` keeps its public API and emits through the same
  `VisualStore` (6.9). Native products and tests don't change, and native
  and guest drawing share one implementation.

## 8. Alternatives considered

| Alternative | Why not |
|---|---|
| Only drop `RetainedMeshBytes` (M0 alone) | Removes the 2.5 MB/frame copy but keeps 22 guest tokens, a batch and 22 host tokens per object per frame. Fails the ownership rule. Kept as M0. |
| Serialize retained `RenderCommand` streams across the ABI | Exposes backend internals to untrusted guests. Already rejected (`wasm-game-runtime-design.md:316`). |
| Mirror the guest `SceneGraph` hierarchy on the host | A second world model. Hierarchy is product state, and flat world matrices suffice. |
| Automatic instancing inside `Renderer` | Heuristic matching of token streams is fragile and still pays per-object emission. |
| Indirect/multi-draw GPU culling | Needs GL 4.3. Out of scope. |
| Copy recorded spans into the frame queue each frame | Simpler, but copies every recorded token every frame. The owner chose direct execution (O4). `ExecuteList` keeps ordering, ceiling and metrics without the copy. |
| Scene-graph v2b attachment payload arenas | Records every attachment every frame, and is gated on the GL-context move. The world store here records per bucket, on change. |
| Require products to adopt a retained UI API instead of proxy diffing (O5) | Rewrites every tool and game screen. Diffing gives the same wire savings with no product change, and a product can still move to retained style later. |
| Keep 2D in the guest and only send `same` for unchanged batch lists (extend v5) | The guest still tessellates, sorts, records tokens and compares every frame, and the host still owns nothing. Fails the ownership rule. |

## 9. Milestones and rollback

| M | Content | Exit | Rollback |
|---|---|---|---|
| M0 | Retain all shaped static meshes (inline until uploaded); guest SDK checks frame quotas before sending and drops with a logged reason; overflow shadow casters merge into the nearest compatible caster; contract tests | No inline world geometry at 2,000 cubes; over-quota frames never reach the host; package tests unchanged | Revert commit; no ABI change |
| M1 | RHI: `DrawIndexedInstanced`, `SetInstanceStream`, instance buffers, per-frame uniform block, instanced shaders, `MockBackend` parity | Token tests; GL capture of an instanced test scene | Additive; unused until M2 |
| M2 | Engine `RenderWorld` (store, buckets, culling, streams, recorded lists, `ExecuteList`) with native benches versus `MeshVisual` | Parity captures versus `MeshVisual`; native bench at 2,000 and 50,000 instances | Additive; not wired to products |
| M3 | Schema v6 world operations + `HostRender` capability + host binding + `GuestRenderWorld`; decoder and deny tests | `Illumo.Wasm.*` pass; package bench unchanged for v5 guests | Capability not granted, so guests stay on batches |
| M4 | `SceneInstance` on `IRenderWorld`; IllMeshViewer and IllEd migrate; game 3D mode | Section 10 world targets; package tests; captures match | Product flag reverts to `MeshVisual` path |
| M5 | Engine `VisualStore`: `GameVisual` item store, sort, tessellation, text layout and batching behind operations, with recorded lists per visual; native `GameVisual` drives it | Existing `GameVisual`, `GLString`, `GuiKit` and `CommandLine` token tests unchanged; parity captures of the menus and tool UIs | Additive refactor; native behavior pinned by existing tests |
| M6 | v6 visual operations + composition lists (main and surfaces) + host binding; decoder and deny tests | `Illumo.Wasm.*` pass; composition `same` round trips | Capability not granted |
| M7 | Guest `GameVisual` proxy with item diffing; IllEd, IllMeshViewer and IllumoGame UI migrate with no product edits; panel surfaces from composition | Idle tool UI under 1 KB/frame; package tests; panel-window tests; UI captures match | SDK flag reverts to recording |
| M8 | `CanvasView` quad as a World-space visual; skybox in the environment; IllEd grid, gizmo and overlays, profiler and file-tree overlays migrate | `IllumoGame.Wasm.GamePackage` (canvas, 3D mode); captures match | Per-drawable revert |
| M9 | In-tree guests emit no tokens or batches; `GuestRecordingBackend` kept for v1-v5 packages only; docs, decision log, `AGENTS.md` | Full Release build and `IllumoWorkspace` suite; tidy; ASan; coverage gate | Per-milestone commits |

## 10. Verification

- **Benchmarks.** The section 1 IllMeshViewer cube runs (0, 500, 2,000,
  8,000 and 50,000), recording FPS, frame p50/p95, guest update and frame
  times, accept time and packet bytes, before and after each milestone. Plus
  native `RenderWorld` benches next to `Illumo.SceneGraph.Bench.Visibility`.
  For 2D, the same `--bench-frames` measurements with the IllEd and
  IllMeshViewer UIs idle, the IllumoGame menus animating, and the CA canvas
  running (`IllumoGame.Wasm.PackageBench`).
- **Parity.** `--capture` PNGs of `render3d-test.ilsc` and the cube scenes,
  old path versus new, with and without shadows and motion blur. Compared
  with `tools/verify_capture.py` tolerances, since bit-identical
  cross-driver output is not promised.
- **Tests.**
  - Decoder and deny tests for every v6 operation and budget.
  - `MockBackend` token tests for recording, invalidation (membership,
    material, mesh release, shader reload) and instance counts.
  - Proxy diff tests: identical rebuilds send nothing, one changed item
    sends one `ItemSet`, a shortened tail sends one `ItemRemove`.
  - Host text placement matches guest `measureText`.
  - Package tests `IllMeshViewer.Wasm.ScenePackage`, `IllEd.Wasm.Package`,
    `IllumoGame.Wasm.GamePackage` and the panel-window tests.
- **Gates.** Full Release build, `IllumoWorkspace` CTest, `IllumoTidy`, the
  Debug ASan profile and the 85% coverage gate.

## 11. Risks and open questions

- **Motion-blur parity.** Instanced shading needs each instance's previous
  model matrix. The design streams it (144 B/instance), and a cheaper
  "unchanged" encoding is possible later.
- **Pixel parity** between the instanced shaders and `MeshVisual`'s lit
  shader, especially the normal matrix. Mitigation: shared GLSL include,
  and captures in M2.
- **Streaming cost** at 50,000+ visible instances is about 7 MB/frame of
  buffer writes. Acceptable for the targets. A persistent-resident
  alternative needs GL 4.4.
- **Animated UI still sends per-frame changes.** Springs and clocks that
  reshape geometry (liquid selection, drawer cards) send their changed
  items every frame. Whole-panel motion and fades cost one property
  operation. This is inherent to the ownership rule, and still far below
  today's full batch lists.
- **Guest layout CPU stays.** Immediate-mode products still rebuild layout
  and pay the proxy diff each frame. Moving hot screens to retained style
  is a per-product follow-up.
- **`VisualStore` refactor risk.** `GameVisual` is widely used. M5 keeps its
  API and pins behavior with the existing token tests before any ABI work.
- **Found during research, not decided here:** `IllEd` doesn't load a
  scene passed with `--open` in the live runtime. It showed an empty
  "Untitled" document for every scene tried, while `IllEd.Wasm.Package`
  passes. This needs its own investigation before M4 relies on IllEd
  benchmarks.

## 12. Validation results

### M0 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - `GuestRecordingBackend` retains every shaped static mesh, drawing small
    ones inline until uploaded.
  - `GuestFrame::exceededLimit` checks the host's count quotas, and
    `GuestModuleApplication::recordFrame` drops an over-quota frame with a
    logged reason.
  - Casters past 256 merge into the nearest compatible caster.
  - New SDK contracts: `staticMeshContract`, `shadowCasterContract`,
    `frameLimitContract`.
- **Tests:** Release. The full `IllumoWorkspace` CTest suite passes, 651 of
  651. So do the 71 cases matching `Wasm|Package`, including
  `Illumo.Wasm.SdkContract` and the three package tests that fail on "Frame
  dropped".
- **Benchmark:** same method as section 1.

  | Cubes | FPS before, after | Frame | Guest frame | Host accept | Packet |
  |---|---|---|---|---|---|
  | 500 | 159, 357 | 6.2, 2.8 ms | 4.0, 1.9 ms | 1.1, 0.11 ms | 865, 162 KB |
  | 2,000 | 41, 85 | 23.9, 11.7 ms | 15.9, 9.0 ms | 4.7, 0.41 ms | 3,396, 583 KB |
  | 8,000 | nothing drawn, nothing drawn | — | 13.6, 26.4 ms | — | — |

- **Still open after M0:**
  - At 2,000 cubes the guest still records about 22 tokens and one
    290-byte batch per cube, which is the 9 ms left in the guest.
  - 8,000 cubes still overflow the guest's 65,536-command ceiling. The drop
    is now reported once ("Frame dropped: Command queue safety ceiling
    exceeded"), but `CommandQueue` also logs its own ceiling error on every
    overflowing frame, which floods the log. That's a follow-up.
  - Removing those per-object costs is M1-M4.
- **Not run for M0:** `IllumoTidy` and the ASan Debug profile.

### M1 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - `BufferHandle`, and `IBackend::CreateBuffer/DestroyBuffer/IsBufferValid`
    with safe defaults.
  - Tokens `WriteBuffer`, `BindUniformBuffer`, `SetInstanceStream` and
    `DrawIndexedInstanced`, with `InstanceLayout::LitModelTint` (144 B:
    model, previous model, tint).
  - Renderer-owned `FrameUniforms` (208 B std140: view-projection, previous
    view-projection, light space, shadow state). It is published lazily by
    `Renderer::useFrameUniforms()`, once per `RenderScene`, at binding
    point 0.
  - Built-in styles `LitMeshInstanced` and `ShadowDepthInstanced`, with
    shaders `mesh_lit_instanced_*` and `shadow_depth_instanced_vertex`.
  - GL buffer table and validated execution. An instanced draw needs a
    stream attached to the bound mesh in the same submission, and never
    more instances than the stream holds.
  - `GLShaderProgram` binds `FrameUniforms` at link.
  - `MockBackend` buffers with usage and range validation.
- **Tests:**
  - New `Illumo.Instancing.FrameUniformsOncePerScene`,
    `.TokensValidateBuffers` and `.StylesRegistered`.
  - New GPU executable `IllumoGpuTests`: `Illumo.Gpu.InstancingParity`,
    label `IllumoGpu`, which skips with code 77 when there's no GL context.
    It draws an 8×8 grid of rotated, non-uniformly scaled lit cubes
    offscreen, per object through `LitMesh` and in one call through
    `LitMeshInstanced`. The images are bit-identical: 20,658 of 65,536
    pixels covered, 0 differing.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`: 655 of 655 pass.
- **Runtime smoke:** the IllMeshViewer 2,000-cube bench is unchanged at 85
  FPS, and the host log shows no shader compile or link errors, so the new
  shaders compile on this machine's GPU.
- **Not covered by M1:**
  - `ShadowDepthInstanced` compiles but hasn't been rendered against the
    per-object shadow pass. That parity check is part of M2's shadow
    captures.
  - `IllumoTidy` and the ASan Debug profile were not run.

### M2 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - `RecordedCommandList`, with a ceiling, owned matrices and nesting
    refused. `Renderer::beginRecording/endRecording` route every push helper
    into a list; `pushExecuteList` and `getRecordedListStats` queue and count
    lists.
  - The `ExecuteList` token runs a list in place in `GLDevice`. The per-token
    switch moved to `executeCommand`. `MockBackend` flattens executed lists
    for inspection.
  - `IRenderWorld` and the engine `RenderWorld`: buckets by mesh range and
    material, blended instances in creation order after opaque ones, camera
    and shadow culling, per-instance previous transforms for motion vectors,
    shadow-caster registration from instance bounds, and a `ShadowDepth`
    rebind after its shadow lists.
- **Tests:**
  - New `Illumo.RenderWorld.BucketsRecordOnce`, `.RejectsInvalidCalls`,
    `.ShadowsAndBlending`, `.Bench` and
    `Illumo.RecordedList.RecordingAndNesting`.
  - New GPU case `Illumo.Gpu.RenderWorldParity`: 26 cubes on a floor with
    low-light PCF shadows, drawn as 26 `MeshVisual`s and as one
    `RenderWorld`. The images are bit-identical: 35,010 pixels covered, 0
    differing. The instanced shadow pass drew all 26, and disabling shadows
    brightens 3,621 pixels, so the shadows are real.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`: 661 of 661 pass.
- **CPU bench** (`Illumo.RenderWorld.Bench`, MockBackend, median of 10
  frames, whole `RenderScene` plus submission):

  | Instances | MeshVisual | RenderWorld |
  |---|---|---|
  | 2,000 | 3.83 ms, 50,017 tokens | 0.11 ms, 30 tokens, 1 recording |
  | 50,000 | overflows the 65,536-token queue | 2.40 ms, 30 tokens |

- **Runtime smoke:** the IllMeshViewer bench is unchanged: 86 FPS at 2,000
  cubes, 1,882 FPS empty, and no log errors. `RenderWorld` isn't used by
  products until M4.
- **Not run:** `IllumoTidy` and the ASan Debug profile.

### M3 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - Frame schema v6: `GuestWorldOperation` and `GuestWorldOp` (8 kinds), a
    trailing operations section with a 65,536 quota
    (`GuestFrameLimits::worldOperations`), and `exceededLimit` coverage.
  - `GuestCapability::HostRender` (bit 12), offered with Render.
  - Host binding in `WasmFrameRenderer`: `planWorld` validates the whole
    sequence against the live world plus this frame's effects, and
    `applyWorld` runs after every fallible step. Each guest's `RenderWorld`
    is dispatched before the world batches. Counters record world operations
    and instances.
  - Guest side: `GuestRecordingBackend::hostMeshState`, `GuestRenderWorld`
    and `IllumoContext::renderWorld`. `GuestModuleApplication` drains
    operations only into frames that will be delivered.
- **Deviations from section 6:**
  - Releasing a mesh that instances use is not refused (`Busy`). Instances
    hold the host mesh alive instead, matching the existing frame-lease rule.
  - Instances cull with the retained mesh's host-computed bounds, so the wire
    format carries none.
  - `InstanceTransform` sends a full 4x4 matrix rather than 3x4.
  - The first version doesn't coalesce repeated transforms of one instance
    within a frame; that's a later optimization if measured.
- **Tests:**
  - New `Illumo.Wasm.WorldFrameValidation`: round trip, truncation, deny
    cases, quota and v5 compatibility.
  - New `Illumo.Wasm.WorldOperations`: host binding with a real retained lit
    mesh, transactional rejection, busy materials, unusable meshes, mesh
    leases and retirement.
  - New SDK `renderWorldContract`: waiting instances, folded transforms,
    host ids, ordering and the quota.
  - The first full run failed `IllumoGame.Wasm.PackageFrameAllocations`,
    because planning allocated eight hash containers every frame. Planning
    now skips frames without operations and reuses its containers.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`: 663 of 663 pass.
- **Runtime smoke** (every guest now granted `HostRender`, frames v6):
  IllMeshViewer at 86 FPS with 2,000 cubes and 1,821 empty; IllumoGame at
  651 FPS; IllEd at 1,627 FPS; no log errors or warnings. No product uses
  the world yet (M4).
- **Not run:** `IllumoTidy` and the ASan Debug profile.

### M4 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - `SceneInstance::setRenderWorld(IRenderWorld*)`. With a world bound,
    every visual backed by a mesh asset becomes a world instance: package
    meshes, and primitives that resolve to a shared primitive mesh. Its
    `MeshVisual` is kept hidden for bounds, picking and the fallback.
    Generated geometry without an asset (placeholders, wire proxies) stays
    on `MeshVisual`.
  - Two shared white materials, shadow-casting and not. Colour travels in
    the instance tint, which reproduces `MeshVisual`'s vertex colour times
    tint.
  - `update()` sends only transforms and visibility that changed since the
    last frame. A reconfigured component replaces its instances; removing a
    node destroys them. Unbinding the world (or destroying the instance)
    removes every instance and both materials.
  - The world environment copies the `MeshVisual` lighting and shadow
    defaults, and follows the scene's light nodes.
  - `nextRenderWorldId()` hands out process-wide ids, so several scenes can
    share one world.
  - Products: IllMeshViewer and the IllumoGame 3D test mode bind
    `IllumoContext::renderWorld`; IllEd binds it through
    `EditorDocument::setRenderWorld`, set at Start and cleared at Exit.
  - Rollback: the guest setting `hostRenderWorld=0` leaves
    `IllumoContext::renderWorld` null, so every product falls back to the
    `MeshVisual` path.
- **Deviation:** transforms still come from a per-frame comparison against
  the last sent matrix, not from `SceneGraph` revisions. It's cheap at
  these sizes (0.37 ms guest update at 2,000 cubes), and revisions can
  replace it if a profile shows the comparison.
- **Tests:**
  - New `Illumo.Content.InstanceRenderWorld`: 20 cubes become instances
    in one bucket while a wire cube draws itself; picking still works
    through the hidden visuals; moves and visibility don't re-record; edits
    replace instances and removal destroys them; unbinding hands drawing
    back to the visuals and rebinding restores every instance.
  - `IllumoGame.Wasm.GamePackage` (3D mode) and
    `IllMeshViewer.Wasm.ScenePackage` now look inside executed recorded
    lists. The scene draws only as instanced render world draws, casts into
    the shared shadow pass, and the package mesh is fetched once.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`, run together with
    M5: 669 of 669 pass.
- **Runtime smoke:**
  - IllumoGame menus at 1,460 FPS, and 1,432 FPS with the 3D test setting.
  - IllEd at 1,749 FPS.
  - No log errors. The 3D scene itself is covered by the package test,
    because the bench stays in the menus.
- **Benchmark** (IllMeshViewer, same method as section 1):

  | Cubes | FPS | Frame p50 | Guest frame | Guest update | Host accept | Packet |
  |---|---|---|---|---|---|---|
  | 0 | 1,874 | 0.53 ms | 0.10 ms | 0.07 ms | 0.01 ms | 6.5 KB |
  | 500 | 1,622 | 0.60 ms | 0.17 ms | 0.14 ms | 0.01 ms | 6.5 KB |
  | 2,000 | 757 | 1.30 ms | 0.41 ms | 0.37 ms | 0.01 ms | 6.5 KB |
  | 8,000 | 177 | 5.53 ms | 1.61 ms | 2.31 ms | 0.01 ms | 6.2 KB |

  Against the section 1 baseline at 2,000 cubes: 41 to 757 FPS, 23.9 to
  1.3 ms, and 3,396 KB to 6.5 KB per frame. 8,000 cubes, which drew
  nothing before, now run at 177 FPS. The packet no longer grows with the
  scene: after the first frames it holds only UI batches.
- **Parity:** IllMeshViewer `--capture` of the 500-cube scene with
  `hostRenderWorld` on and off (vsync on and a 420-frame wait, because the
  viewer's camera easing depends on frame timing). 56-64 pixels differ by
  more than 2, all on cube silhouettes: sub-pixel edge flips from matrices
  multiplied on the GPU instead of the CPU. That's 0.3%, inside the 0.5%
  capture tolerance.

### M5 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - `GameVisual` gains the operations a host store needs:
    - `setItem(index, GameVisualItem)` and `removeItems(first, count)` edit
      items by insertion index. `GameVisualItem` is a variant of the shape,
      sprite and text primitives. Replaced primitives stay hidden until they
      outnumber the live items, then compact.
    - `setOpacity` scales vertex alpha. At 1 the bytes are unchanged.
    - `AppendCommands` is now `prepareFrame` plus `emitDraws`. `prepareFrame`
      rebuilds, grows and uploads directly, and fills a `FrameState`:
      resolution, MVP, clip, enclosing clip and geometry revision.
      `emitDraws` pushes only the clip and batch tokens, so it can run inside
      a recording.
  - `Renderer::getClipState()` exposes the enclosing scissor, because a
    recorded clip push and pop is only valid under the clip it was recorded
    in.
  - `VisualStore` (`Rendering/Primitives/VisualStore.h`): visuals by id,
    each a `GameVisual` plus `VisualProperties` (space, layer, transform,
    opacity, clip, visibility) and one `RecordedCommandList`. `append`
    uploads changed geometry, re-records only when the `FrameState`
    changes, and otherwise queues one `ExecuteList`. It uses the 6.9
    budgets: 4,096 visuals and 65,536 items per visual.
- **Deviations from 6.9:**
  - The store wraps `GameVisual` rather than a new class holding its
    internals. That's the same code with far less churn, and native
    behavior is untouched.
  - The visual transform stays baked into vertices, because existing tests
    pin those vertices and it keeps pixel parity. A transform change
    re-tessellates on the host, which costs host CPU but no wire bytes. A
    GPU-side visual matrix is a later optimization.
- **Tests:**
  - New `Illumo.VisualStore.*`:
    - `RecordsMatchNativeOutput`: items set by record give the same vertex
      bytes and draw tokens as the `add*` calls.
    - `RecordsOnlyOnChange`: unchanged frames replay without uploads; item
      edits, clips and resizes re-record; hidden visuals queue nothing.
    - `EditsAndBudgets`: ids, budgets, kind changes, range removal and
      compaction.
    - `OpacityScalesAlpha`.
    - `Bench`.
  - Every existing `GameVisual`, `GLString`, `GuiKit` and `CommandLine` test
    passes unchanged.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`: 669 of 669 pass.
- **Bench** (`Illumo.VisualStore.Bench`, 200 unchanged 50-item panels,
  median frame): native re-emission 81.5 us (1,200 tokens), store replay
  70.8 us. The host-side saving is small by design. The 2D win comes in
  M6-M7, when the guest stops tessellating, recording and sending batches.

### M6 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - Frame schema v7, still gated by `HostRender`. Two trailing sections
    follow the world operations:
    - **Visual operations** (`GuestVisualOp`: `Create`, `Destroy`, `Set`,
      `ItemSet`, `ItemRemove`, `ItemsClear`) with `GuestVisualItem`
      records: shapes, sprites by texture id, and text by font atlas id with
      an optional heavy font.
    - **Compositions** (`GuestComposition`): one per target, 0 for the main
      frame or a surface id. Each carries the logical size that pixel-space
      visuals lay out in (the guest window over its UI scale, or the
      surface size). Entries are `Visual`, `Batches first count` and
      `World`, or the composition is sent as `same`.
  - **Budgets:** world and visual operations share one 65,536 quota. Text
    is capped at 1 MiB per frame and 64 KiB per item, entries at 16,384
    per target, and compositions at nine per frame. The SDK's
    `exceededLimit` reports all of them.
  - **Fonts:** `WasmFrameRenderer::createFontAtlas` keeps the LoadFont
    `Font` with its atlas texture, and `Font::adoptTextureHandle` makes it
    lay out against that texture. A text item names the atlas texture id,
    so no new resource kind is needed.
  - **Host binding** (`Illumo/Source/Wasm/WasmVisuals`):
    - `plan` checks operations against the live visuals plus this frame's
      effects: ids, item indices, textures, fonts and budgets. It then
      checks every composition against the target's batches: ranges cover
      them once and in order, World-layer entries come first, surfaces take
      only UI visuals, and there is at most one `World`. `apply` runs after
      every fallible step.
    - With a main composition, the World and UI layer drawables walk its
      entries. Visuals go through `VisualStore::append`, with the guest
      frame's logical size and camera as the new
      `GameVisual::FrameOverride`. A `World` entry draws the render world,
      and its shadow work, from the World layer.
    - A surface composition replays offscreen. The surface's reported
      revision now advances when a listed visual changes, not only when its
      batches do.
  - **Deviation:** a composition applies only to the frame that carries it
    (or repeats it with `same`), rather than persisting. A frame without
    one draws exactly as v6 did.
- **Tests:**
  - New `Illumo.Wasm.VisualFrameValidation`: round trip, truncation, 11
    deny cases for operations and 6 for compositions, the shared quotas,
    and v6 compatibility.
  - New `Illumo.Wasm.VisualOperations` covers:
    - Host creation and drawing from one executed list.
    - `same`, and a frame without a composition.
    - Painter order between two batches.
    - 13 rejections that apply nothing.
    - Recovery afterwards.
    - Surface replay only on new content or listed-visual edits.
    - Retirement.
  - Existing trimmed-version tests (v3, v5) now drop the two new counts.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`: 671 of 671 pass.

### M7 (2026-09-26, branch `host-render-world`, uncommitted)

- **Changes:**
  - `IBackend::AppendVisual` and `ForgetVisual` are virtuals whose default
    is off. `GameVisual::AppendCommands` offers the visual whole to its
    backend first. When taken, the guest builds and uploads no geometry
    and records no tokens. A proxied visual tells the backend when it is
    destroyed.
  - `GuestVisualProxies` (guest SDK) diffs each appended visual's items
    and properties against what the host last confirmed. Only changes
    travel: `Create`, `Set`, `ItemSet` per changed index, and `ItemRemove`
    for a shortened tail. Changes are confirmed only when the frame is
    delivered (`commitVisuals`). A dropped frame's changes and destroys are
    sent again, and a destroy whose create never arrived is withheld.
  - `GuestRecordingBackend` records a marker at each taken visual's command
    position. At submission it builds each target's composition (render
    world first, batch ranges, visuals in painter order) and sends `same`
    when it matches the last delivered one. Each composition's logical size
    is the visuals' pixel resolution.
  - `GuestModuleApplication` enables proxying with `HostRender`. It commits
    or drops around delivery and gives world operations what visual
    operations leave of the shared quota. The guest setting `hostVisuals=0`
    is the rollback.
  - **Fallbacks:** a visual records its own tokens when it can't travel:
    custom styles, a texture or font not yet on the host, world space off
    the frame camera, a pixel size different from its target's, or no
    budget left this frame (32,768 operations, 512 KiB of text).
  - **Churn:** a visual that changes at least half its items (32 or more)
    for three consecutive frames records batches instead. It is rechecked
    every 30 frames and returns once settled. The game's menu chrome
    animates about 1,225 of its 1,259 items every frame. As item operations
    that was 159 KB and 747 FPS, against 111 KB and 1,449 FPS as batches.
  - **Host allocation fixes:**
    - Decode reuses operation text and composition entry buffers,
      including across frames that carry fewer of them.
    - The plan scratch is a sorted vector.
    - `GameVisual` reuses free slots of a kind instead of orphaning and
      compacting.
    - Typed `setItem` overloads avoid variant copies.
    - `RecordedCommandList::clear` keeps its matrix slots.
  - Surfaces also replay when a frame writes a texture that one of their
    listed visuals draws.
- **Deviation:** `GameVisual` quad caps (the panels' `GameVisual(256u)`)
  don't travel. The host applies its store's cap, and exceeding a guest cap
  was already a frame error.
- **Tests:**
  - New SDK `visualProxyContract`:
    - Creation and placement after the world.
    - Unchanged frames send nothing plus `same`.
    - A single-item change, and a resend after a drop.
    - A rebuilt, shorter list sends one removal.
    - A custom-styled visual's batch between two proxied visuals.
    - Destroys, including resend after a drop.
    - Disabling.
  - Updated observers in `IllEd.Wasm.Package`, `IllumoGame.Wasm.GamePackage`
    and `IllMeshViewer.Wasm.ScenePackage` walk executed lists.
  - `IllumoGame.Wasm.PackageFrameAllocations` is back to its baseline:
    paused 0, running 1.
  - Full Release `IllumoWorkspace` plus `IllumoGpu`: 671 of 671 pass.
- **Benchmark** (`--bench-frames 300`, idle, proxy on and off):

  | App | Frame bytes | Batches | Guest frame | Host accept | FPS |
  |---|---|---|---|---|---|
  | IllEd | 0.27 KB, 9.25 KB | 1, 60 | 0.034, 0.127 ms | 0.003, 0.009 ms | 1,800, 1,738 |
  | IllMeshViewer | 0.77 KB, 6.55 KB | 2, 40 | 0.032, 0.102 ms | 0.004, 0.008 ms | 1,824, 1,856 |
  | IllumoGame menus | 110.8 KB, 110.8 KB | 29, 29 | 0.205, 0.203 ms | 0.026, 0.025 ms | 1,444, 1,491 |

  Both tool UIs are under the 1 KB/frame target. The game is unchanged:
  its animated chrome churns, and the rest is canvas texture and mesh
  writes.
- **Parity:** captures at frame 200, proxy on against off:
  - IllEd is bit-identical.
  - IllMeshViewer differs in 2 pixels; two runs with it off already
    differ in 20.
  - The game menus animate with time, so its run-to-run noise (272,499
    pixels) swamps any comparison. Its visuals draw through batches anyway
    while they churn.
- **Not run:** `IllumoTidy` and the ASan Debug profile.

## 13. Documentation changes and follow-ups

- On acceptance:
  - Add D-E30, D-R28 and D-R29 to
    `docs/latex/sections/09-design-decision-log.tex`, and mark D-R24 and the
    D-R25 clause superseded.
  - Update `docs/architecture-consensus.md` (frame schema section 5.12, the
    decision table and the deferred list) and
    `docs/wasm-game-runtime-design.md` section 7.
  - Update the LaTeX chapters `05-rendering-current` and
    `06-rendering-target`.
  - Update `AGENTS.md` (sources of truth, the rendering invariants and the
    primitive-composed UI paragraph), `Illumo/Source/Rendering/AGENTS.md`,
    `Illumo/Source/Rendering/Primitives/AGENTS.md` and
    `Illumo/Source/Gui/AGENTS.md`.
- **Follow-ups:**
  - `CommandQueue` logs its ceiling error on every overflowing frame.
  - IllEd `--open` doesn't load (section 11).
  - Retained-style rewrites for UI screens that stay hot after M7.
