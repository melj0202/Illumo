# Illumo

Illumo is a reusable C++23 static runtime and rendering library. This
repository is a source workspace: the library and the in-tree applications
that consume it through `CreateIllumoApplication`. It is not an installable
SDK or a stable DLL ABI. Moving a product to a downstream repository is a
separate packaging step.

Illumo owns the generic application runner, platform entry and native
dialogs, BuildInfo, SysCmdLine, host, services, persistent `SceneGraph`,
token renderer, assets, and module lifetime. It does not depend on Game,
Rulesets, or IllEd. Application policy stays in the consuming product.

## What this repository is

| Piece | Role |
|---|---|
| `Illumo/` | Static library: runner, platform, services, SceneGraph, OpenGL token renderer, assets |
| `IllumoGame/` | Cellular-automata sandbox. Saves write sparse `.csim` version 4; loads versions 4, 3, and 2 plus legacy dense / `.illumo` |
| `IllEd/` | SceneGraph world editor. Writes `.ilsc` version 1 for later Illumo applications |
| `IllMeshViewer/` | Single-mesh `.obj` viewer with orbit, pan, zoom, and rotate |
| `IllumoCapture` | Hidden-window PNG/JSON capture with no game loop ([docs/frame-capture.md](docs/frame-capture.md)) |
| `build.py` | Standard-library Python 3.10+ front end for the CMake build |

`SparseCellGrid` is the production simulator domain. `SceneGraph` is the
retained world hierarchy used by IllEd (and optional 3D diagnostics); it is
not CA cell storage. Dense `CellGrid` / `Canvas` remain compatibility
fixtures. Production drawing appends `RenderCommand` tokens; `IBackend`
executes them (`GLBackend` or headless `MockBackend`).

Architecture, decisions, and current-state truth:
[docs/architecture-consensus.md](docs/architecture-consensus.md).

## Platform status

| Platform | Status |
|---|---|
| Windows, GLFW, OpenGL, MSVC | Supported, verified production path |
| Linux, Ubuntu 24.04 x86_64, X11/XWayland, GCC 13+ or Clang 18, gtkmm-3 | Sources and CMake repaired so the in-tree apps can configure, compile, and run. That is **not a support claim** until native GUI smoke on that host. See [docs/packages/platform-linux.md](docs/packages/platform-linux.md) |
| macOS | Unverified scaffold. See [docs/packages/platform-macos.md](docs/packages/platform-macos.md) |

The stack is C++23, CMake 3.25 or newer, and vendored GLFW, GLEW, GLM, and
FreeType. GLFW, GLEW, and OpenGL stay behind Illumo; game and rules code do
not issue raw OpenGL calls.

## Layout

```
illumo/
  CMakeLists.txt         # Canonical workspace entrypoint
  build.py               # Interactive / CLI CMake front end
  cmake/                 # Shared CMake (tidy, coverage, Linux deps hook)
  tools/                 # install-linux-deps.sh, create_project.py, tests
  docs/                  # Architecture, packages, LaTeX, session records
  Illumo/                # Standalone static-library project
    Include/Illumo/      # Supported consumer headers (<Illumo/...>)
    Source/              # Private library implementation
    TestSupport/         # MockBackend and shared test-only headers
    Tests/               # Illumo.* library cases
    Shader/ Assets/      # Runtime files staged beside executables
    thirdparty/          # Vendored dependencies
  IllumoGame/            # CA simulator
  IllEd/                 # SceneGraph world editor
  IllMeshViewer/         # Mesh viewer
  archive/               # Historical / non-build material
```

`IllumoCapture` is built from the Illumo tree. Product `envvars.json` files
are product-owned and staged next to each executable.

## Get the build running

Clone and work from the repository root:

```bash
git clone https://github.com/melj0202/Illumo.git
cd Illumo
```

`ILLUMO_ENABLE_CLANG_TIDY` defaults **ON**. Configure fails if `clang-tidy`
is not on `PATH`. This repository also defaults `ILLUMO_BUILD_DOCUMENTATION`
**ON** when the LaTeX sources exist. First-time builds should pass
`--no-tidy --no-docs` (or the matching `-D` flags) unless LLVM and a TeX
toolchain are already installed.

