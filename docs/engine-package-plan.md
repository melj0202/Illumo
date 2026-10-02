# Engine and application packages for distribution

Execution plan (Tier 2, `.agent/PLANS.md`) for shipping Illumo as archives:
the engine's own files in `engine.ilpk` and every installed application as
`apps/<name>.ilpk`, produced by one distribution step. Status values: pending,
in progress, done, deferred.

| # | Milestone | Status |
|---|---|---|
| E1 | Overridable default asset source; shader and font reads go through it | done |
| E2 | `EnginePackage`: open `engine.ilpk`, serve it to the host and mount it at `/engine` | done |
| E3 | Engine manifest and archive layout | done |
| E4 | Mesh viewer reads the skybox from `/engine` instead of its own copy | done |
| E5 | Hot reload from a rebuilt `engine.ilpk` | done |
| A1 | Distribution target: runtime, `engine.ilpk`, `apps/*.ilpk` | done |
| A2 | `python build.py dist` | done |
| D1 | Decision D-E38, design and canonical documentation | done |

## 1. Objective and end state

A distribution folder holds only the runtime executable and its DLLs,
`envvars.json`, `licenses/`, `engine.ilpk` and `apps/<name>.ilpk`; there is
no loose `Assets/` or `Shader/`. Launched from it, every application runs as it
does from a development build: the splash, host fonts, all three graphics
backends' shaders, guest fonts, `/engine` reads (the badge) and the debug
overlay. Development builds keep staging loose folders, so shader and asset
iteration is unchanged.

Measured end state:

- the distribution folder contains no loose engine or application files;
- `IllumoRuntime --app game|illed|meshviewer|playground --capture` from it
  succeeds on OpenGL, and the game also on Vulkan and Direct3D 12, with the
  splash-free capture matching a development-build capture;
- a headless test proves shader preprocessing, font loading and an
  `AssetManager` texture all read from an engine archive with no loose files
  present.

## 2. Current-state evidence

- `illumo_stage_app` (`cmake/IllumoWasm.cmake`) copies loose folders into
  `apps/<name>/`; nothing produces `.ilpk` except the `Illumo.Pack.StagedApp`
  test. The content-packages plan records "There is no `.ilpk` staging
  target" as a deviation from design §9.1.
- The runtime already launches archives: `--app` falls back to
  `apps/<name>.ilpk` (`RuntimeApplication.cpp:310-321`), and `--package`,
  `--mount` and `packages/*.ilpk` accept them. Packing the staged game with
  `IllumoPack` and launching it with `--package game.ilpk` works today
  (worker, music, catalogs; 8.5 MB against 15.7 MB loose).
- `/engine` is the loose `Assets/` directory by design (D-E21, design §6.2):
  `PackageMounts::mountAll` opens only a `DirectoryVfsBackend` there.
- Host reads that bypass the virtual file tree:
  - shaders: `RendererStyles.cpp` and `Renderer.cpp` name `Shader/*.glsl`;
    the OpenGL, Vulkan and Direct3D 12 backends and `AssetManager` read them
    with `ShaderPreprocessor::ProcessFile` (`std::ifstream`). No engine shader
    uses a file `#include`; the built-in modules are virtual.
  - fonts: `Font::loadFile` calls `FT_New_Face` on a path; `getDefaultFont`
    and `resolveFontPath` probe the file system; `WasmRenderServices` serves
    guest fonts from `<runtime>/Assets/...`.
  - textures through the host `AssetManager` (the splash, the renderer demo
    atlas) use `DefaultAssetSource()`, the native file source.
- `prepareRuntime` (`applyDefaults`) runs before the window, renderer and
  backends exist, so a source installed there is in place before any shader
  or font loads.
- Mesh viewer ships its own copy of `Skybox/skybox-daylight.png` (1.2 MB) in
  its package. `GuestProgram::packageAssets` already accepts absolute virtual
  paths (the engine badge preloads from `/engine`).

## 3. Scope and non-goals

In scope: the default-source seam, the engine archive and its `/engine` mount,
the mesh viewer skybox, a distribution target and `build.py dist`, tests and
documentation.

Not in scope:

- an installer, code signing, or a zip of the distribution folder;
- packing `licenses/` (notices stay readable files) or the runtime's
  `envvars.json` (user-edited settings);
- file `#include`s in shaders read from an archive (they keep resolving on the
  native file system; no engine shader uses one);
- hot reload from archives (archives are immutable; stamps are 0) — later
  brought in scope as E5, see the validation log;
- changing development staging, `packages/` discovery or the `.ilpk` format.

## 4. Constraints and invariants

- Core `Illumo` never includes `<Illumo/Content/...>`; the archive-backed
  source lives in Content and reaches core only as an `IAssetSource`.
- No host path crosses the ABI; guests still see `/engine/...` only.
- Engine and package mounts stay read-only.
- No exceptions (D-F3); failures return errors and are logged.
- A loose `Assets/` directory beside the runtime wins over `engine.ilpk`, as a
  loose `apps/<name>/` wins over `apps/<name>.ilpk`, so development builds are
  untouched and a stray archive cannot shadow edited files.
