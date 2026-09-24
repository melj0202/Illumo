# Illumo Content

`Illumo::Content` (target `IllumoContent`, headers `<Illumo/Content/...>`,
sources `Illumo/Source/Content/`) is the optional content layer above the
engine: the virtual path grammar, `illumo.json` package manifests, `.ilpk`
archives, the host's virtual file tree, the `.ilsc` format 2 scene model and
codec, and `SceneInstance`, the one scene loader every Illumo program uses.
The design is `../content-packages-and-scenes-design.md`; the execution
record `../content-packages-and-scenes-plan.md` is authoritative where the two
differ.

## Layering

`IllumoContent` links `Illumo::Illumo` publicly and nlohmann privately; only
`IlscCodec.cpp` and `PackageManifest.cpp` include `<nlohmann/json.hpp>`. Core
Illumo never includes `<Illumo/Content/...>`. Content has no dependency on
`Illumo/Wasm`, the guest SDK or any product; the WASM host
(`Illumo/Source/Wasm`, `IllumoRuntime`), the `IllumoPack` tool and the
products depend on it. Core engine tools see the tree only through the
generic `IFileTreeSource` (`<Illumo/Services/FileTreeSource.h>`).

| Unit | Native | Guest | Role |
|---|---|---|---|
| `VirtualPath` | yes | yes | Path grammar, normalization, joins, containment |
| `PackageManifest` | yes | yes | `illumo.json` decoder, `orderPackages` |
| `SceneDocument` | yes | yes | Plain-value scene model, `validateSceneDocument` |
| `IlscCodec` | yes | yes | `.ilsc` format 2 parse and canonical encode |
| `SceneAssetRefs` | yes | yes | Package roots, reference resolution, fetch lists |
| `SceneInstance` | yes | yes | Live `SceneGraph` plus attachments from a document |
| `PackageArchive` | yes | no | `.ilpk` ZIP-subset reader, writer, CRC-32 |
| `VirtualFileSystem` | yes | no | Mount table, layered mounts, directory/archive backends |
| `VfsAssetSource` | yes | no | `IAssetSource` over the tree |
| `VfsConsole` | yes | no | Formatters for the host `vfs` command |
| `VfsTreeSource` | yes | no | `IFileTreeSource` over the tree for engine tools |
| `PackageMounts` | yes | no | Open, discover, mount and pack packages |

The guest build compiles the serial-safe subset as `IllumoGuestContent`
(`IllumoGuest/CMakeLists.txt`), which also carries `GuestSceneFetches`
(`<IllumoGuest/SceneFetches.h>`). Archives and the tree stay host-side;
guests reach them through file protocol v2.

## Virtual paths

A virtual path is absolute, `/`-separated and case-sensitive, at most 1,024
bytes (`VirtualPath::kMaximumPathBytes`). `validComponent` accepts non-empty
well-formed UTF-8 of at most 255 bytes with no `\ / : * ? " < > |`, control
characters, `.` or `..`, trailing dot or space, `.illumo-` staging prefix or
DOS device stem (the last two compared case-insensitively). `validRelative`
is the same rule for relative names and is what `WasmFileServices` applies to
package and storage names. `normalize` collapses repeated `/` and drops a
trailing one but never resolves `..`; `join` resolves a reference against a
base directory; `parent`, `fileName`, `mountName`, `isWithin` and
`relativeTo` complete the set.

## `illumo.json`

Every package, loose directory or `.ilpk`, carries `illumo.json` at its root
(`PackageManifest::kFileName`). It replaces the former `app.json`; the three
in-tree manifests are `IllumoGame/illumo.json` (id `csim`),
`IllEd/illumo.json` (`illed`) and `IllMeshViewer/illumo.json` (`meshviewer`).
`decodePackageManifest` is strict: an unknown key, wrong type or invalid value
rejects the whole manifest, because it requests budgets. Top-level keys are
`format` (`"ilpk"`), `format_version` (1), `id` (`validPackageId`:
`[a-z0-9._-]`, at most 64), `version`, `title` (the window title for apps),
`kind`, `targets`, `dependencies`, `overlays`, `app` and `mod`.

