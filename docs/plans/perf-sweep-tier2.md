# Performance sweep, tier 2 (execution plan)

Tier 2 performance plan (`.agent/PLANS.md`). Status: complete (2026-10-03);
item 7 closed by measurement without a change. Follows `docs/plans/perf-sweep-tier1.md`.

## Objective

Eight items from the 2026-10-03 performance sweep, each with a measurable or
testable end state and no change in observable output:

1. **Canvas, near zoom.** Delta and changed-chunk resamples at one cell per
   texel write texels from the cell states they already hold instead of a
   hash lookup per cell.
2. **Canvas, far zoom.** Incremental overview resamples visit only the chunks
   under the marked texels instead of every occupied chunk in view.
3. **RenderWorld uploads.** A bucket whose instances, transforms and culling
   inputs did not change skips its per-frame instance rebuild and upload.
4. **GameVisual tokens.** Adjacent batches stop re-emitting style, mesh and
   per-visual uniforms; a typical batch emits texture plus draw.
5. **Uniform names.** Backends stop hashing uniform name strings on every
   uniform token.
6. **Scene BVH.** Transform-only changes refit the query BVH bounds instead
   of rebuilding it.
7. **Explicit-backend dynamic data.** Vulkan and Direct3D 12 write per-frame
   dynamic buffer data into a mapped per-frame ring instead of staging copies
   with mid-frame barriers.
8. **Pipeline and shader caches.** Vulkan saves and loads its
   `VkPipelineCache`; Direct3D 12 caches compiled shader bytecode (and
   pipeline state) on disk.

## Constraints

- Rendering output stays identical across OpenGL, Vulkan and Direct3D 12
  (`IllumoGpuTests` parity, captures); simulation and canvas output stay
  identical (existing CanvasView tests compare texels).
- No new third-party dependencies, no exceptions, explicit ownership.
- Allocation gates (`PackageFrameAllocations`, `SteadyTextAllocations`,
  `TestSceneAllocations`) stay green.
- Each item lands with tests or measurements; architecture docs and the
  decision log follow.

## Milestones

1. Canvas items 1-2.
2. GameVisual tokens (4).
3. Uniform names (5).
4. RenderWorld uploads (3).
5. BVH refit (6).
6. Explicit-backend dynamic data (7).
7. Pipeline and shader caches (8).
8. Full Release build and labelled suite, GPU parity, tidy on touched files,
   docs and decision log.

## Validation log

Native Release, Visual Studio 2026, reference machine (NVIDIA), 2026-10-03.

1. **Canvas (D-P37).** `IllumoGame.CanvasInf.IncrementalResampleParity`
   compares incremental delta and in-place revisions with a fresh refill each
   generation at zoom 1 and 0.1: identical. Zoom-1 soup update 0.07 -> 0.055
   ms; settled far-zoom world with one active patch 0.22 -> 0.034 ms; broad
   blinkers (fallback walk) unchanged at about 0.23 ms. All 57 canvas tests
   pass.
2. **GameVisual tokens (D-R39).** `Illumo.GameVisual.SpriteBatches`: four
   same-style batches send one `SetShader`, one `SetMesh`, one `uMVP` and one
   sampler uniform.
3. **Uniform keys (D-R37).** `Illumo.Vulkan.UniformKeys`: garbage keys,
   one resolve per name (66 for the suite's table), arrays, malformed and
   oversized subscripts and samplers through `GpuProgramUniforms`, token size.
   The first version put an 8-byte key (hash plus element) in every
   `CmdUniform*`, which grew `RenderCommand` to 80 bytes and failed
   `PublicHeaderSmoke`; the key is now one 32-bit hash of the whole name in
   the token's former tail padding, and the explicit backends cache slot and
   element together.
4. **RenderWorld uploads (D-R39).** `Illumo.RenderWorld.UnchangedBucketsSkipUploads`
   (moves rewrite for two frames, tint, visibility, camera). RenderWorld bench,
   static world: 99 -> 1.1 us (2,000 instances), 2,640 -> 1.0 us (50,000).
5. **BVH refit (D-E41).** Scene query bench `move_and_ray_us` with a parity
   check against a rebuild: 636 -> 29 us (1,000 nodes), 7,896 -> 312 us
   (10,000). All SceneGraph tests pass.
6. **Explicit-backend dynamic data: no change.** Program default-block
   uniforms already go to mapped per-frame memory on Vulkan and Direct3D 12.
   Vulkan already sends buffer and texture updates for resources not yet used
   in the frame to a separate upload command buffer. Temporary counters on
   Vulkan (since removed) measured render-pass breaks per frame: game menu 1.0,
   IllEd 0, mesh viewer 0, and no inline transfers in any of them. The one
   break is the menu's 256x186 blur target being sampled after it was drawn,
   a layout transition a ring would not remove. Direct3D 12 records updates
   inline with state transitions but has no render pass to break. A ring
   rewrite was not justified by the evidence.
7. **Pipeline and shader caches (D-R38).** `Illumo.Vulkan.ShaderCache` (program
   round trip, truncation, hit, damaged entry, blob store and load). Launch
   time to first frame, warm cache: Direct3D 12 about 1,020 -> 570 ms, Vulkan
   about 865 -> 765 ms; OpenGL 770-830 ms (unaffected).
8. **Full gate.** Release build with the labelled suite: 758/758 (one
   `Illumo.Wasm.GameFiles` failure during the build's run did not recur alone,
   in three parallel repeats or in a full rerun). `IllumoGpu` 7/7, including
   `Illumo.Gpu.BackendParity` on OpenGL, Vulkan and Direct3D 12. Live captures
   of the game and the mesh viewer on all three backends; mesh viewer images
   match OpenGL (game title captures differ run to run on one backend, so
   they are not compared).