- Behaviour with no archive is byte-identical to today: the default source is
  still the native file source.

## 5. Design

### 5.1 Default asset source (core, E1)

`AssetSource.h` gains `SetDefaultAssetSource(IAssetSource*)`. It is set once,
before engine startup, and read through an atomic afterwards; `nullptr`
restores the native file source. The caller keeps the source alive.

Reads that go through `DefaultAssetSource()`:

- `ShaderPreprocessor::ProcessFile`: `canonical()` then `read()`, instead of
  `std::ifstream`; a missing file fails with the same message as before.
- `Font::loadFile`: the face and the fallback face are read as bytes and
  opened with `FT_New_Memory_Face`; the bytes live until rasterization ends.
  `getDefaultFont` tries to load each candidate instead of probing
  `std::filesystem::exists`.
- The host `AssetManager` already defaults to it.

`WasmRenderServices` keeps building absolute font paths under the runtime's
`Assets/`; the engine source maps those back to archive members.

### 5.2 Engine package (Content, E2)

`Illumo/Content/EnginePackage.h`:

- `EnginePackage::open(runtimeDirectory, ceilings, error)` returns the
  archive's backend when there is no loose `Assets/` directory and
  `engine.ilpk` is present, after `PackageMounts::open` validates its
  manifest (kind `content`, id `illumo-engine`); it returns null otherwise.
- `EnginePackageSource : IAssetSource` maps runtime-relative names onto
  archive members: `Assets/<x>` to `<x>` and `Shader/<x>` to `Shader/<x>`,
  for relative paths and for absolute paths under the runtime directory. A
  member that exists gets the canonical name `engine:<member>`; anything else
  falls through to the native file source, so product files and user files
  keep working. Archive stamps are 0; `hasFileSystem()` stays true.
- `PackageMounts::mountAll` gains an overload taking the engine backend, so
  `/engine` mounts the archive (or the loose directory) through one path.

`RuntimeApplication::prepareRuntime` opens the package and installs the
source for the process lifetime; `prepareShell` mounts the same backend at
`/engine`.

### 5.3 Archive layout (E3)

```
engine.ilpk
  illumo.json           {"format":"ilpk","format_version":1,"id":"illumo-engine",
                         "kind":"content","title":"Illumo engine","version":<build>}
  Branding/ Fonts/ RendererDemo/ Skybox/    (Illumo/Assets, as /engine today)
  Shader/                                   (Illumo/Shader)
```

The manifest source lives in `Illumo/EnginePackage/illumo.json` and is
stamped like application manifests. `/engine/Shader/` becomes visible to
guests from an archive; it is read-only GLSL.

### 5.4 Distribution (A1, A2)

A non-default CMake target `IllumoDistribution` (after `IllumoRuntime` and
`IllumoPack`) runs `cmake/IllumoDistribution.cmake`, which rebuilds
`<build>/dist/<config>/`:

1. copies `IllumoRuntime.exe`, its runtime DLLs, `envvars.json` and
   `licenses/`;
2. stages `Illumo/Assets`, `Illumo/Shader` and the stamped engine manifest
   into a scratch directory and packs it to `engine.ilpk`;
3. packs every staged `apps/<name>/` to `apps/<name>.ilpk`;
4. verifies every archive with `IllumoPack --verify`.

`python build.py dist` builds Release and this target and prints the folder.

### 5.5 Alternatives considered

- **Mount the archive and have host code read `/engine` through the VFS.**
  The renderer and fonts load before any VFS exists and core cannot depend on
  Content; the `IAssetSource` seam already exists for exactly this.
- **Embed shaders and fonts in the executable.** Removes the loose files but
  forces a rebuild per asset edit and diverges from the package system mods
  use.
- **Move shaders under `Assets/`.** Cleaner tree, but renames every shader path
  across three backends and the tests for no functional gain.
- **Make development staging produce archives too.** Slower iteration and
  breaks Debug hot reload; development keeps loose files.

## 6. Contracts and compatibility

- New public API: `SetDefaultAssetSource`, `EnginePackage`,
  `EnginePackageSource`, a `mountAll` overload. Existing signatures stay.
- Distribution layout is new; development layout is unchanged.
- The design doc's §6.2 `/engine` row and D-E21's "runtime assets" become
  "runtime `Assets/` or `engine.ilpk`" (D-E38 refines D-E21).

## 7. Ownership, threading, errors, platform

- The engine source is a function-static in the runtime, alive until exit; the
  atomic default pointer is set before the engine starts and never changed
  while it runs.
- `AssetManager`'s decode worker may read concurrently; archive backends
  already serialize positional reads under a mutex, and deflated entries
  inflate per read.
- An invalid `engine.ilpk` (bad manifest or archive) fails startup with a
  logged error, as an invalid application package does; a missing one with no
  loose `Assets/` logs a warning and continues (text falls back to the built-in
  atlas, as today).
- Windows only, like the runtime; the core seam is portable.

## 8. Milestones

E1, E2, E3, E4, A1, A2, D1 in table order. E1 alone is behaviour-neutral and
gated by the full suite; E2 adds the headless engine-archive test; A1 enables
the end-to-end distribution captures.

