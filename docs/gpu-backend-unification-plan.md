# Shared GPU backend plan

Status: M1-M2 implemented (D-R40) on branch `ccr-9a9151a6-vex779`
(2026-10-26), verified on Linux; the Windows run (section 9) is pending.
M3 (OpenGL) is a proposal, not authorized.

## 1. Objective

One `IBackend` implementation and one token interpreter for the explicit
graphics APIs, with each API reduced to the code that calls it. The owner's
request (2026-10-26): "unify the renderer backends into one class and then
take the [parts] that would actually be different (Vulkan, DirectX, etc.)
and have the unified renderer backend work with them to communicate with the
graphics APIs ... so we can reduce repeated logic."

End state of M1-M2:

- `Rendering/Gpu/GpuBackend` is the only `IBackend` for Vulkan and Direct3D
  12. `VulkanBackend` and `D3D12Backend` no longer exist.
- `Rendering/Gpu/GpuDriver` holds, once, what both drivers duplicated: the
  OpenGL context state, the token interpreter, handle registries and their
  validation, the create/replace/destroy front ends, frame errors and
  statistics.
- `VulkanDriver` and `D3D12Driver` derive from `GpuDriver` and implement only
  API hooks: build and release resources, clear, record a validated draw,
  upload, present, read back, submit.
- No observable change: same pixels, error text, state persistence and
  handle semantics. Profiler zone names in the moved code change from
  `VulkanDriver.*`/`D3D12Driver.*` to `GpuDriver.*`/`GpuBackend.*`.

Names: "driver" means the engine class that talks to one graphics API, not
the GPU vendor's driver. M1 and M2 (e2829f0, 49d0de0) first named these
classes devices (`GpuDevice`, `VulkanDevice`, `D3D12Device`); 9f1d853
renamed them drivers at the owner's request. Section 2 uses the file names
of the tree it measured.

## 2. Current-state evidence (release/v26.10 at a76bb68)

Normalized for API names and whitespace, the Vulkan and Direct3D 12 sources
share these lines verbatim (blocks of six or more lines):

| File pair | Vulkan lines | D3D12 lines | Shared |
|---|---|---|---|
| `*Backend.cpp` | 485 | 485 | 475 |
| `*Backend.h` | 134 | 134 | 132 |
| `*DeviceCommands.cpp` | 1,239 | 1,188 | 519 |
| `*DeviceResources.cpp` | 916 | 971 | 651 |
| `*Device.h` | 670 | 653 | 350 |
| `*DevicePresent.cpp` | 769 | 667 | 178 |
| `*Device.cpp` | 1,050 | 855 | 116 |

About 2,400 of each backend's roughly 6,000 lines are copies. A token
semantics fix has to be made twice today, and the D3D12 plan (section 5.1)
shared only the shader front end, uniform state and texel conversion.

OpenGL is structured differently (`GLBackend` owns the registries and
resource front ends; `GLDevice::executeCommand` is its interpreter, driving
OpenGL state directly). Its overlap with the others is semantic, not textual.

## 3. Scope and non-goals

In scope (M1-M2): the Vulkan and Direct3D 12 backends and their factories,
CMake source lists, guidance and documentation.

Not in scope:

