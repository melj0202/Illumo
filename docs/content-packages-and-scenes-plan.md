# Content packages and scenes: execution record

**Design:** `docs/content-packages-and-scenes-design.md` (owner-authorized
2026-09-23). **Baseline:** `release/v26.09` at `53163604`.

Each milestone builds and passes its own tests before the next begins. One
writer. Status values: pending, in progress, done, deferred.

| M | Scope | Status |
|---|---|---|
| 0 | Specification and this record | done |
| 1 | Content scaffold: `VirtualPath`, `PackageManifest`, `IllumoContent` target, shared validator in `WasmFileServices` | done |
| 2 | `.ilsc` format 2: `SceneDocument`, `IlscCodec`, `SceneAssetRefs`, guest mirror | done |
| 3 | SceneGraph insert-before; `SceneInstance` (shared primitives, sprite, light, camera, environment, skybox, incremental updates) | done |
| 4 | IllEd on format 2 (clean break): document over `SceneInstance`, history, selection set, shortcut table, module split, bug fixes | done |
| 5 | `GuiTextEdit` and the typed inspector | done |
| 6 | Transform tools: rotate/scale gizmos, local/world, snapping, frame selection | done |
| 7 | Multi-select, box select, clipboard, hierarchy upgrades | done |
| 8 | `PackageArchive` reader, deflate writer, CRC-32, CMake zip fixture | done |
| 9 | `VirtualFileSystem`, `VfsAssetSource`, `VfsConsole` | done |
| 10 | Runtime packages: `illumo.json` cutover, discovery, `--mount`, `--project`, `.ilpk` launch, host `vfs` command | done |
| 11 | File protocol v2, `ProjectFiles`, guest `GuestFileTree` | done |
| 12 | Guest VFS asset cache replacing `GuestPackageAssets` | done |
| 13 | IllEd content and assets: `GuiFileTree`, asset browser, project, Import, Pack, new node kinds | done |
| 14 | Adoption: IllMeshViewer and IllumoGame | done |
| 15 | DebugModule file-tree overlay, documentation, full gate | done |

## Tests per milestone

| M | Tests |
|---|---|
| 1 | `Illumo.Content.VirtualPathNormalize`, `.VirtualPathRejects`, `.PackageManifestDecode`, `.PackageManifestClampsBudgets`, `.PackageManifestKindMismatch`, `.DependencyOrderAndCycles`; `Illumo.Wasm.FileServices` unchanged |
| 2 | `Illumo.Content.IlscRoundTrip`, `.IlscCanonicalGolden`, `.IlscRejects`, `.IlscPreservesExtensions`, `.SceneAssetRefsResolve` |
| 3 | `Illumo.SceneGraph.SiblingInsertBefore`; `Illumo.Content.InstancePrimitives`, `.InstanceEnvironmentLight`, `.InstanceSkyboxAndAssets`, `.InstanceIncrementalEdit`, `.InstancePicking`, `.InstanceSiblingOrder`, `Illumo.Content.Bench.Instantiate2000` |
| 4 | `IllEd.History.*`, `IllEd.Module.CameraDoesNotDirty`, `.NewNodeParentsToRoot`, `.ShortcutsMatchMenus`, `.Pick2DRotatedHidden`; `IllEd.Wasm.Package` on format 2 |
| 5 | `Illumo.Gui.TextEditUtf8Caret`, `.TextEditSelectionClipboard`, `IllEd.Inspector.*` |
| 6 | `IllEd.Gizmo.*` |
| 7 | `IllEd.Clipboard.*`, `IllEd.Selection.*`, `IllEd.Hierarchy.*` |
| 8 | `Illumo.Content.ArchiveRoundTrip`, `.ArchiveReadsCMakeZip`, `.ArchiveInflateBounded`, `.ArchiveRejectsMalformed`, `.ArchiveFuzzSmoke` |
| 9 | `Illumo.Content.VfsMountsAndStat`, `.VfsOverlayOrder`, `.VfsEscapeRejected`, `.VfsConcurrentReads`, `.VfsUnmountWhileOpen`, `.VfsWriteOnlyWritable`, `.VfsAssetSourceTexture`, `.VfsConsoleListing` |
| 10 | `Illumo.Content.PackageDiscovery`, `Illumo.Wasm.PackageFromArchive`, `Illumo.Pack.StagedApp`, `Illumo.Runtime.MountMissingDir`, `.ProjectMissingDir`, `.PackageMissing` |
| 11 | `Illumo.Wasm.FileProtocolV2Decoder`, `.MountedFiles`, `.MountedDeny`, `Illumo.Wasm.Bench.FileThroughput` |
| 12 | `Illumo.Wasm.GuestAssetFetchAndEvict`, `.GuestPinnedPreload`, `.GuestLocalEntries` |
| 13 | `Illumo.Gui.FileTreeRows`, `Illumo.Content.PackMounted`, `IllEd.Assets.*`, `IllEd.Wasm.ProjectPackage` (Import and Pack denial is covered by `Illumo.Wasm.MountedDeny`) |
| 14 | `IllMeshViewer.Wasm.Package`, `.ScenePackage`, `IllMeshViewer.Module.SceneFromTree`, `.SceneRejectsInvalid`, `IllumoGame.Wasm.GamePackage` (3D from scene), `.CatalogMerge`, `IllumoGame.CellGame.Render3dTestFlag` |
| 15 | `Illumo.Debug.FileTreeOverlay`, `Illumo.Content.VfsTreeSource`, `IllMeshViewer.Wasm.ScenePackage` (tree published and withdrawn); full Release build and `-L IllumoWorkspace`, `IllumoTidy`, ASan Debug, coverage gate, benchmarks, real-GPU capture |

