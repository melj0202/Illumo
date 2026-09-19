# Illumo Linux port

The dual-platform clone-and-build on-ramp (Windows and Ubuntu) is the root
[README.md](../../README.md) section **Get the build running**. This file is
the Linux package list, configure/build failure table, and GUI smoke matrix.

Windows remains the supported, fully verified production path. Linux sources
and CMake on this branch are repaired so an Ubuntu 24.04 x86_64 host with X11
or XWayland can configure, compile, and run the in-tree applications. That is
not a support claim until the named host has completed native launch, dialog,
input, persistence, and shutdown smoke.

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
not compile.

The default Ninja `ALL` target also runs `IllumoRunTests`. Debug AddressSanitizer
makes `Illumo.SceneGraph.Oracle` and the large scene benches very slow (tens of
minutes on WSL). Prefer RelWithDebInfo for a first GUI smoke, or build specific
targets and then:

```bash
ctest --test-dir build-linux -L IllumoWorkspace -E "Bench|Oracle" --output-on-failure
```

### Expected binaries

After a successful Debug build, `build-linux/Debug/` should contain the
executables and staged runtime files (Ninja puts them in a config folder so
they do not collide with CMake's `IllumoGame/` binary directory):

- Applications: `IllumoGame`, `IllEd`, `IllMeshViewer`, `IllumoCapture`
- Tests: `IllumoTests`, `IllumoGameTests`, `IllEdTests`,
  `IllMeshViewerTests`, `IllumoPublicHeaderSmoke`
- Staged runtime: `Shader/`, `Assets/`, `envvars.json`,
  `THIRD_PARTY_NOTICES.md`

Run every headless workspace case from the build tree:

```bash
ctest --test-dir build-linux -L IllumoWorkspace --output-on-failure
```

That label includes `Illumo.AtomicFile` / `Illumo.Platform.AtomicFile`,
capture CLI help/invalid-dimension tests, and the product suites. Headless
tests do not prove GLFW, OpenGL, or GTK dialogs.

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

## Run from the staged directory

Shaders, fonts, and `envvars.json` are staged beside the executables. Launch
from that directory:

```bash
cd build-linux/Debug
./IllumoGame
./IllEd
./IllMeshViewer
```

Each window should accept keyboard and mouse, then exit 0 from `Esc`/`Q` and
from the window-manager close button.

Working-directory independence (config must resolve beside the executable):

```bash
cd /tmp
/path/to/build-linux/Debug/IllumoGame
```

That process must read and write `build-linux/Debug/envvars.json`, not
`/tmp/envvars.json`.

Frame capture (hidden GLFW window, real GL context required). The destination
must not already exist:

```bash
./build-linux/Debug/IllumoCapture --help
./build-linux/Debug/IllumoCapture --output /tmp/illumo-frame.png --mode scene
```

Expect a PNG plus a JSON sidecar on stdout. If hidden-window GLX fails after
the three GUI apps work, record capture as unvalidated rather than treating
the whole port as broken.

### Application smoke (human)

Do this on the Ubuntu host; headless tests cannot replace it.

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

- Headless GitHub Actions on `ubuntu-24.04`
- `QueryProcessMemoryStats` via `/proc/self/statm`
- Native Wayland GLFW plus gtkmm re-smoke
- HiDPI cursor mapping
- Packaged tarball / `.deb`
- aarch64
- macOS (not targeted; no scaffold retained)
