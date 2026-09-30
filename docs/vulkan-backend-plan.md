# Vulkan backend execution plan

Status: complete, M0-M8 (D-R33); see the validation log in section 10.
Tier 2 plan (`.agent/PLANS.md`: a new backend).
Owner request (2026-09-30): "create a Vulkan backend that I can switch to/from
and that is compatible with the engine without the logic leaking into the
renderer frontend. Must have and maintain feature parity with the OpenGL
backend."

## 1. Objective and measurable end state

- `Illumo/Source/Rendering/Vulkan/` implements `IBackend` completely: every
  `RenderCommand`, resource call, readback and statistic the OpenGL backend
  supports, with the same observable results (pixels, row order, error text,
  validation outcomes).
- The backend is selected by the `GraphicsAPI` setting (`OPENGL` default,
  `VULKAN`), the `--graphics-api` command-line option, the console
  (`set GraphicsAPI vulkan`, restart) and the product settings menus (display
  wire version 5). A Vulkan start that fails falls back to OpenGL.
- Nothing Vulkan-specific enters `Renderer`, drawables, `Illumo/Include`,
  guests or products. The frontend keeps emitting the same tokens and the same
  GLSL 330 shader text.
- Parity is demonstrated by real-GPU tests that render the same scenes through
  both backends and compare the images, by the existing GPU parity cases run
  on Vulkan, and by captures of the shipped packages under both backends.

## 2. Current-state evidence

- `IBackend` (`Include/Illumo/Rendering/IBackend.h`) is the only seam the
  frontend uses. `GLBackend`/`GLDevice` execute tokens with GL state-machine
  semantics: state persists across submissions, uniforms persist per program,
  textures bind to units, `SetInstanceStream` edits the bound VAO, updates are
  ordered with draws.
- Composition: `Illumo::initialize` (window factory `CreateRenderWindow`,
  backend factory `CreateOpenGLBackend`), `FrameCapture` and the GPU tests.
  `RenderWindow` creates a GL 3.3 core context and owns swap interval.
- `BackendConfig.h` already maps a `GraphicsAPI` variable, and the console
  already treats `graphicsapi` as restart-required; nothing consumed them.
- Shaders are GLSL 330 core with loose uniforms, implicit varying locations,
  `gl_FragCoord`, `texelFetch`, depth textures sampled as `sampler2D`, and
  fragment outputs at locations 0 and 1 (colour + motion velocity).
- Production path is the WASM runtime: guests record tokens, the host
  `WasmFrameRenderer` replays them (texture writes interleaved between
  batches), `WasmPanelWindows` renders detached panels offscreen and reads
  them back asynchronously, `--capture` reads the backbuffer.
- Machine: Windows 11, NVIDIA RTX 4080, driver loader `vulkan-1.dll`
  present, no Vulkan SDK (no validation layers).

## 3. Scope and non-goals

In scope: the backend, its selection/fallback, window changes, the
dependencies it needs, tests, docs, product settings rows.

Non-goals: changing the token contract or shader language of the frontend
(D-R9 string uniforms stay), a render graph, compute, multi-threaded
recording, Linux verification (sources stay portable; Linux remains
unverified per repository policy), removing OpenGL.

## 4. Constraints and invariants

- No Vulkan types or headers outside `Source/Rendering/Vulkan/` and the
  composition files; `Renderer` keeps depending on `IBackend` only.
- No exceptions (D-F3), no `auto`, no namespaces, no recursion in first-party
  code; clang-format + clang-tidy clean.
- Token payload pointers are consumed during `SubmitCommandQueue`; the
  backend copies anything it needs later (staging) before returning.
- Main-thread affinity as today.
- OpenGL behaviour and tests are unchanged.

## 5. Design

### 5.1 Selection and composition

`Rendering/BackendConfig.h` (source-private) parses `GraphicsAPI`
case-insensitively (`parseBackendDef`, `isBackendImplemented`). `Illumo::initialize` creates the window for the chosen API
(`RenderWindow` gains an API: OpenGL creates the context as before; Vulkan
uses `GLFW_NO_API`, no swap interval, `swapBuffers` only syncs the vsync
preference) and the matching backend. If the Vulkan window or backend fails,
it logs why, releases both and repeats with OpenGL. `IRenderWindow` gains
`graphicsApiName()` so hosts and settings can report the running backend.

### 5.2 GL semantics on Vulkan (the parity contract)

