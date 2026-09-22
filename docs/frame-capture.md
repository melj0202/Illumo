# Frame capture

Illumo has two capture paths:

- **`IllumoRuntime --capture`**, the command-line capture mode. It runs an
  installed WASM application (or any package) exactly as a player would see
  it, reads back one presented frame and writes it as a PNG. It replaces the
  former standalone `IllumoCapture` executable, which rendered a fixed cube
  fixture and has been removed.
- **`FrameCapture`**, the engine API for bounded hidden-context renders from
  native code, verified by the explicit real-GPU `IllumoCaptureGpuTests`
  target.

Both need a real graphics device and driver. The WASM runtime is Windows x64
only, so there is currently no command-line capture on Linux.

## Runtime capture mode

From the staged configuration directory (the runtime works from its own
directory; relative paths resolve against the directory it was started from):

```powershell
cd build-workspace\Release
.\IllumoRuntime.exe --capture game.png
.\IllumoRuntime.exe --app illed --open scene.ilsc --capture editor.png
.\IllumoRuntime.exe --app meshviewer --open model.obj --capture frame.png --capture-frame 90 -ww 1280 -wh 720
```

- `--capture path.png`: required for capture. The path must end in `.png` and
  must not already exist; anything else is refused before the app starts.
- `--capture-frame N`: the frame to capture, counted in dispatched frames;
  default 60. It must be a positive integer (at most 100000); `0` or a
  malformed value refuses to start.
- `-ww` / `-wh`: window width and height, which set the captured size.
- `--app`, `--open`, `--package`, `--storage`, `--game`, `--mod`, `--worker`,
  `--memory-mib`, `--fuel` and `--deadline-ms` select and configure the app as
  they do for a normal launch. `--app` defaults to `game` and cannot be
  combined with `--package` or `--game`.

The runtime renders normally until the target frame. A `Renderer`
before-present hook (`Renderer::setBeforePresent`, run after submission and
before presentation) then reads back the presented backbuffer, writes the PNG
and requests close. A finished capture closes without product confirmation
dialogs. Launch options are never persisted.

### Result

Stdout carries exactly one JSON result line, whatever the outcome; logs go to
the log/stderr. Fields:

| Field | Meaning |
|---|---|
| `success` | `true` only when the PNG was written |
| `application` | The app that ran (the `--app` name or the package id) |
| `output` | The requested PNG path |
| `frame` | The dispatched frame at which the result was reported |
| `width`, `height` | Captured size in pixels (`0` on failure) |
| `error` | Empty on success, otherwise the reason |
| `guestError` | Present only when the guest reported an error |

The process exits with 0 on success and 1 on failure. Failures include an
unknown app, a missing `--open` file, an existing or non-`.png` output, a
package that fails to start, a target frame that is never presented, the
runtime closing before the capture frame, and readback or PNG write errors.

### Output

Output is a top-down RGBA8 PNG written by `FrameCapture::savePng`. It is
written exclusively to a sibling `.partial` file, then published with a
no-replace hard link and the staging name removed. The destination filesystem
must support hard links (e.g. NTFS). Failure never overwrites an existing
output. A crash may leave a staging file; inspect it before removing it and
retrying. No bit-identical cross-driver guarantee is made.

## Public API and ownership

`FrameCapture::render(options, producer)` in
`Illumo/Include/Illumo/Rendering/FrameCapture.h` is the reusable bounded
service. It creates a hidden GLFW context, submits synchronously, reads the
backbuffer before swap, destroys content and GPU resources, then destroys the
context. The producer receives `Renderer&`, `Camera&`, and an error string. It
must create, submit and destroy all renderer-bound content within the
callback, returning false on required-resource failure. It may use
`RenderScene` or direct tokens plus `SubmitOnly`. Payload storage must survive
synchronous submission. It must not swap, retain pointers, or enter a game
loop. The renderer rejects immediate fallback in strict capture mode;
attachment implementations report failures through
`Renderer::reportFrameError`.

This API is main-thread-only and must not run while another Illumo window is
live: the existing window service owns process GLFW lifetime. It intentionally
does not change that lifetime model. `savePng` is a separate CPU-only
operation; the runtime capture mode uses it on a live window's readback
instead of `render`. Backends that do not implement readback return an
explicit unsupported result. OpenGL latches command rejection and
invalid-resource diagnostics across queue reset; readback also checks GL
errors and restores pixel-pack/read-framebuffer state. It does not attempt to
prove the semantic correctness of custom shaders.

## Verification

Headless checks: `Illumo.Capture.Validation`, `Illumo.Capture.Png` and
`Illumo.Capture.StrictSubmission` cover `FrameCapture` validation, PNG output
errors, unsupported readback and strict submission. `Illumo.Runtime.Help` and
`Illumo.Runtime.InvalidCaptureFrame` check the runtime command line; both
finish before any window opens. Do not count headless tests alone as pixel or
platform evidence.

Real GPU verification of the runtime capture mode uses
`tools/verify_capture.py` with Python and Pillow. It makes seven runtime
invocations: non-uniform `game` and `illed` frames; a `meshviewer` launch of
a generated normal-less torus that must render lit and smoothly shaded
through retained geometry; and refusals (existing output, unknown app,
non-`.png` output, missing `--open` file) that must publish nothing, never
overwrite, and leave no `.partial` files. Each JSON result is saved beside the
images.

```powershell
python tools/verify_capture.py build-workspace/Release/IllumoRuntime.exe --output-dir build/capture-proof
cmake --build build --config Release --target IllumoCaptureGpuTests
build/Release/IllumoCaptureGpuTests.exe
```

Use a fresh output directory for each image verification run. The explicit
GPU test executable checks the `FrameCapture` API with actual hidden OpenGL:
queue-error retention after reset, producer exceptions, precise error
propagation and context recreation after failure. It is excluded from the
default build and headless CTest suite.