Do not reuse a CMake cache across operating systems. Use a separate `-B`
directory on each host.

### Windows (supported)

**Prerequisites**

- Windows 10 or 11, x64
- Visual Studio with the **Desktop development with C++** workload: MSVC
  with C++23 (`cxx_std_23`) and a current Windows 10/11 SDK. OpenGL
  (`opengl32`) comes with the SDK. A verified development machine uses
  Visual Studio 18 / MSVC; any current VS toolchain that provides C++23 and
  CMake 3.25 is the practical requirement
- [CMake 3.25+](https://cmake.org/download/) on `PATH`
- Python 3.10+ on `PATH` if you use `build.py` (standard library only)
- Git
- Optional: LLVM so `clang-tidy` is on `PATH`
- Optional: PowerShell plus `latexmk`/TeX for the PDF book
  (`docs/build.ps1`). Not required to run the applications

Third-party libraries are vendored under `Illumo/thirdparty/`. There is no
vcpkg or extra package manager step. If `cmake` cannot find `cl`, use a
**Developer Command Prompt for VS** or the VS generator from an environment
where the compiler is registered.

**Recommended first build** (skip tidy and PDFs until those tools exist):

```powershell
python build.py doctor
python build.py build --config Release --no-tidy --no-docs
```

`build.py` writes to `build-workspace/` by default. With LLVM and TeX already
installed, omit `--no-tidy` and `--no-docs`.

**Direct CMake** (Visual Studio generator; artifacts under `build/Release/`):

```powershell
cmake -S . -B build -DILLUMO_ENABLE_CLANG_TIDY=OFF -DILLUMO_BUILD_DOCUMENTATION=OFF
cmake --build build --config Release
```

**Run** from the staged configuration directory so `Shader/`, `Assets/`, and
`envvars.json` sit beside the executable:

```powershell
cd build-workspace\Release
.\IllumoGame.exe
.\IllEd.exe
.\IllMeshViewer.exe
```

Or `python build.py run --config Release --no-build`. Direct CMake uses
`build\Release\` instead of `build-workspace\Release\`.

**Test**

```powershell
ctest --test-dir build-workspace -C Release -L IllumoWorkspace --output-on-failure
```

Headless tests do not prove the live OpenGL window or native Win32 dialogs.

| Problem | What to do |
|---|---|
| `ILLUMO_ENABLE_CLANG_TIDY=ON requires clang-tidy` | Install LLVM, or `--no-tidy` / `-DILLUMO_ENABLE_CLANG_TIDY=OFF` |
| CMake older than 3.25 | Upgrade CMake |
| No compiler / Windows SDK | Install the VS Desktop C++ workload; use a VS Developer Prompt if `cl` is missing |
| Foreign or Linux CMake cache | New `-B` directory, or `python build.py build --fresh` |
| Docs want `latexmk` | `--no-docs` until TeX is installed |

### Linux (Ubuntu 24.04 x86_64 — repaired, not supported)

Windows remains the verified production path. Use a Linux tree only on a real
Ubuntu 24.04 x86_64 host with X11 or XWayland, GCC 13+ or Clang 18+, and
CMake 3.25+. Ubuntu 22.04's default GCC 11 and CMake 3.22 are not sufficient
(`std::ios::noreplace`, CMake 3.25). Native Wayland GLFW is off
(`GLFW_BUILD_WAYLAND=OFF`) because gtkmm-3 plus GLFW can deadlock.

WSL can configure and compile. GUI, dialogs, and `IllumoCapture` still need a
working GLX display. If `glxinfo -B` fails, that is a driver or session
problem, not an Illumo CMake bug.

Full package list, failure table, and human GUI smoke:
[docs/packages/platform-linux.md](docs/packages/platform-linux.md).

**Packages.** On Debian/Ubuntu, CMake runs `tools/install-linux-deps.sh`
during configure when `ILLUMO_INSTALL_LINUX_DEPS` is ON (the default on
UNIX-not-Apple). That needs root or **passwordless** sudo. If sudo asks for a
password, run the script once in a terminal, then reconfigure. Disable the
helper with `-DILLUMO_INSTALL_LINUX_DEPS=OFF` when the image already has the
packages. Non-Debian hosts skip apt.

The script installs the toolchain, Ninja, pkg-config, Python 3, Mesa/GLX
OpenGL (`libgl1-mesa-dev`, `libopengl-dev`, and `libglx-mesa-dev` when that
name exists in the archive), X11 libraries for vendored GLFW,
`libgtkmm-3.0-dev`, and `mesa-utils`. `--tidy` adds `clang-tidy`; `--docs`
adds `latexmk` only (not a full TeX live). It is safe to re-run. Unknown
package names are skipped (some Ubuntu archives have `libglx-dev` instead of
`libglx-mesa-dev`).

```bash
bash tools/install-linux-deps.sh
pkg-config --modversion gtkmm-3.0   # expect 3.24.x on Ubuntu 24.04
cmake --version
g++ --version
echo "$XDG_SESSION_TYPE"
glxinfo -B
```

**Recommended first build.** Prefer RelWithDebInfo. Skip docs and tidy so
configure does not wait on `latexmk`/`clang-tidy`, and so you do not pay
Debug AddressSanitizer on `Illumo.SceneGraph.Oracle` (tens of minutes on
some hosts). Use a dedicated Linux tree; do not reuse a Windows cache.

```bash
python3 build.py build --config RelWithDebInfo --no-docs --no-tidy
```

`build.py` still defaults to `build-workspace/RelWithDebInfo/` on Linux.
The direct CMake examples below use a dedicated `build-linux/` tree so a
Windows cache is never reused.

Direct CMake / Ninja:

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DILLUMO_BUILD_DOCUMENTATION=OFF \
  -DILLUMO_ENABLE_CLANG_TIDY=OFF
cmake --build build-linux --parallel
```

Ninja stages runtimes under `build-linux/RelWithDebInfo/` (or `Debug/`) so
executables do not collide with CMake directories named `IllumoGame/`.

**Run** from the staged directory. `envvars.json` is resolved from
`/proc/self/exe`; starting the binary from `/tmp` must still use that staged
file.

```bash
cd build-linux/RelWithDebInfo
./IllumoGame
./IllEd
./IllMeshViewer
./IllumoCapture --output /tmp/illumo-frame.png --mode scene
```

**Test.** The default Ninja `ALL` target also runs `IllumoRunTests`. For a
faster headless pass:

```bash
ctest --test-dir build-linux -L IllumoWorkspace -E "Bench|Oracle" --output-on-failure
```

Headless tests do not prove GLFW, OpenGL, or GTK dialogs. Complete the smoke
list in [docs/packages/platform-linux.md](docs/packages/platform-linux.md)
before treating the port as done.

| Problem | What to do |
|---|---|
| dpkg/apt lock (`/var/lib/dpkg/lock-frontend`) | Wait for the other `apt-get`, then re-run CMake or the script |
| sudo needs a password | Run `bash tools/install-linux-deps.sh` yourself; then reconfigure |
| `Could NOT find OpenGL` (`OPENGL_glx_LIBRARY` / `OPENGL_INCLUDE_DIR`) | Install the Mesa/GLX `-dev` packages; CMake 4 FindOpenGL is GLVND-based |
| `libglx-mesa-dev` skipped (not in this apt archive) | Expected on some Ubuntu variants; remaining GL packages may still be enough |
| `ILLUMO_ENABLE_CLANG_TIDY` / missing `clang-tidy` | `--no-tidy`, or install `clang-tidy` (`--tidy` on the script) |
| Configure error shows `--tidy;--docs` | CMake list join; those are flags passed to the helper |
| Debug ASan / `Illumo.SceneGraph.Oracle` very slow | First GUI smoke: RelWithDebInfo; or `ctest -E "Bench\|Oracle"` |
| `glxinfo` / window creation fails | Driver or session; use X11 or XWayland |
| `gtkmm-3.0` not found | Install `libgtkmm-3.0-dev` |
| Reused Windows CMake cache | New `-B build-linux` |

### What the build produces

A successful workspace build stages, in one configuration folder:

- Applications: `IllumoGame`, `IllEd`, `IllMeshViewer`, `IllumoCapture`
- Tests: `IllumoTests`, `IllumoGameTests`, `IllEdTests`,
  `IllMeshViewerTests`, `IllumoPublicHeaderSmoke`
- Runtime: `Shader/`, `Assets/`, `envvars.json`, `THIRD_PARTY_NOTICES.md`

Windows (VS generator): `build-workspace/Release/` for `build.py`, or
`build/Release/` for `-B build`. Linux (Ninja): `build-linux/<CONFIG>/`.

The default `ALL` target also runs the `IllumoWorkspace` CTest label via
`IllumoRunTests`. Debug builds enable AddressSanitizer on MSVC and GCC/Clang.

The library can configure independently, without the products:

```bash
cmake -S Illumo -B build-illumo -DILLUMO_BUILD_DOCUMENTATION=OFF
cmake --build build-illumo --config Release
ctest --test-dir build-illumo -C Release -L Illumo --output-on-failure
```

When Windows PowerShell and `latexmk` are on `PATH`, the default Windows
build also runs `IllumoDocs` and writes `docs/output/illumo.pdf` plus
`docs/output/architecture-map.pdf`. On Linux that target is skipped even if
documentation is enabled. Turn PDFs off with
`-DILLUMO_BUILD_DOCUMENTATION=OFF`.

## Run the applications

Launch from the staged configuration directory (see above). Configuration
lives in `envvars.json` beside the executable, independent of the process
working directory. A first build copies tracked defaults without overwriting
an existing local file. `--help` and `--version` work on every application.

Host-wide shortcuts (yield while the developer console is open):

- **F11** — fullscreen
- **F3** — FPS overlay (`showFPS`)
- **F5** — reload managed textures and shaders
- **Grave / tilde** — developer console (Debug and RelWithDebInfo;
  `DebugModule`)

Applications also honor the window-manager close button. **Q** is not a
global quit key: IllumoGame uses it to request exit; IllEd and IllMeshViewer
bind **Q** to camera motion.

Presentation is monitor-synchronized by default (`"vsync": "1"`). Set
`vsync` to `0` for uncapped profiling.

### IllumoGame

Cellular-automata sandbox. Engine window size: `-ww` / `-wh`. Canvas size in
cells: `-cw` / `-ch`. Catalogs may add ruleset IDs; F2 on the canvas edits a
rule. See [docs/packages/game.md](docs/packages/game.md).

```text
IllumoGame.exe [-ww width] [-wh height] [-cw canvas-width] [-ch canvas-height]
IllumoGame.exe --help
```

- **E** — Edit / Normal (starts in edit, same as paused)
- **Left mouse** (Edit) — live cells; **Right mouse** — dead / erase
- **Shift+left drag** (Edit) — rectangle select
- **Ctrl+C / X / V**, **Delete**, **R** / **F** — copy / cut / paste / erase /
  rotate / flip
- **Middle drag / wheel** — pan / zoom
- **I** — inspector
- **F1** — settings (ruleset, world size in 16x16 chunks, TPS, fade, VSync,
  fullscreen, UI scale, MSAA, FPS cap). Apply takes effect immediately except
  MSAA. Positive width and height select a finite torus; `0`/`0` or
  `inf`/`inf` is the infinite canvas; mixed axes are rejected. Topology
  changes start a fresh world
- **Q** — request quit (exit confirmation on the canvas)
- Bottom control hints are on by default; toggle **Edit control hints** in
  F1. Painting, selecting, and camera motion do not hit through that band

Saves append `.csim` when no extension is given. Writes are sparse version 4
(family and ruleset IDs, topology, camera, sorted chunks). Loads validate
before replacing the canvas.

Set `"render3dTest": "1"` in `envvars.json` for an opt-in 3D diagnostic
`SceneGraph` (not the normal canvas). Set it back to `0` to restore the
orthographic CA view.

Simulation publishes at most one generation per frame on a persistent worker;
overdue whole steps are dropped. The visible viewport is a padded, integer-LOD
cache with tiled uploads. Timing, fade, and upload details:
[docs/packages/game.md](docs/packages/game.md).

### IllEd

SceneGraph world editor. It does not simulate cellular automata. File / Edit /
Create / View authors `.ilsc` JSON (version 1). Set `LaunchScene` in
`IllEd`'s `envvars.json` to open a file at startup, or use File > Open.
Native dialogs use pattern `*.ilsc`. See
[docs/packages/illed.md](docs/packages/illed.md).

```text
IllEd.exe --help
```

### IllMeshViewer

Displays one `.obj` on a 3D reference grid.

```text
IllMeshViewer.exe [OPTION] ... [FILE]
IllMeshViewer.exe -m path/to/mesh.obj
```

`LaunchMesh` in `envvars.json` is the same path. Controls from `--help`:

- LMB / RMB drag — orbit; MMB or Shift+drag — pan
- WASD / arrows — pan; scroll — zoom
- Q / E or Alt+drag — roll / tilt
- **O** — open-mesh dialog; **R** / **F** — reset / frame; **G** — grid;
  **X** — wireframe / bounds

### IllumoCapture

Hidden GLFW window, real GPU required. The output path must not already
exist. See [docs/frame-capture.md](docs/frame-capture.md).

```powershell
# Windows, from the staged Release directory
.\IllumoCapture.exe --output frame.png --mode scene
```

```bash
# Linux, from the staged Ninja directory
./IllumoCapture --output /tmp/illumo-frame.png --mode scene
```

`--mode` is `direct` or `scene` (default `direct`). Linux capture is part of
the smoke matrix and is not claimed until that host produces a PNG.

### Developer console

Available in Debug and RelWithDebInfo (`DebugModule`). Type `help` or
`help <command>`. The table is IllumoGame-oriented; other apps share the
host overlay.

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

`memory` controls the process-memory overlay (`showMemory`, default off).
Windows supplies the counters; unsupported platforms show `Memory:
unavailable`. Values are process working set / peak / private commit in MiB,
not GPU memory. `profiler on` / **F6** is documented under Profiling below.

## Tests, coverage, and clang-tidy

CTest registers one process-isolated entry per logical case. All four
runners support `--list` and exact `--run`. `build.py test` dispatches by
the discovered product or runner prefix. Each case gets an isolated
directory under `build/Testing/<runner-label>/` (or the equivalent in
`build-workspace` / `build-linux`).

```bash
ctest --test-dir build -C Release -L IllumoWorkspace --output-on-failure
ctest --test-dir build -C Release -N -L IllumoWorkspace
# library:  build/Release/IllumoTests.exe --run Illumo.Host.ConfigurationOwnership
# game:     build/Release/IllumoGameTests.exe --run IllumoGame.CellGame.SaveLoadRoundTrip
# editor:   build/Release/IllEdTests.exe --run IllEd.Ilsc.RoundTrip
# viewer:   build/Release/IllMeshViewerTests.exe --list
```

See `Illumo/Tests/README.md`, `IllumoGame/Tests/README.md`,
`IllEd/Tests/README.md`, and `IllMeshViewer/Tests/README.md`.

Clang/LLVM coverage (85% production-line gate and HTML report):

```bash
cmake -S . -B build-coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_COVERAGE=ON
cmake --build build-coverage --target IllumoCoverage
```

The combined report measures headless-testable first-party code linked into
all registered workspace test runners. Tests, TestSupport, vendored/system
code, the live window, and the OpenGL backend are excluded. Native dialogs
and live OpenGL still require smoke testing.

clang-tidy (first-party sources, diagnostics as errors):

```bash
cmake -S . -B build-tidy -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DILLUMO_BUILD_DOCUMENTATION=OFF -DILLUMO_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy --target IllumoTidy
```

`python build.py tidy` configures a Ninja/Clang tree under
`build-workspace-tidy`. Disable compile-time linting with
`-DILLUMO_ENABLE_CLANG_TIDY=OFF` or `python build.py build --no-tidy`.
Vendored translation units are excluded. Selection tests:
`python -B -m unittest discover -s tools -p test_workspace_tidy.py`.

## Build orchestrator

`build.py` is the convenient front end for the existing CMake build. It
requires Python 3.10 or later, uses only the standard library, prints every
command it runs, and leaves all CMake files and targets authoritative. From
an interactive terminal:

```bash
python build.py
```

Use the arrow keys to choose `Release`, `Debug`, `RelWithDebInfo`, or
`MinSizeRel`, toggle documentation and Tracy, select build parallelism, and
run a focused action. On Windows consoles, moving the mouse over a row
highlights it without activating it. Left-click a setting to cycle forward
or an action to run it. Click the setting's left arrow or right-click it to
cycle backward; scroll over menu rows to move the selection. Button
releases, dragging, and the second press of a double-click do not activate
actions. Input mode is restored while commands run and when the dashboard
exits. Other terminals retain keyboard controls. Mouse hit testing requires
the whole dashboard to fit (at least 56 columns and 29 rows); enlarge the
terminal if clicks are ignored.

Build, test, coverage, tidy, and documentation actions open a live progress
view: current phase and command, total/phase elapsed time, a bounded
recent-output panel, and warning/error line counts. CTest counts and
Ninja/CMake progress are shown when the tools report them. These describe
the current tool, not an estimated percentage of the whole action; MSBuild
and quiet tools may show an activity indicator instead. The final view
includes success/failure, exit code, and the log location. Press Enter to
return to the menu.

Complete combined output is saved under Git-ignored
`build-orchestrator-logs/`; logs are retained until you remove them. Ctrl+C
cancels the active progress action and stops its owned processes. Windows
uses a Job Object so compiler or test descendants cannot outlive the action.
Application launches and statistics reports retain direct output. Explicit
CLI commands retain their normal output.

The same operations remain available as explicit commands:

```bash
python build.py build --config Debug
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

When standard input or output is redirected, `python build.py` with no
command performs the normal Release build instead of opening the console.
That build retains CMake's all-target behavior: library, GUI applications,
`IllumoCapture`, all four test runners, every registered workspace case, and
the PDFs when the documentation toolchain is available.

Use `--no-docs` to skip the optional PDF target, `--generator` and
`--architecture` to select a CMake generator, and repeated
`--cmake-arg=-DNAME=VALUE` for an uncommon CMake setting. `--dry-run`
prints the commands without running them. The orchestrator defaults to
`build-workspace` and coverage defaults to `build-workspace-coverage`. It
never deletes a build tree and rejects a cache created from another source
root; use a separate `--build-dir` when changing source roots or generators.

The dashboard's **Run existing build** action, or `run --no-build`, launches
the selected executable immediately and fails clearly if that configuration
has not been built yet. The normal `run` command still configures and builds
before launching.

Direct CMake remains the escape hatch:

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Profiles and diagnostics

The dashboard's **Development Tools** submenu completes the build/test/debug
loop without leaving the console:

- **Test explorer** reads names, labels, commands, working directories, and
  timeouts from CTest's JSON inventory. `/` edits a name search; Enter
  applies it and Escape cancels editing. Ordinary characters (including `q`,
  `h`, `j`, `k`, and `l`) remain text while editing; Backspace edits. The
  project-label action cycles filters. Hovering or selecting a test only
  displays its details. Choose **Run Selected**, **Run Matching**, or
  **Rerun Failed** explicitly.
- Test actions configure and build CMake's declared discovery targets and
  smoke targets first. The execution-mode action offers **Run Existing** to
  skip preparation. **Refresh Inventory** explicitly prepares the selected
  tree; merely opening or searching the explorer never builds. Missing
  configuration discovery files and mismatched single-configuration caches
  are rejected, even when CMake's discovery helper could fall back to
  another configuration.
- Selected tests run through CTest with escaped, anchored name filters split
  into bounded batches. CTest retains its working directories and timeouts.
  An empty selection never runs anything. Local
  `-T Test --no-compress-output` reports are captured per batch; nothing is
  submitted to a dashboard server. Results retain status and duration.
  Reruns use only the latest test run for the same checkout, build
  directory, and configuration. Cancelled/incomplete runs and missing tests
  require explicit selection rather than a guessed rerun.
- **Diagnostic browser** browses recorded runs with error, warning, and all
  filters. It recognizes MSVC, Clang/clang-tidy, and CMake locations. Select
  a diagnostic for a preview, then use **Inspect selected diagnostic** for
  raw log context and a read-only **CURRENT SOURCE** view. Relative paths
  use the recorded command directory. Missing files and invalid locations
  are reported; the browser never searches for replacement files. **Browse
  raw log** retains messages without recognized locations. Current files may
  differ from the run.
- **Profile picker** previews built-in and saved settings, including
  directory, configuration, generator, architecture, feature flags, extra
  CMake arguments, and parallelism. **Apply selected profile** carries them
  into ordinary build, test, and launch actions. Manual dashboard changes
  are session overrides; applying another profile clears them. Saving is
  explicit: type a name with `/`, apply the text, then choose **Save current
  settings**. Coverage and tidy retain their dedicated Debug/Ninja trees and
  honor selected parallelism.
- **Artifact shortcuts** offers existing build directories, the latest log
  for the selected build identity, the coverage HTML report, and generated
  PDFs. Opening uses Windows' default handler and never starts a build.

Subviews scroll with arrows, the wheel, Page Up/Down, and Home/End. Hit
regions follow the visible viewport; hover changes selection and only action
rows execute work. `q` or Escape returns to the previous view outside text
entry. Source-file statistics is also available in Development Tools.

Each recorded dashboard log has a version-1 `.json` sidecar containing build
identity, effective settings, wrapper and child command directories,
completion status, and available test results. `.tests.json`, per-batch XML,
and command context files support interrupted runs. Raw `.log` files remain
authoritative; older logs remain browsable without metadata but cannot
supply trusted reruns. All records stay under the ignored log directory
until explicitly removed. The toolbox uses only the standard library inside
`build.py`, so generated projects retain it when copying the orchestrator.

Use `python build.py profiles` to list named configurations (`--json` is
also available). Built-in `debug` and `release` profiles select their
configuration and separate `build-workspace-debug` /
`build-workspace-release` directories. Commands without a profile retain
their existing defaults.

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
atomically. `--dry-run` previews without writing. Only reusable
configuration, build-directory, generator, architecture, parallelism,
feature flags, and extra CMake arguments are saved; target, application,
clean, fresh, and dry-run choices are not saved. Treat extra CMake arguments
as trusted local build configuration.

For a shared file, pass `--profiles-file PATH` explicitly. Relative
profile-file and build-directory paths resolve from the repository root,
including when the shell is elsewhere. The file's parent directory must
already exist. Its format is:

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

Allowed keys are `config`, `build_dir`, `generator`, `architecture`,
`tracy`, `no_tests`, `no_docs`, `no_tidy`, `parallel`, and `cmake_arg` (a
string array). Parallelism is `null` for no explicit limit option, `0` for
automatic, or a positive job count. On the CLI, `--parallel` or
`--parallel=auto` selects automatic parallelism. Saved profiles replace
matching built-in definitions; there is no inheritance. Unknown keys and
malformed values are rejected.

`doctor` checks the selected cache and required tools, reports versions and
optional documentation tools, and supports `--json`. Errors return a nonzero
exit code. It runs bounded tool-version probes but never configures, builds,
downloads, or repairs anything. A successful report does not prove that a
compiler and Windows SDK can compile the project; CMake configuration
remains that check. Builds reject foreign caches and conflicting explicit
generator/architecture choices. Use a separate build directory, or
explicitly `--fresh` for a same-source generator change. Failed commands
report their exit code, elapsed time, working directory, and command while
preserving native tool diagnostics.

`stats` reports the current Git branch, commit, working-tree counts, tracked
file count, and categorized first-party lines. LOC counts nonblank lines in
the current contents of tracked source, tests, shaders, build tooling,
documentation, and configuration files. It excludes build trees, `archive/`,
`Illumo/thirdparty/`, `docs/output/`, and binary assets. Use `--json` for
machine-readable output; the interactive build console exposes the same
report through **Repository statistics**.

`file-stats` (aliases: `source-stats`, `stats --by-file`) provides a
per-file breakdown of first-party source files sorted from largest to
smallest by LOC. Options include `-n COUNT` / `--top COUNT`, `--min-loc`,
`--include-tests`, `--category`, `--sort`, and `--json`. The interactive
build console exposes this through **Source file statistics**.

Run the orchestrator regression suite with:

```bash
python -m unittest discover -s tools -p test_build.py -v
```

## Creating an application

Standalone generated workspaces include `engine-provenance.json`: source
commit, dirty/unknown status, template and creation options. A dirty source
copy is not an exact Git pin; retain its changes with the generated project.
Git-unavailable sources record unknown identity explicitly. Source must
remain unchanged during copying; the generator does not lock the checkout or
create a Git snapshot.

Names must be ASCII C++ identifiers and must not collide with Windows device
names or workspace/build infrastructure, ignoring case (for example
`Illumo`, `IllEd`, `cmake`, `build`, or `IllumoTests`). Rejection happens
before copying.

```bash
python build.py new-project <destination_path> [--name <ApplicationName>]
```

Or invoke the standalone script directly:

```bash
python tools/create_project.py <destination_path> --name MyGame
```

This generates a turnkey standalone workspace:

- `Illumo/`: engine sources (`Include/`, `Source/`, `Shader/`, `Assets/`,
  `thirdparty/`, `TestSupport/`, `Tests/`, `cmake/`)
- `IllEd/`: SceneGraph world editor
- `<ApplicationName>/` (default `IllumoGame/`): starter template (a 3D lit
  spinning cube with perspective camera, controls, configuration, and tests)
- `cmake/`, `build.py`, `CMakeLists.txt`, `README.md`

Starter controls yield while the console is open. Keys held during capture
must be released before they can control the cube again; animation
continues.

- **Space** — pause / resume rotation
- **R** — reset rotation angle to 0
- **G** — toggle 3D reference grid
- **Up / Down** — rotation speed
- **Escape** — exit

Generated workspaces omit engine PDF sources, so
`ILLUMO_BUILD_DOCUMENTATION` defaults off there. This repository defaults it
on when the documentation inputs are present. Explicitly enabling it without
those inputs fails at configure time.

Parallel builds order shared asset/shader staging for each runtime
directory. Default-file seeding locks its check-and-copy operation and
preserves existing settings; the first successful seed supplies a missing
file.

To exercise project creation and a complete default generated build on a
Windows development machine with CMake, LLVM, PowerShell, and LaTeX
installed:

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

## Documentation and contributing

All first-party architecture, decision, package, history, and build notes
live under `docs/`. Start with:

- [docs/README.md](docs/README.md) — documentation map and PDF commands
- [docs/architecture-consensus.md](docs/architecture-consensus.md) —
  canonical current architecture
- [docs/scene-graph-v2-design.md](docs/scene-graph-v2-design.md) —
  compiled scene/snapshot contract
- [docs/scene-graph-v2-plan.md](docs/scene-graph-v2-plan.md) —
  implementation and validation record
- [docs/charter-direction.md](docs/charter-direction.md) — later
  requirements vs current contracts
- [docs/frame-capture.md](docs/frame-capture.md) — `IllumoCapture`
- [docs/packages/](docs/packages/) — per-package maps
- `docs/latex/illumo.tex` / `docs/latex/architecture-map.tex` — prose book
  and chart pack; generated PDFs under `docs/output/` are not sources of
  truth

Contribution rules: [docs/contributing.md](docs/contributing.md) (no `auto`,
no namespaces, no recursion, Mozilla `clang-format`, `clang-tidy` during
the default build).

Third-party software and font acknowledgements:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). A normal build copies
that notice and license files into `licenses/` beside the executables.

Rebuild the PDFs from the repository root:

```powershell
.\docs\build.ps1
```

## Profiling

In **Debug and RelWithDebInfo**, press **F6** or run `profiler on` in the
console for the in-game timing pie. `profiler off` hides it; `profiler
toggle` toggles it, and `profiler` reports its state. It starts off each
run, independently of FPS and memory visibility. Close the console, then
press **1–3** to inspect Update, Rendering, or Presentation / waits, and
**0** to return to Frame. Number keys belong to the profiler while it is
visible.

The chart shows average milliseconds and percentages over the latest **120
completed frames**, refreshed four times per second. These are
**main-thread elapsed times**: CPU command submission is separate from
presentation / swap and the frame limiter. GPU execution and asynchronous
simulation-worker time are excluded. See
[docs/frame-profiler.md](docs/frame-profiler.md). Use Tracy for deeper
analysis.

Keep the normal Release optimization level while enabling application Tracy
instrumentation:

```bash
cmake -S . -B build-profile -DILLUMO_ENABLE_TRACY=ON -DILLUMO_BUILD_DOCUMENTATION=OFF
cmake --build build-profile --config Release
```

Visual Studio: open the generated solution from the build directory, or
generate with the VS generator.