## Validation log

Record per milestone: what was implemented, deviations from the design and
why, the exact tests and results, and remaining risks.

### M0 (2026-09-23)

Design and this record written. No code changed.

### M1 (2026-09-23)

- `IllumoContent` (`Illumo::Content`) static library in `Illumo/CMakeLists.txt`,
  linking `Illumo::Illumo` publicly and nlohmann privately.
- `VirtualPath`: component, relative and absolute grammar; normalize, join,
  parent, file name, mount name, containment. Components now also require
  well-formed UTF-8 and at most 255 bytes.
- `PackageManifest`: strict `illumo.json` decoder (format 1; app, content and
  mod kinds; targets; dependencies; `/app` overlays; budget clamping moved
  from `AppManifest`) and `orderPackages` (Kahn, ties by id; duplicates,
  missing dependencies and cycles rejected with reasons).
- `WasmFileServices::State::relativeName` now delegates to
  `VirtualPath::validRelative`; `IllumoWasmRendering` links `Illumo::Content`.
- Deviation: the app window title is the manifest's top-level `title`; the
  `app` section has no `title` key.
- Tests (Release, `build-workspace`): `Illumo.Content.VirtualPathNormalize`,
  `.VirtualPathRejects`, `.PackageManifestDecode`,
  `.PackageManifestClampsBudgets`, `.PackageManifestKindMismatch`,
  `.DependencyOrderAndCycles` pass; `Illumo.Wasm.FileServices` passes
  unchanged. `AppManifest` stays in use until M10.

### M2 (2026-09-23)

- `SceneDocument` (plain values; components as a `std::variant` of primitive,
  mesh, sprite, light, camera and opaque) and `validateSceneDocument`, which
  enforces the codec's invariants on programmatic documents too. Parents must
  appear before children, so cycles cannot occur.
- `IlscCodec`: strict transactional parse, explicit messages for format 1,
  other majors and newer minors; canonical encoder with an iterative pretty
  printer (scalar arrays inline) and shortest-float printing.
- `SceneAssetRefs`: package roots, reference resolution, same-package
  relative references, fetch lists, and OBJ `mtllib` discovery.
- Guest mirror `IllumoGuestContent` compiles under WASI.
- Deviations: namespaced components keep their members inline beside `type`
  (no `data` wrapper); sprite `cell` is `[column, row]` into an atlas
  asset's `grid`; a `sphere` (solid) primitive was added; unit quaternions
  within 1e-5 are stored bit-exact so re-encoding stays stable.
- Tests: `Illumo.Content.IlscRoundTrip`, `.IlscCanonicalGolden`,
  `.IlscRejects` (36 malformed cases), `.IlscPreservesExtensions`,
  `.SceneAssetRefsResolve` pass.
### M3 (2026-09-23)

- `SceneGraph::setParent(node, parent, insertBefore)` and
  `getPreviousSibling`; an unchanged position records nothing.