| GL behaviour | Vulkan realisation |
|---|---|
| Default framebuffer, bottom-left origin | Offscreen "backbuffer" image (MSAA per `msaa`, resolved) in GL memory layout; present blits it to the swapchain flipped vertically. All targets use an unflipped viewport, so pixel placement, `gl_FragCoord`, viewport and scissor numbers match GL. Front face is inverted (`CCW` -> `VK_FRONT_FACE_CLOCKWISE`). |
| Clip z in [-w, w] | Vertex shaders are wrapped: user `main` runs, then `gl_Position.z = (z + w) / 2`; `gl_PointSize = 1`. |
| GLSL 330, loose uniforms, name-matched varyings | glslang (Vulkan 1.3 client, relaxed Vulkan rules): `#version` becomes 450, loose uniforms go to a default uniform block, samplers/blocks are auto-bound, varyings are matched by name at link (`mapIO`). Reflection gives offsets/bindings. |
| Program-owned uniform values, sampler units | Each program keeps a CPU copy of its default block (zero-initialised) and sampler unit table; draws upload changed blocks to a per-submission ring and push descriptors. |
| Texture units, incomplete textures | 32 persistent units; missing or mismatched units bind a 1x1 (0,0,0,1) texture of the right type. |
| `glBindBufferBase` uniform blocks | 36 persistent binding points; named blocks read their GL binding point (0 unless declared). |
| VAO + disabled attributes | Vertex input from mesh layout, the mesh's instance stream (locations 4-12) and a stride-0 (0,0,0,1) default binding for inputs nothing supplies. |
| State machine persisting across submits | Device-level state persists; dynamic state is re-applied per command buffer. |
| Updates ordered with draws | Transfers go to the submission's upload command buffer while the resource has not been used in it, otherwise inline after ending dynamic rendering. |
| `glClear` obeys scissor, MRT clears | `vkCmdClearAttachments` with the scissor rect; attachment 0 gets the colour, others zero. |
| Formats | RGB8 stored as RGBA8 with alpha forced to one by the view; R8 sampled textures swizzle `rrr1`; depth 24 picks X8_D24 or D32; readbacks follow GL row order and alpha rules. |
| Error reporting | Same `reportFrameError` strings as `GLDevice`. |

### 5.3 Structure

As built: `VulkanContext` (instance, surface, device selection, queue, VMA,
optional line-rasterization and sample-location features, format mapping),
`VulkanShaderCompiler` (glslang wrapper with reflection), `VulkanTexels`
(CPU texel and readback conversions), `VulkanDevice` split over
`VulkanDevice.cpp` (submissions, staging, deferred destruction),
`VulkanDeviceResources.cpp` (meshes, programs, textures, framebuffers,
buffers), `VulkanDeviceCommands.cpp` (the token executor, pipelines, draws)
and `VulkanDevicePresent.cpp` (backbuffer, swapchain, presentation,
readbacks), `VulkanBackend` (the `IBackend` facade) and
`CreateVulkanBackend`.

Submission model: a recording is an upload and a main command buffer; it is
submitted at `EndFrame` (with present) or at a flush (synchronous readback,
blocking readback wait, shutdown). Three submission slots (fence, staging
ring, deferred deletions). Required device features: Vulkan 1.3 dynamic
rendering, synchronization2, extended dynamic state; `VK_KHR_push_descriptor`;
`fillModeNonSolid` for wireframe when available.

### 5.4 Alternatives considered

- Rewriting the frontend's shaders to Vulkan GLSL: rejected, leaks the
  backend into the frontend and guests.
- Negative-height viewport per swapchain pass: rejected, makes offscreen and
  onscreen conventions differ (`gl_FragCoord`, scissor, readback).
- Versioned host-visible copies for every update: rejected for textures and
  meshes (unbounded copies); ordered transfers match GL exactly.
- Descriptor pools instead of push descriptors: more code, no benefit on the
  supported desktop drivers.

## 6. Dependencies (assessment)

| Component | Version | License | Use |
|---|---|---|---|
| Vulkan-Headers | SDK 1.4.363.0 | Apache-2.0 OR MIT | C headers only |
| volk | SDK 1.4.363.0 | MIT | Loads `vulkan-1.dll` at run time; no SDK needed to build or ship |
| glslang | 16.6.0 (SDK 1.4.363.0) | BSD-3-Clause (+ BSD-2, MIT, Apache-2.0, GPL-3 with Bison exception for the generated parser) | Run-time GLSL to SPIR-V; HLSL front end and optimizer excluded |
| Vulkan Memory Allocator | 3.4.0 | MIT | Device memory sub-allocation |

Maintenance: all are Khronos/AMD maintained and version-locked to one SDK
release; update together. Build: glslang compiles into one static library with
the workspace's no-exceptions flags. Deployment: nothing new ships; the driver
provides `vulkan-1.dll`. Notices go to `THIRD_PARTY_NOTICES.md` and staged
`licenses/`.

