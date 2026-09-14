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

**Current stack (short):** reusable world-mesh and 2D token renderer (`AppendCommands` →
`IBackend`) with typed generational handles, a persistent handle-based scene
hierarchy, painter-correct primitives, dynamic quad buffers,
primitive-composed themed UI, cubemaps, offscreen passes, and managed
texture/cubemap/shader assets. The
retained `SceneGraph` is extracted as one drawable into the existing per-frame
render list; it does not replace CSim's sparse domain. `SparseCellGrid` uses
published dual-grid simulation,
exact retained candidate topology, direct-source parallel preparation and
evaluation, recycled transactional chunk nodes, adaptive frontier stepping,
cached 256x9 transitions, and infinite or finite toroidal topology.
`CanvasView` presents a padded, integer-LOD
camera cache through a world-space `GameVisual` sprite, with active-texel RGB
fades and tiled multi-rectangle uploads through a non-waiting PBO ring.
Headless `IllumoTests`, `IllumoGameTests`, `IllEdTests`, and
`IllMeshViewerTests` use `Illumo::TestSupport` and
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

Use the arrow keys to choose `Release`, `Debug`, `RelWithDebInfo`, or `MinSizeRel`, toggle
documentation and Tracy, select build parallelism, and run a focused action.
On Windows consoles, moving the mouse over a row highlights it without activating
it. Left-click a setting to cycle forward or an action to run
it. Click the setting's left arrow or right-click it to cycle backward; scroll
over menu rows to move the selection. Button releases, dragging, and the second
press of a double-click do not activate actions. Input mode is restored while
commands run and when the dashboard exits. Other terminals retain keyboard
controls. Mouse hit testing requires the whole dashboard to fit (at least
56 columns and 29 rows); enlarge the terminal if clicks are ignored.

Build, test, coverage, tidy, and documentation actions open a live progress view:
current phase and command, total/phase elapsed time, a bounded recent-output
panel, and warning/error line counts. CTest counts and Ninja/CMake progress are
shown when the tools report them. These describe the current tool, not an
estimated percentage of the whole action; MSBuild and quiet tools may show an
activity indicator instead. The final view includes success/failure, exit code,
and the log location. Press Enter to return to the menu.