- `SceneInstance` (Content, native and guest): live graph plus one
  `MeshVisual` per renderable component. Solid primitives share one enrolled
  unit mesh per shape (extent is the visual's model scale); wire shapes stay
  procedural. Mesh, texture/atlas sprite and cubemap assets load through
  `AssetManager` with magenta placeholders and warnings on failure. Lighting:
  first enabled visible light component in preorder, else the environment
  sun; 2D scenes are unlit. Skybox is a separate drawable, not a graph node.
  Picking uses attachment local bounds with rotation, visibility and a
  preorder tie-break; optional pick proxies for empty/light/camera nodes.
  `document()` is rebuilt lazily in graph preorder; editor view state never
  bumps the revision.
- `SkyboxVisual.cpp` moved into `IllumoGuestEngine`.
- Deviation: the MTL-beside-OBJ lookup for byte sources moves to M9/M12,
  where a virtual-path asset source exists to exercise it.
- Tests: `Illumo.SceneGraph.SiblingInsertBefore`,
  `Illumo.Content.InstancePrimitives`, `.InstanceEnvironmentLight`,
  `.InstanceSkyboxAndAssets`, `.InstanceIncrementalEdit`,
  `.InstanceSiblingOrder`, `.InstancePicking`, `.Bench.Instantiate2000`
  (Release: load 2,000 nodes 3.8 ms, 1,000 batched transform edits 0.8 ms,
  document plus encode 45 ms / 905 KB). Full Release build and
  `ctest -L IllumoWorkspace` (via `IllumoRunTests`): 528/528 passed.
### M4 (2026-09-23)

- IllEd's v1 `IlscCodec` and per-node `EditorAttachment` are deleted.
  `EditorDocument` wraps one `SceneInstance` (with pick proxies) plus
  `EditorHistory`: every edit records node before/after patches (parents
  first; a reverse pass restores sibling positions) or scene settings;
  drags merge on a per-drag key; dirty is `history uid != saved uid` with a
  merge barrier at the save, so camera and editor view state never dirty.
  History is capped at 512 commands / 64 MiB estimated.
- `EditorSelection` (ordered set, primary, `topLevel`), `EditorShortcuts`
  (single key table driving menus and keys), `EditorCommand.h`. New
  commands: Undo, Redo, Duplicate, Select All, Deselect, Frame Selection,
  Wire Cube, Wire Sphere.
- `EditorModule` split into `EditorModule.cpp`, `EditorModuleCommands.cpp`
  and `EditorModuleViewport.cpp`. Fixed: camera motion dirtied the document;
  creation parented to the selection; U/2/3/R shown but unwired (Reset is now
  Home); 2D picking ignored rotation and visibility (now
  `SceneInstance::pickRay` everywhere); the selection box ignored rotation
  (now oriented per selected node). Reparenting keeps the world pose.
  Camera keys moved to arrows and PageUp/PageDown so letters are free.
- `SceneInstance` gained in-place visual reconfiguration when a node's
  visual component kinds are unchanged (recolor/resize keep attachment
  identity), `sceneNodesEqual`, and fixes found by the editor tests: ray-pick
  ties no longer collapse when the first hit is at infinity.
- Bug found and fixed during porting: `record(label from before.front(),
  std::move(before))` read a moved-from vector under MSVC's right-to-left
  parameter construction; `record` now takes the states by const reference.
- Tests: all 52 prior IllEd cases ported (`IllEd.Module.AttachmentBounds` was
  retired with `EditorAttachment`; `IllEd.Ilsc.*` moved to Content); new
  `IllEd.History.UndoRedoEveryCommand`, `.DeleteRestoresSiblingOrder`,
  `.DragMerges`, `.CapBytes`, `.DirtyTracksSavedCursor`,
  `IllEd.Document.ReparentKeepsWorldPose`, `IllEd.Module.CameraDoesNotDirty`,
  `.NewNodeParentsToRoot`, `.ShortcutsMatchMenus`, `.KeyboardUndoRedo`.
  `IllEd.Wasm.Package` passes against the rebuilt `IllEd.wasm` (arrow-key
  pan, Ctrl+S save of the format 2 scene). `IllEdCloseWindowTests` (real
  hidden GLFW window) passes. Full Release build plus `ctest -L
  IllumoWorkspace`: 532/532 passed.
### M5 (2026-09-23)

- `GuiTextEdit` (Illumo/Gui, native and guest): value-state single-line UTF-8
  editing with code-point caret movement, word jumps, selection, byte limit
  clipped at code points, Enter/Tab commit, Escape cancel, and clipboard
  hand-off (copy/cut return text, paste raises a request). While active it
  consumes the key and character queues, including keys after a commit in
  the same frame. `GuiKit::drawTextField` draws it (selection, blinking caret,
  caret-following scroll, invalid border, read-only style).
- `EditorInspector`: typed fields rebuilt from the document each frame —
  name, enabled/visible, position, rotation (Euler degrees), scale, and per
  component (primitive shape/extent/color, mesh asset/tint/shadows, sprite
  texture/size/facing/tint/flip, light color/intensity/shadows, camera
  projection/fov/clip/zoom/primary), add/remove components, namespaced
  components read-only; with no selection, the scene mode, metadata and
  environment (skybox, ambient, sun). Number fields scrub by dragging (one
  merged command per scrub). Multi-selection shows shared values and dashes
  for mixed ones and applies one command to every node.
- `EditorDocument::editNodes` (atomic batch edit, restores on rejection),
  `setEnvironment`, `setMetadata`. `IllEdPlatform` gained a clipboard seam
  (native `Clipboard`, guest `GuestClipboard`; IllEd now requires the
  Clipboard capability). F2 focuses the name field. The sidebar is 240 wide
  and hosts the inspector below the tools. IllEd registers `scene_select`,
  `scene_undo`, `scene_redo` and `scene_frame` console commands.
- Visual check: `IllumoRuntime --app illed --open scene.ilsc --capture` with
  a `scene_select crate ball` capture script shows the multi-selection,
  oriented boxes and the mixed-value inspector.
- Tests: `Illumo.Gui.TextEditUtf8Caret`, `.TextEditSelectionClipboard`,
  `IllEd.Inspector.CommitCreatesCommand`, `.InvalidAndEscape`,
  `.MixedMultiValue`, `.ComponentsAndScene`. Full Release build plus
  `ctest -L IllumoWorkspace`: 538/538 passed.
### M6 (2026-09-23)

- `EditorGizmo` (IllEd): ray-based hit testing, drag math and drawing for
  translate (axes, planes, center), rotate (three rings; the Z ring only in
  2D) and scale (axes and uniform center), in world or the primary node's
  local axes, at constant screen size. Deltas are cumulative from the press
  and applied to transforms captured at the press, so drags never drift.
  Snapping quantizes translation per gizmo axis, angles and scale factors;
  Ctrl inverts the snap setting while dragging.
- Module: W/E/R choose the mode, X toggles world/local, G toggles snapping
  (editor view state, never dirty); a new Tools menu; the status bar shows
  mode, space and snap. Rotation pivots on the primary node; scaling keeps
  each node's own axes and spreads positions from the pivot. Grabbing a body
  still moves the selection on the edit plane. Every drag is one merged
  history command (`EditorDocument::setTransforms`). Frame selection (F,
  from M4) is covered by a test.
- Tests: `IllEd.Gizmo.TranslateAxisProjection`, `.RotateRingAngle`,
  `.ScaleUniformAndAxis`, `.LocalSpaceAxes`, `.ModuleRotateAndScaleDrags`
  (real pointer drags through `Update`), `.LocalSpaceUnderRotatedParentAndSnap`,
  `.FrameSelectionFitsBounds`; all 64 prior IllEd cases still pass. Visual
  check: a capture with `@key E` and `@key X` shows local rotate rings.
### M7 (2026-09-23)

- Box select: a drag that starts on empty viewport space draws a marquee
  and, on release, selects every visible node whose world-bounds center
  projects inside it (Ctrl/Shift adds). A click without a drag still clears.
  `EditorModule::worldToScreen` inverts the picking ray exactly in 2D and 3D.
- `EditorClipboard`: Ctrl+C/Ctrl+X/Ctrl+V (Edit menu and context menu) copy
  the selection's top-level subtrees as an `.ilsc` format 2 fragment tagged
  `illed.fragment`, with roots at their world pose and the assets they
  reference; at most 4 MiB, untagged or foreign text is refused.
  `EditorDocument::paste` inserts after the primary selection, remaps every
  node id, reuses identical assets and renames conflicting ones (`tex_2`), as
  one history command. `EditorHistory` now brings asset tables and nodes to a
  target through the union of both tables, so undoing a paste that added an
  asset never leaves a node referencing a missing one.
- Hierarchy panel: rows outside the row window are neither drawn nor hit;
  the wheel scrolls it (with a thumb), a drag near its edges auto-scrolls,
  and a new primary selection unfolds its ancestors and scrolls into view.
  Fold arrows; a visibility eye per row (H toggles the selection); drops land
  before, into or after a row by thirds, drawn as an insertion line;
  double-click renames; a right-click menu (Rename, Duplicate, Copy, Cut,
  Paste, Add Child, Show/Hide, Enable/Disable, Unparent, Delete) hands its
  command to the module. Labels are cut to fit before the eye.
- `SceneInstance::parentOf`: `findNode` records keep an empty `parentId` (the
  graph owns parents); several new call sites relied on it, now documented.
- Tests: `IllEd.Clipboard.RoundTripRemapsIds`, `.RejectsForeignOrOversize`,
  `.PastePlacesAfterSelection`, `IllEd.Selection.BoxSelect2D` (real pointer
  drag), `.BoxSelect3D`, `IllEd.Hierarchy.RowWindowScroll`, `.ReorderUndo`,
  `.FoldAndVisibility`. Full Release build plus `ctest -L IllumoWorkspace`:
  553/553 passed; after two small follow-ups (single settings pass when an
  undo drops no asset; a right-click that opens the menu never orbits the
  camera) the rebuilt IllEd suite and guest package pass 76/76.
- Visual check: a capture of a nested scene with a hidden, long-named node
  and `scene_select crate ghost` shows fold arrows, per-row eyes (struck
  through and dimmed when hidden), label truncation and the multi-selection.

### M8 (2026-09-23)

- `PackageArchive` (Content, native only): a strict ZIP-subset reader over a
  positional `IPackageByteSource` (`MemoryPackageSource`, mutex-guarded
  `FilePackageSource`). It accepts stored and deflate entries, data
  descriptors and explicit directory entries, and refuses zip64, multi-disk,
  encryption, other methods, Unix symlinks/special files and Windows reparse
  points, names failing `VirtualPath::validRelative`, duplicates,
  file/directory collisions, local headers that disagree with the central
  directory, entries whose data overlaps another entry or the directory, and
  anything past `PackageArchiveLimits` (entries, directory bytes, per-entry
  and total inflated bytes, deflate ratio). `read` inflates into an exactly
  sized buffer through stb's `stbi_zlib_decode_noheader_buffer` (a stream
  that inflates further fails) and checks the CRC-32. `list` and
  `isDirectory` answer from sorted file and implied-directory tables.