- `kind` is `app`, `content` or `mod`; `app` requires the `app` section,
  `mod` requires the `mod` section, `content` has neither.
- `app` carries `module`, `worker`, `launchAccess` (`read` or `edit`),
  `metering` (`fuel` or `epoch`), `memoryMiB`, `fuelPerCall`,
  `deadlineMilliseconds`, `workers`, `workerMemoryMiB` and
  `workerDeadlineMilliseconds`, clamped to `PackageCeilings`.
- `mod` carries `module` and `extensionApi`.
- `targets` lists application ids the package applies to (`"*"` for all).
- `dependencies` are `{id, version}`; the version is recorded, not compared.
  `orderPackages` orders them with Kahn's algorithm (ties by id) and rejects
  packages with missing dependencies or on a cycle, with a reason each.
- `overlays` are `{target, priority}`; format 1 accepts only `/app`.

## `.ilpk` archives

`PackageArchive` reads a strict ZIP subset over a positional
`IPackageByteSource` (`MemoryPackageSource`, or `FilePackageSource`, one file
handle with mutex-guarded reads). It accepts stored and deflate entries, data
descriptors, UTF-8 names and explicit directory entries, and refuses zip64,
multi-disk archives, encryption, other methods, symlinks and special files,
names that fail `VirtualPath::validRelative`, duplicates, file/directory
collisions, local headers that disagree with the central directory,
overlapping entries, and anything past `PackageArchiveLimits` (by default
65,535 entries, a 16 MiB central directory, 256 MiB per entry, 1 GiB in
total and an inflate ratio of 1024). A full `read` inflates into an exactly
sized buffer through stb's `stbi_zlib_decode_noheader_buffer` and checks the
CRC-32 (`packageCrc32`); `readRange` reads stored entries in place.

`PackageArchiveWriter` is deterministic: sorted names, fixed 1980-01-01
timestamps and the UTF-8 flag. It deflates through the engine's
`stbi_zlib_compress` (the vendored `stb_image_write` compressor) and stores
already-compressed data (`storesUncompressed`) or data deflate cannot shrink.
`write` replaces the destination through `AtomicFile`. The `IllumoPack` host
tool (`Illumo/tools/IllumoPack.cpp`) runs `IllumoPack <package-dir>
<out.ilpk>` (a valid `illumo.json` is required) and `IllumoPack --verify
<file.ilpk>` (every entry read back with its CRC checked).

## Virtual file tree

`VirtualFileSystem` (native only) holds an immutable
`std::shared_ptr<const VfsMountTable>` swapped under a mutex; each request
copies the pointer once, so the console and the file-service worker never see
a half-built table. Mount points are normalized, never `/`, and never nest;
`/` and `/packages` are synthesized directories. The runtime mounts:

| Mount | Source | Writable |
|---|---|---|
| `/engine` | runtime `Assets/` directory | no |
| `/app` | the launched package, under its targeted overlays | no |
| `/packages/<id>` | each package in `packages/` beside the runtime, plus each `--mount` | no |
| `/project` | `--project <dir>` | yes |

A `VfsMount` has layers, top first. In a layered mount (only `/app` today) the
topmost layer holding a name decides its kind, a file hides the name in every
lower layer, and directories union; there are no whiteouts. File/directory
collisions are found at mount time and kept in `VfsMount::conflicts`. `stat`
returns kind, size, stamp and the supplying package id; `list` returns a
name-sorted, paged, merged listing (`kMaximumListPage` 256); `read` and
`readRange` read bytes; `open` returns a `VfsFile`, which holds its backend so
it stays readable after its mount is removed. `write` works only on a
single-layer writable mount and refuses `*.wasm` and `illumo.json`. Nothing
returns a host path.

