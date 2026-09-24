# Illumo assets and runtime staging

Illumo owns `Shader/`, `Assets/RendererDemo`, fonts, notices, third-party
dependencies, and redistribution licenses. `AssetManager` retains its managed
file texture/shader/static-mesh caches, stable typed handles, reference
counting, and shutdown-before-backend lifetime. Texture and shader assets use
the one CPU file/decode worker, render-thread GPU pump, Debug timestamp polling,
explicit reload, and last-good fallbacks. Static meshes are synchronously
decoded through `MeshLoader`, enrolled once, and shared by handle; they do not
yet participate in hot reload.

`AssetManager` reads every byte through an `IAssetSource`
(`assetSource()`). Natively the default is the file system; a tool or test can
supply `VfsAssetSource` (`Illumo::Content`), whose canonical names are
normalized absolute virtual paths with relative names joined to `/app`. Every
WASM guest uses the SDK's `GuestVfsAssets` (`GuestModuleApplication::
assetCache()`), which replaced `GuestPackageAssets`: `packageAssets()` names
pinned preloads read before `bootstrap()`, fetch sets load a scene's
references on demand (collect, fetch, then instantiate), unpinned and unheld
bytes are evicted least recently used past a 256 MiB default budget, and
`putLocal` holds app-supplied bytes under `/local/<name>`. Reads stay
synchronous. A mount change does not hot-reload guest assets. When the source
has no file system, `acquireMesh` reads each `mtllib` beside the OBJ through
the same source (`MeshLoader::materialLibraryNames`) and passes the text as
`MeshLoadOptions::materialText`; native files keep tinyobj's search beside the
OBJ.

The library supplies the runtime-staging implementation that copies those
files beside a consuming executable. Staging copies current inputs but does
not prune stale output files when a source shader or asset is removed.
Shared runtime-copy stages are ordered to prevent concurrent writes to the
same output directory. IllumoGame, IllEd, and IllMeshViewer supply their own
`envvars.json` seeds as product data. Illumo's locked copy-if-missing operation
preserves existing user settings; when products share an output directory,
the first successful seed creates the shared file.

For `IllumoRuntime`, `illumo_stage_app` (`cmake/IllumoWasm.cmake`) stages each
application as a loose package in `apps/<name>/`: its `illumo.json` (which
replaced `app.json`), modules, `envvars.json` and package-relative data such
as `Scenes/render3d-test.ilsc` or `Assets/IllEd/editor-ui-atlas.jpg`. The
runtime `Assets/` directory is mounted read-only at `/engine`.
`IllumoPack` packs a staged directory into an `.ilpk`; `Illumo.Pack.StagedApp`
packs and verifies the staged IllEd. See `content.md`.
