# Illumo workspace

Illumo is a reusable static C++ runtime/rendering foundation for current and
future projects. In-tree applications consume it through
`CreateIllumoApplication`: `IllumoGame` is the cellular-automata simulator,
and `IllEd` is the SceneGraph world editor used to author `.ilsc` scenes for
later Illumo applications. Moving a product to a downstream repository is a
separate packaging step.

## Layout

```
illumo/
  CMakeLists.txt         # Canonical Illumo workspace entrypoint
  docs/                 # All first-party documentation and LaTeX sources
    latex/              # Prose-book and chart-pack PDF entrypoints
    packages/           # Source-package maps formerly scattered by code
    sessions/           # Dated implementation and verification records
    history/            # Superseded/original material
  Illumo/               # Standalone static-library project
    Include/Illumo/     # Supported consumer headers
    Source/             # Private library implementation
      Engine/           # Generic host, application runner + module lifetime
      Scene/            # Persistent hierarchy, transforms + render extraction
      Rendering/        # Graphics / backend interfaces
      Services/         # Log, input, env, system CLI, allocators
      Foundation/       # Build metadata, macros and shared helpers
      Platform/         # OS entry and native save/load dialogs
    TestSupport/        # MockBackend and shared test-only headers
    Tests/              # Illumo.* library cases
    Shader/             # GLSL shaders
    Assets/             # Runtime asset files (fonts, …)
    thirdparty/         # Vendored dependencies
  IllumoGame/           # Simulator product project
    Source/Game/        # CA domain, config, module factory, editor, persistence
    Source/Rulesets/    # Cellular-automata rules
    Tests/              # IllumoGame.* product cases
    envvars.json        # Product configuration seed
  IllEd/                # World-editor product project
    Source/             # Editor module, document, .ilsc codec, toolbar
    Tests/              # IllEd.* product cases
    envvars.json        # Product configuration seed
  archive/              # Historical / non-build material
```

## Design documentation

All first-party architecture, decision, package, history, and build notes live
under `docs/`. Start with:

- `docs/README.md` — documentation map and PDF build commands
- `docs/architecture-consensus.md` — canonical current architecture
- `docs/scene-graph-v1-design.md` — retained scene hierarchy contract and scope
- `docs/latex/illumo.tex` — the canonical prose-book entrypoint
- `docs/latex/architecture-map.tex` — the current chart-only entrypoint
- `docs/output/*.pdf` — generated locally; never sources of truth

**Current stack (short):** reusable 2D token renderer (`AppendCommands` →
`IBackend`) with typed generational handles, a persistent handle-based scene
hierarchy, painter-correct primitives, dynamic quad buffers,
primitive-composed themed UI, and asynchronous texture/shader assets. The
retained `SceneGraph` is extracted as one drawable into the existing per-frame
render list; it does not replace CSim's sparse domain. `SparseCellGrid` uses
published dual-grid simulation,
exact retained candidate topology, direct-source parallel preparation and
evaluation, recycled transactional chunk nodes, adaptive frontier stepping,
cached 256x9 transitions, and infinite or finite toroidal topology.
`CanvasView` presents a padded, integer-LOD
camera cache through a world-space `GameVisual` sprite, with active-texel RGB
fades and tiled multi-rectangle uploads through a non-waiting PBO ring.
Headless `IllumoTests` and `IllumoGameTests` use `Illumo::TestSupport` and
`MockBackend`. Windows is the supported runtime;
Linux and macOS retain stale source/CMake scaffolding pending native validation.

**Architecture (single source for later sessions):** [`docs/architecture-consensus.md`](docs/architecture-consensus.md) — unified consensus (purpose, history of old plans, current renderer/sim truth, decisions, bugs, debt, work order).

Contribution rules are in [`docs/contributing.md`](docs/contributing.md).

Third-party software and font acknowledgements, license choices, and the
source/binary redistribution checklist are in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Build

`build.py` is the convenient front end for the existing CMake build. It requires
Python 3.10 or later, uses only the standard library, prints every command it
runs, and leaves all CMake files and targets authoritative. From an interactive
terminal, open its build console with:

```bash
python build.py
```

Use the arrow keys to choose `Release`, `Debug`, or `RelWithDebInfo`, toggle
documentation and Tracy, select build parallelism, and run a focused action.
The same operations remain available as explicit commands for scripts, CI, or
anyone who prefers a shell. For example, a Debug build is:

```bash
python build.py build --config Debug
```

Common focused workflows are:

```bash
python build.py build --config Debug --target IllumoGame --parallel
python build.py test
python build.py test --list-tests
python build.py test --test IllumoGame.CellGame.SaveLoadRoundTrip
python build.py run -- -ww 1280 -wh 720
python build.py run --config Debug --no-build
python build.py stats
python build.py stats --json
python build.py coverage
python build.py tidy
python build.py docs
```

