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
| `IllumoGame/` | Cellular-automata sandbox, shipped only as the isolated `IllumoGame.wasm` package. Saves write sparse `.csim` version 4; loads versions 4, 3, and 2 plus legacy dense / `.illumo` |
| `IllumoRuntime` | Generic native host (Windows x64): window, OpenGL, services and a Wasmtime sandbox. Runs every interactive client program as a WASM package staged beside it in `apps/<name>/`, and captures PNG frames with `--capture` ([docs/wasm-game-runtime-design.md](docs/wasm-game-runtime-design.md), [docs/frame-capture.md](docs/frame-capture.md)) |
| `IllumoGuest/` | Guest SDK and WASI build tree: ABI wire headers, guest-side engine (`GuestModuleApplication`), recording backend |
| `IllEd/` | SceneGraph world editor, shipped as the `IllEd.wasm` package (`--app illed`). Writes `.ilsc` version 1 for later Illumo applications |
| `IllMeshViewer/` | Single-mesh `.obj` viewer with orbit, pan, zoom, and rotate, shipped as the `IllMeshViewer.wasm` package (`--app meshviewer`) |
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
| Linux, Ubuntu 24.04 x86_64, X11/XWayland, GCC 13+ or Clang 18, gtkmm-3 | Sources and CMake repaired so the engine libraries and native test suites configure, compile, and run; the WASM runtime and its apps are Windows x64 only. That is **not a support claim** until native GUI smoke on that host. See [docs/packages/platform-linux.md](docs/packages/platform-linux.md) |
| macOS | Not targeted; no port sources are retained |

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
  IllumoGame/            # CA simulator sources, catalogs and app.json manifest
  IllumoGuest/           # WASM guest SDK and guest build tree (WASI SDK)
  IllEd/                 # SceneGraph world editor and app.json manifest
  IllMeshViewer/         # Mesh viewer and app.json manifest
  archive/               # Historical / non-build material
```

Product `envvars.json` files are product-owned. Each package's copy is
staged in its `apps/<name>/` directory and supplies that app's first-run
defaults.

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
- The pinned WASM toolchain (Wasmtime 48.0.2 C API and WASI SDK 34.0). Run
  `python build.py wasm-tools` (or `.\tools\bootstrap-wasm.ps1`) once; it
  downloads SHA256-verified archives into `build-wasm-tools/` without
  touching `PATH`. IllumoGame, IllEd and IllMeshViewer are built only as WASM
  packages, so a default Windows configure requires these tools. `--no-wasm` /
  `-DILLUMO_BUILD_WASM_RUNTIME=OFF` builds the engine libraries and native
  test suites without `IllumoRuntime` or any runnable application
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
python build.py play --config Release --no-tidy --no-docs
```

