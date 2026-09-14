# Illumo assets and runtime staging

Illumo owns `Shader/`, `Assets/RendererDemo`, fonts, notices, third-party
dependencies, and redistribution licenses. `AssetManager` retains its managed
file texture/shader cache, one CPU file/decode worker, render-thread GPU pump,
stable typed handles, Debug timestamp polling, explicit reload, last-good
fallback, reference counting, and shutdown-before-backend lifetime.

The library supplies the runtime-staging implementation that copies those
files beside a consuming executable. Staging copies current inputs but does
not prune stale output files when a source shader or asset is removed.
Shared runtime-copy stages are ordered to prevent concurrent writes to the
same output directory. IllumoGame, IllEd, and IllMeshViewer supply their own
`envvars.json` seeds as product data. Illumo's locked copy-if-missing operation
preserves existing user settings; when products share an output directory,
the first successful seed creates the shared file.