- `PackageArchiveWriter`: validated names, collision checks, `addDirectory`
  (symlinks refused, UTF-8 names), deterministic output (sorted names, fixed
  1980-01-01 time, UTF-8 flag) and deflate through the engine's
  `stbi_zlib_compress` with the zlib wrapper stripped; PNG/JPEG/archives and
  data deflate cannot shrink are stored. `write` goes through `AtomicFile`.
- `packageCrc32` (table CRC-32, resumable).
- Fixture: the build packs `Illumo/Tests/Content/Fixtures/archive` with
  `cmake -E tar --format=zip` (libarchive), so the reader is checked against
  a second archiver.
- Tests: `Illumo.Content.ArchiveCrc32`, `.ArchiveRoundTrip`,
  `.ArchiveReadsCMakeZip`, `.ArchiveInflateBounded`, `.ArchiveRejectsMalformed`
  (hand-assembled bad archives), `.ArchiveFuzzSmoke` (3000 seeded mutations
  and truncations; each is refused or read without faults).

### M9 (2026-09-23)

- `VirtualFileSystem` (Content, native): an immutable `VfsMountTable`
  swapped under a mutex; every request copies the pointer once. Mount points
  are normalized, never `/`, and never nest; `/` and `/packages` are
  synthesized directories. Multi-layer mounts resolve component by component:
  the topmost layer holding a name decides its kind, a file hides the name in
  every lower layer, directories union; file/directory collisions are found at
  mount time (iterative walk, capped) and kept in `VfsMount::conflicts`.
  `stat` reports kind, size, stamp and the supplying package; `list` pages;
  `open` returns a `VfsFile` that keeps its backend alive after unmount;
  `write` works only on a single-layer writable mount and refuses `*.wasm`
  and `illumo.json` in any case.