`build.py` writes to `build-workspace/` by default. With LLVM and TeX already
installed, omit `--no-tidy` and `--no-docs`. `doctor` reports the WASM
toolchain, whether the tree holds `IllumoRuntime` with its staged
`apps\<name>\` packages, and stale pre-WASM outputs to delete (native
`IllumoGame.exe`, `IllEd.exe`, `IllMeshViewer.exe`, `IllumoCapture.exe` and an
old `game\` folder). A successful build ends with a summary of those outputs.

**Direct CMake** (Visual Studio generator; artifacts under `build/Release/`):

```powershell
cmake -S . -B build -DILLUMO_ENABLE_CLANG_TIDY=OFF -DILLUMO_BUILD_DOCUMENTATION=OFF
cmake --build build --config Release
```

**Run** `IllumoRuntime.exe` from the staged configuration directory. It works
from its own directory wherever it is started, so `Shader/`, `Assets/` and
`apps\` are always found beside it; relative command-line paths resolve
against the directory you started it from. With no arguments it plays the
IllumoGame package in `apps\game\` and keeps its settings, saves and user
rule catalogs in `storage\csim\`. `--app` selects another installed package
and `--open` hands it one document:

```powershell
cd build-workspace\Release
.\IllumoRuntime.exe
.\IllumoRuntime.exe --app illed --open scene.ilsc
.\IllumoRuntime.exe --app meshviewer --open model.obj
```

Or `python build.py play --app game|illed|meshviewer --config Release
--no-build`. Direct CMake uses `build\Release\` instead of
`build-workspace\Release\`.

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
| `Missing .../wasmtime.h` / `The pinned WASM toolchain is incomplete` | `python build.py wasm-tools`, or `--no-wasm` / `-DILLUMO_BUILD_WASM_RUNTIME=OFF` to skip the runtime and its apps |
| No `IllumoRuntime.exe` or `apps\` after a build | `python build.py doctor`; `build.py` always passes `ILLUMO_BUILD_WASM_RUNTIME`, so a rebuild replaces a stale cached `OFF` |
| Old `IllEd.exe`, `IllMeshViewer.exe`, `IllumoCapture.exe`, `IllumoGame.exe` or `game\` in the build tree | Stale pre-WASM outputs that nothing builds or launches; `python build.py doctor` lists them for deletion |

### Linux (Ubuntu 24.04 x86_64 — repaired, not supported)

Windows remains the verified production path. The pinned WASM runtime is
Windows x64 only, and every interactive application (IllumoGame, IllEd,
IllMeshViewer) and the capture mode now run only inside it, so a Linux tree
has no runnable applications until a Linux Wasmtime pin is added and
verified. It still builds the engine libraries and the native test suites
(`IllumoTests`, `IllumoGameTests`, `IllEdTests`, `IllMeshViewerTests`). Use a Linux tree only on a real
Ubuntu 24.04 x86_64 host with X11 or XWayland, GCC 13+ or Clang 18+, and
CMake 3.25+. Ubuntu 22.04's default GCC 11 and CMake 3.22 are not sufficient
(`std::ios::noreplace`, CMake 3.25). Native Wayland GLFW is off
(`GLFW_BUILD_WAYLAND=OFF`) because gtkmm-3 plus GLFW can deadlock.

WSL can configure and compile. Platform GUI and dialog smoke still need a
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

**Run.** There is nothing to launch on Linux yet: IllumoGame, IllEd,
IllMeshViewer and frame capture run only as WASM packages inside the Windows
x64 `IllumoRuntime`. The staged directory holds the native test runners.

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

- Runtime (Windows x64): `IllumoRuntime` plus `apps/game/`
  (`IllumoGame.wasm`), `apps/illed/` (`IllEd.wasm`) and `apps/meshviewer/`
  (`IllMeshViewer.wasm`), each with its `app.json` manifest; private
  `storage/<id>/` directories are created on first launch
- Tests: `IllumoTests`, `IllumoGameTests`, `IllEdTests`,
  `IllMeshViewerTests`, `IllumoPublicHeaderSmoke`, and on Windows the WASM
  host and package test runners
- Runtime files: `Shader/`, `Assets/`, `envvars.json`, `THIRD_PARTY_NOTICES.md`

Windows (VS generator): `build-workspace/Release/` for `build.py`, or
`build/Release/` for `-B build`. Linux (Ninja): `build-linux/<CONFIG>/`.

The default `ALL` target also runs the `IllumoWorkspace` CTest label via
`IllumoRunTests`. Debug builds enable AddressSanitizer on MSVC and GCC/Clang
(`ILLUMO_ENABLE_ASAN`, default `ON`). Debug is the sanitizer profile: its
WASM guests also run with explicit bounds checks on every memory access, so
it is markedly slower to play. Play and profile with RelWithDebInfo
(`python build.py play --profile dev`), which keeps the developer console
and profiler; `--profile debug-noasan` keeps Debug code generation without
the sanitizer.

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

Every interactive application is a WASM package run by `IllumoRuntime.exe`
from the staged configuration directory (see above; Windows x64 only).
Installed packages live in `apps\<name>\`, each with an `app.json` manifest:

| `--app` | Module | Manifest notes | Private storage |
|---|---|---|---|
| `game` (default) | `IllumoGame.wasm` | | `storage\csim\` |
| `illed` | `IllEd.wasm` | `launchAccess: "edit"`; preloads `Assets/IllEd/editor-ui-atlas.jpg` | `storage\illed\` |
| `meshviewer` | `IllMeshViewer.wasm` | `launchAccess: "read"`; preloads `Assets/Skybox/skybox-daylight.png` | `storage\meshviewer\` |

```text
IllumoRuntime.exe [--app name] [--open file] [-ww width] [-wh height]
IllumoRuntime.exe [--app name] [--open file] --capture new.png [--capture-frame n] [--capture-script file]
IllumoRuntime.exe --package dir [--storage dir]
IllumoRuntime.exe --game module.wasm --package dir --storage dir
IllumoRuntime.exe --help
```

`--app` cannot be combined with `--package` or `--game`. `--open` hands one
document to the app: the guest sees only the file's base name, and the host
grants the file as the selection `launch`, writable when the manifest says
`launchAccess: "edit"`. The window title comes from the manifest `title`.
Runtime settings live in `envvars.json` beside the runtime; each app keeps
its own settings in its storage directory, seeded from the package's
`envvars.json` on first run. `--help` and `--version` work on the runtime.
Design and cutover record:
[docs/wasm-apps-cutover-plan.md](docs/wasm-apps-cutover-plan.md).

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

Cellular-automata sandbox. The whole product (menus, canvas, editor, console
commands, save/load, clipboard, ruleset workshop and the 3D diagnostic)
executes inside `IllumoGame.wasm`; `IllumoRuntime` supplies only the window,
input, rendering, file, dialog, clipboard, display and console services.
Catalogs may add ruleset IDs; F2 on the canvas edits a rule. See
[docs/packages/game.md](docs/packages/game.md) and
[docs/wasm-game-cutover-plan.md](docs/wasm-game-cutover-plan.md).

`apps\game\app.json` names the module and the simulation lane worker and
requests memory, metering (`epoch`: calls are bounded by a wall-clock
deadline, not fuel), deadline and lane budgets; the runtime clamps them to
host ceilings. `--memory-mib`, `--deadline-ms` and `--fuel` (which forces fuel
metering) override a launch. Launch options are never persisted. Game settings (`tps`, ruleset, fade, `render3dTest`, ...) live in
the game's own `storage\csim\envvars.json`; the host console's `set`/`get`
address runtime settings, while the game's commands are forwarded into the
package.

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

Set `"render3dTest": "1"` in `storage\csim\envvars.json` for an opt-in 3D
diagnostic `SceneGraph` (not the normal canvas). Set it back to `0` to
restore the orthographic CA view. Its lit meshes cast into the runtime's
shared shadow pass through frame schema version 2.

Simulation publishes at most one generation per frame. Once a generation
costs more than 4 ms, it runs on up to eight isolated simulation lanes
(`CSimWorkerGuest.wasm` stores, each owning bands of chunk rows) while frames
keep rendering; smaller worlds and elementary 1D rules run in the game store.
Editing, loading or saving while running discards at most the one generation
in flight. `status` shows where generations execute. Overdue whole steps are
dropped. The visible viewport is a padded, integer-LOD
cache with tiled uploads. Timing, fade, and upload details:
[docs/packages/game.md](docs/packages/game.md).

### IllEd

SceneGraph world editor. It does not simulate cellular automata. File / Edit /
Create / View authors `.ilsc` JSON (version 1). The whole editor runs inside
`IllEd.wasm`. Open a scene at startup with `--open`, or use File > Open.
Because the manifest grants the launch document for editing, Ctrl+S saves it
in place. Dialogs use pattern `*.ilsc`; save, open and close confirmation
are asynchronous, and editing input is held while a transfer is in flight.
See [docs/packages/illed.md](docs/packages/illed.md).

```text
IllumoRuntime.exe --app illed
IllumoRuntime.exe --app illed --open scene.ilsc
```

### IllMeshViewer

Displays one `.obj` on a 3D reference grid inside `IllMeshViewer.wasm`. The
launch document is read-only; its bytes are parsed in the guest with
`MeshLoader::loadFromMemory`, large static meshes are uploaded once as
retained host meshes (frame schema v3), and the skybox cross is preloaded
from the package and drawn as a host cubemap. As in every app, dynamic UI and
line geometry stays on the host and only changed bytes travel (frame schema
v4).

```text
IllumoRuntime.exe --app meshviewer
IllumoRuntime.exe --app meshviewer --open model.obj
```

Controls:

- LMB / RMB drag — orbit; MMB or Shift+drag — pan
- WASD / arrows — pan; scroll — zoom
- Q / E or Alt+drag — roll / tilt
- **O** — open-mesh dialog; **R** / **F** — reset / frame; **G** — grid;
  **X** — wireframe / bounds

### Frame capture

`IllumoRuntime --capture` replaces the former `IllumoCapture` tool. It runs
the selected app until `--capture-frame` (default 60), reads back the
presented backbuffer, writes a new PNG, prints one JSON result line and exits
with 0 or 1. Real GPU required; the output must be a new `.png` path.
`--capture-script file` first runs the same steps as `--bench-script` (below),
so a capture can reach a later screen; the capture frame then counts from the
end of the script. See [docs/frame-capture.md](docs/frame-capture.md).

```powershell
# Windows, from the staged Release directory
.\IllumoRuntime.exe --app meshviewer --open model.obj --capture frame.png
.\IllumoRuntime.exe --capture game.png --capture-frame 120 -ww 1280 -wh 720
.\IllumoRuntime.exe --capture settings.png --capture-script f1.txt --capture-frame 30
```

`--bench-frames n` times `n` frames after `--bench-warmup` (default 120) and
prints one JSON line (frame intervals, module update time, WASM exchange
timings, frame payload counters); `--bench-script file` first runs console
lines, `@key Name` presses and `@wait n` pauses. The in-app `wasm_stats`
command shows the same exchange statistics. See
[docs/frame-capture.md](docs/frame-capture.md).

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
runners support `--list` and exact `--run`. `build.py test` builds every
executable CTest will run, including the `Illumo.Wasm.*` host tests and the
package tests that drive each real app through the generic host:
`IllumoGame.Wasm.GamePackage`, `IllEd.Wasm.Package` (launch scene,
package-preloaded atlas, keyboard pan, Ctrl+S save in place) and
`IllMeshViewer.Wasm.Package` (launch mesh as one retained host mesh, skybox
cubemap). `Illumo.Runtime.Help` and `Illumo.Runtime.InvalidCaptureFrame`
check the runtime command line headlessly; real captures are verified by
`tools/verify_capture.py`. An exact `--test` name is resolved through
CTest first and then by discovered product or runner prefix. Each case gets an isolated
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
`MinSizeRel`, cycle the **Application** setting through the installed apps
(IllumoGame / IllEd / Mesh Viewer), toggle documentation, Tracy and the WASM
runtime, select build parallelism, and run a focused action: **Play**,
**Build everything**, **Build runtime and apps**, **Run headless tests**,
**Run existing build**, and the tools below. **Play** builds `IllumoRuntime`
(which stages every package) and runs the selected app. The header line
reports which apps are staged in the selected tree, whether the WASM
toolchain is missing, and how many stale pre-WASM outputs remain. On Windows consoles, moving the mouse over a row
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
python build.py doctor
python build.py wasm-tools
python build.py build --config Debug
python build.py build --config Debug --target IllumoRuntime --parallel
python build.py build --no-wasm
python build.py play
python build.py play --app illed -- --open scene.ilsc
python build.py play --app meshviewer --no-build -- --open model.obj --capture frame.png
python build.py play --no-build -- --package D:\MyGame
python build.py test
python build.py test --list-tests
python build.py test --test IllumoGame.CellGame.SaveLoadRoundTrip
python build.py test --test IllumoGame.Wasm.GamePackage
python build.py test --test IllEd.Wasm.Package
python build.py play --app illed -- -ww 1280 -wh 720
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
That build retains CMake's all-target behavior: library, `IllumoRuntime` with
every app package, all test runners, every registered workspace case, and
the PDFs when the documentation toolchain is available.

Use `--no-docs` to skip the optional PDF target, `--generator` and
`--architecture` to select a CMake generator, and repeated
`--cmake-arg=-DNAME=VALUE` for an uncommon CMake setting. `--dry-run`
prints the commands without running them. The orchestrator defaults to
`build-workspace` and coverage defaults to `build-workspace-coverage`. It
never deletes a build tree and rejects a cache created from another source
root; use a separate `--build-dir` when changing source roots or generators.

The dashboard's **Run existing build** action, or `play --no-build`, launches
the selected app immediately and fails clearly if that configuration has not
staged it yet. The normal `play` command still configures and builds before
launching. `run` launches native executables, which in this workspace means
only `IllumoRuntime`.

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
`build-workspace-release` directories; `dev` is RelWithDebInfo in
`build-workspace-dev` (play and profile), and `debug-noasan` is Debug without
AddressSanitizer in `build-workspace-debug-noasan`. A saved profile with a
built-in's name replaces it. Commands without a profile retain their
existing defaults.

```bash
python build.py doctor --profile release
python build.py play --profile dev
python build.py build --profile debug --parallel 4
python build.py profile-save mine --profile debug --no-docs --parallel 4
python build.py test --profile mine
python build.py build --profile mine --docs --config RelWithDebInfo --dry-run
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
optional documentation tools, reports an `apps` check for the staged
`apps\<name>\` packages, flags stale native `IllumoGame.exe`, `IllEd.exe`,
`IllMeshViewer.exe`, `IllumoCapture.exe` and an old `game\` folder for
deletion, and supports `--json`. Errors return a nonzero
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
- [docs/frame-capture.md](docs/frame-capture.md) — `IllumoRuntime --capture`
  and the `FrameCapture` API
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