`stats` reports the current Git branch, commit, working-tree counts, tracked
file count, and categorized first-party lines. LOC counts nonblank lines in
the current contents of tracked source, tests, shaders, build tooling,
documentation, and configuration files. It excludes build trees, `archive/`,
`Illumo/thirdparty/`, `docs/output/`, and binary assets. Use `--json` for
machine-readable output; the interactive build console exposes the same report
through **Repository statistics**.

When standard input or output is redirected, running `python build.py` without
a command performs the normal Release build instead of opening the console.
That build retains CMake's existing all-target behavior: it builds the
library, `IllumoGame`, both test runners, every registered workspace case, and
the PDFs when the documentation toolchain is available.

Use `--no-docs` for a build tree that should skip the optional PDF target,
`--generator` and `--architecture` to select a CMake generator, and repeated
`--cmake-arg=-DNAME=VALUE` options for an uncommon CMake setting. `--dry-run`
prints the commands without running them. The orchestrator defaults to
`build-workspace` and coverage defaults to `build-workspace-coverage`, keeping
the workspace separate from standalone or pre-extraction build trees. It never
deletes a build tree and rejects a cache created from another source root; use a
separate `--build-dir` when changing source roots or generators.

The dashboard's **Run existing build** action, or `run --no-build`, launches
the selected executable immediately and fails clearly if that configuration
has not been built yet. The normal `run` command still configures and builds
before launching.

Direct CMake remains fully supported and is the escape hatch for anything the
front end does not expose:

```bash
cmake -S . -B build
cmake --build build --config Release
```

The canonical workspace build produces `Illumo`, `IllumoGameCore`,
`IllumoGame`, `IllEdCore`, `IllEd`, `IllumoTests`, `IllumoGameTests`,
`IllEdTests`, and the consumer-header smoke target in a single output
folder. With a Visual Studio generator, the orchestrator's default artifacts
are under `build-workspace/Release/`; the direct CMake example above uses
`build/Release/` instead.

The library can also configure independently, without IllumoGame:

```bash
cmake -S Illumo -B build-illumo
cmake --build build-illumo --config Release
ctest --test-dir build-illumo -C Release -L Illumo --output-on-failure
```

When Windows PowerShell and `latexmk` are on `PATH`, the default build also runs
`IllumoDocs` and writes `docs/output/illumo.pdf` plus
`docs/output/architecture-map.pdf`. Disable that optional target at configure
time with `-DILLUMO_BUILD_DOCUMENTATION=OFF`.

Headless tests (no GPU):

```bash
ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
ctest --test-dir build -C Release -N -L IllumoWorkspace
# library: build/Release/IllumoTests.exe --run Illumo.Host.ConfigurationOwnership
# game: build/Release/IllumoGameTests.exe --run IllumoGame.CellGame.SaveLoadRoundTrip
# editor: build/Release/IllEdTests.exe --run IllEd.Ilsc.RoundTrip
```

CTest registers one process-isolated entry per logical case. Both runners
support `--list` and exact `--run`; `build.py test` dispatches by the
`Illumo.*` or `IllumoGame.*` prefix. Each case gets an isolated directory
under `build/Testing/Illumo/` or `build/Testing/IllumoGame/`.

Clang/LLVM coverage (85% production-line gate and HTML report):

```bash
cmake -S . -B build-coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_COVERAGE=ON
cmake --build build-coverage --target IllumoCoverage
```

The combined report measures headless-testable first-party code from both
production targets. Tests, TestSupport, vendored/system code, the concrete live
window, and the OpenGL backend are excluded;
native dialogs, window behavior, and live OpenGL still require smoke testing.
See `Illumo/Tests/README.md` and `IllumoGame/Tests/README.md` for the exact
scope and commands.

clang-tidy (first-party sources, warnings as errors):

```bash
cmake -S . -B build-tidy -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy --target IllumoTidy
```

`ILLUMO_ENABLE_CLANG_TIDY` defaults to ON, so a normal first-party compile
runs `clang-tidy` with `.clang-tidy` and treats diagnostics as errors.
Vendored translation units are excluded. Disable it with
`-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`.
`python build.py tidy` still configures a Ninja/Clang tree under
`build-workspace-tidy` and builds the batch `IllumoTidy` target.

### Optimized Tracy profiling

Keep the normal Release optimization level while enabling application Tracy
instrumentation:

```bash
cmake -S . -B build-profile -DILLUMO_ENABLE_TRACY=ON -DILLUMO_BUILD_DOCUMENTATION=OFF
cmake --build build-profile --config Release
```

Visual Studio: open the generated solution from the build directory, or generate with the VS generator.

## Controls

- **E** — Toggle Edit / Normal mode  
  (Simulation starts in edit mode, same as paused)
