# 2026-09-17 Linux port repair on release/v26.09

Repaired the Linux platform path so Ubuntu 24.04 x86_64 (X11/XWayland) can
configure, compile, and run the in-tree applications without a second main
loop or a new dialog library.

## Source changes

- `Illumo/CMakeLists.txt`: gtkmm-3 via pkg-config imported target, Threads/dl,
  GLEW `OUTPUT_NAME` gated to Windows, GLFW Wayland off / X11 on.
- `LinuxSaveLoad.cpp`: honors `SaveLoadDialogSpec`, cancel is empty, overwrite
  confirmation, cwd restore, one-shot GTK init, no `Gtk::Stock`.
- `LinuxClipboard.cpp` + `LinuxGtk.cpp`: GTK clipboard after the same init.
- `EnvVars.cpp`: Linux `/proc/self/exe` for `envvars.json`.
- `RenderWindow.cpp` / `GLBackend.cpp`: no GL calls before `glewInit`; first
  viewport is set after GLEW loads.
- `PosixAtomicFile.cpp` unchanged: same-filesystem `rename`, no cross-device
  copy fallback.

## Documentation

Canonical build/run instructions: `docs/packages/platform-linux.md`. README,
package maps, AGENTS, architecture-consensus, frame-capture, and the platform
LaTeX chapter point at that guide. Linux is not described as supported.

## Verification

- Windows Release `IllumoTests` / `IllumoGame` rebuilt after the shared-path
  edits. `Illumo.EnvVars.ApplicationPath` and `Illumo.Platform.AtomicFile`
  passed.
- Native Linux Debug Ninja configure/compile on NixOS WSL (GCC 15.2, gtkmm
  3.24, `GLFW_BUILD_WAYLAND=OFF`): `IllumoGame`, `IllEd`, `IllMeshViewer`,
  `IllumoCapture`, and the test runners linked into `build/Debug/`.
  `Illumo.Platform.AtomicFile` and `Illumo.EnvVars.ApplicationPath` passed
  on that host. `ctest -L IllumoWorkspace -E "Bench|Oracle"`: 431/431 passed
  under Debug ASan. `Illumo.SceneGraph.Oracle` was excluded; it timed out at
  30 minutes under Debug ASan on WSL.
- Ubuntu 24.04 X11 remains the documented first GUI host. This WSL run had
  no GLX display, so window/dialog/capture smoke is still a human step.
