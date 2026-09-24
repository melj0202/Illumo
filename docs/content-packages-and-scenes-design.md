# Content packages, virtual file tree, and `.ilsc` format 2

**Status:** Owner-authorized Tier-3 design, 2026-09-23. Baseline is
`release/v26.09` at `53163604`. The companion execution record is
`docs/content-packages-and-scenes-plan.md`.

**Supersedes:** D-E10 (`.ilsc` v1 is editor-owned; promote the codec "when a
second loader exists"). Every Illumo program now loads scenes, so the codec
moves into a shared content layer. SceneGraph v2 (D-E12) is unchanged except
for one narrow sibling-order addition (section 7); the graph still never
serializes itself.

**Owner decisions (2026-09-23):**

| Question | Decision |
|---|---|
| How scenes reference assets | A package format that stores assets beside scenes, mounted onto an emulated file tree; every package mounts, so mods and expansions are additive |
| Consumers | Every Illumo program uses the scene format and the package format |
| Compatibility | Clean break: `.ilsc` v1 is not read |
| Manifest | `illumo.json` at every package root; it absorbs `app.json` |
| Compression | Deflate on the host via the vendored `stb_image_write` compressor |
| Sibling order | Add a narrow SceneGraph insert-before API |
| Writable projects | `--project <dir>` only in this effort |
| IllEd wave 1 | Core editing, transform tools, a real inspector, content and assets |

---

## 1. Problem

- `.ilsc` v1 (`IllEd/Source/IlscCodec.*`) describes only seven flat-colored
  procedural kinds. A node cannot reference a mesh, texture, sprite, light,
  camera or skybox, and an unknown kind fails the whole load, so the format
  cannot grow without breaking every older reader. Only IllEd reads it.
- Every WASM app reads assets only from its own flat package directory
  (`apps/<name>/`), preloaded in full before its first module starts
  (`GuestPackageAssets`). There is no listing, no archive, no second mount and
  no on-demand load. The debug console cannot show what an app can see.
- IllEd is a prototype: single selection, translate-only world-axis gizmo, no
  undo, no text entry, a read-only inspector, and several visible defects
  (camera motion dirties the document, creation parents to the selection,
  unwired menu shortcuts, a non-scrolling hierarchy, rotation-blind 2D picks).

## 2. End state

| # | Condition |
|---|---|
| G1 | `Illumo::Content` owns `.ilsc` format 2, `.ilpk` packages, the virtual file tree, and a live `SceneInstance`; core `Illumo` does not depend on it |
| G2 | IllEd, IllMeshViewer and IllumoGame load `.ilsc` format 2 through `SceneInstance`; no app keeps a private scene codec |
| G3 | Every app package is described by `illumo.json`; `app.json` no longer exists |
| G4 | The host mounts `/engine`, `/app`, `/packages/<id>` (discovered and `--mount`) and an optional writable `/project`; guests list, stat and read virtual paths |
| G5 | `vfs mounts|ls|tree|stat|cat` works from the developer console and never discloses host paths |
| G6 | IllEd has patch undo/redo, multi-selection and box select, duplicate, copy/paste, rename, translate/rotate/scale gizmos in local or world space with snapping, frame selection, a typed inspector with text entry, mesh/sprite/light/camera nodes, an environment editor, an asset browser, import into a project, and Pack to `.ilpk` |
| G7 | `.csim` saves (sparse v4, reading v4/3/2 and legacy dense) are unchanged |
| G8 | Package tests for all three apps stay green at every milestone |

## 3. Non-goals

- A material system (MTL materials stay parsed-but-unbound), animation,
  scripting, physics, prefabs or nested scene instancing.
- Whiteouts (a mod deleting a base file), arbitrary mount points chosen by a
  package, or overlays onto anything other than `/app`.
- Chunked texture creation. The existing guest upload cap (payload below
  16 MiB; cubemap faces about 836 px, `Services.h:145-149,278-280`) stays and
  imports validate against it.
- A folder-chooser dialog, recent-project lists, or persisted mounts. Launch
  options remain unpersisted.
- ZIP64, multi-disk archives, encryption, or compression methods other than
  stored (0) and deflate (8).
- Host-side decoding of package content for guests (section 9.4).

## 4. Invariants that remain true

- SceneGraph never serializes itself; handles, node addresses, attachment
  ownership and ECS storage never cross its seam. Content builds graphs only
  through the public SceneGraph API.
- `IllumoRuntime` and `Illumo/Source/Wasm` stay product-agnostic. The VFS,
  archive reader and manifest decoder are generic.
- Guests reach files only through the batched services exchange; completions
  arrive on a later exchange. New capabilities, frame fields and file actions
  arrive with decoder and deny tests.
- Package mounts are read-only. Only an explicitly mounted project is
  writable.
- Scene mutation, extraction and attachment emission stay main-thread affine.
- UI stays primitive-composed through `GameVisual`; new helpers
  (`GuiTextEdit`, `GuiFileTree`) are value-state helpers, not a retained tree.
- Coding rules: no `auto`, no namespaces, no recursion; nlohmann stays private
  to `.cpp` files.

---

## 5. Library layout: `Illumo::Content`

`Illumo/Include/Illumo/Content/` and `Illumo/Source/Content/`, target
`IllumoContent` (alias `Illumo::Content`). It links `Illumo::Illumo` publicly
and nlohmann privately. The guest build (a separate CMake project compiled with
`ILLUMO_SERIAL_GUEST`) gets a mirror, `IllumoGuestContent`, compiling the
serial-safe subset.

| Unit | Native | Guest | Role |
|---|---|---|---|
| `VirtualPath` | yes | yes | Path grammar, normalization, component validation |
| `PackageManifest` | yes | yes | `illumo.json` decoder, dependency order |
| `SceneDocument` | yes | yes | Plain-value scene model |
| `IlscCodec` | yes | yes | `.ilsc` format 2 parse and canonical encode |
| `SceneAssetRefs` | yes | yes | Reference resolution, fetch-set collection |
| `SceneInstance` | yes | yes | Live graph + attachments built from a document |
| `PackageArchive` | yes | no | `.ilpk` ZIP-subset reader and writer |
| `VirtualFileSystem` | yes | no | Mount table, overlays, directory/archive backends |
| `VfsAssetSource` | yes | no | `IAssetSource` over the VFS (native tests, tools) |
| `VfsConsole` | yes | no | Pure listing formatters for the `vfs` command |

`SkyboxVisual.cpp` moves from the IllMeshViewer-only guest list into the shared
guest engine so `SceneInstance` can use it everywhere.

---

## 6. Virtual file tree

### 6.1 Paths

One grammar everywhere: an absolute, `/`-separated, case-sensitive path such as
`/packages/forest/meshes/tree.obj`. Components follow the rules currently in
`WasmFileServices::State::relativeName` (`WasmFileServices.cpp:89-132`), which
move into `VirtualPath` and are reused by the host file services:

- non-empty, at most 255 bytes, total path at most 1,024 bytes;
- no `\ : * ? " < > |`, control characters, `.` or `..`;
- no trailing dot or space;
- no `.illumo-` prefix (host staging files);
- no DOS device names (`CON`, `NUL`, `COM1`...), case-insensitively.

Normalization collapses repeated `/` and strips a trailing `/`; it never
resolves `..` (rejected). Relative references are joined against a base
directory before validation. Lookups are case-sensitive; the directory backend
compares each component against the exact on-disk name so a loose folder and
its packed archive behave the same.

### 6.2 Mounts

| Mount | Source | Writable |
|---|---|---|
| `/engine` | runtime `Assets/` directory | no |
| `/app` | the launched package (directory or `.ilpk`) merged with overlays | no |
| `/packages/<id>` | every package in `runtimeDirectory()/packages/` plus each `--mount` | no |
| `/project` | `--project <dir>` (loose package directory) | yes |

A package never chooses its mount point; it is always `/packages/<id>` (or
`/app` for the launched one). Duplicate ids are rejected at discovery (the
first in sorted file-name order wins; the rest are logged and skipped).

### 6.3 Overlays

A package may declare `overlays: [{ "target": "/app", "priority": N }]`. The
merged `/app` view is resolved as:

1. Layers ordered by priority (highest first), then dependency order, then id.
   The launched package is the base layer below every overlay.
2. Directories are unioned; for a file, the topmost layer wins.
3. When a file in one layer collides with a directory in another, the topmost
   layer wins and the conflict is logged once at mount time.
4. There are no whiteouts; a layer cannot hide a lower file.

A package that overlays `/app` is still also mounted at `/packages/<id>`.
Overlays apply only when the package's `targets` include the launched app id
or `"*"`. Writes never go through a merged view.

Data catalogs need merge semantics rather than file replacement. The product
owns those: IllumoGame discovers `/packages/*/csim/*.json` and merges them the
same way it merges `*.user.json` from storage today (`CatalogBootstrap.cpp`).

### 6.4 Operations

`stat(path) -> {kind file|directory, size, packageId}`, `list(path, cursor,
limit) -> entries[]` (paged, sorted by name), `read(path, offset, size)`,
`open/close` for staged guest reads, `write` only on a writable mount (atomic
through `AtomicFile`), and host-only `mount/unmount`. Listings hide `.illumo-*`
staging files. Nothing returns a host path.

Writing `*.wasm` or `illumo.json` into `/project` is refused, so a guest
cannot plant executable content or rewrite a manifest.

### 6.5 Threading and lifetime

- The mount table is an immutable `std::shared_ptr<const VfsMountTable>`,
  swapped under a mutex. Each request copies the pointer once, so the console
  on the main thread and the file-service worker never see a half-built table.
- An open file holds a `shared_ptr` to its backend; unmounting while a file is
  open is safe, and the backend closes when the last reader finishes.
- Archive backends use mutex-guarded positional reads from one file handle.
- A deflated entry is not seekable; opening it inflates the whole entry once on
  the IO worker into a staged buffer. Total inflated staging is capped at
  256 MiB (`inflatedBytes`) in addition to the existing `fileBytes` and
  `openFiles` limits.
- Windows cannot replace a mounted archive; Pack refuses a target that is
  currently mounted.

---

## 7. SceneGraph sibling order

SceneGraph v2 appends a reparented node as the last child
(`SceneGraph.h:71-86`). The hierarchy panel needs reordering, and undoing a
delete must restore the exact sibling position. Add one narrow API:

```cpp
bool setParent(SceneNodeHandle node, SceneNodeHandle parent,
               SceneNodeHandle insertBefore);
```

`insertBefore` is null (append) or a current child of `parent` that is not
`node`. The existing cycle check and the Reparented change record apply; the
compiled preorder rebuilds as for any structural change. This is recorded as a
scene decision; nothing else in the v2 contract changes.

---

## 8. `.ilpk` package format

### 8.1 Physical forms

A package is either a **loose directory** (development, and the only writable
form through `/project`) or an **`.ilpk` archive** (distribution, read-only).
Both carry `illumo.json` at the root and resolve identically.

### 8.2 `illumo.json`

```json
{
  "format": "ilpk",
  "format_version": 1,
  "id": "forest-pack",
  "version": "1.0.0",
  "title": "Forest assets",
  "kind": "content",
  "targets": ["illed", "meshviewer"],
  "dependencies": [{ "id": "base-materials", "version": "1" }],
  "overlays": [{ "target": "/app", "priority": 10 }],
  "app": { "module": "IllEd.wasm", "launchAccess": "edit", "memoryMiB": 512 },
  "mod": { "module": "ForestMod.wasm", "extensionApi": 1 }
}
```

- `id` keeps the current rule (`[a-z0-9._-]`, at most 64, `AppManifest.cpp:19-33`).
- `kind` is `app`, `content` or `mod`. `app` requires an `app` section, `mod`
  requires a `mod` section, `content` has neither. A mismatch is rejected.
- The `app` section carries every current `AppManifest` field (module, worker,
  title, launchAccess, memoryMiB, metering, fuelPerCall, deadlineMilliseconds,
  workers, workerMemoryMiB, workerDeadlineMilliseconds) with the same
  host-ceiling clamping (`AppManifest.cpp:92-116`).
- `mod` fits the existing `GuestRole::Mod` / `extensionApi` / `--mod` path.
- `dependencies` are ordered with Kahn's algorithm; missing dependencies and
  cycles are rejected for that package, which is then not mounted.
- Unknown top-level keys are rejected (strict), because the manifest grants
  budgets. `format_version` above 1 is rejected.
- `envvars.json` stays a separate data file.

### 8.3 Archive (ZIP subset)

`.ilpk` is a ZIP file, so standard tools can create and inspect it.

Reader rules:

- methods 0 (stored) and 8 (deflate) only; deflate is inflated with
  `stbi_zlib_decode_noheader_buffer` into a buffer sized from the central
  directory (bounded; never `_malloc`);
- data descriptors (general-purpose bit 3) are supported by trusting central
  directory sizes and CRC;
- rejected: ZIP64 markers, multi-disk, encryption (bit 0), symlink external
  attributes, names failing `VirtualPath` component rules, backslashes,
  absolute or drive names, duplicate names, file/directory collisions, and a
  local-header name that differs from the central directory;
- bounds: at most 65,535 entries, central directory at most 16 MiB, each entry
  and the total inflated size at most 512 MiB, and a compression ratio of at
  most 1:200;
- CRC-32 (implemented in Content) is checked on every full entry read.

Writer rules (host only): deflate through glfw's vendored `stb_image_write`
compressor (`stbi_zlib_compress`, already compiled in `FrameCaptureImage.cpp`),
except already-compressed `.png`, `.jpg` and `.jpeg` entries, which are stored.
Entries are sorted by path, timestamps are fixed to the DOS epoch, and
`illumo.json` is written first, so packing the same tree twice is
byte-identical.

---

## 9. Runtime integration

### 9.1 Discovery and launch

- `--app name` resolves `apps/<name>/` (directory) or `apps/<name>.ilpk`.
  `--package` accepts a directory or an `.ilpk`. The module and worker `.wasm`
  bytes are read through the VFS.
- `runtimeDirectory()/packages/` is scanned for directories containing
  `illumo.json` and for `*.ilpk` files.
- `--mount <dir|.ilpk>` (repeatable) adds packages; `--project <dir>` mounts a
  writable loose package at `/project`. A missing path fails startup.
- Staging (`illumo_stage_app`) emits `illumo.json` instead of `app.json`, keeps
  loose directories for development, and adds an optional `.ilpk` target.

### 9.2 File protocol v2

`GuestFileRequest` version 2 (`FileProtocol.h`):

| Addition | Meaning |
|---|---|
| Area `Mounted` | `path` is an absolute virtual path |
| Area `Package` | kept as an alias for `/app` |
| Action `List` | paged directory listing (`offset` is the cursor, `count` the limit, at most 256) |
| Action `Stat` | one entry's kind, size and package id |
| Action `Import` | copy a Selected grant into `/project/<path>` |
| Action `Pack` | pack `/project` into an `.ilpk` at a Selected save grant, on a dedicated host worker |

Version 1 requests are rejected once the guest SDK moves to version 2 (the
guest SDK and host ship together). Mounted reads may use blocks up to 1 MiB;
the guest raises its in-flight request limit from 8 to 16 and sets
`ServicesPending` while transfers are active.

New capability `ProjectFiles` (bit 9; `KnownCapabilities` becomes
`(1u << 10u) - 1u`). The host offers it only when a project is mounted; writes,
Import and Pack require it. Listing and reading mounted paths require `Assets`.
Project writes use an incrementally maintained quota instead of the full
storage-tree walk in `storageFits` (`WasmFileServices.cpp:173-199`).

### 9.3 Guest asset cache

`GuestPackageAssets` is replaced by a VFS-backed guest cache:

- `packageAssets()` stays as a **pinned** bootstrap preload; names become paths
  relative to `/app`;
- **fetch sets** load on demand: a scene load collects its asset references,
  fetches them, then instantiates. `IAssetSource::read` stays synchronous,
  because mesh acquisition is synchronous and guest texture jobs run inline;
- unpinned bytes live under a byte budget with least-recently-used eviction;
- a guest-local `/local/...` namespace holds bytes the app supplies itself (for
  example an `.obj` opened through a Selected grant in IllMeshViewer).

AssetManager receives normalized absolute virtual paths as canonical keys. The
host AssetManager keeps `FileAssetSource` (shaders need a real file system). A
mount change does not hot-reload guest assets; reloading the scene is required.
The same bytes reached through two virtual paths are two cache entries.

### 9.4 Rejected: host-side decode

A "create texture from virtual path" service would avoid copying bytes into the
guest, but it would run image and mesh decoders on untrusted package content in
the host process, which the WASM sandbox exists to prevent. Rejected.

### 9.5 Console

Host-owned `vfs` command, registered beside `wasm_stats`:

```
vfs mounts                 mount table with package ids, kinds, layers
vfs ls <path>              one directory
vfs tree <path> [depth]    iterative tree, default depth 3, capped output
vfs stat <path>            kind, size, supplying package
vfs cat <path> [bytes]     text preview, default 4 KiB, binary shown as hex
vfs mount <dir|.ilpk>      debug-tools builds only
vfs unmount <id>           debug-tools builds only
```

A DebugModule overlay renders the same tree with `GuiFileTree`.

---

## 10. `.ilsc` format 2

### 10.1 Shape

```json
{
  "format": "ilsc",
  "format_version": [2, 0],
  "metadata": { "title": "Forest", "author": "", "description": "" },
  "settings": {
    "world_mode": "3d",
    "environment": {
      "skybox": "sky",
      "ambient": [0.25, 0.27, 0.3],
      "sun": { "direction": [-0.4, -1, -0.3], "color": [1, 0.96, 0.9],
               "intensity": 1, "shadows": true }
    }
  },
  "assets": [
    { "id": "tree", "type": "mesh", "path": "meshes/tree.obj",
      "options": { "center_and_normalize": false } },
    { "id": "sky", "type": "cubemap_cross", "path": "/engine/Skybox/skybox-daylight.png" }
  ],
  "nodes": [
    { "id": "n1", "parent": null, "name": "Tree", "enabled": true, "visible": true,
      "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
      "tags": ["static"],
      "components": [
        { "type": "mesh", "asset": "tree", "tint": [255, 255, 255, 255] },
        { "type": "vendor.wind", "strength": 0.3 }
      ] }
  ],
  "extensions": { "illumogame.world": { "csim": "worlds/start.csim" } },
  "editor": { "camera": { "x": 0, "y": 0, "zoom": 32, "yaw": 0.7, "pitch": 0.45 },
              "grid": { "spacing": 1, "visible": true } }
}
```

### 10.2 Rules

- `format_version` is `[major, minor]`. Major must be 2; a minor newer than the
  reader supports is rejected with "requires format 2.N". A v1 file (integer
  `version: 1`) fails with a clear "format 1 is no longer supported" message.
- **Strict core.** Every core object rejects unknown keys and wrong types.
  Numbers must be finite and in range; scale components must have magnitude at
  least 1e-4; extents are positive; colors are 0..255 integers (tint) or 0..1
  floats (lighting).
- **Opaque namespaced data.** A component whose `type` contains a dot keeps
  every other member it carries, and every `extensions` entry (keys must
  contain a dot) keeps its value; both are preserved verbatim as canonical
  (sorted-key) JSON text. Readers that do not understand it skip it; IllEd shows
  it read-only and copies it verbatim. Node ids inside opaque data are not
  remapped on duplicate or paste. An unknown type without a dot is a newer core
  type and fails with the minor-version message.
- **Ids.** Node and asset ids are non-empty, unique within their table, at most
  128 bytes. Parents must exist; cycles are rejected. Node order in the file is
  graph preorder, so sibling order round-trips.
- **Asset references** are package-relative (`meshes/tree.obj`), resolved
  against the directory of the scene's own package root, or explicit absolute
  virtual paths (`/engine/...`, `/packages/<id>/...`). A scene inside a project
  therefore works unchanged after packing and mounting at `/app` or
  `/packages/<id>`. Components reference assets by asset id, never by path.
- **One environment.** Skybox and sun live only in `settings.environment`,
  matching the renderer's single shared directional light. `light` components
  are directional in format 2.0 and override the environment sun when present
  (the first enabled one in preorder wins; others warn).
- **Editor block.** `editor` is written only on a save the user triggers and
  never contributes to the dirty state. Non-editor apps ignore it.
- **Canonical encoding.** Keys in a fixed schema order, two-space indentation,
  floats printed with the shortest round-trip `std::to_chars` form of the
  stored `float`, so `0.45f` is written `0.45`. Encoding the same document twice
  is byte-identical.

### 10.3 Core components (format 2.0)

| Type | Fields |
|---|---|
| `primitive` | `shape` (`rect`, `ellipse`, `triangle`, `cube`, `pyramid`, `sphere`, `wire_cube`, `wire_sphere`), `extent` [3], `color` [4] |
| `mesh` | `asset` (mesh id), `tint` [4], `cast_shadows` |
| `sprite` | `texture` (texture or atlas id), `region` [u0,v0,u1,v1] or `cell` [column,row] (atlas only), `size` [2], `facing` (`world` or `billboard`), `tint` [4], `flip` [2] |
| `light` | `kind` (`directional`), `color` [3], `intensity`, `shadows` |
| `camera` | `projection` (`orthographic` or `perspective`), `fov_degrees`, `near`, `far`, `zoom`, `primary` |

Asset types: `mesh` (OBJ, with a sibling `.mtl` resolved by virtual path),
`texture` (PNG/JPG with filter/wrap/mipmap options), `atlas` (texture options
plus `grid` [columns, rows]), `cubemap_cross` (one cross image), `cubemap_faces` (six paths).

### 10.4 `SceneInstance`

A live object, not a one-shot builder:

- owns the `SceneDocument`, a `SceneGraph`, the attachments, and the id →
  handle map; stable ids are stored as graph names and a stable per-node
  record key (not an array index) as `userData`;
- per-node updates (transform, enabled/visible, components, reparent with
  sibling position, create, remove) change only what they touch, and call
  `invalidateSnapshots()` before reconfiguring an attachment;
- primitives share one immutable unit mesh per shape, acquired through
  `AssetManager::acquireMesh(MeshData)`, with extent applied as a local scale in
  the attachment, instead of one dynamic mesh per node;
- environment lighting and shadow settings are pushed into every visual; the
  skybox is a `SkyboxVisual` attachment on an internal root;
- a missing or failed asset draws a magenta placeholder and records a warning
  readable by the app;
- `collectAssetFetches()` returns the virtual paths a load needs, so guests can
  prefetch before instantiating.

Mesh byte loads gain a related-file lookup so a `.mtl` beside an `.obj` is
found through the asset source (`AssetManager.cpp:575-583` passes an empty
material path today).

---

## 11. IllEd

### 11.1 Structure

`EditorModule.cpp` (1,835 lines) splits into focused units:
`EditorDocument` (over `SceneInstance`), `EditorHistory`, `EditorSelection`,
`EditorShortcuts`, `EditorGizmo`, `EditorInspector`, `EditorClipboard`,
`EditorAssetBrowser`, `EditorProject` and `EditorPickProxy`. `EditorDocument`
stays the single mutation gateway.

### 11.2 Editing model

- **History** stores patches, not snapshots. A command is a list of
  `{id, before record or absent, after record or absent, parent, insertBefore}`,
  which covers create, delete, modify and reparent uniformly. Continuous edits
  (drags, slider scrubs) merge on a session key, keeping the first before and
  the last after. History is capped at 512 commands and 64 MiB of estimated
  records, and cleared on load.
- **Dirty** means the history cursor differs from the saved cursor. Camera and
  editor-block changes never dirty the document.
- **Selection** is an ordered set with a primary node.
- **Shortcuts** come from one table that drives both menus and key handling.
- **Creation** places at the root unless "create as child" is chosen.
- **Picking** uses `raycastCandidates` in 2D and 3D, respecting rotation and
  effective visibility.

### 11.3 Tools

- translate, rotate (view-facing and axis rings) and scale (axis and uniform)
  gizmos, in world or local space, with pivot at the primary node or the
  selection center;
- grid, angle and scale snapping, toggled and configured in the toolbar;
- frame selection (F) fits the selection's world bounds;
- oriented selection boxes;
- box select in 2D and 3D (projected candidate bounds);
- duplicate (Ctrl+D), copy/cut/paste (Ctrl+C/X/V) as a fragment on the
  clipboard: a format 2 `.ilsc` document tagged with the `illed.fragment`
  extension, holding the copied subtrees (roots at their world pose) and the
  assets they reference; at most 4 MiB; on paste every id is remapped,
  identical assets are reused and conflicting asset ids renamed; rename
  (F2), delete. Asset paths stay package-relative, so pasting between scenes
  of different packages resolves against the target package (a follow-up may
  rewrite them);
- hierarchy row-window scrolling, fold/unfold, drag reorder with insert-before,
  visibility and enable toggles, and a context menu.

### 11.4 Inspector

`GuiTextEdit` (Illumo/Gui) is value-state text editing: UTF-8 caret and
selection, clipboard, and input from `InputManager::getCharQueue`, drawn by
`GuiKit::drawTextField`. The inspector shows typed rows for name, transform
(Euler degrees in the UI, quaternion in the file), each component, and the
scene environment; Enter commits one history command, Escape cancels, invalid
text is rejected in place. Multi-selection shows shared values and marks mixed
ones. Components are added and removed from a menu; namespaced components are
read-only.

### 11.5 Content and assets

- New nodes: mesh, sprite, light and camera, plus every primitive shape.
- The asset browser lists the VFS through `GuestFileTree` and `GuiFileTree`;
  dragging a mesh or texture into the viewport creates a node and an asset
  entry.
- With `--project <dir>`, Import copies a picked file into `/project/<folder>`,
  Save writes scenes into `/project`, and Pack writes an `.ilpk` to a chosen
  location. Imports validate the texture upload cap.

---

## 12. Adoption

- **IllMeshViewer** opens `.ilsc` (from any mount), `.ilpk` (mounted and its
  first scene opened) or `.obj` (wrapped in a transient `/local` scene). The
  skybox becomes a package asset referenced by the default environment.
- **IllumoGame** reads its catalogs from `/app`, merges `/packages/*/csim/*.json`,
  and loads its `render3dTest` diagnostic from `Scenes/render3d-test.ilsc` in
  its package, animating nodes by id. `.csim` is unchanged.
- Future apps load scenes through `SceneInstance` and declare their package in
  `illumo.json`.

---

## 13. Alternatives considered

| Alternative | Why not |
|---|---|
| Project-folder grants only | Owner chose packages over a folder grant; packages also cover mods and distribution |
| Embedded base64 assets in `.ilsc` | Bloats scenes, defeats sharing between scenes, poor for large meshes |
| Custom archive format | ZIP is inspectable and creatable by standard tools, which matters for modders |
| New compression dependency (zlib, miniz) | stb already provides bounded inflate and a deflate writer |
| Keep `app.json` beside a content manifest | Two manifests per package drift; `illumo.json` with an `app` section is one source of truth |
| `package.json` as the manifest name | Editors and npm treat it as an npm project root |
| Whole-document undo snapshots | About 600 KB per snapshot at 2,000 nodes; hundreds exceed the guest budget |
| Lenient core parsing (ignore unknown keys) | Silently drops data on round trip; namespaced opaque data gives forward compatibility instead |
| Detach-and-re-append reordering | O(siblings) per move and noisy change journals |
| Host-side asset decode | Moves untrusted decoders into the host process |

## 14. Milestones and rollback

See `docs/content-packages-and-scenes-plan.md`. Milestone 4 (IllEd switches to
format 2, deleting v1) and milestone 10 (`illumo.json` cutover) are each
reverted as a unit; v1 code is deleted only once milestone 4 is green.

## 15. Follow-ups

Whiteouts; overlays on mounts other than `/app`; chunked texture creation;
a material system binding MTL data; glTF; a folder-chooser dialog; remapping
node ids inside opaque component data; prefabs and nested scene instances;
hot reload of guest assets after a mount change.
