---
name: illumo-visual-capture-check
description: Produce and check real-GPU screenshots of Illumo apps (game, IllEd, mesh viewer) with IllumoRuntime --capture, --capture-script and tools/verify_capture.py, including cross-backend (OpenGL/Vulkan/D3D12) parity. Use when a change affects what is drawn, when asked to screenshot or visually verify an app or menu, or when comparing backends. Carries known capture traps.
---

# Visual capture checks

Headless tests do not prove pixels. Use this when rendering, UI or backend
behaviour changed. Canonical reference: `docs/frame-capture.md`; command
options: `IllumoRuntime --help`.

## Before capturing

- **The workstation must be unlocked.** On a locked session (LogonUI
  running) live OpenGL captures fail silently with exit 1, and presents pause
  on Vulkan/D3D12. Check before blaming the change. (IllEd captures have been
  seen to work while locked; do not rely on it.)
- Use a fresh output directory. `--capture` refuses to overwrite an existing
  PNG; delete old shots first.
- Use a scratch `--storage <dir>` (it must exist). Captures write their
  1280x720 window size into `build-workspace/Release/envvars.json`
  (`WinX`/`WinY`), overwriting the user's window. Note the old values and
  restore them afterwards.
- Build the `dev` (RelWithDebInfo) or Release profile, not Debug/ASan.

## Commands

- Engine-level check across backends:
  `python tools/verify_capture.py build-workspace/Release/IllumoRuntime.exe --output-dir <dir>`
- One app, one frame:
  `IllumoRuntime --app game|illed|meshviewer --capture <file.png> --capture-frame N --storage <dir> [--graphics-api vulkan|d3d12]`
- Later screens (menus, overlays): add `--capture-script <file>`. Scripts take
  console lines and single `@key <Name>` lines only: no modifiers, no mouse,
  no right-click. IllEd exposes `scene_field <inspector key>` to open pickers
  in scripts.
- GPU parity proof: `IllumoGpuTests backendparity [--dump dir]` (CTest label
  `IllumoGpu`) and `IllumoCaptureGpuTests [--api vulkan]` (run by hand).

## App-specific traps

- **Game:** captures show only the canvas, not the guest UI layer (toolbar,
  hints, corner buttons). To check a glyph, dump the `GameVisual` shapes from
  a temporary test and rasterize them. The title screen is nondeterministic
  (two OpenGL captures differ by ~86k pixels), so do not diff game captures
  across backends. Use the mesh viewer for parity.
- **Splash:** capture and bench runs skip the splash; grab the live window
  instead.
- **Mesh viewer parity:** needs vsync on and a long wait (about 420 frames)
  before the capture frame.
- **IllEd:** under `--capture`, `--open scene.ilsc` has not loaded the scene
  (cause not established). Instead use `--project <dir>` plus a fresh
  `--storage` whose `envvars.json` sets `LaunchScene` to
  `vfs:/project/scenes/<x>.ilsc` and a `panelLayout`.
- **Guest logs** reach `log.txt` only at runtime `logLevel` 4 (the runtime's
  own `envvars.json` beside the exe).
- **Texture replacement:** guest `replaceTexture` is asynchronous; a frame
  can show the old texture under new geometry. Capture a few frames later
  before calling it a regression.
- **FPS on OpenGL** halving under NVIDIA "Present method: Auto" is a driver
  setting, not a code regression.

## Reporting

Look at every PNG you cite (Read the image). Report the backend, frame,
script, window size, and any trap above that limited the check. Delete
scratch captures you do not hand to the user, and restore `envvars.json`.
