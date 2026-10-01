# Vulkan backend guidance

This file specializes `Illumo/Source/Rendering/AGENTS.md` for
`Illumo/Source/Rendering/Vulkan/`.

## Status

A complete `IBackend` on Vulkan 1.3 (D-R33), selected by the `GraphicsAPI`
setting, `--graphics-api`, or `set GraphicsAPI vulkan` (restart). OpenGL stays
the default; a Vulkan start that fails falls back to OpenGL. Verified on
Windows with an NVIDIA driver; Linux sources compile but are unverified. The
plan, parity contract and validation log are in `docs/vulkan-backend-plan.md`.

## Scope and boundaries

This directory is the only place Vulkan types, headers and calls exist. The
composition files (`Engine/Illumo.cpp`, `Rendering/FrameCapture.cpp`,
`Rendering/RenderWindow.cpp` for `GLFW_NO_API`) name it through
`CreateVulkanBackend.h`, which exposes no Vulkan types. `Renderer`, drawables,
public headers, guests and products never see it.

- `VulkanContext`: instance, surface, device selection, queue, VMA, optional
  features (line rasterization, sample locations), format mapping.
- Shared with Direct3D 12 in `Rendering/Gpu/`: `GlslToSpirv` (glslang, GLSL
  330 to SPIR-V with reflection), `GpuProgramState` (OpenGL program uniform
  state, attribute layouts) and `GpuTexels` (CPU texel and readback
  conversions), all headless-tested.
- `VulkanDevice*`: the token executor, resources, submissions, presentation.
- `VulkanBackend`: the `IBackend` facade (queue, statistics, validation text).
- Dependencies: `thirdparty/{vulkan-headers,volk,vma,glslang}` built by
  `cmake/IllumoVulkanDeps.cmake`; the driver supplies `vulkan-1.dll`.

## Required invariants

- Observable behaviour is the OpenGL backend's: same pixels (within MSAA edge
  noise), row order, readback conversions, error strings and state-machine
  persistence. Section 5.2 of the plan is the contract; change both backends
  together or neither.
- Every image, including the stand-in default framebuffer, is stored bottom
  row first with an unflipped viewport; presentation flips. Front faces are
  inverted and vertex shaders remap clip z. Never flip per pass.
- Shaders stay the frontend's GLSL 330 text; rewriting happens here only
  (`prepareGlslForSpirv`). Loose uniforms live in a per-program default block
  whose values persist like OpenGL program uniforms.
- Updates are ordered with draws: an upload goes to the recording's upload
  command buffer until the resource is used in the main one, then inline.
- Resource destruction is deferred until the submissions using it completed.
- Every wait on presentation is bounded; while the session is locked presents
  are skipped (`PlatformSessionLocked`), because the driver's present can stall
  there. Frames keep rendering and readbacks keep working.
- No exceptions, `auto`, namespaces or recursion, as everywhere.

## Verification

- Headless: `Illumo.Vulkan.*` in `IllumoTests` (GLSL preparation, program
  semantics, engine shaders compile, texel conversion), plus the selection
  tests (`Illumo.Host.GraphicsApiSelection`, `Illumo.BackendConfig.*`,
  `Illumo.SysCmdLine.GraphicsApiOption`, `Illumo.Capture.Validation`).
- Real GPU (`IllumoGpu` label): `Illumo.Gpu.BackendParity` renders a scene
  suite through both backends in one process and compares pixels
  (`IllumoGpuTests backendparity --dump <dir>` writes the images);
  `Illumo.Gpu.Vulkan*Parity` run the instancing and RenderWorld cases on
  Vulkan. Run them for any change here or to a token's OpenGL behaviour.
- Live: `IllumoRuntime --app <id> --graphics-api vulkan --capture <png>` and
  `--bench-frames` for every package, compared with OpenGL.
- No validation layers are installed on the reference machine;
  `ILLUMO_VULKAN_VALIDATION=1` enables them where they exist.