`DirectoryVfsBackend` compares each component against the exact on-disk name,
so lookups are case-sensitive on every host and a loose folder behaves like
its packed archive; a path that leaves the root through a symlink or junction
does not exist, staging and invalid names are hidden from listings, and writes
create parents one checked component at a time and commit through
`AtomicFile`. `ArchiveVfsBackend` serves a `PackageArchive`.

`PackageMounts` assembles the tree for `IllumoRuntime` and tools. `open` reads
a directory or `.ilpk` and its manifest (at most 64 KiB). `discover` returns
every package directly inside a directory, in file-name order, skipping
unreadable packages and taken ids with warnings. `mountAll` mounts `/engine`,
`/app` (the launched package under every overlay whose package targets it,
stacked by priority, then dependency order, then id), `/packages/<id>` for
every package whose dependencies resolve (an overlaying package is mounted
there too) and the writable `/project`. `packMounted` packs a mounted
directory that holds a valid `illumo.json` into an `.ilpk`; it is the one
pack implementation, used by the host's Pack file action.

`VfsAssetSource` is an `IAssetSource` over the tree for native tools and
tests: canonical names are normalized absolute virtual paths, relative names
join a base (`/app` by default), and invalid references fail rather than
touching the host file system. `VfsConsole` holds the pure formatters behind
the host `vfs` command (`mounts`, `ls <path>`, `tree <path> [depth]`, `stat
<path>`, `cat <path> [bytes]`); `tree` is an explicit-stack walk capped at 400
lines, and `cat` previews text or dumps hex, 4 KiB by default and at most
64 KiB. `vfs mount` and `vfs unmount` were not added; the mount table is fixed
per launch. `VfsTreeSource` publishes the tree as a read-only
`IFileTreeSource` (at most 4,096 entries per listing); `WasmGameModule` sets
it as `IllumoContext::fileTree`, which the debug `files` browser reads.

## `.ilsc` format 2

`SceneDocument` is the plain-value scene: metadata, settings (world mode and
one environment with skybox, ambient and sun), an asset table (`mesh`,
`texture`, `atlas`, `cubemap_cross`, `cubemap_faces`), nodes in graph preorder
with transform, tags and components (a `std::variant` of primitive, mesh,
sprite, light, camera and opaque), namespaced extensions, and an optional
editor block. `validateSceneDocument` enforces the codec's invariants on
programmatic documents too; parents precede children, so cycles cannot occur.

`IlscCodec::parse` is strict and transactional: core objects reject unknown
keys, wrong types and out-of-range values; a component type or extension key
containing a dot is preserved verbatim as opaque data; an unknown core type or
a newer minor fails with "requires format 2.N"; a format 1 file fails with an
explicit message (there is no v1 reader). `IlscCodec::encode` is canonical
(schema key order, two-space indentation, shortest round-trip floats), so
encoding a document twice is byte-identical; the editor block is written only
when requested. Details of the shape are in design section 10, with the M2
deviations in the plan (namespaced members inline beside `type`, sprite
`cell` into an atlas `grid`, a solid `sphere` primitive).

`SceneAssetRefs` makes references package-relative. `scenePackageRoot` maps a
scene path to its package root (`/packages/<id>`, else the first component:
`/app`, `/project`, `/engine`, `/local`); `resolveSceneReference` resolves a
relative or absolute reference against it; `sceneReferenceFor` writes a
reference relative when the target is in the same package; and
`collectSceneFetches` lists every resolved path a document needs, reporting
unresolved references. `objMaterialLibraries` resolves an OBJ's `mtllib`
names through `MeshLoader::materialLibraryNames`. A scene saved in a project
therefore keeps working after it is packed and mounted at `/app` or
`/packages/<id>`.

## `SceneInstance`