Complete combined output is saved under Git-ignored `build-orchestrator-logs/`;
logs are retained until you remove them. Ctrl+C cancels the active progress
action and stops its owned processes. Windows uses a Job Object so compiler or
test descendants cannot outlive the action. Application launches and statistics
reports retain direct output. Explicit CLI commands retain their normal output.

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
python build.py file-stats
python build.py file-stats -n 25
python build.py coverage
python build.py tidy
python build.py docs
python build.py new-project ../MyNewGame --name MyNewGame
```

### Build profiles and diagnostics

The dashboard's **Development Tools** submenu completes the build/test/debug
loop without leaving the console:

- **Test explorer** reads names, labels, commands, working directories, and
  timeouts from CTest's JSON inventory. `/` edits a name search; Enter applies
  it and Escape cancels editing. Ordinary characters (including `q`, `h`, `j`,
  `k`, and `l`) remain text while editing; Backspace edits. The project-label
  action cycles filters. Hovering or selecting a test only displays its details.
  Choose **Run Selected**, **Run Matching**, or **Rerun Failed** explicitly.
- Test actions configure and build CMake's declared discovery targets and
  smoke targets first. The execution-mode action offers **Run Existing** to skip
  preparation. **Refresh Inventory** explicitly prepares the selected tree;
  merely opening or searching the explorer never builds. Missing configuration
  discovery files and mismatched single-configuration caches are rejected, even
  when CMake's discovery helper could fall back to another configuration.
- Selected tests run through CTest with escaped, anchored name filters split
  into bounded batches. CTest retains its working directories and timeouts.
  An empty selection never runs anything. Local `-T Test --no-compress-output`
  reports are captured per batch; nothing is submitted to a dashboard server.
  Results retain status and duration. Reruns use only the latest test run for
  the same checkout, build directory, and configuration. Cancelled/incomplete
  runs and missing tests require explicit selection rather than a guessed rerun.
- **Diagnostic browser** browses recorded runs with error, warning, and all
  filters. It recognizes MSVC, Clang/clang-tidy, and CMake locations. Select a
  diagnostic for a preview, then use **Inspect selected diagnostic** for raw
  log context and a read-only **CURRENT SOURCE** view. Relative paths use the
  recorded command directory. Missing files and invalid locations are reported;
  the browser never searches for replacement files. **Browse raw log** retains
  messages without recognized locations. Current files may differ from the run.
- **Profile picker** previews built-in and saved settings, including directory,
  configuration, generator, architecture, feature flags, extra CMake arguments,
  and parallelism. **Apply selected profile** carries them into ordinary build,
  test, and launch actions. Manual dashboard changes are session overrides;
  applying another profile clears them. Saving is explicit: type a name with
  `/`, apply the text, then choose **Save current settings**. Coverage and tidy
  retain their dedicated Debug/Ninja trees and honor selected parallelism.
- **Artifact shortcuts** offers existing build directories, the latest log for
  the selected build identity, the coverage HTML report, and generated PDFs.
  Opening uses Windows' default handler and never starts a build.

Subviews scroll with arrows, the wheel, Page Up/Down, and Home/End. Hit regions
follow the visible viewport; hover changes selection and only action rows
execute work. `q` or Escape returns to the previous view outside text entry.
Source-file statistics is also available in Development Tools.

Each recorded dashboard log has a version-1 `.json` sidecar containing build
identity, effective settings, wrapper and child command directories, completion
status, and available test results. `.tests.json`, per-batch XML, and command
context files support interrupted runs. Raw `.log` files remain authoritative;
older logs remain browsable without metadata but cannot supply trusted reruns.
All records stay under the ignored log directory until explicitly removed.
The toolbox uses only the standard library inside `build.py`, so generated
projects retain it when copying the orchestrator.

Use `python build.py profiles` to list named configurations (`--json` is also
available). Built-in `debug` and `release` profiles select their configuration
and separate `build-workspace-debug` / `build-workspace-release` directories.
Commands without a profile retain their existing defaults.

```bash
python build.py doctor --profile release
python build.py build --profile debug --parallel 4
python build.py profile-save dev --profile debug --no-docs --parallel 4
python build.py test --profile dev
python build.py build --profile dev --docs --config RelWithDebInfo --dry-run
```

Profiles apply to `configure`, `build`, `test`, `run`, `doctor`, and
`profile-save`. Explicit CLI settings override profile values; repeated
`--cmake-arg` values append after the profile's arguments. Use `--docs`,
`--tests`, `--tidy`, or `--no-tracy` to reverse saved boolean choices.
Application arguments after `run --` remain application arguments.
Coverage and tidy retain their dedicated toolchains and build directories.

`profile-save NAME` creates or replaces that name in the Git-ignored
`build-profiles.local.json`. It preserves other names and replaces the file
atomically. `--dry-run` previews without writing. Only reusable configuration,
build-directory, generator, architecture, parallelism, feature flags, and extra
CMake arguments are saved; target, application, clean, fresh, and dry-run choices
are not saved. Treat extra CMake arguments as trusted local build configuration.

For a shared file, pass `--profiles-file PATH` explicitly. Relative profile-file
and build-directory paths resolve from the repository root, including when the
shell is elsewhere. The file's parent directory must already exist. Its format is:

```json
{
  "version": 1,
  "profiles": {
    "dev": {
      "config": "Debug",
      "build_dir": "build-workspace-dev",
      "no_docs": true,
      "parallel": 4
    }
  }
}
```

Allowed keys are `config`, `build_dir`, `generator`, `architecture`, `tracy`,
`no_tests`, `no_docs`, `no_tidy`, `parallel`, and `cmake_arg` (a string array).
Parallelism is `null` for no explicit limit option, `0` for automatic, or a
positive job count. On the CLI, `--parallel` or `--parallel=auto` selects automatic
parallelism. Saved profiles replace matching built-in definitions;
there is no inheritance. Unknown keys and malformed values are rejected.

`doctor` checks the selected cache and required tools, reports versions and
optional documentation tools, and supports `--json`. Errors return a nonzero
exit code. It runs bounded tool-version probes but never configures, builds,
downloads, or repairs anything. A successful report does not prove that a compiler
and Windows SDK can compile the project; CMake configuration remains that check.
Builds reject foreign caches and conflicting explicit generator/architecture
choices. Use a separate build directory, or explicitly `--fresh` for a same-source
generator change. Failed commands report their exit code, elapsed time, working
directory, and command while preserving native tool diagnostics.

Run the orchestrator regression suite with:

```bash
python -m unittest discover -s tools -p test_build.py -v
```

## Project creation (Unreal Engine style)

Standalone generated workspaces include `engine-provenance.json`: source commit,
dirty/unknown status, template and creation options. A dirty source copy is not
an exact Git pin; retain its changes with the generated project. Git-unavailable
sources record unknown identity explicitly. Source must remain unchanged during
copying; the generator does not lock the checkout or create a Git snapshot.

## Standalone rendering tool

`cmake --build build --config Release --target IllumoCapture` builds the bounded
capture client. Run `build/Release/IllumoCapture.exe --output build/frame.png
--mode scene` to produce a PNG and JSON diagnostics without a game loop. See
[capture inputs and ownership](docs/frame-capture.md) and
[charter direction](docs/charter-direction.md). The existing destination must not
exist. Capture requires a real graphics context; Linux remains unverified.

## Creating an application

To scaffold a new Illumo application project, use the project creation tool:

Names must be ASCII C++ identifiers and must not collide with Windows device
names or workspace/build infrastructure, ignoring case (for example `Illumo`,
`IllEd`, `cmake`, `build`, or `IllumoTests`). Rejection happens before copying.

```bash
python build.py new-project <destination_path> [--name <ApplicationName>]
```

Or invoke the standalone script directly:

```bash
python tools/create_project.py <destination_path> --name MyGame
```

This generates a turnkey standalone workspace following an Unreal Engine style layout:
- `Illumo/`: Full source code of the engine framework (`Include/`, `Source/`, `Shader/`, `Assets/`, `thirdparty/`, `TestSupport/`, `Tests/`, `cmake/`)
- `IllEd/`: Debug tools and the SceneGraph world editor
- `<ApplicationName>/` (default: `IllumoGame/`): Game application containing the starter template (a 3D lit spinning cube with perspective camera, controls, configuration, and automated tests)
- `cmake/`, `build.py`, `CMakeLists.txt`, `README.md`: Workspace build orchestration and configuration

Starter controls yield while the console is open. Keys held during capture must
be released before they can control the cube again; animation continues.

Generated workspaces omit engine PDF sources, so `ILLUMO_BUILD_DOCUMENTATION`
defaults off there. This repository defaults it on when the documentation inputs
are present. Explicitly enabling it without those inputs fails at configure time.

Parallel builds order shared asset/shader staging for each runtime directory.
Default-file seeding locks its check-and-copy operation and preserves existing
settings; the first successful seed supplies a missing file.

To exercise project creation and a complete default generated build on a Windows
development machine with CMake, LLVM, PowerShell, and LaTeX installed:

```powershell
$env:ILLUMO_TEST_GENERATED_BUILD = "1"
python -B -m unittest discover -s tools -p test_create_project.py
Remove-Item Env:ILLUMO_TEST_GENERATED_BUILD
```

To build and run the newly generated application:

```bash
cd <destination_path>
python build.py build
python build.py run --app <ApplicationName>
python build.py test
```

Controls for the spinning cube starter application:
- **Space**: Pause / resume rotation
- **R**: Reset rotation angle to 0
- **G**: Toggle 3D reference grid
- **Up / Down**: Increase / decrease rotation speed
- **Escape**: Exit application


`stats` reports the current Git branch, commit, working-tree counts, tracked
file count, and categorized first-party lines. LOC counts nonblank lines in
the current contents of tracked source, tests, shaders, build tooling,
documentation, and configuration files. It excludes build trees, `archive/`,
`Illumo/thirdparty/`, `docs/output/`, and binary assets. Use `--json` for
machine-readable output; the interactive build console exposes the same report
through **Repository statistics**.

`file-stats` (aliases: `source-stats`, `stats --by-file`) provides a per-file
breakdown of first-party source files sorted from largest to smallest by LOC
to highlight large components and potential god objects. Options include
`-n COUNT` / `--top COUNT` to limit output, `--min-loc LOC` for size
thresholds, `--include-tests` to include test suites alongside production code,
`--category` to filter categories, `--sort` for sorting keys, and `--json` for
machine-readable output. The interactive build console exposes this through
**Source file statistics**.

When standard input or output is redirected, running `python build.py` without
a command performs the normal Release build instead of opening the console.
That build retains CMake's existing all-target behavior: it builds the
library, all three applications, all four test runners, every registered workspace case, and
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

Builds require CMake 3.25 or newer, including generated workspaces and standalone
engine builds. The third-party SYSTEM subdirectory handling requires this version.

Direct CMake remains fully supported and is the escape hatch for anything the
front end does not expose:

```bash
cmake -S . -B build
cmake --build build --config Release
```

The canonical workspace build produces `Illumo`, `IllumoGameCore`,
`IllumoGame`, `IllEdCore`, `IllEd`, `IllumoTests`, `IllumoGameTests`,
`IllEdTests`, `IllMeshViewerCore`, `IllMeshViewer`, `IllMeshViewerTests`, and
the consumer-header smoke target in a single output
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

CTest registers one process-isolated entry per logical case. All four runners
support `--list` and exact `--run`; `build.py test` dispatches by the discovered
product or runner prefix. Each CTest case gets an isolated directory under
`build/Testing/<runner-label>/`.

Clang/LLVM coverage (85% production-line gate and HTML report):

```bash
cmake -S . -B build-coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_COVERAGE=ON
cmake --build build-coverage --target IllumoCoverage
```

The combined report measures headless-testable first-party code linked into all
registered workspace test runners: Illumo, IllumoGame, IllEd, and IllMeshViewer.
Coverage builds and refreshes discovery for every runner before running CTest;
generated projects include their custom application and optional editor runners.
Tests, TestSupport, vendored/system code, the concrete live
window, and the OpenGL backend are excluded;
native dialogs, window behavior, and live OpenGL still require smoke testing.
See `Illumo/Tests/README.md` and `IllumoGame/Tests/README.md` for the exact
scope and commands.

clang-tidy (first-party sources, warnings as errors):

The batch target reads the compile database and selects sources inside the
workspace, excluding `thirdparty` and the configured build tree. Custom
application names and tool directories are included. The exact selected
entries are saved to `build-tidy/tidy-compile-commands/compile_commands.json`;
the LLVM batch driver uses that database directly. Selection and failure
propagation tests run with `python -B -m unittest discover -s tools -p test_workspace_tidy.py`.

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
simulation speed, fade speed, VSync, fullscreen, UI scale, restart-only MSAA,
FPS cap, simulation inspector, and reduced menu motion in both Debug and Release.
FPS presets are Uncapped, 30, 60, 90, 120, 144, 165, 240, and 360; type a custom
integer from 0 to 1000. Zero disables software limiting; VSync still limits
presentation to monitor refresh. Applying changes takes effect immediately
except MSAA and saves through the existing environment service from either menu.
Scroll or use Page Up/Down to browse; Home/End jump to the first/last row.
Click a setting's label to cycle backward or its value to cycle forward.
Large UI scales are fitted to the window so actions remain reachable.
Positive width and height select a finite torus whose opposite edges are
adjacent. Enter `0`/`0` or `inf`/`inf` for the infinite canvas; mixed finite and
infinite axes are rejected. The finite world is drawn once inside its centered
rectangle; camera space outside it stays blank while simulation still wraps
across opposite edges. Applying a topology change starts a fresh world. Menu
labels use larger, high-contrast text, readable ruleset names, and contextual
help for the selected setting. The final menu action exits through Illumo's
normal runtime shutdown path; Discard, Escape, and F1 only close the menu. A short eased
reveal, gliding row highlight, and value-change pulse provide motion without
delaying input. The main menu adds a layered cyan/violet surface, drifting cell
outlines, staggered entry, and animated selection. Reduced menu motion snaps
transitions and suppresses decorative motion in both menus without changing
simulation speed or cell fading. The in-game settings button eases on hover
and displays its F1 shortcut. F1 also opens settings from the main menu.

## Global hotkeys

The engine host handles common shortcuts across applications:

- **F11**: Toggle fullscreen mode.
- **F3**: Toggle the FPS overlay (`showFPS`).
- **F5**: Reload all managed asset resources (textures and shaders).

Global shortcuts yield while typing in the developer console (`~` / Grave).

## Developer console commands

The in-app console is provided by `DebugModule`, so it is available in Debug
and RelWithDebInfo builds. It is a global overlay: grave/tilde toggles it on
the main menu, settings, and cell canvas. Type `help` for the live list or `help <command>`
for details.

| Group | Commands |
|---|---|
| Simulation | `pause`, `run`, `step [count]`, `status` |
| Canvas | `clear_canvas`, `randomize [percent]`, `setcell <x> <y> <state>` |
| Rules and files | `ruleset [name]`, `save <file>`, `load <file>`, `save_dialog`, `load_dialog` |
| Camera and display | `camera [x y [zoom]]`, `camera_reset`, `fullscreen`, `fps`, `memory` |
| Renderer diagnostics | `renderer_demo [on|off]`, `assets`, `asset_reload <all|path>` |
| Timing | `tps`, `speed`, `fade` |
| Environment | `get`, `set`, `toggle`, `vars [filter]` |
| Console/app | `help`, `echo`, `clear`, `close`, `quit` |

`memory [on|off|toggle]` independently controls process memory statistics;
without an argument it reports visibility. The persisted `showMemory` setting
defaults to `0`. FPS remains controlled by F3 or `fps` (`showFPS`). Both sections
share the existing top-left diagnostics panel, and either may be shown alone.

Memory is sampled immediately on enable and then once per second while visible.
`RAM` is the process working set (resident physical memory, including shared
pages); `Peak RAM` is its process-lifetime peak; `Private commit` is private
committed memory, which may be backed by RAM or the page file. Values use MiB
(1,048,576 bytes) with one decimal place and cover the entire application.
They are not GPU memory or a leak detector. Windows supplies the counters;
failed or unsupported queries display `Memory: unavailable` and retry on the
next interval. Compare with matching working-set/private-commit counters, not
Task Manager's default private-working-set column.

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
