# Direct3D 12 backend execution plan

Status: complete, M0-M8 (D-R36); see the validation log in section 10.
Tier 2 plan (`.agent/PLANS.md`: a new backend).
Owner request (2026-10-01): "let's see if we can get DirectX 12 support on in
just like we did with Vulkan" — the Vulkan contract of D-R33 applies: a backend
to switch to and from, no backend logic in the renderer frontend, and the
OpenGL backend's feature set and observable behaviour.

## 1. Objective and measurable end state

- `Illumo/Source/Rendering/D3D12/` implements `IBackend` completely, with the
  OpenGL backend's observable results (pixels, row order, error text,
  validation outcomes, readback conversions), exactly as the Vulkan backend
  does (`docs/vulkan-backend-plan.md`, section 5.2).
- `GraphicsAPI=DIRECTX12` (`--graphics-api d3d12` or `directx12`, the console,
  CSim's Video tab) selects it on Windows; a start that fails falls back to
  OpenGL. Other platforms keep parsing the name and fall back.
- Nothing Direct3D-specific enters `Renderer`, drawables, `Illumo/Include`,
  guests or products. The frontend keeps its tokens and GLSL 330 text.
- Parity is shown by real-GPU image comparison against OpenGL
  (`Illumo.Gpu.BackendParity` gains Direct3D 12), the existing GPU cases run on
  Direct3D 12, `IllumoCaptureGpuTests --api d3d12`, and captures of every
  package.

## 2. Current-state evidence

- The Vulkan backend (D-R33) already solved the semantic gap between GL's
  state machine and an explicit API; its structure and contract are the
  template. Its GLSL front end (glslang to SPIR-V with OpenGL program
  reflection) and CPU texel conversions use no Vulkan types.
- `BackendConfig.h` already parses `DIRECTX12`; `isBackendImplemented` says
  no. `RenderWindow` creates `GLFW_NO_API` windows for Vulkan.
- The Display wire (version 5) codes backends as 1 OpenGL, 2 Vulkan and
  rejects larger codes.
- Machine: Windows 11 (10.0.26200), NVIDIA RTX 4080, Windows SDK
  10.0.28000 (`d3d12.h`, `dxgi1_6.h`, `d3dcompiler.h`), the system
  `d3dcompiler_47.dll` (FXC), the D3D12 debug layer installed.

## 3. Scope and non-goals

In scope: the backend, its selection and fallback, the window, the shared GLSL
front end and texel code moved out of `Vulkan/`, the SPIRV-Cross dependency,
tests, documentation and the settings rows.

Non-goals: DXIL/DXC, Direct3D 11, the Agility SDK, changing tokens or shader
language, multi-threaded recording, non-Windows support.

## 4. Constraints and invariants

- Direct3D and DXGI types and `<windows.h>` stay inside
  `Source/Rendering/D3D12/`; composition files see only `CreateD3D12Backend.h`.
- No exceptions (D-F3), no `auto`, namespaces or recursion in first-party
  code (COM references use a small first-party holder, not WRL); clang-format
  and clang-tidy clean.
- Token payloads are consumed during submission; uploads copy them.
- OpenGL and Vulkan behaviour and tests stay unchanged.

## 5. Design

### 5.1 Shared pieces

`Source/Rendering/Gpu/` holds what both explicit backends use:
`GlslToSpirv` (was `VulkanShaderCompiler`: GLSL 330 to SPIR-V with OpenGL
program reflection, vertex clip depth remapped to [0, w]) and `GpuTexels` (was
`VulkanTexels`). Direct3D 12 shares Vulkan's [0, w] clip depth, so both
backends compile the same SPIR-V.

### 5.2 Shaders

SPIR-V goes through SPIRV-Cross to HLSL shader model 5.1 with
`flip_vert_y` (images are stored bottom row first, see 5.3) and point size
ignored (always one), then through the system FXC (`D3DCompile`) to DXBC.
Every binding keeps its number as its register (`b`, `t`, `s`). The root
signature is built per program from the reflection: one root CBV per uniform
block, one one-descriptor table per sampled texture and per sampler, so a draw
points tables straight at persistent descriptors and copies none.

### 5.3 OpenGL semantics on Direct3D 12

| GL behaviour | Direct3D 12 realisation |
|---|---|
| Default framebuffer, bottom-left origin | An offscreen backbuffer stored bottom row first (vertex shaders negate clip y), so viewport, scissor, `gl_FragCoord` and readback numbers equal OpenGL's. Presentation draws it into the swapchain flipped. GL's counter-clockwise is front-face clockwise. |
| Clip z in [-w, w] | Remapped in the shared SPIR-V. |
| Program-owned uniform values | As Vulkan: a CPU default block per program, uploaded to a ring when changed. |
| Texture units, incomplete textures | 32 units; missing or mismatched units read a 1x1 (0, 0, 0, 1) texture. |
| Uniform block binding points | 36 points; blocks read binding point 0 (FrameUniforms) as on Vulkan. |
| Disabled attribute arrays | A stride-0 vertex buffer holding (0, 0, 0, 1). |
| Updates ordered with draws | Copies are recorded in the same command list between draws with state transitions. |
| `glClear` with scissor, MRT clears | Clear calls with the scissor rectangle; extra attachments get zero. |
| Formats | RGB8 stored RGBA8 with alpha forced to one by the SRV mapping; R8 sampled `rrr1`; depth is D24S8 (typeless, SRV R24). |
| Multisampled default framebuffer | Standard sample positions mirrored vertically where programmable positions exist; the resolve uses the sRGB format so it averages in linear light, as the NVIDIA OpenGL driver does. Lines are aliased (diamond exit) single-sampled and narrow quadrilaterals multisampled where supported. |
| `GL_FRONT_AND_BACK` culling | Triangle draws are skipped. |
| Mipmap generation | A CPU 2x2 box filter at creation. |
| Error reporting | The `GLDevice`/`VulkanDevice` strings. |

### 5.4 Structure

`D3D12Context` (factory, adapter, device, queue, debug layer, features,
formats, sample counts), `D3D12ShaderCompiler` (SPIRV-Cross and FXC, root
signatures), `D3D12Device*` (as `VulkanDevice*`: submissions and staging,
resources, the token executor, presentation and readbacks), `D3D12Backend`
(the `IBackend` facade) and `CreateD3D12Backend`.

Submission model: one command list per recording, submitted at `EndFrame`
(with present) or a flush; three submission slots with a fence value, an
upload ring and deferred releases. Descriptors: one shader-visible
CBV/SRV/UAV heap with a persistent SRV per texture, one shader-visible sampler
heap with every sampler state, and CPU heaps for RTVs and DSVs.

### 5.5 Alternatives considered

- DXC and DXIL: needs `dxcompiler.dll` and `dxil.dll` shipped; FXC ships with
  Windows and shader model 5.1 covers the frontend's GLSL 330.
- Writing GLSL-to-HLSL by hand: unbounded; SPIRV-Cross is the standard tool.
- Descriptor copies per draw: one-descriptor tables pointing at persistent
  descriptors avoid them.
- A negative viewport height instead of flipping clip y: changes scissor and
  `gl_FragCoord` conventions between targets.

## 6. Dependencies (assessment)

| Component | Version | License | Use |
|---|---|---|---|
| SPIRV-Cross | Vulkan SDK 1.4.363.0 tag | Apache-2.0 OR MIT | Run-time SPIR-V to HLSL |

Owner approval: 2026-10-01. Built from six sources (`spirv_cross`,
`spirv_parser`, `spirv_cross_parsed_ir`, `spirv_cfg`, `spirv_glsl`,
`spirv_hlsl`) without exceptions (`SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS`); its
errors end the process through a hook into `illumoFatal`, which only
first-party engine shaders can reach (guests cannot supply host shaders).
Maintenance: updated with the other Khronos components of one SDK release.
Deployment: nothing new ships; Direct3D 12, DXGI and `d3dcompiler_47.dll` are
part of Windows.

## 7. Milestones

- M0 plan, vendoring, shared `Gpu/` move, CMake.
- M1 selection, window, fallback, clear and present.
- M2 resources and uploads.
- M3 shaders, root signatures, pipelines.
- M4 token executor, readbacks, MSAA, presentation modes.
- M5 parity tests and package captures.
- M6 settings rows (Display wire version 6), command line, console.
- M7 documentation and decision log.
- M8 full Release build, labelled CTest, tidy, format.

## 8. Verification strategy

Headless: HLSL translation and FXC compilation of every engine shader;
backend selection. Real GPU: `IllumoGpuTests --api d3d12` cases and
`backendparity`; `IllumoCaptureGpuTests --api d3d12`; every package captured
and benchmarked; the debug layer (`ILLUMO_D3D12_DEBUG=1`) clean during
development.

## 9. Rollback

OpenGL stays the default; `GraphicsAPI=OPENGL` or a failed start returns to
it. The Vulkan backend is unaffected.

## 10. Validation log

- 2026-10-01, M0: survey done; SPIRV-Cross approved by the owner and vendored
  (`Illumo/thirdparty/spirv-cross-1.4.363`: the six sources of the parser and
  GLSL/HLSL back ends, their headers, license and readme; one patch in
  `spirv_cross_error_handling.hpp` routes errors to the embedding program).
  `VulkanShaderCompiler` and `VulkanTexels` moved to `Rendering/Gpu/` as
  `GlslToSpirv` and `GpuTexels`; Vulkan's program uniform state and attribute
  table moved beside them as `GpuProgramState`, which both backends use.
- M1-M4: selection (`DIRECTX12`/`d3d12`), `GLFW_NO_API` windows, fallback,
  the device, resources, shaders and presentation. Every engine shader file
  translated and compiled to DXBC before the device existed
  (`Illumo.D3D12.EngineShadersBuild`). Two linkage gaps the first package runs
  found: OpenGL reflection omits vertex inputs it calls inactive while the
  translated HLSL still declares them (input layouts now come from the DXBC
  signature through `D3DReflect`), and Direct3D links stages by signature
  register, so a pixel stage reading fewer varyings than the vertex stage
  writes misplaced `SV_Position` (SPIRV-Cross's output masking is not
  implemented for HLSL; the pixel input struct now declares the unread vertex
  outputs). The debug layer reports nothing else; its clear-value and
  unbound-output notes, both OpenGL semantics, are filtered.
- M5: `Illumo.Gpu.BackendParity` compares Vulkan and Direct3D 12 with OpenGL:
  all seven scenes match within the tolerance, Direct3D 12 as closely as
  Vulkan (the world scene differs in one pixel on both). The instancing and
  RenderWorld cases pass on Direct3D 12 (`Illumo.Gpu.D3D12*Parity`), and
  `IllumoCaptureGpuTests --api d3d12` matches OpenGL byte for byte. Package
  captures at 1600x900 with 4x MSAA: the mesh viewer matches OpenGL exactly,
  IllEd differs in the same 54 pixels Vulkan does, and the animated CSim title
  matches apart from its moving background.
- M6: `--graphics-api d3d12`, the console, and CSim's Video tab Renderer row
  (OpenGL, Vulkan, DirectX 12) over Display wire version 6, which adds backend
  code 3; version 5 reports name it unknown and refuse it in requests. Tests:
  `Illumo.Host.GraphicsApiSelection`, `Illumo.BackendConfig.*`,
  `Illumo.Capture.Validation`, `Illumo.Wasm.DisplayServices`,
  `Illumo.Wasm.SdkContract`, the CSim settings and title-scene tests.
- M7: D-R36 in `docs/architecture-consensus.md` and the LaTeX decision log,
  the rendering chapter, AGENTS files, README, frame-capture and game package
  docs, third-party notices and the staged SPIRV-Cross license.
- M8, live presentation: vsync holds the 16.667 ms interval with the debug
  layer on and silent. Resizing, minimizing and maximizing IllEd and CSim
  first ended the process under the debug layer (OBJECT_DELETED_WHILE_STILL_IN_USE
  from `ResizeBuffers`): the frame fence was signalled before `Present`, so
  waiting for the last serial did not cover the queued present's use of the
  swapchain buffer. The signal now follows the present, and both packages
  survive the same sequence with the layer on.
- M8, uncapped frame interval p50 at 1600x900, 4x MSAA (OpenGL / Vulkan /
  Direct3D 12): CSim 0.366 / 0.450 / 0.476 ms, IllEd 0.202 / 0.244 /
  0.303 ms, mesh viewer 0.199 / 0.247 / 0.294 ms. Render time matches Vulkan;
  the difference is the windowed flip-model present (0.18 ms p50, against
  0.13 on Vulkan).
- Not verified: non-NVIDIA drivers.
