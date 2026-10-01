# Shared Mesh Asset Execution Plan

## Objective and measurable end state

Verify and remove the current one-visual/one-upload model for imported meshes.
`AssetManager` will manage reference-counted immutable mesh resources and their
backend `MeshHandle`s. `MeshVisual` instances will bind a managed handle plus
per-instance transform/tint state without copying source vertices or destroying
the shared GPU resource. Two acquisitions of the same file/options must return
one handle and one upload, and the final release must destroy that resource.

## Current-state evidence

- `MeshVisual::addMesh` copies `MeshData` into visual-owned CPU arrays.
- Each `MeshVisual` enrolls and later destroys its own dynamic line, triangle,
  and sprite meshes.
- `AssetManager` manages textures, cubemaps, and shaders, but no mesh entries.
- The backend already exposes typed generational `MeshHandle`s and executes
  indexed and instanced draw commands. `Renderer` currently exposes indexed
  draw emission but no instanced-draw helper.
- Canonical decision D-R17 explicitly left general 3D meshes outside its
  earlier texture/shader milestone; this change extends that boundary without
  changing backend ownership.

## Scope and non-goals

In scope:

- reference-counted static mesh enrollment in `AssetManager` from `MeshData`;
- canonical file/options caching through the existing `MeshLoader`;
- immutable draw metadata lookup for managed handles;
- a non-owning managed mesh binding on `MeshVisual`;
- migration of the mesh viewer's filled model to the managed path;
- focused ownership, sharing, token, and lifecycle tests;
- synchronization of canonical rendering/package documentation.

Non-goals:

- automatic instance aggregation or a new render graph/pass system;
- changing procedural line/grid/sprite primitives from visual-owned dynamic
  buffers;
- material/texture binding, submesh material draws, asynchronous mesh loading,
  or mesh hot reload;
- changing `SceneGraph` attachment ownership or `.ilsc` persistence.

## Constraints and invariants

- Concrete GPU objects remain owned by backend registries; `AssetManager`
  manages cache/reference lifetime through existing `MeshHandle`s.
- Resource creation remains outside the per-frame token stream.
- `MeshVisual` never destroys a managed mesh. The acquiring owner must retain
  and release it through `AssetManager` while any visual can reference it.
- Existing dynamic primitive behavior and public `addMesh` compatibility remain
  intact during this migration.
- Renderer/backend calls remain main-thread affine.
- Invalid metadata is rejected before binding; stale generational handles are
  rejected by backend validation and surface through the existing frame-error
  diagnostics.

## Proposed design and alternatives

Add a `MeshAssetInfo` value containing immutable draw counts/bounds and an
`AssetManager::MeshEntry` containing the backend handle, cache identity,
reference count, and metadata. File acquisition canonicalizes the path and
includes all geometry-affecting loader options in the cache key. Data
acquisition creates an uncached managed entry; callers can explicitly retain
its handle when sharing it.

`MeshVisual::setMeshAsset` stores only the managed `MeshHandle`, immutable index
count, and instance tint. The managed mesh is emitted through the existing
world look and shadow paths in addition to any legacy procedural batches.

Alternatives rejected:

- A second asset-handle registry would duplicate the existing typed
  generational backend handle without improving the current single-backend
  lifetime model.
- Moving every procedural primitive into `AssetManager` would conflate dynamic
  visual scratch with immutable assets.
- Implementing automatic instancing now would require instance-buffer and
  grouping policy that is not needed to eliminate duplicate uploads and lacks a
  measured workload target.

## Public contracts, lifetime, threading, and errors

- `AssetManager` gains mesh acquire/retain/release/status/info methods.
- `MeshVisual` gains managed mesh set/clear/query methods. The handle is
  non-owning, matching texture handles already stored by visuals; its external
  owner controls the `AssetManager` reference.
- File decode and GPU enrollment are synchronous on the render thread for this
  milestone. Failure returns an invalid handle and records no cache entry.
- Stale managed handles are rejected by metadata lookup and backend validation.

## Ordered milestones

1. Add mesh entry/cache APIs and deterministic vertex conversion/enrollment.
2. Add managed-handle command emission to `MeshVisual` while preserving dynamic
   primitives.
3. Migrate `IllMeshViewer` filled-mesh ownership and release behavior.
4. Add focused sharing/lifetime/token tests and public-header coverage.
5. Update architecture/package/decision documentation and rebuild PDFs.
6. Run formatting, focused tests, full Release workspace tests, tidy if
   available, and final diff review.

## Verification strategy

- Headless tests: duplicate file acquisition yields one handle/upload;
  retain/release preserves then destroys the resource; distinct loader options
  do not alias; two visuals bind the same handle with distinct transforms and
  no per-frame upload.
- Build `IllumoTests` and `IllMeshViewerTests`; run the new exact cases.
- Run the full Release build and `IllumoWorkspace` CTest label.
- Run `clang-format --dry-run --Werror` on modified C++/headers and
  `git diff --check`.
- Run `IllumoTidy` if the configured toolchain is available.
- Rebuild documentation. A live OpenGL visual smoke is desirable but is not
  authorized by this request; headless checks cannot prove pixels.

## Rollback and containment

The new path is additive: existing dynamic `addMesh` remains available and can
be restored at the mesh-viewer call site if managed enrollment fails during
development. No persistence or backend resource format changes are involved.

## Open questions

No user decision is required for the bounded ownership fix. Automatic
instancing remains a separate follow-up that should begin with a measured
multi-node workload and a concrete instance-data contract.

## Validation results

- Focused Release targets `IllumoTests` and `IllMeshViewerTests` built.
- Exact tests passed: `Illumo.AssetManager.MeshLifecycle`,
  `Illumo.MeshVisual.SharedMeshAsset`,
  `Illumo.MeshVisual.DynamicMeshReuse`, and
  `IllMeshViewer.Module.MeshLoading`.
- Full Release workspace build passed and its `IllumoWorkspace` CTest run
  passed 415/415 tests.
- `clang-format --dry-run --Werror` passed for every modified C++/header file.
- `IllumoTidy` passed 141 first-party source files with Clang 22.1.8.
- `docs/build.ps1` rebuilt `illumo.pdf` (81 pages) and confirmed the
  architecture-map target current.
- `git diff --check` and final status/diff review passed; only scoped source,
  tests, guidance, and documentation are modified.
- No live OpenGL visual smoke was run; headless token tests and compilation do
  not prove pixels or driver behavior.
