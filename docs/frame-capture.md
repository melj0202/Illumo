# Standalone frame capture

`IllumoCapture` renders one requested result through the production OpenGL
backend without starting Illumo's application host, input, modules, simulation,
audio, or configuration persistence. It creates a hidden GLFW context, submits
synchronously, reads the backbuffer before swap, destroys content and GPU
resources, then destroys the context. A real graphics device/driver is required.

Build from the workspace root (or configure `-S Illumo` for library-only use):

```powershell
cmake --build build --config Release --target IllumoCapture
build/Release/IllumoCapture.exe --output build/direct.png --mode direct --width 640 --height 480
build/Release/IllumoCapture.exe --output build/scene.png --mode scene --width 640 --height 480
```

Both modes use the same colored 3D cube, shaders, camera, state and normal
`Renderer::RenderScene`. Direct mode uses a drawable in the transient `Scene`
frame list, without persistent nodes. Scene mode extracts a transformed
`SceneGraph` attachment into that same frame list. The fixture is deliberately
unlit, with depth enabled and temporal effects disabled; it is not the corridor
game or a lighting benchmark.

## Inputs and outputs

- `--output path.png`: required, existing output/staging files are refused.
- `--mode direct|scene`: defaults to direct.
- `--width N --height N`: integer dimensions in 1..4096; default 640 by 480.
- `--eye X Y Z --target X Y Z --fov degrees`: Y-up perspective camera; default
  eye (4,3,5), target origin, vertical FOV 45. Degenerate/nonfinite inputs fail.
- `--rotation degrees`: fixed Y rotation, default 25. No simulation is advanced.
- `--mesh file.obj`: CPU-load an explicit OBJ instead of the built-in cube.
  The diagnostic fixture uses vertex colors and geometry, not MTL textures.
- `--vertex-shader file --fragment-shader file`: replace fixture shaders, which
  must accept position at location 0, normalized RGBA8 at 1, and `mat4 uMVP`.
  Defaults are staged `Shader/capture_*.glsl` beside the executable.
- `--backend opengl`: the only supported implementation; other values fail.
- `--inspect-mesh file.obj`: print CPU mesh-load results and exit without
  creating any graphics context or host.

Stdout is a JSON result. Debug logs and shader compiler output go to stderr.
Exit 0 means the requested operation completed, 2 means invalid arguments,
and 1 means asset, context, submission, readback or output failure. JSON records
camera, object state, paths, dimensions, backend, stage, error and elapsed time.
Engine revision is a configure-time Git revision with dirty suffix, or unknown;
paths alone do not pin asset contents. Save the fixture assets and diagnostics
with captures used as reproducible bug reports. No bit-identical cross-driver
guarantee is made.

Output is a top-down RGBA8 PNG. It is written exclusively to a sibling
`.partial` file, then published with a no-replace hard link and the staging
name removed. The destination filesystem must support hard links (e.g. NTFS or
ext4). Failure never overwrites an existing output. A crash may leave a staging
file; inspect it before removing it and retrying.

## Public API and ownership

`FrameCapture::render(options, producer)` in
`Illumo/Include/Illumo/Rendering/FrameCapture.h` is the reusable bounded service.
The producer receives `Renderer&`, `Camera&`, and an error string. It must create,
submit and destroy all renderer-bound content within the callback, returning
false on required-resource failure. It may use `RenderScene` or direct tokens
plus `SubmitOnly`. Payload storage must survive synchronous submission. It must
not swap, retain pointers, or enter a game loop. The renderer rejects immediate
fallback in strict capture mode; attachment implementations report failures
through `Renderer::reportFrameError`.

This API is main-thread-only and must not run while another Illumo window is
live: the existing window service owns process GLFW lifetime. It intentionally
does not change that lifetime model. `savePng` is a separate CPU-only operation.
Backends that do not implement readback return an explicit unsupported result.
OpenGL latches command rejection and invalid-resource diagnostics across queue
reset; readback also checks GL errors and restores pixel-pack/read-framebuffer
state. It does not attempt to prove the semantic correctness of custom shaders.

Set `ILLUMO_BUILD_CAPTURE=OFF` to omit the executable. Existing applications keep
their normal visible window defaults, render loop and immediate test fallback.

## Verification

Headless `Illumo.Capture.*` checks cover validation, PNG output errors, unsupported
readback and strict submission. Real GPU verification uses
`tools/verify_capture.py` with Python and Pillow; it checks direct/scene pixel
equivalence, geometry visibility, depth occlusion and diagnostic failure paths.
Do not count headless tests alone as pixel or platform evidence.

```powershell
python tools/verify_capture.py build/Release/IllumoCapture.exe --output-dir build/capture-proof
cmake --build build --config Release --target IllumoCaptureGpuTests
build/Release/IllumoCaptureGpuTests.exe
```

Use a fresh output directory for each image verification run. The explicit GPU
test executable also checks queue-error retention after reset, producer
exceptions, precise error propagation and context recreation after failure.
It is excluded from the default build and headless CTest suite.
