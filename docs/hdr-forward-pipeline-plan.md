# UBO HDR Forward Pipeline Execution Plan

**Status:** complete (`M0`–`M8` implemented and verified; `O1`–`O5` defaults adopted).  
**Tier:** Tier 2 execution plan (`.agent/PLANS.md`: subsystem-scale rendering, wire-schema, and content extension).  
**Target branch:** `hdr-forward-pipeline` off `release/v26.10`.  
**Proposed formal decisions:**

- **D-R41:** Linear HDR `RGBA16F` forward pass chain, named UBO binding table (`LightUniforms`), 4-quadrant shadow atlas (3-split CSM + 1 spot), physically based bloom pyramid, and AgX + FXAA composite.
- **D-E43:** Frame schema v10 punctual world lights and textured PBR-lite materials (`HostRender`), plus `.ilsc` format `2.1` minor extension.

## 1. Objective and Measurable End State

Upgrade Illumo's 3D world pipeline from single-light, untextured LDR Lambertian shading into a multi-light, linear HDR physically based forward pipeline—without raising the OpenGL 3.3 Core baseline, adding third-party dependencies, breaking `sizeof(RenderCommand) <= 72`, or regressing pure-2D cellular-automata performance.

When `M0`–`M8` are complete, the workspace must satisfy six measurable acceptance criteria:

1. **Multi-light PBR-lite shading:** `RenderWorld` and `MeshVisual` shade surfaces in linear space using Cook-Torrance GGX specular plus energy-conserving diffuse, optional `albedo`, `normal` (cotangent-frame), `metallicRoughness` (`ORM`), and `emissive` textures, one directional sun, and up to `64` frustum-culled point or spot lights uploaded through a `std140` `LightUniforms` UBO at binding point `1`.
2. **4-quadrant 2D shadow atlas:** `Renderer::appendShadowPass` partitions a single `2 * shadowMapSize` (`2048x2048` default) `Depth24` target into three stabilized directional [Cascaded Shadow Map (CSM)](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps) quadrants plus one hero spot-light quadrant, while `RenderWorld` retains per-quadrant `PassUpload` tracking (`D-R39`) so static cascades perform zero CPU culling and zero instance-buffer uploads.
3. **Linear HDR post-processing:** Active 3D worlds render into `WorldHdrVelocity` (`RGBA16F` color + `RG16F` velocity + `Depth24`), resolve a 5-mip Karis-weighted 13-tap downsample and 3x3 tent-upsample [Physically Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom) chain, and composite motion blur, analytical [AgX tonemapping](https://github.com/krojew/AgXHDR), and luma FXAA into the sRGB backbuffer before `RenderLayerId::UI` draws crisp un-tonemapped `GameVisual` chrome.
4. **Zero-overhead 2D fast path:** Pure-2D frames with an empty `RenderWorld`, no 3D `SceneGraphDrawable` attachments, and `canvasBloomEnabled=0` bypass `WorldHdrVelocity` and bloom allocation completely, keeping `IllumoGame.Wasm.PackageFrameAllocations` at zero host heap allocations on warmed frames.
5. **Cross-backend pixel parity and zero Vulkan pass breaks:** `Illumo.Gpu.BackendParity` passes across OpenGL 3.3 and Vulkan 1.3 (within `<= 2/255` sRGB LSB tolerance for FP16 filtering), and `VulkanFrameStats::renderPassBreaks` equals `0` across a full multi-pass HDR frame on `IllEd`, `IllMeshViewer`, `Playground`, and `IllumoGame`.
6. **60/144 Hz frame budget:** At `2560x1440` on the reference Windows 11 RTX 4080 desktop (`--bench-frames`), `2,000` textured lit instances with `32` dynamic lights, 3-cascade `2048x2048` PCF shadows, 5-mip bloom, and AgX + FXAA hold `< 2.5 ms` GPU frame time and `< 0.4 ms` host CPU world emission.

## 2. Current-State Evidence and Relevant Documentation

Inspection of the live `release/v26.10` tree establishes six concrete constraints in current production code:

- **Single-block UBO hardcoding (`GLShaderProgram.h:184-189`, `VulkanDeviceCommands.cpp:1065-1082`):** `IBackend.h:46-47` defines only `FrameUniformsBlockName = "FrameUniforms"` at `FrameUniformsBindingPoint = 0`. `GLShaderProgram` binds only that one block name at link time, and `VulkanDeviceCommands.cpp:1068` resolves *every* `GlslBindingKind::NamedBlock` descriptor to `m_uniformBindings[0]`, ignoring `m_uniformBindings[1..35]`. Adding `LightUniforms` without generalizing both sites would alias `LightUniforms` onto `FrameUniforms`.
- **Untextured Lambertian shaders (`Illumo/Shader/mesh_lit*_frag.glsl`):** Despite `RenderStyle.h:18` naming Blinn-Phong, both `mesh_lit_frag.glsl` and `mesh_lit_instanced_frag.glsl` compute `ambient + (1.0 - shadow) * max(dot(norm, lightDir), 0.0) * lightColor` with no specular term and no material sampler bindings. `RenderMaterialDesc` (`IRenderWorld.h:22-29`) carries only `tint`, `receivesShadow`, `castsShadow`, and `blend`.
- **Tangent-less vertex layouts (`IMesh.h:8-24`, `RenderCommand.h:47-53`):** `MeshVertexLayout::Pos3Norm3Color4U8Uv2` (`36 B`) and `Pos3Norm3Uv2` (`32 B`) occupy vertex attribute locations `0..3`, while `InstanceLayout::LitModelTint` (`144 B`) occupies locations `4..12`. Adding a precomputed vertex tangent attribute would collide with location `4` or break mesh stride contracts across `MeshLoader` and `GuestMeshRequest`.
- **Single-buffer shadow state (`RenderWorld.h:153-163`, `RenderWorld.cpp:571-606`, `Renderer.cpp:764-792`):** `Renderer::appendShadowPass` fits one orthographic frustum and clears `shadowFramebuffer` once. Each `RenderWorld::Bucket` holds a single `shadowBuffer` and single `shadowUpload`. Calling `AppendShadowCommands` four times per frame for four atlas quadrants without per-quadrant instance buffers would overwrite `bucket.shadowBuffer` at offset `0` before synchronous submission executes.
- **Conditional LDR offscreen pass (`Illumo.cpp:387-452`):** `configureScenePipeline()` installs `WorldColorVelocity` (`RGBA8` + `RG16F` + `Depth24`) and `MotionBlurResolve` only when `motionBlurEnabled` is non-zero; otherwise `World` draws straight into the LDR default framebuffer. `ITexture.h:23-25` and both backends (`GLTexture.h:113`, `VulkanContext.cpp:480`, `GpuTexels.cpp:87`) already implement `TextureFormat::RGBA16F`, `RG16F`, and `R16F`.
- **Wire and scene contracts (`IllumoGuest/Frame.h:109-175, 366`, `SceneDocument.h:129-135, 290`):** `GuestFrame::Version` is `9` (accepting `1`–`9`), `GuestWorldOp` ends at `DestroyWorld = 12`, and `.ilsc` is at `kFormatMajor = 2`, `kFormatMinor = 0` where `SceneLight` represents only a directional sun replacement.

Canonical references consulted in-tree: `AGENTS.md`, `Illumo/Source/Rendering/AGENTS.md`, `Illumo/Source/Rendering/Vulkan/AGENTS.md`, `Illumo/Source/Rendering/OpenGL/AGENTS.md`, `docs/contributing.md`, `docs/charter-direction.md`, `docs/host-render-world-design.md`, `docs/vulkan-backend-plan.md`, `docs/output/illumo.pdf`, and `docs/latex/sections/09-design-decision-log.tex`.

## 3. Scope and Explicit Non-Goals

In scope:

- Generalizing named UBO binding assignment (`0` = `FrameUniforms`, `1` = `LightUniforms`) across `IBackend`, `GLShaderProgram`, `GlslToSpirv`, `VulkanDevice`, and `MockBackend`.
- Extending `IRenderWorld` / `RenderWorld` with caller-numbered punctual lights (`Point`, `Spot`), CPU frustum culling + deterministic priority selection (`<= 64` active lights), and textured PBR-lite materials (`albedo`, `normal`, `metallicRoughness`, `emissive`).
- Upgrading `Renderer` directional shadows to a 4-quadrant `Depth24` shadow atlas (`3` stabilized CSM splits + `1` optional closest shadow-casting spotlight) with per-quadrant `RenderWorld` upload caching.
- Replacing the motion-blur-only `WorldColorVelocity` path with a unified `WorldHdrVelocity` (`RGBA16F` + `RG16F` + `Depth24`) + 5-mip Dual-Kawase/Jimenez Bloom + `HdrComposite` (Motion Blur + AgX/Reinhard tonemapping + FXAA) pass sequence, bypassed automatically for pure-2D non-bloom frames.
- Ensuring `VulkanDevice` batches upload/layout transitions ahead of `vkCmdBeginRendering` so the multi-pass HDR chain incurs zero `renderPassBreaks`.
- Extending Wasm frame schema to `v10` (preserving `v1`–`v9`) and `.ilsc` to `v2.1` (preserving `v2.0`), plus `IllEd` inspector support for point/spot lights and material maps.

Explicit non-goals:

- Raising the OpenGL baseline above `3.3 Core` or introducing compute shaders, `std430` SSBOs, or froxel light clustering (deferred to a future Tier-2/3 Clustered Forward+ plan once dynamic light counts exceed `64`).
- Introducing a general declarative render graph (`docs/contributing.md` forbids speculative render graphs).
- Multi-render-target G-Buffer deferred shading, Screen-Space Reflections (SSR), SSAO, or Temporal Anti-Aliasing (TAA) camera jitter.
- Omnidirectional point-light shadow cubemaps (local shadows in Setup 1 are scoped to the single highest-priority spotlight in atlas quadrant `3`).
- Adding any new third-party library (`glslang`, `volk`, `vma`, `glm`, and `GLFW` remain unchanged).

## 4. Constraints and Invariants

Every milestone must preserve the following repository invariants:

- **Language & formatting discipline (`docs/contributing.md`, `D-F3`):** No `auto`, no `namespace`, no recursion, and no C++ exceptions (`try`/`catch`/`throw`) in first-party host or guest code. Format only touched line ranges with Mozilla-style `clang-format` (`80` columns, `2`-space indent); keep `IllumoTidy` (`clang-tidy` warnings-as-errors) and `Illumo.Build.NoExceptions` green.
- **Token contract (`D-R1`, `D-R37`):** `RenderCommand` must remain trivially copyable with `sizeof(RenderCommand) <= 72` (`PublicHeaderSmoke`). `CommandQueue` and `RecordedCommandList` retain their `65,536` default ceilings and explicit rejection counters.
- **Backend seam (`D-R8`, `D-R11`, `D-R33`):** No OpenGL or Vulkan headers/types outside `Source/Rendering/OpenGL/`, `Source/Rendering/Vulkan/`, and composition roots. All shaders stay valid `#version 330 core` GLSL preprocessed for both OpenGL and `GlslToSpirv`.
- **Host-guest ownership (`D-E30`, `D-R28`, `D-R29`):** Guests own game policy and choose nonzero resource/world IDs; host `RenderWorld` owns GPU buffers, culling, and `RecordedCommandList` execution. Malformed or over-quota `v10` frames fail validation transactionally without partial mutation.
- **UI & readback fidelity (`D-R20`, `D-R27`):** `RenderLayerId::UI` and `RenderLayerId::Debug` draw *after* `HdrComposite` directly in sRGB screen space so UI colors, font weight ramps (`D-UI9`), and living-glass menus (`D-R26`) never pass through AgX tonemapping or FXAA. `FrameCapture` and detached-panel `requestFramebufferReadback` continue returning top-down `RGBA8` pixels.
- **Shader cache safety (`D-R38`):** Any change to `GlslToSpirv` reflection or `GlslProgram` serialization bumps `kGlslProgramCacheFormat` and `vulkan-pipelines-N` so stale on-disk caches miss cleanly instead of binding wrong descriptors.

## 5. Proposed Design and Alternatives Considered

### 5.1 Named Uniform Block Table and `LightUniforms` (Binding Point 1)

Replace the single `FrameUniformsBlockName` constant in `IBackend.h` with a fixed two-entry named-block table shared by `Renderer`, `GLShaderProgram`, `GlslToSpirv`, and `VulkanDevice`:

```cpp
// Illumo/Include/Illumo/Rendering/IBackend.h
inline constexpr const char* FrameUniformsBlockName = "FrameUniforms";
inline constexpr unsigned int FrameUniformsBindingPoint = 0u;

inline constexpr const char* LightUniformsBlockName = "LightUniforms";
inline constexpr unsigned int LightUniformsBindingPoint = 1u;

inline int
namedUniformBlockBinding(const char* blockName)
{
  if (blockName == nullptr) {
    return -1;
  }
  if (std::strcmp(blockName, FrameUniformsBlockName) == 0) {
    return static_cast<int>(FrameUniformsBindingPoint);
  }
  if (std::strcmp(blockName, LightUniformsBlockName) == 0) {
    return static_cast<int>(LightUniformsBindingPoint);
  }
  return -1;
}
```

In `GLShaderProgram.h`, iterate both canonical names after `glLinkProgram` and call `glUniformBlockBinding` whenever `glGetUniformBlockIndex != GL_INVALID_INDEX`. In `GlslToSpirv.cpp`, store the resolved UBO slot (`0` or `1`) in `GlslBinding::blockSlot` for `GlslBindingKind::NamedBlock`, and update `VulkanDeviceCommands.cpp` to index `m_uniformBindings[binding.blockSlot]` instead of hardcoded `[0]`.

`LightUniforms` (`std140`, `3,392 B` total—well inside OpenGL 3.3's guaranteed `16,384 B` `GL_MAX_UNIFORM_BLOCK_SIZE`) holds the 4 shadow-atlas light-space matrices, cascade view-depth splits, spot-shadow light index, active punctual light count, and `64` packed `48 B` punctual light records:

```cpp
static constexpr std::size_t kShadowAtlasQuadrants = 4u;
static constexpr std::size_t kMaximumForwardLights = 64u;

struct GpuPunctualLight
{
  // xyz: world position, w: inverse range (1.0f / max(range, 1e-4f))
  std::array<float, 4> positionAndInvRange{};
  // rgb: linear color * intensity, w: cos(outerConeAngle) (-2.0f for point)
  std::array<float, 4> radianceAndOuterCos{};
  // xyz: normalized spot direction, w: 1.0f / max(cosInner - cosOuter, 1e-4f)
  std::array<float, 4> directionAndAngleScale{};
};

struct LightUniforms
{
  std::array<std::array<float, 16>, kShadowAtlasQuadrants> atlasLightMatrices{};
  // xyz: view-space far depths of cascades 0..2, w: atlas texel size (1 / 2048)
  std::array<float, 4> cascadeSplitsAndTexel{};
  // x: punctualCount, y: cascadeCount (0..3), z: spotShadowLightIdx (-1 none),
  // w: shadowPcf (0 or 1)
  std::array<int, 4> countsAndFlags{ 0, 0, -1, 0 };
  std::array<GpuPunctualLight, kMaximumForwardLights> punctualLights{};
};
```

When more than `64` punctual lights pass `Renderer::isWorldBoundsVisible` (sphere-AABB test), `RenderWorld` scores visible lights deterministically by `score = (intensity * range * range) / max(distanceSqToCamera, 1.0f)` with tie-breaker `lightId < other.lightId` via `std::partial_sort`, uploading the top `64` and electing the highest-scoring `castsShadow == true` spot light for atlas quadrant `3`.

### 5.2 Textured GGX PBR-Lite Materials and Tangent-Free Normal Mapping

Extend `RenderMaterialDesc` (`IRenderWorld.h`) with non-owning `TextureHandle` slots and scalar PBR factors:

```cpp
struct RenderMaterialDesc
{
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
  bool receivesShadow = true;
  bool castsShadow = true;
  bool blend = false;
  TextureHandle albedoTexture{};
  TextureHandle normalTexture{};
  // R: ambient occlusion, G: perceptual roughness, B: metallic
  TextureHandle metallicRoughnessTexture{};
  TextureHandle emissiveTexture{};
  float metallic = 0.0f;
  float roughness = 0.8f;
  std::array<float, 3> emissiveFactor{ 0.0f, 0.0f, 0.0f };
};
```

`Renderer` lazily enrolls three `1x1` fallback textures alongside `whiteTexture()`:

- `whiteTexture()` (`255, 255, 255, 255`) for unbound `albedoTexture` (unit `1`) and unbound `emissiveTexture` (unit `4`, multiplied by `uEmissiveFactor = {0,0,0}`).
- `flatNormalTexture()` (`128, 128, 255, 255`) for unbound `normalTexture` (unit `2`).
- `defaultOrmTexture()` (`255, 255, 255, 255`) for unbound `metallicRoughnessTexture` (unit `3`), modulated by `uMaterialFactors = vec4(metallic, roughness, hasNormalMap, 1.0)`.
- Unit `0` remains `uShadowMap` (`WorldLook::kShadowTextureUnit` stays `0`, or keeps existing unit assignment without colliding).

Because `MeshVertexLayout` (`IMesh.h`) carries no precomputed vertex tangents, `mesh_lit*_frag.glsl` reconstructs the tangent-to-world frame per pixel when `uMaterialFactors.z > 0.5` using Christian Schüler's screen-space cotangent frame (`dFdx`/`dFdy` on `vFragPos` and `vTexCoord`). To prevent `NaN` propagation on degenerate or constant-UV primitive faces, if `max(dot(T, T), dot(B, B)) < 1e-8`, the shader falls back cleanly to the interpolated vertex normal `N`. Albedo and emissive samples convert from sRGB to linear in the shader so existing `RGBA8` texture enrollment semantics in `IBackend` stay untouched.

### 5.3 4-Quadrant Shadow Atlas (3-Split CSM + 1 Hero Spot) and Per-Quadrant `PassUpload`

Instead of introducing `sampler2DArray` across `ITexture`, `GLTexture`, `VulkanTexture`, and `MockBackend`, `Renderer` allocates `shadowFramebuffer` at `2 * shadowMapSize` square (`2048x2048` default for `shadowMapSize = 1024`, clamped to `maxTextureSize()`) and addresses four quadrants via `CommandType::SetViewport` and `CommandType::SetScissorState`:

| Quadrant | Viewport `(x, y, w, h)` | Role | Projection Fit |
|---|---|---|---|
| `0` | `(0, 0, S, S)` | Sun Cascade 0 (near) | Stabilized bounding sphere of camera sub-frustum `[zNear, d0]` snapped to texel increments |
| `1` | `(S, 0, S, S)` | Sun Cascade 1 (mid) | Stabilized bounding sphere of camera sub-frustum `[d0, d1]` snapped to texel increments |
| `2` | `(0, S, S, S)` | Sun Cascade 2 (far) | Stabilized bounding sphere of camera sub-frustum `[d1, d2]` snapped to texel increments |
| `3` | `(S, S, S, S)` | Local Hero Spot | Perspective `fov = 2 * outerAngle`, `[0.1, range]` for highest-priority shadow-casting spot light |

Cascade split depths `d0, d1, d2` use the practical logarithmic-uniform blend ($\lambda = 0.75$) clamped to `shadowCasterDistance`. Each cascade clamps UV reads inside its quadrant with a `1.5`-texel guard band so PCF never bleeds across quadrant seams.

To preserve `D-R39` (`UnchangedBucketsSkipUploads`) without overwriting one shared shadow instance buffer across four passes in the same submission, `RenderWorld::Bucket` replaces its scalar `shadowBuffer` / `shadowUpload` / `shadowStaging` with fixed 4-element arrays:

```cpp
std::array<PassUpload, kShadowAtlasQuadrants> shadowUploads;
std::array<BufferHandle, kShadowAtlasQuadrants> shadowBuffers;
std::array<std::vector<GpuInstance>, kShadowAtlasQuadrants> shadowStaging;
size_t shadowStream = 0; // Index of CmdInstanceStream in bucket.shadow
size_t shadowDraw = 0;   // Index of CmdDrawIndexedInstanced in bucket.shadow
```

### 5.4 Linear HDR Pass Chain, Physically Based Bloom, and AgX + FXAA Composite

`Illumo::configureScenePipeline()` (`Illumo/Source/Engine/Illumo.cpp`) is generalized from motion-blur-only to the full configurable HDR pipeline driven by `RenderPipelineConfig`:

```text
PassType::ShadowDepth (2048x2048 Depth24 4-quadrant atlas, cleared once)
  -> WorldGeomPass (Pooled "WorldHdrVelocity": RGBA16F color0 + RG16F vel1 + Depth24)
  -> BloomDown0..4 (Pooled 1/2, 1/4, 1/8, 1/16, 1/32 RGBA16F, Linear + ClampToEdge)
  -> BloomUp3..0   (Additive/interpolated 3x3 tent upsample back to 1/2 res)
  -> HdrComposite  (Screen backbuffer or pre-FXAA target: MotionBlur + Bloom mix + Exposure + AgX + FXAA)
  -> RenderLayerId::UI & RenderLayerId::Debug (Direct sRGB backbuffer overlay)
```

### 5.5 Wasm Frame Schema v10 and `.ilsc` Format `2.1`

- **Frame Schema v10 (`IllumoGuest/Include/IllumoGuest/Frame.h`, `WasmFrameRenderer.cpp`):** Bump `GuestFrame::Version` to `10` while keeping `v1`–`v9` decoders intact. Add `GuestWorldOp::LightCreate = 13`, `LightUpdate = 14`, `LightDestroy = 15` carrying `GuestWorldLight` (`kind`, `position`, `direction`, `color`, `intensity`, `range`, `innerCos`, `outerCos`, `castsShadow`, `visible`). Extend `GuestWorldMaterial` in `v10` streams with 4 `GuestResourceId` texture fields (`0` = unbound) plus `metallic`, `roughness`, and `emissiveFactor`. Enforce `GuestFrameLimits::maximumWorldLights = 1024` and validate finite floats (`std::isfinite`), positive `range`, and `innerCos >= outerCos` before applying any frame operation.
- **`.ilsc` Format `2.1` (`SceneDocument.h`, `IlscCodec.cpp`, `SceneInstance.cpp`):** Bump `SceneDocument::kFormatMinor` from `0` to `1`. Reader accepts `2.0` and `2.1`. `SceneLight` gains `SceneLightKind kind = SceneLightKind::Directional` (`Directional`, `Point`, `Spot`), `float range = 10.0f`, `float innerAngleDegrees = 25.0f`, `float outerAngleDegrees = 35.0f`. `SceneMeshRenderer` gains optional asset IDs `albedoTexture`, `normalTexture`, `ormTexture`, `emissiveTexture`, `metallic`, `roughness`, and `emissive`. `SceneInstance` maps `Point` and `Spot` nodes onto `IRenderWorld::createLight` / `updateLight` as node world transforms move.

### 5.6 Eliminating `VulkanFrameStats::renderPassBreaks` Across Multi-Pass HDR

1. `FrameUniforms` and `LightUniforms` are written **once** per `RenderScene` before any pass begins (`useSerial < m_recordingSerial`), so `VulkanDevice::transferCommands` routes them cleanly into `Slot::upload` with zero pass breaks.
2. Per-quadrant `RenderWorld` shadow instance buffers (`shadowBuffers[0..3]`) are distinct `VkBuffer` allocations, so writing `shadowBuffers[q]` never collides with `shadowBuffers[0..q-1]`'s `useSerial` and routes to `Slot::upload` as well.
3. Render-target transitions between `ShadowDepth` -> `WorldGeomPass` -> `BloomDown*` -> `BloomUp*` -> `HdrComposite` occur outside active `vkCmdBeginRendering`, keeping `m_stats.renderPassBreaks == 0`.

### 5.7 Alternatives Considered and Rejected

| Alternative | Why Rejected |
|---|---|
| **`sampler2DArray` for CSM cascades** | Requires adding array-texture handles, views, layer framebuffers, and readback paths across `IBackend`, `GLDevice`, `VulkanDevice`, `GlslToSpirv`, and `MockBackend`. A 4-quadrant 2D atlas needs zero `IBackend` signature changes. |
| **Precomputed `vec4 aTangent` in `MeshVertexLayout`** | Collides with `InstanceLayout::LitModelTint` at vertex location `4`, bloats static/guest mesh memory by `16 B/vertex` (+44%), and invalidates existing procedural/OBJ loaders. Screen-space cotangent reconstruction needs zero vertex changes. |
| **Bake a 3D LUT texture for AgX** | Requires `sampler3D` support in `IBackend`, `GLDevice`, `VulkanDevice`, and `GlslToSpirv`. Troy Sobotka's closed-form 6th-order polynomial AgX approximation costs ~12 ALU ops per pixel with zero texture fetches. |
| **Thresholded Gaussian blur bloom** | Causes temporal flickering on subpixel specular highlights and requires wide separable kernels for large glow radii; Jimenez's un-thresholded Karis-weighted mip pyramid is faster, temporally stable, and matches physical lens scatter. |

## 6. Affected Public Contracts, Compatibility, and Migration Needs

- **`Illumo/Include/Illumo/Rendering/IBackend.h`:** Additive `LightUniformsBlockName`, `LightUniformsBindingPoint`, and `namedUniformBlockBinding`. Existing `IBackend` implementations remain source-compatible.
- **`Illumo/Include/Illumo/Rendering/IRenderWorld.h`:** Additive fields with defaults on `RenderMaterialDesc`; new `RenderLightId`, `RenderLightKind`, `RenderLightDesc`, and virtual methods `createLight`, `updateLight`, `destroyLight`.
- **`Illumo/Include/Illumo/Rendering/RenderPipelineConfig.h`:** Additive fields (`hdrEnabled`, `tonemapper`, `exposure`, `bloomEnabled`, `bloomIntensity`, `bloomMipCount`, `shadowCascades`, `fxaaEnabled`, `canvasBloomEnabled`) seeded into `IEnvVars` with safe defaults.
- **`Illumo/Include/Illumo/Rendering/RenderStyle.h`:** Additive `RenderStyleId` entries (`BloomDownsample`, `BloomUpsample`, `HdrComposite`, `FxaaResolve`) before `Count`.
- **`IllumoGuest/Include/IllumoGuest/Frame.h`:** `GuestFrame::Version` bumped to `10`. Pre-compiled `.wasm` packages (`v1`–`v9`) continue running identically on the `v10` host.
- **`Illumo/Include/Illumo/Content/SceneDocument.h`:** `kFormatMinor` bumped to `1`. All existing `.ilsc` `2.0` scenes load without modification.
- **On-disk GPU cache (`cache/gpu`, `D-R38`):** Bump `kGlslProgramCacheFormat` (`"glsl-program-2"`) and `"vulkan-pipelines-2"` so existing `cache/gpu` blobs cleanly miss and recompile with named-block slot reflection.

## 7. Ownership, Lifetime, Threading, Error, and Platform Implications

- **Resource ownership:** `Renderer` owns `m_lightUniformBuffer`, `flatNormalTexture()`, `defaultOrmTexture()`, and the pooled `WorldHdrVelocity` / `Bloom*` framebuffers through `_renderTargetPool`, destroying all handles in `Renderer::~Renderer()` before backend shutdown. `RenderWorld` owns all four `shadowBuffers[q]` per bucket and releases them in `RenderWorld::releaseResources()`.
- **Texture lifetime in `RenderWorld`:** `TextureHandle` fields in `RenderMaterialDesc` are non-owning handles whose underlying `AssetManager` or `WasmFrameRenderer` texture references stay alive while bound; fallback `1x1` textures guard invalid/retired handles.
- **Threading:** All `Renderer`, `RenderWorld`, `RenderTargetPool`, and `IBackend` work remains strictly main-thread affine.
- **Error handling (`D-F3`):** No exceptions anywhere. If `RGBA16F` framebuffer allocation or shader enrollment fails, `Illumo::configureScenePipeline()` logs a warning, clears `SetDefaultLayerPasses(RenderLayerId::World, {})`, and degrades gracefully to direct LDR backbuffer rendering.
- **Platform readiness:** All C++23 and GLSL 330 code remains portable across Windows MSVC/Clang and Ubuntu 24.04 GCC/Clang.

## 8. Ordered Implementation Milestones

| Milestone | Scope | Primary Files Touched | Exit Gate |
|---|---|---|---|
| **M0** | Named UBO binding table (`FrameUniforms`=0, `LightUniforms`=1) & cache key bump | `IBackend.h`, `GLShaderProgram.h`, `GlslToSpirv.*`, `GlslProgramCache.*`, `VulkanDevice*.cpp`, `MockBackend.h`, `TestVulkanShaders.cpp` | `Illumo.Vulkan.*` & `Illumo.Gpu.BackendParity` pass with multi-UBO test shader |
| **M1** | Textured GGX PBR-lite shaders, fallback `1x1` textures, NaN-safe cotangent normals, & `LightUniforms` punctual lights in `RenderWorld` / `MeshVisual` | `IRenderWorld.h`, `RenderWorld.*`, `Renderer.*`, `MeshVisual.*`, `mesh_lit*.glsl` | `Illumo.RenderWorld.*` & new `Illumo.Gpu.PbrPunctualLightsParity` match OpenGL vs. Vulkan within `<= 2` LSB |
| **M2** | 4-quadrant 2D shadow atlas (3 stabilized CSM splits + 1 hero spot) & per-quadrant `Bucket::shadowBuffers[4]` upload skip (`D-R39`) | `Renderer.*`, `RenderWorld.*`, `shadow_depth*.glsl`, `mesh_lit*.glsl`, `GroundPlaneVisual.cpp` | Static cascades emit `0` uploads; `Illumo.Gpu.ShadowAtlasParity` passes |
| **M3** | `WorldHdrVelocity` (`RGBA16F`), 5-mip Karis/Jimenez Bloom pyramid, analytical AgX + Reinhard + FXAA composite, & pure-2D bypass | `RenderPipelineConfig.h`, `RenderStyle.h`, `RendererStyles.cpp`, `RenderTargetPool.h`, `Illumo.cpp` | `Illumo.Gpu.HdrBloomAgxParity` passes; `VulkanFrameStats::renderPassBreaks == 0` |
| **M4** | Wasm frame schema v10 (`LightCreate/Update/Destroy`, textured `GuestWorldMaterial`) & host quota/decoder validation | `IllumoGuest/Frame.h`, `IllumoGuest/RenderWorld.*`, `WasmFrameRenderer.cpp` | `Illumo.Wasm.WorldV10Validation` (round-trip, truncation, NaN/quota deny, v1–v9 compat) passes |
| **M5** | `.ilsc` format `2.1` (`Point`/`Spot` `SceneLight`, PBR texture refs on `SceneMeshRenderer`) & `SceneInstance` binding | `SceneDocument.h`, `IlscCodec.cpp`, `SceneInstance.*`, `TestContent*` | Round-trip `.ilsc` `2.0` & `2.1` codec tests and `SceneInstance` light-move tests pass |
| **M6** | Product integration: `IllEd` light/material inspector rows, `IllMeshViewer` HDR/tonemap/exposure controls, & `Playground` demo scene | `IllEd/Source/EditorInspector.*`, `IllMeshViewer/Source/*`, `Playground/Scenes/demo.ilsc` | `IllEdTests`, `IllMeshViewerTests`, `Playground` Wasm package tests, & `IllEd.Gpu.Smoke` pass |
| **M7** | Performance sweep & warmed zero-allocation verification across `CSim` and shipped apps | `Illumo/Tests/TestSceneBenchmarks.cpp`, `tools/verify_capture.py` | `< 2.5 ms` @ 1440p 2k cubes + 32 lights; `PackageFrameAllocations` == `0` |
| **M8** | Canonical docs (`docs/architecture-consensus.md`, `docs/packages/rendering.md`, LaTeX Ch. 05 & `09-design-decision-log.tex` `D-R41`/`D-E43`), `clang-tidy`, & full Release gate | `docs/*`, `Illumo/Source/Rendering/AGENTS.md` | `IllumoTidy`, `CheckNoExceptions`, `IllumoWorkspace` + `IllumoGpu` CTest suites, and `docs/build.ps1` clean |

## 9. Build, Test, Static-Analysis, Sanitizer, and Benchmark Strategy

1. **Headless contract & decoder suites (every milestone):**
   - `IllumoTests --run Illumo.Vulkan.*`
   - `IllumoTests --run Illumo.RenderWorld.*`
   - `IllumoTests --run Illumo.Wasm.*`
   - `PublicHeaderSmoke` & `Illumo.Build.NoExceptions`
2. **Real-GPU parity & capture suites (`M0`–`M3`, `M6`, `M8`):**
   - Extend `IllumoGpuTests` (`Illumo/Tests/Gpu/GpuBackendParity.cpp`) with `PbrPunctualLightsParity`, `ShadowAtlasParity`, and `HdrBloomAgxParity`.
   - Run real-GPU capture checks across `--graphics-api opengl` and `--graphics-api vulkan`.
3. **Sanitizers, static analysis, and coverage (`M4`, `M8`):**
   - Run ASan Debug tests on v10 fuzz/deny cases.
   - Run `IllumoTidy` and verify `clang-format` on strictly touched line ranges.
4. **Benchmarks (`M7`):**
   - Measure 2,000 textured lit instances + 32 dynamic lights + 3-cascade shadows + bloom + AgX + FXAA and verify `PackageFrameAllocations == 0` and `renderPassBreaks == 0`.

## 10. Rollback and Containment Strategy

- **Runtime kill-switch (`hdrEnabled=0`):** Setting `set hdrEnabled 0` bypasses `WorldHdrVelocity`, Bloom, AgX, and FXAA immediately on the next frame.
- **Cascade count fallback (`shadowCascades=1`):** Setting `shadowCascades=1` reverts `appendShadowPass` to a single directional fit.
- **Wire & file isolation:** Frame schema `v10` (`M4`) and `.ilsc` `2.1` (`M5`) are strictly additive versions; `CSim/ENGINE_MIN_COMMIT` does not need to move until `CSim` explicitly opts into `v10` features.

## 11. Decisions Adopted (`O1`–`O5`)

| # | Decision Point | Adopted Choice | Notes |
|---|---|---|---|
| **O1** | Default `hdrEnabled` & `tonemapper` in `RenderPipelineConfig` | `hdrEnabled = true`, `tonemapper = AgX`, `bloomEnabled = true` (`bloomIntensity = 0.04`) for 3D scenes; bypassed when `World` has 0 3D drawables | Preserves legacy LDR tests when `hdrEnabled=0` or no 3D drawables are active |
| **O2** | Optional 2D `CSim` canvas bloom | `canvasBloomEnabled = false` by default | Keeps CSim default look and zero-allocation 2D fast path unchanged |
| **O3** | Shadow atlas layout for `shadowMapSize = 1024` | Allocate `2 * shadowMapSize` (`2048x2048` default, clamped to `maxTextureSize()`) | Each of the 4 quadrants keeps full `shadowMapSize` resolution |
| **O4** | Anti-aliasing on offscreen HDR targets | Single-pass luma FXAA 3.11 (`fxaaEnabled = true`), leaving UI unblurred | Anti-aliases 3D HDR edges on both OpenGL and Vulkan without MSAA VRAM bloat |
| **O5** | Execution cadence | End-to-end `M0`–`M8` execution authorized via `/goal` | Verified at each milestone gate before advancing |

## 12. Validation Results Log

| Milestone | Commit | `IllumoWorkspace` | `IllumoGpu` Parity | `IllumoTidy` / `NoExceptions` | Notes / Measured Medians |
|---|---|---|---|---|---|
| **M0** | working-tree | PASS (`12/12` `Illumo.*`) | PASS (`5/5` `Illumo.Gpu.*`) | PASS (`cl`+`clang-tidy`, `NoExceptions`) | Multi-UBO slot reflection (`glsl-program-2`, `vulkan-pipelines-2`) & simultaneous `FrameUniforms`+`LightUniforms` parity verified |
| **M1** | working-tree | PASS (`38/38` `Illumo.*`) | PASS (`6/6` `Illumo.Gpu.*`, `PbrPunctualLightsParity`) | PASS (`cl`+`clang-tidy`, `NoExceptions`) | Textured GGX + cotangent normals + 64 punctual UBO lights (`0` diff px on `pbr` GL vs VK) |
| **M2** | working-tree | PASS (`125/125` `Illumo.*`) | PASS (`7/7` `Illumo.Gpu.*`, `ShadowAtlasParity`) | PASS (`cl`+`clang-tidy`, `NoExceptions`) | 4-quadrant CSM/spot atlas + per-quadrant `D-R39` `0` static uploads (`0/65536` diff px, largest `1` LSB GL vs VK) |
| **M3** | working-tree | PASS (`31/31` `Illumo.Host/Renderer/PublicHeaders/NoExceptions`) | PASS (`8/8` `Illumo.Gpu.*`, `HdrBloomAgxParity`) | PASS (`cl`+`clang-tidy`, `NoExceptions`) | `WorldHdrVelocity` + 5-mip Karis Bloom + AgX + FXAA (`0/65536` diff px, largest `1` LSB GL vs VK, `renderPassBreaks == 0`) |
| **M4** | working-tree | PASS (`71/71` `Illumo.Wasm.*` Release + `7/7` Debug ASan) | N/A (wire/host contract) | PASS (`clang-tidy` on M4 TUs, `NoExceptions`) | Wasm frame schema v10 `LightCreate/Update/Destroy`, PBR material textures/factors, truncation & quota deny, v1–v9 compat |
| **M5** | working-tree | PASS (`55/55` `Illumo.Content.*`) | N/A (codec/graph contract) | PASS (`cl` clean, `NoExceptions`) | `.ilsc` v2.1 codec (`Point`/`Spot` `SceneLight`, PBR `SceneMeshRenderer`), `2.0` compat, & `SceneInstance` punctual light + shared PBR bucket sync |
| **M6** | working-tree | PASS (`142/142` `IllEd`/`IllMeshViewer`/`Playground`/`Display`) | PASS (`IllEd.Gpu.Smoke`) | PASS (`cl` + Wasm `clang++` clean) | `IllEd` PBR/light inspector + clipboard/paste, `IllMeshViewer` HDR/AgX/exposure/bloom/FXAA controls + Display wire v9, `Playground` v2.1 dynamic lights |
| **M7** | working-tree | PASS (`PackageFrameAllocations == 0`, `GamePackage`, `PackageBench`) | PASS (`Illumo.Gpu.HdrForwardBench`, `verify_capture.py` GL+VK) | PASS (`cl` clean) | 1440p 2k PBR instances + 32 lights + 4-quadrant shadows + 5-mip Bloom + AgX + FXAA: GL `0.607 ms`, VK `0.573 ms` (`0` pass breaks), CPU `272.7–273.9 us` (`< 400 us`), `0` 2D host frame allocs |
| **M8** | working-tree | PASS (`806/806` `IllumoWorkspace`) | PASS (`10/10` `IllumoGpu`) | PASS (`clang-tidy` clean on all touched lines, `NoExceptions`) | `D-R41` / `D-E43` docs, `docs/output/illumo.pdf` rebuilt, changed-lines `clang-format`, full Release gate |