## 9. Verification

- Headless: `Illumo.Content.EnginePackage` builds an engine archive in a
  temporary directory from `Illumo/Assets` and `Illumo/Shader`, works from a
  directory without loose files, installs the source and checks
  `ShaderPreprocessor::ProcessFile`, `Font::loadFile`, `getDefaultFont`,
  `AssetManager` texture loading and the `/engine` mount; then restores the
  default source. `TestFont` and `TestShaderPreprocessor` keep passing
  unchanged.
- Full Release build and `ctest -L IllumoWorkspace`; `IllumoTidy` for the new
  and changed translation units.
- Real GPU: captures from the distribution folder for every application, the
  game on all three backends, compared with development-build captures.
- Manual: an interactive launch from the distribution folder shows the splash.

## 10. Rollback and containment

E1 is behaviour-neutral (the default is unchanged) and revertible alone. The
archive path activates only when `engine.ilpk` exists and `Assets/` does not,
so development builds cannot regress; deleting the archive restores loose
behaviour.

## 11. Open questions

None blocking. Decided here: the distribution goes to `<build>/dist/<config>/`
rather than an install prefix; loose wins over archives; licences stay loose.

## 12. Validation log

2026-10-02, Release, Windows 11, RTX 4080:

- Implemented as designed in §5. `Illumo.Content.EnginePackageOpen` (no
  package, a distribution's package, loose `Assets/` winning, a wrong id) and
  `Illumo.Content.EnginePackageSource` (member mapping for relative and
  absolute names, fall-through, stamps, byte-identical reads, shader
  preprocessing, relative and absolute font loads, `/engine` through the new
  `mountAll` overload, restoring the default) pass; so do the existing font,
  shader, branding, splash, Vulkan and Direct3D 12 shader cases.
  `Illumo.Dist.Assemble` assembles a distribution under `Testing/dist`.
- `IllumoDistribution` produced `dist/Release/` with only `IllumoRuntime.exe`,
  `IllumoWasmCompiler.exe`, `wasmtime.dll`, `envvars.json`,
  `THIRD_PARTY_NOTICES.md`, `licenses/`, `engine.ilpk` (3.4 MB, 44 files) and
  `apps/{game,illed,meshviewer,playground}.ilpk`. The log shows "Engine files
  from engine.ilpk". Captures from it succeeded for the game on OpenGL,
  Vulkan and Direct3D 12 (identical menus, fonts, glass shaders and the engine
  badge from `/engine`), the game canvas (cell shader, toolbar), IllEd, the
  mesh viewer (with its skybox) and Playground.
- `tools/test_build.py`: 126 of 127 passed at first; the failing
  `test_native_console_mouse_and_keyboard_smoke` needs a real console handle
  (`WinError 6` in a non-interactive shell) and does not touch `dist`. Its
  skip guard (and that of `test_native_console_progress_cancellation`)
  trusted `isatty()`, which is also true for a `NUL` stdin; both now check
  `GetConsoleMode` (`has_windows_console`), and the suite passes with the two
  skipped in such shells.

Deviations:

- E4 deferred: `IllMeshViewer.Wasm.Package` runs the viewer without a virtual
  file tree, so moving its skybox preload to `/engine` needs that harness
  reworked first. The viewer still ships its own 1.2 MB copy.
- An invalid `engine.ilpk` is detected in `prepareRuntime` (which cannot fail)
  and fails the launch in `prepareShell`.

Follow-ups: shader file `#include`s through the asset source; a logged
reason when `--storage` names no directory (it returns silently today, found
while testing).

Same day, owner follow-up (E4, E5):

- E4: the mesh viewer preloads `MeshViewerScene::kDefaultSkybox`
  (`/engine/Skybox/skybox-daylight.png`) and its package no longer carries a
  copy. `IllMeshViewer.Wasm.Package` and `.ScenePackage` now run the viewer
  on a tree mounting the package at `/app` and `Illumo/Assets` at `/engine`,
  as the runtime does; both pass (one host cubemap from `/engine`). Staging
  copies but never deletes, so an existing build keeps a stale
  `apps/meshviewer/Assets/Skybox/` until removed by hand.
- E5: `EngineArchiveBackend` reads `engine.ilpk` into memory and follows the
  file (size or write time); stamps are member CRCs; corrupt or foreign
  replacements are ignored and reported once. `ShaderPreprocessor::ProcessFile`
  gained an optional source, which `AssetManager` now passes, so a manager's
  own source serves its shader files (before, they always came from the
  default source), and `engine:` names are their own canonical form.
  `Illumo.Content.EnginePackageReload` rebuilds the package under a live
  `AssetManager`: only the changed shader reloads, the texture does not,
  reads take the new content, a corrupt file and a foreign id are ignored,
  and with polling off an explicit `reloadAll` takes a rebuilt package.
  Rebuilding the package for a running distribution means packing over
  `engine.ilpk` (`IllumoPack <dir> engine.ilpk`); re-running the whole
  distribution target deletes the folder and fails while the runtime holds
  its executable.