## 7. Milestones

- M0 plan, vendoring, CMake (library builds).
- M1 selection, window API, fallback, clear + present.
- M2 resources and uploads.
- M3 shaders, uniforms, pipelines.
- M4 token executor complete, readbacks, MSAA, present modes.
- M5 parity tests (GL vs Vulkan images), package captures, fixes.
- M6 settings rows (display wire v5), command line, console.
- M7 documentation and decision log.
- M8 full Release build, labelled CTest, tidy, format.

## 8. Verification strategy

Headless unit tests for the backend-independent pieces (shader rewriting,
uniform packing, format conversion, row order, API parsing). Real-GPU:
`IllumoGpuTests` gains `--api vulkan` for the existing cases and a
`backendparity` case comparing GL and Vulkan images across a scene suite.
Manual: every package launched with Vulkan, `--capture` compared with OpenGL.
No validation layers are installed; if they can be built locally they are
used during development only.

## 9. Rollback

The OpenGL path is unchanged and remains the default; `GraphicsAPI=OPENGL`
or a failed Vulkan start returns to it.

## 10. Validation log

- 2026-09-30, M0: survey complete; pinned sources fetched outside the
  repository. Vendoring was held for the owner's dependency approval.
- 2026-09-30, M0 (continued): the owner approved all four dependencies and
  vendored them (`Illumo/thirdparty/{vulkan-headers-1.4.363,volk-1.4.363,
  vma-3.4.0,glslang-16.6.0}`, glslang reduced to its GLSL front end and SPIR-V
  generator); `cmake/IllumoVulkanDeps.cmake` builds them without exceptions.
- M1-M4: selection, `GLFW_NO_API` windows, fallback, and the full token
  executor. Parity fixes found by captures: line rasterization (Bresenham
  aliased, rectangular multisampled), mirrored standard sample locations and
  a gamma-correct resolve to match the NVIDIA OpenGL driver.
- M5: `Illumo.Gpu.BackendParity` (seven scenes: world with RenderWorld,
  shadows and sky; 2D through `renderOffscreen`; raw tokens; texture formats
  and in-frame updates; render-target readback conversions; the detached-panel
  readback loop; the default framebuffer with motion blur) matches within one
  pixel. It found and fixed two gaps: non-blocking framebuffer readbacks never
  completed until a later frame (now a poll submits the copy, as OpenGL's
  flushing fence poll does), and presentation stalled forever on a locked
  session (acquire waits are bounded, an image the compositor holds stays
  pending, and presents pause while `PlatformSessionLocked()` is true because
  the driver's present call itself stalls there; OpenGL keeps pacing, so the
  loop sleeps one refresh per frame when vsynced). `Illumo.Gpu.Vulkan*Parity`
  run the instancing and RenderWorld cases on Vulkan;
  `IllumoCaptureGpuTests --api vulkan` runs the neutral `FrameCapture` checks
  and matches OpenGL byte for byte. Every package captures and benchmarks on
  both backends; a 1,500-frame run through fullscreen toggles, resizes,
  minimize, maximize and restore holds 60 FPS on Vulkan.
- M6: `--graphics-api`, the console, and CSim's Video tab Renderer row over
  Display wire version 5 (saved and running backend; a guest sends a choice
  only after the host reported its own, so a stale local value never
  overrides the command line). Tests: `Illumo.Host.GraphicsApiSelection`,
  `Illumo.SysCmdLine.GraphicsApiOption`, `Illumo.BackendConfig.*`,
  `Illumo.Capture.Validation`, `Illumo.Wasm.DisplayServices`,
  `Illumo.Wasm.SdkContract` (`displayBackendContract`), the CSim settings and
  title-scene tests.
- M7: D-R33 in `docs/architecture-consensus.md` and the LaTeX decision log,
  the rendering chapter, AGENTS files, README, frame-capture and game package
  docs, third-party notices and staged licenses.
- M8: the full workspace Release build with clang-tidy is clean (the vendored
  dependencies are not linted); `IllumoWorkspace` and `IllumoGpu` CTest labels
  pass (709 tests); `IllumoCaptureGpuTests` passes on both APIs;
  `CheckNoExceptions` passes; changed C++ is clang-formatted.
- Measurements (RTX 4080, 60 Hz): all packages hold 60 FPS on both backends;
  CPU record time per frame is higher on Vulkan (median about 0.13 against
  0.04 ms, where the OpenGL driver defers work to its own thread).
- Not verified: Linux, non-NVIDIA drivers, validation layers (not installed).