- OpenGL (M3, below; needs the owner's go-ahead).
- Submission, staging, deferred destruction, presentation and readback.
  They differ per API in substance (fences, VMA, descriptor heaps, layout
  transitions); sharing them would mean hooks for nearly every line.
- `IBackend`, `Renderer`, tokens, public headers, guests, products. Nothing
  outside `Rendering/{Gpu,Vulkan,D3D12}` changes except CMake.

## 4. Constraints and invariants

- The OpenGL-observable contract of `docs/vulkan-backend-plan.md` 5.2 and
  `docs/d3d12-backend-plan.md` 5.3: pixels, error strings, state persistence,
  handle generations, unit-0 bindings after uploads and creation.
- No exceptions, `auto`, namespaces or recursion; changed lines formatted.
- Main-thread affinity is unchanged; nothing new is shared across threads.
- `Gpu/` stays free of Vulkan and Direct3D types.

## 5. Design

```text
IBackend
  GpuBackend            queue, FPS, shader preprocessing, readback guards
    owns GpuDriver      OpenGL context state, token interpreter, handle
                        registries, validation, front ends (abstract)
      VulkanDriver      API hooks: build/release, clear, draw, upload,
      D3D12Driver       frames, present, readback
```

- `GpuDriver` keeps API resources through base records: `GpuMesh`,
  `GpuProgram`, `GpuTexture`, `GpuBuffer` (polymorphic, non-copyable) and the
  concrete `GpuFramebuffer`. Each API type derives from its record and adds
  its handles (`VulkanMesh : GpuMesh` adds its two buffers). The base sets
  `GpuTexture::width`/`height` itself after a successful build.
- Each driver resolves its own types with a `static_cast` in typed helpers
  (`VulkanDriver::resolveMesh`); a driver only ever registers its own types.
- Hooks are protected pure virtual functions. The base calls them only after
  it has validated the token or call, so every API sees the same checks in
  the same order. A virtual call per draw or update is a few nanoseconds;
  the shipped apps issue tens to hundreds of draws per frame.
- Profiling plot names stay per API (`VK.*`, `DX12.*`): `plotFrameStats` is a
  hook because Tracy names must be literals.

Alternatives considered:

- A class template over the API's resource types (no casts, no virtual
  calls). Rejected: about 1,500 lines of template code in headers for two
  instantiations, harder errors, and nothing measurable to gain.
- One class (`GpuBackend` holding the interpreter, APIs deriving from it).
  Rejected for now: it merges two layers the existing docs describe
  separately and makes the largest diff in code that can only be compiled,
  not run, outside Windows.
- Moving OpenGL in the same change. Rejected: OpenGL is the reference the
  parity tests compare against. Moving the reference and the candidates in
  one step would hide a shared regression.

## 6. Contracts and compatibility

No public or ABI change. The factories keep their signatures and return a
`GpuBackend`. Log text that named the facade class (`VulkanBackend: Vertex
preprocessor failed`) now names the API (`Vulkan backend: Vertex
preprocessor failed`); no test or tool matches it.

## 7. Ownership, lifetime, threading, errors, platform

- `GpuBackend` owns the driver; the driver owns every resource as before.
- Destruction order: each driver's destructor calls its own `shutdown`,
  which calls `GpuDriver::releaseResources` where it released the tables
  before, so API objects are retired while the API device is alive.
- The base never calls a hook from its constructor or destructor.
- Direct3D 12 stays Windows-only; `Gpu/` builds everywhere.

## 8. Milestones

- M1: `GpuBackend` replaces both facades; `GpuDriver` is the driver's
  abstract public interface and holds the frame error, statistics and shader
  cache.
- M2: the context state, interpreter, registries and front ends move into
  `GpuDriver`; drivers keep only hooks.
- M3 (proposal): OpenGL as a third `GpuDriver`. `GLBackend`'s registries and
  front ends and `GLDevice::executeCommand` would go; the GL driver would set
  OpenGL state at draw time from the shared context state, keeping its bind
  cache. Before starting: extend `Illumo.GpuDriver.*` (added in M2) until
  the shared semantics have a reference that is not one of the backends
  under test, since the parity tests would then compare three users of one
  interpreter.

## 9. Verification

On Linux (this container; Ubuntu 24.04, GCC 13.3, Mesa 25.2.8):

- Release build of the workspace with `-DILLUMO_BUILD_WASM_RUNTIME=OFF`
  and the `IllumoWorkspace` label (CSim not cloned).
- `IllumoGpuTests` under Xvfb on Mesa llvmpipe (OpenGL) and lavapipe
  (Vulkan): `backendparity`, `instancing`, `renderworld`, both APIs.
- Byte comparison of every `backendparity --dump` image against the same
  dump from the unchanged tree. Two unchanged runs were byte-identical, so
  any difference is a regression.