- **Left mouse** (Edit) — Place living cells
- **Right mouse** (Edit) — Place dead cells
- **C** (Edit) — Clear the cell colony
- **F1** — Open the Release-visible simulator settings menu
- **Q** / **ESC** — Quit
- **`** — Toggle the developer console
- **Console:** **Tab** completes commands, variables, and rulesets; **Left/Right**, **Home/End**, and **Delete** edit in place; hold **Ctrl** with Left/Right or Backspace/Delete for word edits; hold **Shift** while moving to select; **Ctrl+A** selects all

## Launch options

The executable keeps its persisted configuration in `envvars.json` beside the
executable, independent of the process working directory. A first build places
the tracked defaults there without overwriting an existing local configuration.
Command-line dimensions override the persisted values:

```text
IllumoGame.exe [-ww width] [-wh height] [-cw canvas-width] [-ch canvas-height]
IllumoGame.exe --help
IllumoGame.exe --version
IllEd.exe
IllEd.exe --help
```

`IllEd.exe` is the SceneGraph world editor. It writes `.ilsc` JSON scenes
(File / Edit / Create / View). Set `LaunchScene` in its `envvars.json` to
open a file at startup. It does not simulate cellular automata.

Presentation is synchronized to the monitor by default (`"vsync": "1"`). Set
`vsync` to `0` for uncapped profiling; Debug builds also apply `toggle vsync`
live. The Debug FPS overlay reports frame-paced swap cadence separately from
CPU submissions so an uncapped submission rate is not presented as display FPS.

Set `"render3dTest": "1"` in `envvars.json` to replace the cellular canvas
with an opt-in 3D diagnostic scene: a `SceneGraph` of `MeshVisual` attachments
(axes/grid plus orbiting cubes) drawn through the product camera in perspective.
It is a rendering smoke path, not a model or lighting feature; set it back to
`0` to restore the normal orthographic canvas.

The F1 menu configures ruleset, world width/height in 16x16 chunks, TPS,
simulation speed, fade speed, VSync, and fullscreen in both Debug and Release.
Positive width and height select a finite torus whose opposite edges are
adjacent. Enter `0`/`0` or `inf`/`inf` for the infinite canvas; mixed finite and
infinite axes are rejected. The finite world is drawn once inside its centered
rectangle; camera space outside it stays blank while simulation still wraps
across opposite edges. Applying a topology change starts a fresh world. Menu
labels use larger, high-contrast text, readable ruleset names, and contextual
help for the selected setting. The final menu action exits through Illumo's
normal runtime shutdown path; Discard, Escape, and F1 only close the menu. A short eased
reveal, gliding row highlight, and value-change pulse provide motion without
delaying input.

## Global hotkeys

The engine host handles common shortcuts across applications:

- **F11**: Toggle fullscreen mode.
- **F3**: Toggle the FPS overlay (`showFPS`).
- **F5**: Reload all managed asset resources (textures and shaders).

Global shortcuts yield while typing in the developer console (`~` / Grave).

## Developer console commands

The in-app console is provided by `DebugModule`, so it is available in Debug
builds only. It is a global overlay: grave/tilde toggles it on the main menu,
settings, and cell canvas. Type `help` for the live list or `help <command>`
for details.

| Group | Commands |
|---|---|
| Simulation | `pause`, `run`, `step [count]`, `status` |
| Canvas | `clear_canvas`, `randomize [percent]`, `setcell <x> <y> <state>` |
| Rules and files | `ruleset [name]`, `save <file>`, `load <file>`, `save_dialog`, `load_dialog` |
| Camera and display | `camera [x y [zoom]]`, `camera_reset`, `fullscreen`, `fps` |
| Renderer diagnostics | `renderer_demo [on|off]`, `assets`, `asset_reload <all|path>` |
| Timing | `tps`, `speed`, `fade` |
| Environment | `get`, `set`, `toggle`, `vars [filter]` |
| Console/app | `help`, `echo`, `clear`, `close`, `quit` |

Normal mode keeps at most one generation in flight on a persistent worker and
publishes completed sparse grids only at frame boundaries. It never builds a
catch-up backlog; overdue whole steps are dropped while the fractional clock
remainder is retained. Pause, edit, save/load, ruleset changes, manual stepping,
and shutdown drain first. `status` reports requested and achieved published TPS
plus rolling 256-sample simulation, cache-refill, upload-byte, and
upload-rectangle p50/p95/max values. Broad generations publish a lightweight
replacement marker rather than a complete chunk snapshot: the spare grid reads
the immutable published grid directly, updates retained nodes in place, and
reuses exact candidate topology when the same source grid returns unchanged.

The visible viewport samples from a globally aligned cache padded by two
16-cell chunks on each side. Camera motion inside that cache changes only the
MVP. Far zoom uses integer density LOD with 80% refinement hysteresis. Dirty
16x16-texel tiles merge into at most eight upload rectangles. Uploads through
64 KiB use direct `glTexSubImage2D`; larger uploads use the first available slot
in a non-waiting three-PBO/fence ring and fall back to direct upload when all
slots are busy or mapping fails. Replacements preserve the opaque handle while
deleting the old GL texture, PBOs, and fences.

Save commands append `.illumo` when no extension is supplied. Version 3 saves
include world topology as well as ruleset, camera, and sorted sparse chunks.
Loading validates the save before changing the canvas, reads version 3,
version 2, and legacy dense files, and activates the stored ruleset.