`SceneInstance` is a live scene, not a one-shot builder: it owns the document
state, one `SceneGraph`, the `SceneGraphDrawable`, a `MeshVisual` per
renderable component and the asset references they hold. It builds the graph
only through the public SceneGraph API; stable ids become graph names. `load`
validates first and leaves the current scene untouched on failure. Edits
(`insertNode`, `removeSubtree`, `setParent` with an insert-before sibling,
`setTransform(s)`, `setComponents`, `replaceNode`, `setAssets`,
`setEnvironment` and others) touch only the affected nodes, invalidate
snapshots before reconfiguring attachments, and bump `revision()`;
`setEditorState` never does. `document()` is rebuilt lazily in preorder.

Solid primitives share one unit mesh per shape acquired with
`AssetManager::acquireMesh(MeshData)`; wire shapes stay procedural. Meshes,
texture and atlas sprites and cubemaps load through `AssetManager`; a missing
or failed asset draws a magenta placeholder and adds to `warnings()`. Lighting
comes from the first enabled, visible light component in preorder, else the
environment sun; 2D scenes are unlit. The skybox is a separate `SkyboxVisual`
drawable (`skybox()`), not a graph node. `pickRay` uses attachment local
bounds with rotation, scale and effective visibility; `SceneInstanceOptions::
pickProxies` adds pick boxes for nodes that draw nothing. Programs call
`update()` once per frame. All of it is main-thread affine.

IllEd's `EditorDocument`, IllMeshViewer's scene view and IllumoGame's
`render3dTest` scene all load through `SceneInstance`. Guests first fetch the
paths from `collectSceneFetches` (and the OBJ material libraries) through
`GuestSceneFetches` into the guest asset cache, then instantiate.

## Tests

`IllumoTests` covers the library under `Illumo.Content.*`:

- paths and manifests: `VirtualPathNormalize`, `VirtualPathRejects`,
  `PackageManifestDecode`, `PackageManifestClampsBudgets`,
  `PackageManifestKindMismatch`, `DependencyOrderAndCycles`;
- scene format: `IlscRoundTrip`, `IlscCanonicalGolden`, `IlscRejects`,
  `IlscPreservesExtensions`, `SceneAssetRefsResolve`;
- instances: `InstancePrimitives`, `InstanceEnvironmentLight`,
  `InstanceSkyboxAndAssets`, `InstanceIncrementalEdit`,
  `InstanceSiblingOrder`, `InstancePicking`, and the benchmark
  `Bench.Instantiate2000`;
- archives: `ArchiveCrc32`, `ArchiveRoundTrip`, `ArchiveReadsCMakeZip`
  (a fixture zipped by `cmake -E tar --format=zip`), `ArchiveInflateBounded`,
  `ArchiveRejectsMalformed`, `ArchiveFuzzSmoke`;
- tree: `VfsMountsAndStat`, `VfsOverlayOrder`, `VfsEscapeRejected`,
  `VfsConcurrentReads`, `VfsUnmountWhileOpen`, `VfsWriteOnlyWritable`,
  `VfsAssetSourceTexture` (texture and OBJ plus MTL through `AssetManager`),
  `VfsConsoleListing`, `VfsTreeSource`;
- packages: `PackageDiscovery`, `PackMounted`.

Related cases outside the prefix: `Illumo.SceneGraph.SiblingInsertBefore`,
`Illumo.Debug.FileTreeOverlay`, `Illumo.Pack.StagedApp`,
`Illumo.Runtime.MountMissingDir`, `.ProjectMissingDir` and `.PackageMissing`,
and the host file-service cases `Illumo.Wasm.PackageFromArchive`,
`.FileProtocolV2Decoder`, `.MountedFiles`, `.MountedDeny`, `.GuestFileTree`,
`.GuestPinnedPreload`, `.GuestAssetFetchAndEvict` and `.GuestLocalEntries`.
The symlink branch of `VfsEscapeRejected` runs only where the account may
create links.