- Backends: `DirectoryVfsBackend` (exact-case lookups on every host by
  comparing the canonical path, which also rejects symlinks and junctions
  that leave the root; staging and invalid names hidden from listings;
  writes create parents one checked component at a time and go through
  `AtomicFile`), `ArchiveVfsBackend` over `PackageArchive` (plus the new
  `PackageArchive::readRange`: stored entries read in place).
- `VfsAssetSource`: `IAssetSource` over the tree; names join a base
  (`/app`), invalid references canonicalize to nothing.
- MTL beside OBJ for byte sources (deferred from M3): `MeshLoadOptions`
  gained `materialText`, `MeshLoader::materialLibraryNames` is the single
  `mtllib` scanner (Content's `objMaterialLibraries` now uses it), and
  `AssetManager::acquireMesh` reads each library beside the OBJ through the
  same source.
- `VfsConsole`: pure `mounts`/`ls`/`tree`/`stat`/`cat` formatters and `run`
  dispatch (tree is an explicit-stack walk, capped at 400 lines; cat shows
  text or a hex dump, at most 64 KiB).
- Tests: `Illumo.Content.VfsMountsAndStat`, `.VfsOverlayOrder`,
  `.VfsEscapeRejected`, `.VfsConcurrentReads` (six readers during mount
  churn), `.VfsUnmountWhileOpen`, `.VfsWriteOnlyWritable`,
  `.VfsAssetSourceTexture` (texture and OBJ+MTL through `AssetManager`),
  `.VfsConsoleListing`; the Content, mesh and scene suites pass (74/74).
- Risk: the symlink branch of `.VfsEscapeRejected` runs only where the
  account may create links (it is skipped on this Windows machine without
  Developer Mode); the canonical-path check that guards it is shared with
  the exact-case check, which does run.

### M10 (2026-09-23)

- `illumo.json` replaces `app.json` in all three packages (same budgets, now
  under `app`); `AppManifest.*` and `Illumo.Wasm.Manifest` are deleted (the
  Content manifest tests cover the same clamping and rejections). Staging
  copies `illumo.json`; `build.py` looks for it.
- `PackageMounts` (Content, native): `open` (directory or `.ilpk`, manifest
  at most 64 KiB), `discover` (`packages/` in file-name order; unreadable and
  duplicate ids skipped with warnings) and `mountAll` (`/engine` from the
  runtime `Assets/`, `/app` = the launched package under its targeted
  overlays stacked by priority, dependency order and id, `/packages/<id>`
  for every package whose dependencies resolve, writable `/project` named
  by its manifest when it has one).
- Runtime: `--app` resolves `apps/<name>/` or `apps/<name>.ilpk`;
  `--package` takes either; new repeatable `--mount` (a SysCmdLine `paths`
  option, newline-joined) and `--project`; a missing mount, project or
  package refuses to start, as do `--mount`/`--project` with `--game`.
  Module and worker bytes are read from `/app` through the tree.
  `WasmFileRoots::packages` carries the tree; `WasmFileServices` serves the
  Package area from `/app` (overlays and archives included) through
  `VfsFile` handles; the directory constructor now mounts its directory the
  same way, so direct `--game` launches and tests share one path.
- Host console: `vfs mounts|ls|tree|stat|cat`, registered beside
  `wasm_stats` when a tree exists.
- `IllumoPack <package-dir> <out.ilpk>` / `--verify <file.ilpk>` (new host
  tool).
- Deviation: `vfs mount`/`vfs unmount` (debug-tools only) are not added yet;
  the mount table is fixed per launch.
- Tests: `Illumo.Content.PackageDiscovery`, `Illumo.Wasm.PackageFromArchive`
  (Package area over an archive `/app` with an overlay),
  `Illumo.SysCmdLine.StringOptionsAndPositionalArguments` (repeatable
  paths), `Illumo.Pack.StagedApp` (packs and verifies the staged IllEd),
  `Illumo.Runtime.MountMissingDir`, `.ProjectMissingDir`, `.PackageMissing`;
  `Illumo.Wasm.FileServices` and every package test unchanged.
- Real launches: `--app illed` (directory), `--package illed.ilpk` (the
  editor draws its package-preloaded atlas from the archive), the same with
  `--mount <content dir> --project <dir>` and a `vfs` capture script, and
  `--app game` all capture successfully. Full Release build plus
  `ctest -L IllumoWorkspace`: 572/572 passed.

### M11 (2026-09-23)

- `GuestFileRequest` version 2 (version 1 is refused; host and SDK ship
  together): a `target` field; area `Mounted` (absolute virtual paths;
  `Package` stays the `/app` alias); actions `List` (cursor and page of at
  most 256), `Stat`, `Import` (Selected grant to a project path) and `Pack`
  (project directory to a writable Selected grant). Mounted transfers use
  blocks up to 1 MiB; Package, Storage and Selected files keep 64 KiB
  (enforced per open file on the host). `GuestFileListing` and
  `GuestFileStatus` are the completion payloads.
- `GuestCapability::ProjectFiles` (bit 9; `KnownCapabilities` is now
  `(1 << 10) - 1`), offered only when `/project` is mounted. Reading and
  listing mounted paths need `Assets`; project writes, Import and Pack need
  `ProjectFiles` (Import and Pack also `SelectedFiles`).
- Host (`WasmFileServices`): mounted opens through the tree; project writes
  stage in memory and commit through `VirtualFileSystem::write` (modules and
  manifests refused); an incrementally maintained project quota
  (`WasmFileLimits::projectBytes`, measured once by an iterative walk);
  Import copies a grant into the project; Pack requires a valid
  `illumo.json` at the source, walks the tree and writes the archive on a
  dedicated pack worker so asset reads never wait behind it.
- Guest SDK: `GuestFiles` holds 16 tasks and in-flight requests, reads and
  writes mounted files in 1 MiB blocks and gains `list`, `stat`,
  `importFile` and `pack`; `GuestFileTree` wraps them with callbacks
  (whole-directory listings page automatically; callbacks may start new
  operations). `ServicesPending` was already raised whenever requests are
  queued, so no flag change was needed.
- Deviation: Pack reports a failed replace (for example an archive that is
  currently mounted and locked) as `IoError` rather than checking mounted
  archive paths up front, because the tree never exposes host paths.
- Tests: `Illumo.Wasm.FileProtocolV2Decoder`, `.MountedFiles` (stat, paged
  list, 1 MiB reads, project write, Import, Pack), `.MountedDeny`
  (capabilities, read-only mounts, modules and manifests, quota, block
  sizes, manifest-less Pack), `.GuestFileTree` (the SDK's file client driven
  natively against the host service); `.FileServices` and
  `.PackageFromArchive` unchanged.
- Benchmark `Illumo.Wasm.Bench.FileThroughput` (label `IllumoBenchmark`),
  32 MiB whole-file read through the SDK: mounted 434 MB/s, storage
  440 MB/s. Larger blocks do not help yet because the directory backend
  resolves the path and reopens the file on every block; caching the open
  stream per `VfsFile` is a follow-up.
- Full Release build plus `ctest -L IllumoWorkspace`: 576/576 passed (every
  package test now speaks protocol v2).

### M12 (2026-09-23)

- `GuestVfsAssets` (guest SDK) replaces `GuestPackageAssets` as
  AssetManager's byte source in every guest (`GuestModuleApplication::
  assetCache()`). Canonical names are absolute virtual paths; relative
  names join `/app`, `\` and `.` are normalized and `..` never resolves.
  - `preload` (fed by `packageAssets()`): pinned bootstrap files read
    through the Mounted area before `bootstrap()`; missing ones are logged
    once.
  - `fetch(paths)` / `fetched(set, missing)` / `release(set)`: the
    collect-fetch-instantiate order for scene loads; requests for a path
    already in flight share one load; a set holds its entries until
    released.
  - A byte budget (256 MiB by default) evicts unpinned, unheld entries least
    recently used first (reads update recency).
  - `putLocal` / `removeLocal`: app-supplied bytes under `/local/<name>`,
    pinned until removed.
- `DefaultAssetSource()` for guests moved to `GuestAssetSource.cpp`, so the
  cache also compiles natively for tests.
- Tests (the SDK cache driven natively against the host service):
  `Illumo.Wasm.GuestPinnedPreload`, `.GuestAssetFetchAndEvict`,
  `.GuestLocalEntries`; every package test preloads through the new cache.
  Full Release build plus `ctest -L IllumoWorkspace`: 579/579 passed.

### M13 (2026-09-23)

- `GuiFileTree` (Illumo/Gui, native and guest): expansion state plus the
  listings received so far, flattened depth first with an explicit stack
  (directories first, capped at 20000 rows); `takePendingListings` names
  the directories to list next; drawn with `GuiKit::drawTreeRow`.
- IllEd `EditorAssetBrowser` docked below the hierarchy (38% of the left
  column): lists the tree through `IllEdPlatform::listDirectory`, expands in
  place, scrolls, drags a file out as a drop and double-clicks a scene to open
  it (with the usual unsaved-changes confirmation).
- `EditorDocument::placeAsset` turns a dropped mesh (.obj) or texture into an
  asset entry plus a mesh-renderer or sprite node, as one pasted command
  (fresh id, reference package-relative to the document's root, an identical
  entry reused); `rebase` moves a document to another package root.
- `IllEdPlatform` gained the tree: `vfs:` document locations, `listDirectory`,
  `hasProject`, `fetchAssets`/`releaseAssets`, `importIntoProject`,
  `packProject`. Guest: `GuestFileTree` and the SDK asset cache (an OBJ's MTL
  libraries fetched in a second stage, missing ones tolerated); Import reads
  the pick first to enforce the texture cap, then copies it with the host.
  Native: a VFS installed through `IllEdNativeTree`.
- Scene loading now collects, fetches, then instantiates. With `--project`
  new documents resolve against `/project`; File gains Save to Project,
  Import to Project and Pack Project; Create gains Light and Camera. Console
  commands `scene_place <path> [x y]` and `scene_save_project` script the
  same flows.
- `GuestApplication::grantedCapabilities()` exposes what the host offered, so
  IllEd enables project commands only with `ProjectFiles`.
- `PackageMounts::packMounted` is the one pack implementation; the host's
  Pack action now uses it.
- Deviation: dropping from the browser places at the release point on the
  edit plane; there is no drag preview in the viewport yet.
- Tests: `Illumo.Gui.FileTreeRows`, `Illumo.Content.PackMounted`,
  `IllEd.Assets.KindsAndIds`, `.ImportValidatesTextureCap`,
  `.BrowserListsTree`, `.ProjectFlow` (drops, reuse, undo, light and camera,
  Save to Project, reopening from the tree), `IllEd.Wasm.ProjectPackage`
  (the real package with a writable `/project`: places a project mesh with
  its MTL and a texture through the guest cache and saves into the project).
  Import and Pack dialogs cannot be scripted natively; their host halves are
  covered by `Illumo.Wasm.MountedFiles` and `.MountedDeny`. Full Release
  build plus `ctest -L IllumoWorkspace`: 586/586 passed.

### M14 (2026-09-23)

- IllumoGame catalogs: `CSimCatalogBootstrap` now runs ReadPackaged, then
  ListPackages (`/packages`), ListCatalogs (`/packages/<id>/csim`, `*.json`,
  packages in id order and files in name order, at most 64), ReadCatalogs,
  then Apply. Package catalogs merge after the packaged pair and before the
  storage `*.user.json` overlays, so a player's edits still win.
  `families*.json` add families and other names add rules. Each merges into a
  candidate registry copy, and an invalid one is skipped with a logged
  warning. The game logs what merged.
- IllumoGame `render3dTest` is data: `IllumoGame/Scenes/render3d-test.ilsc`
  (ground, three axes, an orbiting cube with a wire frame and a child). The
  guest preloads it; the native oracle stages it beside `IllumoGameTests`.
  `CellGameModule` reads it through `AssetManager::assetSource()` (new
  accessor), instantiates it with `SceneInstance` and animates only the
  `orbit` and `child` nodes by id. The procedural graph and `MeshVisual`
  members are gone. `IllumoGameCore` links `Illumo::Content`, and the guest
  links `IllumoGuestContent`. The native fixture now has an `AssetManager`
  over a `VfsAssetSource`, with the executable directory mounted at `/app`.
- IllMeshViewer opens `.ilsc` scenes through `SceneInstance`:
  - Dialog and launch picks go through `openLocation`, which dispatches by
    extension; the dialog pattern is `*.obj;*.OBJ;*.ilsc`.
  - `viewer_open <virtual path>` opens a scene or mesh from the file tree.
  - Loading reads the text, then `collectSceneFetches`, then
    `MeshViewerPlatform::fetchAssets`, then instantiates.
  - The camera frames the world bounds of every node, and the wireframe
    toggle outlines them.
  - The info card becomes "Scene Info" (nodes, assets, missing).
  - The scene's own skybox replaces the default one.
  - Opening a mesh closes the scene, and the reverse.
  - Platform additions: `vfs:` locations, `fetchAssets`/`releaseAssets`, and
    `MeshViewerNativeTree` for the native oracle.
- `GuestSceneFetches` (`IllumoGuest/SceneFetches.h`, in `IllumoGuestContent`)
  is the guest fetch pipeline shared by IllEd and IllMeshViewer: a fetch set,
  then a second stage for the OBJ material libraries it declares, with
  missing libraries tolerated. IllEd's guest platform dropped its private
  copy.
- Deviations:
  - A loose `.obj` keeps the dedicated mesh path (centered, normalized, one
    retained `MeshVisual`, triangle wireframe) instead of becoming a temporary
    `/local` scene, so mesh metadata and the retained-mesh contract are
    unchanged.
  - The viewer's default skybox stays a package preload rather than an asset
    of a default scene environment.
  - There is no `--open x.ilpk`. A packaged scene is viewed by mounting it
    (`packages/` or `--mount`) and running `viewer_open`.
  - A scene picked through a dialog has no package, so its relative
    references resolve under `/local` and draw as placeholders. Absolute
    `/engine` and `/packages` references still work.
- Tests:
  - `IllumoGame.Wasm.CatalogMerge`: order, families versus rules, and an
    invalid catalog skipped with a warning.
  - `IllumoGame.CellGame.Render3dTestFlag`: the scene loads, `orbit` and
    `child` exist and animate, and one World drawable.
  - `IllumoGame.Wasm.GamePackage`: the 3D mode draws lit and shadowed from
    the scene.
  - `IllMeshViewer.Module.SceneFromTree`: `viewer_open` from a mounted
    package, package-relative mesh, card, framing, drawables and wireframe,
    mesh/scene exchange, missing and relative paths, and a dialog pick under
    `/local`.
  - `IllMeshViewer.Module.SceneRejectsInvalid`.
  - `IllMeshViewer.Wasm.ScenePackage`: the real guest opens a mounted
    package's scene, and its torus is fetched and retained, with 1 retained
    mesh and 4 retained draws.
  - `IllMeshViewer.Wasm.Package` and `IllEd.Wasm.ProjectPackage` are
    unchanged and pass.
  - Full Release build plus `ctest -L IllumoWorkspace`: 590/590 passed.

### M15 (2026-09-23)

- The debug `files` browser:
  - `IFileTreeSource` (`Illumo/Services/FileTreeSource.h`) is a read-only core
    view of a file tree.
  - `IllumoContext::fileTree` is the one context member a module publishes:
    `WasmGameModule` sets it to a `VfsTreeSource` over its virtual file tree
    once the guest starts, and clears it on Exit.
  - DebugModule's `files [path|off]` command opens `FileTreeOverlay`, a
    `GuiFileTree` panel. It lists directories synchronously as they expand,
    shows size and supplying package, and closes itself if the tree is
    withdrawn.
  - Input is keyboard and wheel only, all consumed before the product: arrows,
    Page/Home/End, Enter, Escape.
  - Debug and RelWithDebInfo only, like the rest of DebugModule.
- Docs:
  - Architecture consensus: summary, source table, scene paragraph, §5.12
    packages bullet and tests, decision rows D-E18 to D-E26 and D-UI6, D-E10
    marked superseded.
  - Decision log D-E18 to D-E26 and D-UI6. D-E10 is marked superseded by
    D-E18/D-E19, and D-E14 is refined by D-E22.
  - LaTeX chapters 02, 03, 05, 07, 08, 10, 12, A and B.
  - New `docs/packages/content.md`. Updated package docs: README, assets,
    scene, app, source-layout, services, engine, illed, tests.
  - `wasm-game-runtime-design.md` (dated notes), `contributing.md`, and a
    dated note in `scene-graph-v2-design.md`.
  - README: runtime, IllEd and the viewer.
  - Root and nested AGENTS: Content layering, virtual-path and mount rules,
    `fileTree`, Gui, Engine, Scene and Tests.
  - New `Illumo/Source/Content/AGENTS.md`.
  - PDF rebuilt with no errors or undefined references.
- IllumoTidy findings fixed:
  - integer division in float context (`TestSceneInstance`);
  - embedded NUL literal (`TestPackageMounts`);
  - `atoi`/`atoll` replaced by a whole-string `std::from_chars`
    (`VfsConsole`);
  - an unused local (`TestWasmFiles`).
- Where the code differs from the design (the design text is kept; these are
  the facts):
  - Archive limits are 256 MiB per entry, 1 GiB total and a ratio of 1024,
    not 512 MiB and 1:200.
  - There is no separate `inflatedBytes` host cap; archive limits bound
    inflation.
  - `SceneInstance` keeps its own id map and sets no graph user data.
  - There is no `.ilpk` staging target; `IllumoPack` and the Pack action
    make archives.
  - Project logic lives in `EditorModuleCommands.cpp` and `IllEdPlatform`
    (no `EditorProject` unit); pick proxies are
    `SceneInstanceOptions::pickProxies` (no `EditorPickProxy`).
  - The fetch helper is the free `collectSceneFetches`.
  - Snap increments live in the scene's editor block; the inspector's Add
    component offers shape, light and camera, while mesh and sprite nodes
    come from dropping a file or `scene_place`.
  - A packed target that is mounted fails its replace with `IoError`; there
    is no up-front refusal.
  - A `mod` manifest section is decoded, but mod modules still load only
    through `--mod`.
- Follow-ups carried forward:
  - whiteouts;
  - chunked textures past the 16 MiB cap;
  - a material system (MTL colour is read but not applied);
  - a folder dialog;
  - remapping ids inside opaque extension data;
  - `vfs mount`/`unmount`;
  - an IllEd drag preview;
  - faster mounted reads (the directory backend reopens per block);
  - loading mod modules from `mod` packages;
  - `--open x.ilpk` in the viewer;
  - scripted `@key` presses reach the product, not DebugModule, so the
    browser's navigation is covered by `Illumo.Debug.FileTreeOverlay` rather
    than by a capture script.
- Full gate:
  - Release build plus the in-build `IllumoRunTests` pass: 592/592.
  - ASan Debug `ctest -L IllumoWorkspace -E "Bench|Oracle"`: 573/573. The
    Oracle case is excluded as the README advises; it passed under Release.
  - Coverage (`build-f34-coverage`, full build first so the WASM runners
    exist): 594/594 tests and 85.11% production-line coverage (gate 85%).
    The Content sources are at 80-100%.
  - `IllumoTidy`: clean after the fixes above.
  - `tools/verify_capture.py`: 7 invocations passed.
  - Real-GPU captures: IllMeshViewer opening a `--mount`ed package's scene
    with `viewer_open`, and the Debug `files` browser over `/packages`.
- Benchmarks (Release):
  - `Illumo.Content.Bench.Instantiate2000`: load 3.65-3.91 ms, 1,000 edits
    0.72-0.92 ms, encode 45-53 ms. Unchanged from M3; one cold first run
    read 8 ms.
  - `Illumo.Wasm.Bench.FileThroughput`: mounted 450 MB/s, storage
    488 MB/s (M11: 434 and 440).
  - `IllEd.SceneGraph.Bench.EditLatency` (2,000 cubes): recolor 382 us,
    ray 85 us, BVH query 18 us.
- Untracked `IllEd/Source/IlscCodec.cpp`/`.h` (dated 2026-09-18/19, differing
  from HEAD) are in the working tree beside the staged deletion. They are not
  built and were not created by this effort; left for the owner.
