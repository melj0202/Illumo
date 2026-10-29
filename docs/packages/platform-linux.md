# Illumo Linux port

The dual-platform clone-and-build on-ramp (Windows and Ubuntu) is the root
[README.md](../../README.md) section **Get the build running**. This file is
the Linux package list, configure/build failure table, and GUI smoke matrix.

Windows remains the supported, fully verified production path. Linux sources
and CMake on this branch let an Ubuntu 24.04 x86_64 host with X11 or XWayland
configure and compile the engine libraries and run the native test suites —
verified 2026-10-23 on Ubuntu 24.04.5 (GCC 13.3) at `main` a0aaf67: the full
build succeeds and all 626 registered tests pass. `LinuxChildProcess.cpp`,
the relaunch arguments in `LinuxProcessRelaunch.cpp` and
`SaveLoad::GetFolderLocation` came later on `release/v26.10`; that branch
built on Ubuntu 24.04.5 (GCC 13.3) on 2026-10-29 and passed its 452
registered tests without CSim. That is not a support claim until the
named host has completed native launch, dialog, input, persistence, and
shutdown smoke; GUI/GLX smoke remains untested (the verifying VM has no
display).

Every interactive application (IllumoGame, IllEd, IllMeshViewer) and the
command-line frame capture run only as WASM packages inside `IllumoRuntime`.
On Linux the runtime is **opt-in** (D-PL2, [WASM runtime](#wasm-runtime-opt-in-d-pl2)
below); a default Linux tree has no runnable applications. The native
executables `IllEd`, `IllMeshViewer` and `IllumoCapture` are no longer built
on any platform.

macOS is not targeted; its scaffold has been removed.

## What this port covers

- Process entry: `Illumo/Source/Platform/Linux/_main.cpp` still calls
  `RunIllumoApplication`. There is no second main loop.
- Native save/load: gtkmm-3.0 dialogs honor `SaveLoadDialogSpec` (filter,
  default filename, description). OK returns a path. Cancel, close, and init
  failure return an empty string and must not change process cwd or product
  state. Save uses overwrite confirmation. Dialogs never parse `.csim` /
  `.illumo` / `.ilsc`.
- Clipboard: GTK clipboard (`Clipboard::GetText` / `SetText`) after the same
  one-shot GTK init. Headless sessions without a display return empty/false.
- Atomic publish: `PosixAtomicFile.cpp` uses `std::filesystem::rename`. Linux
  `rename(2)` replaces on the same filesystem and fails with `EXDEV` across
  devices. There is no copy fallback (Windows also refuses cross-volume
  replace).
- `envvars.json` is resolved from `/proc/self/exe` so the working directory
  does not matter. Fallback is still `current_path() / envvars.json` if
  `readlink` fails.
- GLFW is built with X11 only (`GLFW_BUILD_WAYLAND=OFF`). gtkmm-3 plus GLFW
  on native Wayland can deadlock.
- Process memory overlay stays on `UnsupportedProcessMemoryStats` and reports
  unavailable.
- The startup machine report uses `UnsupportedSystemInfo`, which supplies only
  the logical processor count.
- In-application restart (`RelaunchCurrentProcess`, D-UI13) uses
  `posix_spawn` of `/proc/self/exe` with the original `argv`; like the rest of
  this port it is unverified on Linux.

Not in this port: `.deb` / AppImage / Flatpak, native Wayland, aarch64,
`gtkmm-4`, a second graphics API, XDG config directories, `/proc/self/statm`
memory stats.

## First host

| Item | Required |
|---|---|
| Distro | Ubuntu 24.04 LTS x86_64 |
| Session | X11 or XWayland (`echo $XDG_SESSION_TYPE`) |
| Compiler | GCC 13+ or Clang 18+ (C++23 `std::ios::noreplace`) |
| CMake | 3.25 or newer |
| GPU | Working GLX (Mesa or vendor driver) |

Ubuntu 22.04's default GCC 11 and default CMake 3.22 are not sufficient.

## Packages

Debian/Ubuntu packages are listed in `tools/install-linux-deps.sh` (OpenGL/GLX,
X11 for GLFW, gtkmm-3, toolchain). CMake runs that script on Linux configure
when `ILLUMO_INSTALL_LINUX_DEPS` is ON (the default). If you are not root and
sudo needs a password, run it once in a terminal:

```bash
bash tools/install-linux-deps.sh --tidy
```

Or let CMake do it (passwordless sudo or root):

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DILLUMO_BUILD_DOCUMENTATION=OFF
```

Disable the helper with `-DILLUMO_INSTALL_LINUX_DEPS=OFF` if packages are
already provided by the image. Non-Debian hosts skip the script.

Confirm the dialog toolkit and a live GLX display before the first GUI run:

```bash
pkg-config --modversion gtkmm-3.0
g++ --version
cmake --version
echo "$XDG_SESSION_TYPE"
glxinfo -B
```

If `glxinfo` fails, stop. That is a driver/session problem, not an Illumo bug.
On a desktop Ubuntu 24.04 install `gtkmm-3.0` should report 3.24.x. The apt
helper also installs `clang-tidy` when CMake's tidy gate is on.

## Configure, build, and test

From the repository root. Use a dedicated Linux build tree; do not reuse a
Windows cache.

```bash
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DILLUMO_BUILD_DOCUMENTATION=OFF \
  -DILLUMO_ENABLE_CLANG_TIDY=OFF

cmake --build build-linux --parallel
```

Repeat RelWithDebInfo in a separate tree:

```bash
cmake -S . -B build-linux-rel -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DILLUMO_BUILD_DOCUMENTATION=OFF \
  -DILLUMO_ENABLE_CLANG_TIDY=OFF

cmake --build build-linux-rel --parallel
```

`python build.py build --config Debug --no-docs --no-tidy` is the orchestrator
equivalent if you prefer it. `python build.py doctor` reports tools; it does
not compile. `python build.py play` needs the runtime (`--wasm` on Linux).

The default Ninja `ALL` target builds the tests without running them, unless
`ILLUMO_RUN_TESTS_ON_BUILD` is `ON`. Debug AddressSanitizer
makes `Illumo.SceneGraph.Oracle` and the large scene benches very slow (tens of
minutes on WSL). Prefer RelWithDebInfo for a first GUI smoke, or build specific
targets and then:

```bash
ctest --test-dir build-linux -L IllumoWorkspace -E "Bench|Oracle" --output-on-failure
```

### Expected binaries

After a successful Debug build, `build-linux/Debug/` should contain the
test executables and staged runtime files (Ninja puts them in a config folder
so they do not collide with source-named binary directories such as `IllEd/`):

- Applications: none unless the WASM runtime is on (see below)
- Tests: `IllumoTests`, `IllumoGameTests`, `IllEdTests`,
  `IllMeshViewerTests`, `IllumoPublicHeaderSmoke`
- Staged runtime: `Shader/`, `Assets/`, `envvars.json`,
  `THIRD_PARTY_NOTICES.md`

Run every headless workspace case from the build tree:

```bash
ctest --test-dir build-linux -L IllumoWorkspace --output-on-failure
```

That label includes `Illumo.AtomicFile` / `Illumo.Platform.AtomicFile`,
the headless `Illumo.Capture.*` API checks, and the native product oracle
suites. The `*.Wasm.*`, `Illumo.Runtime.*`, `Illumo.Dist.*` and
`Illumo.Pack.*` cases are registered only with the WASM runtime on (below);
run those under a display. Headless tests do not prove GLFW, OpenGL, or GTK
dialogs.

If CTest discovery is empty on Ninja, list and run the runners directly:

```bash
./build-linux/Debug/IllumoTests --list
./build-linux/Debug/IllumoGameTests --list
./build-linux/Debug/IllEdTests --list
./build-linux/Debug/IllMeshViewerTests --list
```

Debug builds link AddressSanitizer. Treat an ASan hit that also fails on
Windows as a shared bug, not a Linux-only defect.

### Common configure/build failures

| Failure | Fix |
|---|---|
| CMake older than 3.25 | Install CMake 3.25+ (Ubuntu 24.04 archive is fine) |
| `ILLUMO_ENABLE_CLANG_TIDY` requires clang-tidy | Pass `-DILLUMO_ENABLE_CLANG_TIDY=OFF` |
| `gtkmm-3.0` not found | Install `libgtkmm-3.0-dev` |
| GLFW Wayland protocols / wayland-scanner | This tree forces `GLFW_BUILD_WAYLAND=OFF` |
| `ios::noreplace` missing | Compiler too old; use GCC 13 or Clang 18 |
| `glxinfo` / window creation fails | Driver or session; use X11/XWayland |

## WASM runtime (opt-in, D-PL2)

Linux x86_64 has its own runtime pin since 2026-10-29: the Linux builds of
the Windows toolchain, Wasmtime 48.0.7 (C API and CLI) and WASI SDK 34.0,
SHA256-pinned in `tools/bootstrap-wasm.sh` and named in
`cmake/IllumoWasm.cmake`. `ILLUMO_BUILD_WASM_RUNTIME` defaults to `OFF` on
Linux, so the default build is unchanged and needs no toolchain.

```bash
python3 build.py wasm-tools        # or: bash tools/bootstrap-wasm.sh
python3 build.py build --wasm --config Release --no-docs --no-tidy
```

With CMake directly, add `-DILLUMO_BUILD_WASM_RUNTIME=ON` to a configure
above. The build adds `IllumoRuntime`, the module compiler helper
`IllumoWasmCompiler` and the app packages under `apps/`.

How it differs from Windows:

- `libwasmtime.so` has no SONAME. CMake links it by name
  (`IMPORTED_NO_SONAME`) and copies it beside the runtime, and the runtime and
  the helper search `$ORIGIN` first, so a `python build.py dist` folder runs
  where it is moved.
- `LinuxWasmCompiler.cpp` / `LinuxWasmCompilerMain.cpp` keep the Windows
  helper's contract (buffer layout, exit codes, error texts). The buffer is a
  `memfd` on descriptor 3; stdin and stdout are `/dev/null`; no other
  descriptor is inherited except stderr. The helper leads its own process
  group, which is killed on the deadline and after every run, asks to die
  with its parent, writes no core dump, and sets `RLIMIT_DATA` to the
  compiler memory limit before it reads the module.
- Gaps against the Windows job object: there is no one-process limit (a
  descendant of the helper dies with its group), and `RLIMIT_DATA` counts
  private writable mappings, which is close to but not the job's commit
  limit. The helper needs glibc 2.34+ (`posix_spawn_file_actions_addclosefrom_np`);
  the deadline waits on `pidfd_open` (Linux 5.3+) and polls every 2 ms
  without it.

Verified 2026-10-29 on Ubuntu 24.04.5 (GCC 13.3, CMake 3.28, glibc 2.39) in a
display-less cloud container, `release/v26.10` with this change, without
CSim:

- Release build with the runtime: 541 registered `IllumoWorkspace` tests (452
  without it), all passing under Xvfb: 68 `Illumo.Wasm.*`, 7
  `Illumo.Runtime.*`, the IllEd, IllMeshViewer and Playground package tests,
  `Illumo.Dist.*` and `Illumo.Pack.StagedApp`.
- Without a display, `Illumo.Runtime.MountMissingDir`, `ProjectMissingDir`,
  `PackageMissing` and `CaptureScriptNeedsCapture` pass for the wrong reason:
  they expect a failure (`WILL_FAIL`) and the runtime fails at GLFW start-up
  before it reads the option. Run the suite under `xvfb-run -a` or a desktop
  session. Without CSim, `CaptureScriptNeedsCapture` fails because the default
  `game` app is missing, on every host.
- The helper compiles `IllEd.wasm` within 1 GiB and 256 MiB limits and is
  stopped at 64 MiB and 16 MiB (`WASM compiler terminated (signal 6)`); a
  1 ms deadline reports `exceeded its deadline`; a descriptor the host left
  open without close-on-exec does not reach it.
- Under Xvfb with Mesa llvmpipe (OpenGL 4.5), `--capture` of `illed`,
  `meshviewer` and `playground` renders correct frames and exits 0, and
  `IllEd.Gpu.Smoke` passes. The Vulkan GPU cases skip (no Vulkan driver).
- Not verified: a desktop session, a GPU driver, input, GTK dialogs,
  fullscreen, audio output, the Vulkan backend, CSim's game package, and a
  Debug (AddressSanitizer) runtime build.

## Run from the staged directory

A default Linux tree has nothing to launch. With the WASM runtime on,
`IllumoRuntime --app illed` (or `meshviewer`, `playground`, and `game` with
CSim) launches from the staged directory, and `--capture` writes a PNG (see
[frame-capture.md](../frame-capture.md)). Launches have been checked only on
a virtual X server; the desktop launch, working-directory-independence and
application smoke below remain unvalidated.

Each window should accept keyboard and mouse, then exit
0 from `Esc`/`Q` and from the window-manager close button, and a launch from
`/tmp` must read and write the staged `envvars.json`, not `/tmp/envvars.json`.

### Application smoke (human)

Do this on the Ubuntu host once applications can run there; headless tests
cannot replace it.

**IllumoGame:** window, menu or canvas, mouse edit on the canvas, F1, Grave
(Debug overlay), F3, F11 fullscreen and restore, Q/Esc, WM close, second
launch from `/tmp`. Save dialog filter/default name, load/save cancel is a
no-op, a real save survives process restart.

**IllEd:** UI visible, one create, save cancel, one `.ilsc` save that
round-trips.

**IllMeshViewer:** HUD visible, open-mesh cancel, optional `.obj` load.

Clipboard: copy/paste a pattern in IllumoGame. If the GTK clipboard is empty
in a nested session, say so rather than calling the port done.

## Constraints that remain in force

- GTK is limited to native dialogs and clipboard. GLFW owns the window,
  including the hidden capture window.
- Platform code must not include Game, Rulesets, or IllEd types.
- Cancel is an empty string. Dialogs do not mutate game state.
- `AtomicFile::write` failure leaves the previous destination and removes only
  that call's staging file.
- Do not describe Linux as supported in product copy until the smoke matrix
  above is filled on a named Ubuntu 24.04 host.

## Follow-up, not required to launch

- Headless GitHub Actions on `ubuntu-24.04` (Windows Release runs are `.github/workflows/windows-tests.yml`)
- `QueryProcessMemoryStats` via `/proc/self/statm`
- `QuerySystemInfo` via `uname`, `/proc/cpuinfo` and `sysinfo`
- Native Wayland GLFW plus gtkmm re-smoke
- HiDPI cursor mapping
- Packaged tarball / `.deb`
- aarch64
- macOS (not targeted; no scaffold retained)