- Direct3D 12 compiled with MinGW-w64 g++ 13 (`-fsyntax-only -Wall
  -Wextra`) against Microsoft's DirectX-Headers, compared with the same
  check of the unchanged tree (whose only errors are MinGW header ABI
  differences in `D3D12Driver.cpp` and `D3D12ShaderCompiler.cpp`).
- clang-tidy with the repository configuration on the changed sources.

On Windows (required before merging, not available here): Release build,
full `IllumoWorkspace` with CSim, `Illumo.Gpu.BackendParity`,
`Illumo.Gpu.D3D12*Parity`, `IllumoCaptureGpuTests --api d3d12`, and an
`ILLUMO_D3D12_DEBUG=1` run of each package with resize, minimize and
maximize.

## 10. Rollback

M1 and M2 are separate commits on one branch; reverting M2 restores the
per-API interpreter, reverting both restores the facades.

## 11. Open decisions

- M3: move OpenGL onto `GpuDriver`, or keep it as the independent reference.

## 12. Validation log

Environment: Ubuntu 24.04 container, GCC 13.3, CMake 3.28, Ninja, Release,
`-DILLUMO_BUILD_WASM_RUNTIME=OFF`, CSim not cloned; Xvfb with Mesa 25.2.8
llvmpipe (OpenGL 4.5) and lavapipe (Vulkan 1.4); MinGW-w64 g++ 13.2 with
Microsoft DirectX-Headers (main, fetched for the check only, not vendored).
Baseline: the unchanged tree a76bb68 built the same way.

- Baseline: `IllumoGpuTests backendparity` (0 of 7 differ), `instancing`
  and `renderworld` on OpenGL and Vulkan pass; two `--dump` runs are
  byte-identical, so the software rasterizers are deterministic.
- M1 (e2829f0): the same GPU tests pass and all 14 dumped images (7 scenes,
  OpenGL and Vulkan) are byte-identical to the baseline's. The Direct3D 12
  syntax check reports exactly the baseline's diagnostics (MinGW header
  differences: `GetCPU/GPUDescriptorHandleForHeapStart` in
  `D3D12Driver.cpp`, a duplicate `d3d12shader.h` in
  `D3D12ShaderCompiler.cpp`); every other D3D12 file is clean with `-Wall
  -Wextra`.
- M2 (49d0de0): the same GPU results, all 14 images byte-identical to the
  baseline's. Full build and `ctest -L IllumoWorkspace`: 452/452 pass
  (before the two new cases). `Illumo.GpuDriver.Resources` (22 checks) and
  `Illumo.GpuDriver.TokenValidation` (17 checks) pass; removing the
  per-submission program reset or the index-range check makes three checks
  fail. The Direct3D 12 syntax check first caught a helper
  (`compileProgramCode`) the move had dropped; after restoring it the
  diagnostics equal the baseline's. clang-tidy 18 with the repository
  configuration is clean on every changed file that builds on Linux, apart
  from one finding the baseline has too
  (`bugprone-suspicious-memory-comparison` on `VkViewport` in the Vulkan
  draw code, unchanged by this work).
- Size: Vulkan sources 6,000 to 4,350 lines, Direct3D 12 5,774 to 4,128,
  shared layer (`GpuBackend`, `GpuDriver`) 2,049; production code net
  -1,247 lines. Verbatim overlap between the two driver implementations
  fell from about 2,400 to about 700 lines, nearly all in presentation,
  readback and submission (out of scope, section 3).

Deviations from the design:

- Mesh enrollment moved into the base too (validation, record fields,
  uploads, GLMesh's byte-index widening); `buildMesh` only creates the
  buffers. Both drivers had the same code apart from the buffer calls.
- The headless `Illumo.GpuDriver.*` tests were added in M2 instead of
  waiting for M3.
- Two checks in the moved code became capacity checks: "the mesh has no
  index buffer" became "its index capacity (or count) is zero". A mesh has
  an index buffer exactly when its index capacity is not zero, so both
  refuse the same updates and draws.

Not verified here: anything on Windows, Direct3D 12 at run time, NVIDIA or
other hardware drivers, CSim's tests, the WASM packages and captures.
