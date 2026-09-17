# Illumo assets and runtime staging

Illumo owns `Shader/`, `Assets/RendererDemo`, fonts, notices, third-party
dependencies, and redistribution licenses. `AssetManager` retains its managed
file texture/shader/static-mesh caches, stable typed handles, reference
counting, and shutdown-before-backend lifetime. Texture and shader assets use
the one CPU file/decode worker, render-thread GPU pump, Debug timestamp polling,
explicit reload, and last-good fallbacks. Static meshes are synchronously
decoded through `MeshLoader`, enrolled once, and shared by handle; they do not
yet participate in hot reload.

The library supplies the runtime-staging implementation that copies those
files beside a consuming executable. Staging copies current inputs but does
not prune stale output files when a source shader or asset is removed.
Shared runtime-copy stages are ordered to prevent concurrent writes to the
same output directory. IllumoGame, IllEd, and IllMeshViewer supply their own
`envvars.json` seeds as product data. Illumo's locked copy-if-missing operation
preserves existing user settings; when products share an output directory,
the first successful seed creates the shared file.
