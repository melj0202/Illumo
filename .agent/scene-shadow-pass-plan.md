# Scene-level directional shadow pass

## Objective and measurable end state

Replace `MeshVisual`'s per-object shadow maps with one renderer-owned depth
pass per rendered scene. A frame with multiple visible shadow-enabled meshes
must allocate/use one shared depth target, clear it once, draw every caster
before any color draw samples it, and give every receiver the same light-space
matrix and depth texture.

## Current-state evidence

- `MeshVisual::appendCommandsWithWorld` currently creates a private depth FBO,
  clears it, draws only that visual's triangle mesh, restores the active target,
  and immediately draws the visual's color pass.
- `Renderer::RenderScene` already owns frame extraction and ordered scene-layer
  execution.
- `SceneGraph` is one World drawable and supplies resolved world transforms to
  borrowed `ISceneRenderAttachment` values.
- Canonical renderer documentation requires backend-neutral tokens, retained
  `SceneGraph` ownership boundaries, and main-thread-affine renderer resources.
- `docs/output/illumo.pdf` is absent in this worktree; the canonical Markdown
  and LaTeX sources are available and will be updated directly.

## Scope and non-goals

In scope:

- one shared directional shadow map and depth pass for visible World meshes;
- bounds fitted to the complete visible caster set rather than the origin;
- direct `MeshVisual` drawables and `MeshVisual` scene attachments;
- compatibility for existing `MeshVisual` lighting/shadow tuning setters;
- deterministic MockBackend coverage and synchronized architecture docs.

Out of scope:

- cascaded, point, or spot-light shadows;
- a render graph, material system, spatial culling structure, or new backend;
- changing shaders beyond the existing shared depth-map contract;
- interactive OpenGL acceptance without separate user authorization.

## Constraints and invariants

- All work remains on the existing token path and main thread.
- `Scene` stays a non-owning per-frame list; `SceneGraph` continues to own node
  transforms and only borrows attachments.
- Resource creation/destruction remains behind `Renderer`/`IBackend` handles.
- Existing public `MeshVisual` setters remain source compatible.
- Disabled/invisible meshes neither cast nor receive shadows.
- UI and Debug layers do not enter the world shadow pass.

## Proposed design and alternatives

Add two optional token-extraction hooks to `DrawableBase` and
`ISceneRenderAttachment`: caster collection and depth-command emission.
`SceneGraph` runs those hooks through its existing iterative, visibility-aware
transform traversal. `MeshVisual` prepares its retained mesh once, contributes
its transformed local triangle bounds and shadow tuning during collection,
then emits only its depth draw during the shared pass.

`Renderer` accumulates all caster bounds, chooses the first visible caster's
normalized light direction deterministically, takes the maximum requested map
size/minimum radius/light distance, fits a square orthographic projection to
the combined light-space bounds, owns/reuses one depth framebuffer, and exposes
a read-only per-frame shadow context to the color pass. Per-receiver bias,
slope, normal offset, and PCF settings remain on `MeshVisual`.

Rejected alternatives:

- Retain private maps and append other objects into each map: still N maps and
  N complete passes.
- Add a general render graph: disproportionate to one required scene pass.
- Make `SceneGraph` own shadow resources: direct World drawables would remain
  outside the pass and graph ownership would leak into rendering policy.

## Public contracts and compatibility

The drawable and attachment interfaces gain default no-op shadow hooks, so
existing implementations remain source compatible. `MeshVisual` setters and
getters remain available. Their projection settings now contribute to the one
scene pass instead of configuring private per-object resources. When multiple
casters differ, frame-list order selects the light direction and maximum
quality/coverage values are used for the remaining scalar projection settings.

## Ownership, lifetime, threading, errors, and platforms

`Renderer` owns the shared framebuffer and destroys it before backend shutdown.
The depth texture lifetime follows that framebuffer, and the frame context is
valid only during `RenderScene`. Meshes and matrices referenced by tokens remain
alive through synchronous queue submission. Failed framebuffer/style setup
disables shadows for that frame while preserving ordinary color rendering.
There is no threading or platform-contract change; Windows remains the only
verified live backend.

## Ordered implementation milestones

1. Add optional shadow hooks and renderer shared-shadow state/resource helpers.
2. Add visibility-aware shadow traversal to `SceneGraph`.
3. Move bounds preparation and depth emission out of `MeshVisual`'s color path.
4. Update focused tests for one clear/map, multiple casters, shared matrix, and
   custom-target ordering.
5. Synchronize renderer package, canonical architecture, LaTeX chapter,
   decision log, and durable subsystem guidance.
6. Format, build affected targets, run exact tests and the full Release suite,
   then review the complete diff.

## Verification strategy

- `clang-format --dry-run --Werror` on every modified C++/header file.
- Build `IllumoTests`, plus any affected product targets.
- Exact MeshVisual, SceneGraph, and renderer E2E cases covering shadow order.
- Full Release build and `IllumoWorkspace` CTest label.
- `git diff --check`, complete diff review, and status review.
- No performance claim or benchmark: this is a correctness ownership fix.
- No interactive OpenGL smoke without separate authorization; headless tests
  prove token/resource/order behavior, not rendered pixels.

## Rollback and containment

The change is contained to additive extraction hooks, renderer-owned transient
state, `SceneGraph` forwarding, `MeshVisual`, focused tests, and matching docs.
No persistence or asset format changes are involved. If the shared pass cannot
be made reliable, the code changes can be reverted without data migration.

## Open questions and decisions

No user decision is required for the bounded fix. A future explicit scene-light
object could replace first-caster light-direction selection if the product gains
multiple independently configured lights; that is not needed by current
consumers.

## Validation results

- Release configuration completed with documentation disabled for the code
  build; the full Release workspace build completed successfully.
- Focused shadow, renderer ordering, SceneGraph extraction, motion-blur, and
  IllMeshViewer environment tests passed.
- `ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure`
  passed 324/324 tests.
- `clang-format --dry-run --Werror` passed for every modified C++ and header
  file; `git diff --check` passed.
- `docs/build.ps1` completed successfully and produced the 65-page architecture
  book plus the 8-page architecture map. The current-rendering text and D-R22
  decision pages were rendered and visually inspected without clipping or
  overlap.
- No live OpenGL window smoke test was performed. MockBackend validation proves
  shared resource ownership, pass ordering, caster coverage, and uniform
  agreement, but not final GPU pixels.
