# Direct3D 12 backend guidance

This file specializes `Illumo/Source/Rendering/AGENTS.md` for
`Illumo/Source/Rendering/D3D12/`.

## Status

A complete `IBackend` on Direct3D 12 (D-R36), Windows only, selected by the
`GraphicsAPI` setting (`DIRECTX12`, or `d3d12`), `--graphics-api d3d12`, or
`set GraphicsAPI d3d12` (restart). OpenGL stays the default; a Direct3D 12
start that fails falls back to OpenGL. Verified on Windows 11 with an NVIDIA
driver. The plan, parity contract and validation log are in
`docs/d3d12-backend-plan.md`.

## Scope and boundaries

This directory is the only place Direct3D, DXGI and `<windows.h>` appear for
rendering. The composition files (`Engine/Illumo.cpp`,
`Rendering/FrameCapture.cpp`, `Rendering/RenderWindow.cpp` for
`GLFW_NO_API`) name it through `CreateD3D12Backend.h`, which exposes no
Direct3D types. `Renderer`, drawables, public headers, guests and products
never see it.

- `D3D12Common`: the Windows headers and `D3D12Ref`, the COM reference
  holder (no WRL; no namespaces in first-party code).
- `D3D12Context`: factory, adapter, device, queue, the debug layer
  (`ILLUMO_D3D12_DEBUG=1`), features and sample counts.
- `D3D12ShaderCompiler`: the shared SPIR-V (`Rendering/Gpu/GlslToSpirv`)
  to HLSL 5.1 through SPIRV-Cross, DXBC through the system FXC, input
  signatures through `D3DReflect`, root signatures from the reflection.
- `D3D12Device*`: the token executor, resources, submissions, presentation.
- `D3D12Backend`: the `IBackend` facade (queue, statistics, validation text).
- Dependency: `thirdparty/spirv-cross-1.4.363` built by
  `cmake/IllumoD3D12Deps.cmake`; Windows supplies `d3d12.dll`, `dxgi.dll` and
  `d3dcompiler_47.dll`.

## Required invariants

- Observable behaviour is the OpenGL backend's: same pixels (within MSAA edge
  noise), row order, readback conversions, error strings and state-machine
  persistence. Section 5.3 of the plan is the contract; change all backends
  together or none.
- Every image, the stand-in default framebuffer included, is stored bottom
  row first: vertex shaders negate clip y (SPIRV-Cross `flip_vert_y`) and
  OpenGL's counter-clockwise front faces are clockwise here. Presentation
  draws the flip; never flip per pass.
- Shaders stay the frontend's GLSL 330 text, compiled to SPIR-V by the shared
  front end; HLSL is only ever generated here. Each binding keeps its number
  as its register, and the pixel stage declares every vertex output it does
  not read so the signatures link register for register.
- Input layouts follow the DXBC input signature, never OpenGL reflection
  (which drops inputs it calls inactive).
- Updates are ordered with draws through explicit resource-state transitions
  in the one command list; resource destruction waits for the submissions
  that used it. The frame fence is signalled after `Present`, so a serial
  also covers the present's use of the swapchain buffer; `ResizeBuffers`
  depends on that.
- While the session is locked presents are skipped (`PlatformSessionLocked`),
  as on Vulkan; frames keep rendering and readbacks keep working.
- SPIRV-Cross errors end the process through `IllumoSpirvCrossFatal`; only
  first-party engine shaders reach it.
- Programs and their DXBC are cached through `GpuShaderCache` (D-R38); change
  the `d3d12-dxbc-N` key when HLSL generation or compile flags change.
- No exceptions, `auto`, namespaces or recursion, as everywhere.

## Verification

- Headless: `Illumo.D3D12.*` in `IllumoTests` (program translation and every
  engine shader file through HLSL and DXBC), plus the selection tests.
- Real GPU (`IllumoGpu` label): `Illumo.Gpu.BackendParity` compares every
  scene on Vulkan and Direct3D 12 with OpenGL (`--dump <dir>` writes the
  images); `Illumo.Gpu.D3D12*Parity` run the instancing and RenderWorld cases;
  `IllumoCaptureGpuTests --api d3d12` runs the neutral capture checks.
- Live: `IllumoRuntime --app <id> --graphics-api d3d12 --capture <png>` and
  `--bench-frames` for every package, compared with OpenGL; run with
  `ILLUMO_D3D12_DEBUG=1` after changes and keep the debug layer silent.
  Resize, minimize and maximize the window during such a run: the layer
  turns a use-after-release into a silent exit (code 2173), and its message
  reaches only a debugger (`cdb`), not the log.
