#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmResourceTable.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

struct WasmFrameRenderer::State
{
  struct Budget
  {
    std::uint64_t bytes = 0;
    static constexpr std::uint64_t Maximum = 256u * 1024u * 1024u;
  };
  struct Texture
  {
    Texture(Renderer& value,
            std::shared_ptr<Budget> account,
            std::uint64_t size)
      : renderer(value)
      , lifetime(value.getLifetimeIdentity())
      , budget(std::move(account))
      , bytes(size)
    {
      budget->bytes += bytes;
    }
    ~Texture()
    {
      if (!lifetime.expired() && handle.isValid()) {
        renderer.destroyTexture(handle);
      }
      budget->bytes -= bytes;
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;
    Renderer& renderer;
    std::weak_ptr<const void> lifetime;
    std::shared_ptr<Budget> budget;
    std::uint64_t bytes;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    // Cubemaps are sampled only by Skybox batches and never written.
    bool cubemap = false;
    TextureHandle handle{};
  };
  // A retained host mesh. The table shares it as const; its upload state
  // changes while bytes arrive, so that state lives behind an owned pointer.
  // Static meshes stage bytes until complete, then enroll immutable GPU
  // geometry. Dynamic meshes (frame v4) keep a validated CPU shadow that
  // frame mesh writes patch; dirty ranges upload before the next draw.
  struct RetainedMesh
  {
    struct Upload
    {
      std::vector<std::byte> vertices;
      std::vector<std::byte> indices;
      std::uint32_t vertexWritten = 0;
      std::uint32_t indexWritten = 0;
      MeshHandle handle{};
      std::uint32_t indexCount = 0;
      AxisAlignedBounds3 bounds;
      bool ready = false;
      bool failed = false;
      bool dynamic = false;
      bool queued = false;
      std::uint32_t vertexDirtyBegin = 0;
      std::uint32_t vertexDirtyEnd = 0;
      std::uint32_t indexDirtyBegin = 0;
      std::uint32_t indexDirtyEnd = 0;
    };
    RetainedMesh(Renderer& value,
                 std::shared_ptr<Budget> account,
                 const GuestMeshRequest& request)
      : renderer(value)
      , lifetime(value.getLifetimeIdentity())
      , budget(std::move(account))
      , bytes(static_cast<std::uint64_t>(request.vertexBytes) +
              request.indexBytes)
      , style(static_cast<GuestBatchStyle>(request.style))
      , vertexBytes(request.vertexBytes)
      , indexBytes(request.indexBytes)
      , upload(std::make_unique<Upload>())
    {
      budget->bytes += bytes;
    }
    ~RetainedMesh()
    {
      if (!lifetime.expired() && upload->handle.isValid()) {
        renderer.destroyMesh(upload->handle);
      }
      budget->bytes -= bytes;
    }
    RetainedMesh(const RetainedMesh&) = delete;
    RetainedMesh& operator=(const RetainedMesh&) = delete;
    RetainedMesh(RetainedMesh&&) = delete;
    RetainedMesh& operator=(RetainedMesh&&) = delete;
    Renderer& renderer;
    std::weak_ptr<const void> lifetime;
    std::shared_ptr<Budget> budget;
    std::uint64_t bytes;
    GuestBatchStyle style;
    std::uint32_t vertexBytes;
    std::uint32_t indexBytes;
    std::unique_ptr<Upload> upload;
  };
  struct PreparedBatch
  {
    std::vector<std::byte> vertices;
    std::shared_ptr<const Texture> texture;
    std::shared_ptr<const RetainedMesh> retained;
    // Lit meshes: world bounds for host shadow-caster relevance.
    AxisAlignedBounds3 worldBounds;
  };
  struct Mesh
  {
    MeshHandle handle{};
    MeshVertexLayout layout = MeshVertexLayout::Pos3Color4U8;
    std::size_t vertexCapacity = 0;
    std::size_t indexCapacity = 0;
  };
  // An inline batch's host mesh: slot `index` of the pool for its style.
  struct Slot
  {
    std::uint32_t pool = 0;
    std::uint32_t index = 0;
  };
  static constexpr std::uint32_t kStylePools = 5;
  struct Layer : DrawableBase
  {
    Layer(State& value, GuestLayer requested)
      : state(value)
      , layer(requested)
    {
    }
    void Draw() override {}
    bool AppendCommands(Renderer* renderer) override
    {
      return state.append(renderer, layer);
    }
    // World lit meshes join the host's one shared shadow pass.
    void CollectShadowCasters(Renderer* renderer) override
    {
      if (layer == GuestLayer::World) {
        state.collectShadowCasters(renderer);
      }
    }
    void AppendShadowCommands(Renderer* renderer) override
    {
      if (layer == GuestLayer::World) {
        state.appendShadowCommands(renderer);
      }
    }
    State& state;
    GuestLayer layer;
  };
  struct Surface;
  // Draws one surface's batches; used by renderOffscreen for its window.
  struct SurfaceLayer : DrawableBase
  {
    SurfaceLayer(State& value, Surface& owner)
      : state(value)
      , surface(owner)
    {
    }
    void Draw() override {}
    bool AppendCommands(Renderer* renderer) override
    {
      return state.appendSurface(renderer, surface);
    }
    State& state;
    Surface& surface;
  };
  // Frame schema v5: the latest content of one surface window. Its inline
  // batches use pools of their own, so replaying a surface never disturbs
  // the main frame's meshes.
  struct Surface
  {
    std::uint32_t id = 0;
    float width = 0.0f;
    float height = 0.0f;
    std::uint64_t revision = 0;
    std::vector<GuestBatch> batches;
    std::vector<PreparedBatch> payloads;
    std::vector<Slot> slots;
    std::array<std::vector<Mesh>, kStylePools> pools;
    std::unique_ptr<SurfaceLayer> drawable;
  };
  State(Renderer& value, std::uint64_t owner, GuestFrameLimits quotas)
    : renderer(value)
    , lifetime(value.getLifetimeIdentity())
    , limits(quotas)
    , textures(owner)
    , retainedMeshes(owner)
    , world(*this, GuestLayer::World)
    , ui(*this, GuestLayer::Ui)
  {
  }
  ~State()
  {
    if (!lifetime.expired()) {
      destroyPools(pools);
      for (const std::unique_ptr<Surface>& surface : surfaces) {
        destroyPools(surface->pools);
      }
      for (const std::pair<const std::uint32_t, RenderStyleHandle>& style :
           derivedStyles) {
        renderer.destroyStyle(style.second);
      }
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  void destroyPools(const std::array<std::vector<Mesh>, kStylePools>& value)
  {
    for (const std::vector<Mesh>& pool : value) {
      for (const Mesh& mesh : pool) {
        if (mesh.handle.isValid() && !lifetime.expired()) {
          renderer.destroyMesh(mesh.handle);
        }
        const std::uint64_t bytes = mesh.vertexCapacity + mesh.indexCapacity;
        budget->bytes -= std::min(budget->bytes, bytes);
      }
    }
  }

  Surface* findSurface(std::uint32_t id) const
  {
    for (const std::unique_ptr<Surface>& surface : surfaces) {
      if (surface->id == id) {
        return surface.get();
      }
    }
    return nullptr;
  }

  // Removes a surface's content from the live list, keeping its pools.
  std::unique_ptr<Surface> takeSurface(std::uint32_t id)
  {
    for (std::size_t index = 0; index < surfaces.size(); ++index) {
      if (surfaces[index]->id == id) {
        std::unique_ptr<Surface> taken = std::move(surfaces[index]);
        surfaces.erase(surfaces.begin() + static_cast<std::ptrdiff_t>(index));
        return taken;
      }
    }
    return nullptr;
  }

  // Inline batches take the next slot of their style's pool, so a slot's
  // layout never changes and it is only replaced to grow.
  static std::vector<Slot> assignSlots(
    const std::vector<GuestBatch>& batches,
    std::array<std::uint32_t, kStylePools>& used)
  {
    std::vector<Slot> slots(batches.size());
    for (std::size_t index = 0; index < batches.size(); ++index) {
      if (!batches[index].retained()) {
        const std::uint32_t pool =
          static_cast<std::uint32_t>(batches[index].style) - 1u;
        slots[index] = { pool, used[pool]++ };
      }
    }
    return slots;
  }

  // Bytes the pools grow by to hold the inline batches, and the largest
  // single replacement (old and new buffers coexist briefly).
  static std::uint64_t slotGrowth(
    const std::vector<GuestBatch>& batches,
    const std::vector<PreparedBatch>& prepared,
    const std::vector<Slot>& slots,
    const std::array<std::vector<Mesh>, kStylePools>& value,
    std::uint64_t& replacementPeak)
  {
    std::uint64_t growth = 0;
    for (std::size_t index = 0; index < batches.size(); ++index) {
      if (batches[index].retained()) {
        continue;
      }
      const std::size_t vertices = prepared[index].vertices.size();
      const std::size_t indices =
        batches[index].indices.size() * sizeof(std::uint32_t);
      const std::vector<Mesh>& pool = value[slots[index].pool];
      if (slots[index].index < pool.size()) {
        const Mesh& mesh = pool[slots[index].index];
        growth += std::max(mesh.vertexCapacity, vertices) - mesh.vertexCapacity;
        growth += std::max(mesh.indexCapacity, indices) - mesh.indexCapacity;
        if (mesh.vertexCapacity < vertices || mesh.indexCapacity < indices) {
          replacementPeak = std::max(
            replacementPeak,
            static_cast<std::uint64_t>(std::max(mesh.vertexCapacity, vertices) +
                                       std::max(mesh.indexCapacity, indices)));
        }
      } else {
        growth += vertices + indices;
      }
    }
    return growth;
  }

  // Enrolls or grows the slot meshes. False (with error set) when the
  // backend refuses an allocation.
  bool growSlots(const std::vector<GuestBatch>& batches,
                 const std::vector<PreparedBatch>& prepared,
                 const std::vector<Slot>& slots,
                 const std::array<std::uint32_t, kStylePools>& used,
                 std::array<std::vector<Mesh>, kStylePools>& value)
  {
    for (std::uint32_t pool = 0; pool < kStylePools; ++pool) {
      value[pool].resize(std::max<std::size_t>(value[pool].size(), used[pool]));
    }
    for (std::size_t index = 0; index < batches.size(); ++index) {
      if (batches[index].retained()) {
        continue;
      }
      Mesh& mesh = value[slots[index].pool][slots[index].index];
      const MeshVertexLayout requested = layout(batches[index].style);
      const std::size_t vertexBytes = prepared[index].vertices.size();
      const std::size_t indexBytes =
        batches[index].indices.size() * sizeof(std::uint32_t);
      if (mesh.handle.isValid() && mesh.layout == requested &&
          mesh.vertexCapacity >= vertexBytes &&
          mesh.indexCapacity >= indexBytes) {
        continue;
      }
      const std::size_t vertexCapacity =
        std::max(mesh.vertexCapacity, vertexBytes);
      const std::size_t indexCapacity =
        std::max(mesh.indexCapacity, indexBytes);
      bool allocated = true;
      if (mesh.handle.isValid()) {
        allocated = renderer.replaceDynamicMesh(
          mesh.handle, vertexCapacity, nullptr, indexCapacity, requested);
        ++counters.meshReplacements;
      } else {
        mesh.handle = renderer.enrollDynamicMesh(
          vertexCapacity, nullptr, indexCapacity, requested);
        allocated = mesh.handle.isValid();
        ++counters.meshEnrollments;
      }
      if (!allocated) {
        error = "Guest frame mesh allocation failed";
        return false;
      }
      mesh.layout = requested;
      budget->bytes += vertexCapacity - mesh.vertexCapacity + indexCapacity -
                       mesh.indexCapacity;
      mesh.vertexCapacity = vertexCapacity;
      mesh.indexCapacity = indexCapacity;
    }
    return true;
  }

  // Resolves textures and retained meshes and packs inline vertices.
  bool prepareBatches(const std::vector<GuestBatch>& batches,
                      std::vector<PreparedBatch>& prepared)
  {
    prepared.resize(batches.size());
    for (std::size_t index = 0; index < batches.size(); ++index) {
      const GuestBatch& batch = batches[index];
      PreparedBatch& ready = prepared[index];
      if (batch.style != GuestBatchStyle::Shape &&
          batch.style != GuestBatchStyle::LitMesh) {
        ready.texture = textures.resolve(batch.texture);
        const bool wantsCubemap = batch.style == GuestBatchStyle::Skybox;
        if (!ready.texture || ready.texture->cubemap != wantsCubemap) {
          error = "Invalid guest texture authority";
          return false;
        }
      }
      if (batch.retained()) {
        if (!prepareRetained(batch, ready)) {
          return false;
        }
      } else {
        prepareInline(batch, ready);
      }
    }
    return true;
  }

  static MeshVertexLayout layout(GuestBatchStyle style)
  {
    if (style == GuestBatchStyle::Sprite) {
      return MeshVertexLayout::Pos3Color4U8Uv2;
    }
    if (style == GuestBatchStyle::Canvas) {
      return MeshVertexLayout::Pos3Color3Uv2;
    }
    if (style == GuestBatchStyle::LitMesh) {
      return MeshVertexLayout::Pos3Norm3Color4U8Uv2;
    }
    if (style == GuestBatchStyle::Skybox) {
      return MeshVertexLayout::Pos3;
    }
    return MeshVertexLayout::Pos3Color4U8;
  }

  static RenderStyleId builtinStyle(GuestBatchStyle style)
  {
    return style == GuestBatchStyle::Shape    ? RenderStyleId::Shape
           : style == GuestBatchStyle::Sprite ? RenderStyleId::Sprite
           : style == GuestBatchStyle::Canvas ? RenderStyleId::Canvas
           : style == GuestBatchStyle::Skybox ? RenderStyleId::Skybox
                                              : RenderStyleId::LitMesh;
  }

  // Depth-tested, line and blend variants derive from the built-in styles,
  // as MeshVisual derives its world styles. Created once per variant.
  // Versions 1 and 2 carry no blend flag and keep the historical defaults.
  bool bindBatchStyle(const GuestBatch& batch)
  {
    const RenderStyleId base = builtinStyle(batch.style);
    const RenderStyle* source = renderer.getStyle(base);
    if (source == nullptr) {
      return false;
    }
    if (batch.style == GuestBatchStyle::Skybox) {
      return renderer.bindStyle(base);
    }
    PipelineState wanted = source->pipeline;
    if (batch.style != GuestBatchStyle::LitMesh) {
      wanted.primitives = batch.primitive == GuestPrimitive::Lines
                            ? Primitives::Lines
                            : Primitives::Triangles;
      if (batch.depthTest) {
        wanted.depthTestEnabled = true;
        wanted.faceCullingEnabled = false;
        wanted.blendEnabled = batch.style == GuestBatchStyle::Sprite;
      }
    }
    if (batch.hasBlend) {
      wanted.blendEnabled = batch.blend;
    }
    const PipelineState& current = source->pipeline;
    if (wanted.primitives == current.primitives &&
        wanted.depthTestEnabled == current.depthTestEnabled &&
        wanted.faceCullingEnabled == current.faceCullingEnabled &&
        wanted.blendEnabled == current.blendEnabled) {
      return renderer.bindStyle(base);
    }
    const std::uint32_t key =
      static_cast<std::uint32_t>(batch.style) * 16u +
      (wanted.primitives == Primitives::Lines ? 8u : 0u) +
      (wanted.depthTestEnabled ? 4u : 0u) +
      (wanted.faceCullingEnabled ? 2u : 0u) + (wanted.blendEnabled ? 1u : 0u);
    std::map<std::uint32_t, RenderStyleHandle>::const_iterator found =
      derivedStyles.find(key);
    if (found == derivedStyles.end()) {
      RenderStyle derived = *source;
      derived.pipeline = wanted;
      const RenderStyleHandle handle = renderer.createStyle(derived);
      if (!handle.isValid()) {
        return false;
      }
      found = derivedStyles.emplace(key, handle).first;
    }
    return renderer.bindStyle(found->second);
  }

  static void appendFloat(std::vector<std::byte>& output, float value)
  {
    const std::size_t offset = output.size();
    output.resize(offset + sizeof(value));
    std::memcpy(output.data() + offset, &value, sizeof(value));
  }

  static glm::mat4 matrix(const std::array<float, 16>& values)
  {
    glm::mat4 result(1.0f);
    std::memcpy(glm::value_ptr(result), values.data(), sizeof(float) * 16);
    return result;
  }

  // Resolves a retained batch against its mesh: ready, same style and an
  // index range inside the mesh.
  bool prepareRetained(const GuestBatch& batch, PreparedBatch& ready)
  {
    ready.retained = retainedMeshes.resolve(batch.mesh);
    if (!ready.retained || !ready.retained->upload->ready ||
        ready.retained->style != batch.style ||
        static_cast<std::uint64_t>(batch.firstIndex) + batch.indexCount >
          ready.retained->upload->indexCount) {
      error = "Invalid, incomplete or mismatched retained guest mesh";
      return false;
    }
    if (batch.style == GuestBatchStyle::LitMesh) {
      const glm::mat4 model = matrix(batch.lighting.model);
      const AxisAlignedBounds3& local = ready.retained->upload->bounds;
      for (unsigned int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point(
          (corner & 1u) != 0 ? local.maximum.x : local.minimum.x,
          (corner & 2u) != 0 ? local.maximum.y : local.minimum.y,
          (corner & 4u) != 0 ? local.maximum.z : local.minimum.z);
        const glm::vec3 placed(model * glm::vec4(point, 1.0f));
        if (corner == 0) {
          ready.worldBounds.minimum = placed;
          ready.worldBounds.maximum = placed;
        } else {
          ready.worldBounds.include(placed);
        }
      }
    }
    return true;
  }

  void prepareInline(const GuestBatch& batch, PreparedBatch& ready)
  {
    const bool lit = batch.style == GuestBatchStyle::LitMesh;
    const bool sky = batch.style == GuestBatchStyle::Skybox;
    const std::size_t stride = batch.style == GuestBatchStyle::Shape    ? 16u
                               : batch.style == GuestBatchStyle::Sprite ? 24u
                               : lit                                    ? 36u
                               : sky                                    ? 12u
                                                                        : 32u;
    ready.vertices.reserve(batch.vertices.size() * stride);
    const glm::mat4 model = matrix(batch.lighting.model);
    for (const GuestVertex& vertex : batch.vertices) {
      for (float value : vertex.position) {
        appendFloat(ready.vertices, value);
      }
      if (sky) {
        continue; // Pos3
      }
      if (lit) {
        // Pos3Norm3Color4U8Uv2, matching MeshVisual's lit vertices.
        for (float value : vertex.normal) {
          appendFloat(ready.vertices, value);
        }
        const glm::vec3 placed(model * glm::vec4(vertex.position[0],
                                                 vertex.position[1],
                                                 vertex.position[2],
                                                 1.0f));
        if (&vertex == &batch.vertices.front()) {
          ready.worldBounds.minimum = placed;
          ready.worldBounds.maximum = placed;
        } else {
          ready.worldBounds.include(placed);
        }
      }
      if (batch.style == GuestBatchStyle::Canvas) {
        for (unsigned int component = 0; component < 3; ++component) {
          appendFloat(
            ready.vertices,
            static_cast<float>((vertex.rgba >> (component * 8u)) & 255u) /
              255.0f);
        }
      } else {
        for (unsigned int component = 0; component < 4; ++component) {
          ready.vertices.push_back(
            static_cast<std::byte>(vertex.rgba >> (component * 8u)));
        }
      }
      if (batch.style != GuestBatchStyle::Shape) {
        for (float value : vertex.uv) {
          appendFloat(ready.vertices, value);
        }
      }
    }
  }

  // Validates one frame mesh write against its dynamic mesh: whole vertices
  // with finite positions (and Canvas colors), or whole indices inside the
  // vertex capacity. Nothing is applied here.
  static bool validMeshWrite(const RetainedMesh& mesh,
                             const GuestFrameMeshWrite& write)
  {
    const RetainedMesh::Upload& upload = *mesh.upload;
    if (!upload.dynamic || !upload.ready) {
      return false;
    }
    const std::size_t size = write.bytes.size();
    if (write.indices) {
      if (write.offset % 4u != 0 || size % 4u != 0 ||
          write.offset > mesh.indexBytes ||
          size > mesh.indexBytes - write.offset) {
        return false;
      }
      const std::uint32_t stride =
        GuestMeshRequest::stride(static_cast<std::uint32_t>(mesh.style));
      const std::uint32_t vertexCount = mesh.vertexBytes / stride;
      for (std::size_t offset = 0; offset < size; offset += 4u) {
        std::uint32_t value = 0;
        std::memcpy(&value, write.bytes.data() + offset, sizeof(value));
        if (value >= vertexCount) {
          return false;
        }
      }
      return true;
    }
    const std::uint32_t stride =
      GuestMeshRequest::stride(static_cast<std::uint32_t>(mesh.style));
    if (write.offset % stride != 0 || size % stride != 0 ||
        write.offset > mesh.vertexBytes ||
        size > mesh.vertexBytes - write.offset) {
      return false;
    }
    // Positions lead every layout; Canvas adds three float colors.
    const std::size_t floats = mesh.style == GuestBatchStyle::Canvas ? 6u : 3u;
    for (std::size_t vertex = 0; vertex < size; vertex += stride) {
      for (std::size_t component = 0; component < floats; ++component) {
        float value = 0.0f;
        std::memcpy(&value,
                    write.bytes.data() + vertex + component * sizeof(float),
                    sizeof(value));
        if (!std::isfinite(value)) {
          return false;
        }
      }
    }
    return true;
  }

  // Copies validated writes into their shadows and records the dirty span.
  void applyMeshWrites(
    const GuestFrame& proposed,
    const std::vector<std::shared_ptr<const RetainedMesh>>& targets)
  {
    for (std::size_t index = 0; index < proposed.meshWrites.size(); ++index) {
      const GuestFrameMeshWrite& write = proposed.meshWrites[index];
      RetainedMesh::Upload& upload = *targets[index]->upload;
      std::vector<std::byte>& shadow =
        write.indices ? upload.indices : upload.vertices;
      std::memcpy(
        shadow.data() + write.offset, write.bytes.data(), write.bytes.size());
      std::uint32_t& begin =
        write.indices ? upload.indexDirtyBegin : upload.vertexDirtyBegin;
      std::uint32_t& end =
        write.indices ? upload.indexDirtyEnd : upload.vertexDirtyEnd;
      const std::uint32_t last =
        write.offset + static_cast<std::uint32_t>(write.bytes.size());
      if (end <= begin) {
        begin = write.offset;
        end = last;
      } else {
        begin = std::min(begin, write.offset);
        end = std::max(end, last);
      }
      queueDirty(targets[index]);
    }
  }

  void queueDirty(const std::shared_ptr<const RetainedMesh>& mesh)
  {
    if (!mesh->upload->queued) {
      mesh->upload->queued = true;
      dirtyMeshes.push_back(mesh);
    }
  }

  bool accept(std::span<const std::byte> packet)
  {
    if (retired || lifetime.expired()) {
      error = "Renderer authority retired";
      return false;
    }
    GuestFrame proposed;
    if (!GuestFrame::read(packet, proposed, limits)) {
      error = "Malformed or over-budget guest frame";
      return false;
    }
    std::vector<std::shared_ptr<const RetainedMesh>> writeTargets;
    writeTargets.reserve(proposed.meshWrites.size());
    for (const GuestFrameMeshWrite& write : proposed.meshWrites) {
      std::shared_ptr<const RetainedMesh> mesh =
        retainedMeshes.resolve(write.mesh);
      if (!mesh || !validMeshWrite(*mesh, write)) {
        error = "Invalid guest mesh write authority, range or contents";
        return false;
      }
      writeTargets.push_back(std::move(mesh));
    }
    std::vector<PreparedBatch> prepared;
    std::vector<std::shared_ptr<const Texture>> preparedWrites;
    for (const GuestTextureWrite& write : proposed.textureWrites) {
      std::shared_ptr<const Texture> texture = textures.resolve(write.texture);
      if (!texture || texture->cubemap || texture->channels != write.channels ||
          write.x > texture->width || write.y > texture->height ||
          write.width > texture->width - write.x ||
          write.height > texture->height - write.y) {
        error = "Invalid guest texture upload authority or region";
        return false;
      }
      preparedWrites.push_back(std::move(texture));
    }
    if (!prepareBatches(proposed.batches, prepared)) {
      return false;
    }
    std::array<std::uint32_t, kStylePools> used{};
    std::vector<Slot> slots = assignSlots(proposed.batches, used);
    // Surfaces (frame v5): unchanged ones keep their content; changed ones
    // are prepared against their own pools, so validation of every surface
    // finishes before anything is mutated.
    struct ProposedSurface
    {
      Surface* existing = nullptr;
      std::vector<PreparedBatch> prepared;
      std::vector<Slot> slots;
      std::array<std::uint32_t, kStylePools> used{};
    };
    std::vector<ProposedSurface> surfacePlans(proposed.surfaces.size());
    std::uint64_t replacementPeak = 0;
    std::uint64_t growth =
      slotGrowth(proposed.batches, prepared, slots, pools, replacementPeak);
    for (std::size_t index = 0; index < proposed.surfaces.size(); ++index) {
      const GuestSurfaceFrame& surface = proposed.surfaces[index];
      ProposedSurface& plan = surfacePlans[index];
      plan.existing = findSurface(surface.surface);
      if (surface.same) {
        if (plan.existing == nullptr ||
            plan.existing->revision != surface.revision) {
          error = "Unchanged guest surface has no matching content";
          return false;
        }
        continue;
      }
      if (plan.existing != nullptr &&
          surface.revision <= plan.existing->revision) {
        error = "Guest surface revision did not advance";
        return false;
      }
      if (!prepareBatches(surface.batches, plan.prepared)) {
        return false;
      }
      plan.slots = assignSlots(surface.batches, plan.used);
      static const std::array<std::vector<Mesh>, kStylePools> empty{};
      growth +=
        slotGrowth(surface.batches,
                   plan.prepared,
                   plan.slots,
                   plan.existing != nullptr ? plan.existing->pools : empty,
                   replacementPeak);
    }
    // Validation/lease acquisition completes before mutating renderer state.
    if (growth > Budget::Maximum - budget->bytes ||
        replacementPeak > Budget::Maximum - budget->bytes - growth) {
      error = "Guest geometry exceeds the resident resource quota";
      return false;
    }
    changingResources = true;
    if (!growSlots(proposed.batches, prepared, slots, used, pools)) {
      renderer.reportFrameError(error);
      frame.batches.clear();
      frame.textureWrites.clear();
      uploadTextures.clear();
      payloads.clear();
      changingResources = false;
      return false;
    }
    std::vector<std::unique_ptr<Surface>> nextSurfaces;
    for (std::size_t index = 0; index < proposed.surfaces.size(); ++index) {
      GuestSurfaceFrame& surface = proposed.surfaces[index];
      ProposedSurface& plan = surfacePlans[index];
      std::unique_ptr<Surface> content = takeSurface(surface.surface);
      if (!content) {
        content = std::make_unique<Surface>();
        content->id = surface.surface;
        content->drawable = std::make_unique<SurfaceLayer>(*this, *content);
      }
      if (!surface.same) {
        if (!growSlots(surface.batches,
                       plan.prepared,
                       plan.slots,
                       plan.used,
                       content->pools)) {
          destroyPools(content->pools);
          for (const std::unique_ptr<Surface>& kept : nextSurfaces) {
            destroyPools(kept->pools);
          }
          for (const std::unique_ptr<Surface>& kept : surfaces) {
            destroyPools(kept->pools);
          }
          nextSurfaces.clear();
          surfaces.clear();
          renderer.reportFrameError(error);
          changingResources = false;
          return false;
        }
        content->width = surface.width;
        content->height = surface.height;
        content->revision = surface.revision;
        content->batches = std::move(surface.batches);
        content->payloads = std::move(plan.prepared);
        content->slots = std::move(plan.slots);
      }
      nextSurfaces.push_back(std::move(content));
    }
    // Surfaces the frame no longer names were closed: free their meshes.
    for (const std::unique_ptr<Surface>& closed : surfaces) {
      destroyPools(closed->pools);
    }
    surfaces = std::move(nextSurfaces);
    applyMeshWrites(proposed, writeTargets);
    frame = std::move(proposed);
    payloads = std::move(prepared);
    batchSlots = std::move(slots);
    uploadTextures = std::move(preparedWrites);
    changingResources = false;
    error.clear();
    count();
    return true;
  }

  void count()
  {
    counters.batches = frame.batches.size();
    counters.retainedBatches = 0;
    counters.inlineVertexBytes = 0;
    counters.inlineIndexBytes = 0;
    for (std::size_t index = 0; index < frame.batches.size(); ++index) {
      if (frame.batches[index].retained()) {
        ++counters.retainedBatches;
        continue;
      }
      counters.inlineVertexBytes += payloads[index].vertices.size();
      counters.inlineIndexBytes +=
        frame.batches[index].indices.size() * sizeof(std::uint32_t);
    }
    counters.textureWrites = frame.textureWrites.size();
    counters.textureWriteBytes = 0;
    for (const GuestTextureWrite& write : frame.textureWrites) {
      counters.textureWriteBytes += write.pixels.size();
    }
    counters.meshWriteBytes = 0;
    for (const GuestFrameMeshWrite& write : frame.meshWrites) {
      counters.meshWriteBytes += write.bytes.size();
    }
  }

  // The mesh a batch draws from: its retained mesh or its per-frame slot.
  MeshHandle batchMesh(std::size_t index) const
  {
    return payloads[index].retained
             ? payloads[index].retained->upload->handle
             : pools[batchSlots[index].pool][batchSlots[index].index].handle;
  }

  // Uploads the dirty spans of dynamic retained meshes from their shadows.
  // The meshes stay referenced until the next upload, after the synchronous
  // submission that reads these payload pointers.
  bool uploadDynamicMeshes()
  {
    uploadingMeshes.clear();
    uploadingMeshes.swap(dirtyMeshes);
    bool uploaded = true;
    for (const std::shared_ptr<const RetainedMesh>& mesh : uploadingMeshes) {
      RetainedMesh::Upload& upload = *mesh->upload;
      upload.queued = false;
      if (upload.vertexDirtyEnd > upload.vertexDirtyBegin) {
        uploaded = renderer.pushUpdateBuffer(
                     upload.handle,
                     upload.vertexDirtyBegin,
                     upload.vertexDirtyEnd - upload.vertexDirtyBegin,
                     upload.vertices.data() + upload.vertexDirtyBegin) &&
                   uploaded;
      }
      if (upload.indexDirtyEnd > upload.indexDirtyBegin) {
        uploaded = renderer.pushUpdateIndexBuffer(
                     upload.handle,
                     upload.indexDirtyBegin,
                     upload.indexDirtyEnd - upload.indexDirtyBegin,
                     upload.indices.data() + upload.indexDirtyBegin) &&
                   uploaded;
      }
      upload.vertexDirtyBegin = upload.vertexDirtyEnd = 0;
      upload.indexDirtyBegin = upload.indexDirtyEnd = 0;
    }
    return uploaded;
  }

  void appendWrites(std::size_t beforeBatch)
  {
    while (nextWrite < frame.textureWrites.size() &&
           frame.textureWrites[nextWrite].beforeBatch <= beforeBatch) {
      const GuestTextureWrite& write = frame.textureWrites[nextWrite];
      renderer.pushUpdateTexture(uploadTextures[nextWrite]->handle,
                                 static_cast<int>(write.x),
                                 static_cast<int>(write.y),
                                 static_cast<int>(write.width),
                                 static_cast<int>(write.height),
                                 static_cast<int>(write.channels),
                                 write.pixels.data());
      ++nextWrite;
    }
  }

  // Geometry uploads precede the first pass that draws it: the shared shadow
  // pass runs before the world color pass. Uploaded once per RenderScene.
  bool ensureUploads()
  {
    const Renderer::FrameContext& context = renderer.getFrameContext();
    if (context.active && uploadedSerial == context.frameSerial) {
      return true;
    }
    uploadedSerial = context.active ? context.frameSerial : 0;
    if (!uploadDynamicMeshes()) {
      renderer.reportFrameError("Guest mesh write upload rejected");
      return false;
    }
    for (std::size_t index = 0; index < frame.batches.size(); ++index) {
      const GuestBatch& batch = frame.batches[index];
      if (batch.retained()) {
        continue;
      }
      if (!renderer.pushUpdateBuffer(
            batchMesh(index),
            0,
            static_cast<unsigned int>(payloads[index].vertices.size()),
            payloads[index].vertices.data()) ||
          !renderer.pushUpdateIndexBuffer(
            batchMesh(index),
            0,
            static_cast<unsigned int>(batch.indices.size() *
                                      sizeof(std::uint32_t)),
            batch.indices.data())) {
        renderer.reportFrameError("Guest frame upload rejected");
        return false;
      }
    }
    return true;
  }

  void collectShadowCasters(Renderer* target)
  {
    if (target != &renderer || lifetime.expired()) {
      return;
    }
    for (const GuestShadowCaster& caster : frame.shadowCasters) {
      Renderer::ShadowCasterDesc desc;
      desc.boundsMin = caster.boundsMin;
      desc.boundsMax = caster.boundsMax;
      desc.lightDirection = caster.lightDirection;
      desc.mapSize = static_cast<int>(caster.mapSize);
      desc.minimumRadius = caster.minimumRadius;
      desc.lightDistance = caster.lightDistance;
      desc.casterDistance = caster.casterDistance;
      renderer.registerShadowCaster(desc);
    }
  }

  void appendShadowCommands(Renderer* target)
  {
    const Renderer::ShadowFrameContext& shadow =
      renderer.getShadowFrameContext();
    if (target != &renderer || lifetime.expired() || !shadow.active ||
        !ensureUploads()) {
      return;
    }
    glm::mat4 lightSpace(1.0f);
    std::memcpy(glm::value_ptr(lightSpace),
                shadow.lightSpaceMatrix.data(),
                shadow.lightSpaceMatrix.size() * sizeof(float));
    for (std::size_t index = 0; index < frame.batches.size(); ++index) {
      const GuestBatch& batch = frame.batches[index];
      if (batch.style != GuestBatchStyle::LitMesh ||
          !batch.lighting.castsShadow ||
          !renderer.isShadowCasterRelevant(payloads[index].worldBounds)) {
        continue;
      }
      const glm::mat4 lightMvp = lightSpace * matrix(batch.lighting.model);
      renderer.pushUniformMat4(WorldLook::kMvpUniform,
                               glm::value_ptr(lightMvp));
      renderer.pushSetMesh(batchMesh(index));
      renderer.pushDrawIndexed(batch.drawCount(), batch.firstIndex);
    }
  }

  // Mirrors MeshVisual's lit draw; the light space, shadow map and resolved
  // light direction come from the host's shared shadow pass.
  void appendLitMesh(const GuestBatch& batch, MeshHandle mesh)
  {
    const Renderer::ShadowFrameContext& shadow =
      renderer.getShadowFrameContext();
    const GuestLighting& lighting = batch.lighting;
    const bool shadowed =
      lighting.receivesShadow && shadow.active && shadow.depthTexture.isValid();
    static const std::array<float, 16> identity{ 1, 0, 0, 0, 0, 1, 0, 0,
                                                 0, 0, 1, 0, 0, 0, 0, 1 };
    const std::array<float, 3>& direction =
      shadowed ? shadow.lightDirection : lighting.lightDirection;
    renderer.pushSetMesh(mesh);
    renderer.pushUniformMat4(WorldLook::kMvpUniform, batch.mvp.data());
    renderer.pushUniformMat4(WorldLook::kModelUniform, lighting.model.data());
    renderer.pushUniformMat4(WorldLook::kLightSpaceMatrixUniform,
                             shadowed ? shadow.lightSpaceMatrix.data()
                                      : identity.data());
    renderer.pushUniformVec3(
      WorldLook::kLightDirUniform, direction[0], direction[1], direction[2]);
    renderer.pushUniformVec3(WorldLook::kLightColorUniform,
                             lighting.lightColor[0],
                             lighting.lightColor[1],
                             lighting.lightColor[2]);
    renderer.pushUniformVec3(WorldLook::kAmbientColorUniform,
                             lighting.ambientColor[0],
                             lighting.ambientColor[1],
                             lighting.ambientColor[2]);
    renderer.pushUniformInt(WorldLook::kShadowsEnabledUniform,
                            shadowed ? 1 : 0);
    renderer.pushUniformFloat(WorldLook::kShadowBiasUniform,
                              lighting.shadowBias);
    renderer.pushUniformFloat(WorldLook::kShadowSlopeScaleUniform,
                              lighting.shadowSlopeScale);
    renderer.pushUniformFloat(WorldLook::kShadowNormalOffsetUniform,
                              lighting.shadowNormalOffset);
    renderer.pushUniformInt(WorldLook::kShadowPcfUniform,
                            lighting.shadowPcf ? 1 : 0);
    renderer.pushUniformMat4(WorldLook::kPrevMvpUniform, batch.mvp.data());
    renderer.pushUniformInt(WorldLook::kMotionBlurEnabledUniform, 0);
    renderer.pushUniformFloat(WorldLook::kMotionBlurAmountUniform, 0.0f);
    renderer.pushUniformFloat(WorldLook::kMotionBlurMaxUniform, 0.0f);
    renderer.pushUniformVec4(WorldLook::kTintUniform,
                             lighting.tint[0],
                             lighting.tint[1],
                             lighting.tint[2],
                             lighting.tint[3]);
    if (shadowed) {
      renderer.pushSetTexture(shadow.depthTexture,
                              WorldLook::kShadowTextureUnit);
      renderer.pushUniformInt(WorldLook::kShadowMapUniform,
                              WorldLook::kShadowTextureUnit);
    }
    renderer.pushDrawIndexed(batch.drawCount(), batch.firstIndex);
  }

  // Mirrors SkyboxVisual: the rotation-only view projection comes from the
  // guest; the cubemap is a host resource the guest acquired.
  void appendSkybox(const GuestBatch& batch,
                    const PreparedBatch& payload,
                    MeshHandle mesh)
  {
    const std::array<float, 4>& tint = batch.lighting.tint;
    renderer.pushSetTexture(payload.texture->handle, 0);
    renderer.pushUniformInt("uSkybox", 0);
    renderer.pushUniformVec4("uTint", tint[0], tint[1], tint[2], tint[3]);
    renderer.pushUniformMat4("uViewProjection", batch.mvp.data());
    renderer.pushSetMesh(mesh);
    renderer.pushDrawIndexed(batch.drawCount(), batch.firstIndex);
  }

  // A Shape, Sprite or Canvas batch in a space of width x height logical
  // units; clips scale into the current pass viewport.
  void appendFlat(const GuestBatch& batch,
                  const PreparedBatch& payload,
                  MeshHandle mesh,
                  float width,
                  float height)
  {
    if (batch.clipped) {
      const std::array<int, 4> viewport = renderer.getCurrentPassViewport();
      const double left =
        std::clamp(static_cast<double>(batch.clip[0]) / width, 0.0, 1.0);
      const double right = std::clamp(
        static_cast<double>(batch.clip[0] + batch.clip[2]) / width, left, 1.0);
      const double top =
        std::clamp(static_cast<double>(batch.clip[1]) / height, 0.0, 1.0);
      const double bottom = std::clamp(
        static_cast<double>(batch.clip[1] + batch.clip[3]) / height, top, 1.0);
      const int x0 = static_cast<int>(std::floor(left * viewport[2]));
      const int x1 = static_cast<int>(std::ceil(right * viewport[2]));
      const int y0 = static_cast<int>(std::floor(top * viewport[3]));
      const int y1 = static_cast<int>(std::ceil(bottom * viewport[3]));
      renderer.pushClipRect(
        viewport[0] + x0, viewport[1] + viewport[3] - y1, x1 - x0, y1 - y0);
    }
    renderer.pushSetMesh(mesh);
    renderer.pushUniformVec2(WorldLook::kResolutionUniform, width, height);
    renderer.pushUniformMat4(WorldLook::kMvpUniform, batch.mvp.data());
    if (payload.texture) {
      renderer.pushUniformInt(WorldLook::kTextureUniform, 0);
      renderer.pushSetTexture(payload.texture->handle, 0);
    }
    renderer.pushDrawIndexed(batch.drawCount(), batch.firstIndex);
    if (batch.clipped) {
      renderer.popClipRect();
    }
  }

  // Replays one surface (renderOffscreen into its window's target). Inline
  // geometry uploads into the surface's own pool meshes first.
  bool appendSurface(Renderer* target, Surface& surface)
  {
    if (target != &renderer || lifetime.expired() || retired) {
      return true;
    }
    for (std::size_t index = 0; index < surface.batches.size(); ++index) {
      const GuestBatch& batch = surface.batches[index];
      if (batch.retained()) {
        continue;
      }
      const MeshHandle mesh =
        surface.pools[surface.slots[index].pool][surface.slots[index].index]
          .handle;
      if (!renderer.pushUpdateBuffer(
            mesh,
            0,
            static_cast<unsigned int>(surface.payloads[index].vertices.size()),
            surface.payloads[index].vertices.data()) ||
          !renderer.pushUpdateIndexBuffer(
            mesh,
            0,
            static_cast<unsigned int>(batch.indices.size() *
                                      sizeof(std::uint32_t)),
            batch.indices.data())) {
        renderer.reportFrameError("Guest surface upload rejected");
        return true;
      }
    }
    for (std::size_t index = 0; index < surface.batches.size(); ++index) {
      const GuestBatch& batch = surface.batches[index];
      const PreparedBatch& payload = surface.payloads[index];
      const MeshHandle mesh =
        payload.retained
          ? payload.retained->upload->handle
          : surface.pools[surface.slots[index].pool][surface.slots[index].index]
              .handle;
      if (!bindBatchStyle(batch)) {
        renderer.reportFrameError("Guest surface style rejected");
        return true;
      }
      appendFlat(batch, payload, mesh, surface.width, surface.height);
    }
    return true;
  }

  bool append(Renderer* target, GuestLayer layer)
  {
    if (target != &renderer || lifetime.expired()) {
      return true;
    }
    if (layer == GuestLayer::World) {
      nextWrite = 0;
    }
    if (!ensureUploads()) {
      return true;
    }
    for (std::size_t index = 0; index < frame.batches.size(); ++index) {
      const GuestBatch& batch = frame.batches[index];
      if (batch.layer != layer) {
        continue;
      }
      appendWrites(index);
      const PreparedBatch& payload = payloads[index];
      const MeshHandle mesh = batchMesh(index);
      if (!bindBatchStyle(batch)) {
        renderer.reportFrameError("Guest frame style rejected");
        return true;
      }
      if (batch.style == GuestBatchStyle::LitMesh) {
        appendLitMesh(batch, mesh);
        continue;
      }
      if (batch.style == GuestBatchStyle::Skybox) {
        appendSkybox(batch, payload, mesh);
        continue;
      }
      appendFlat(batch, payload, mesh, frame.width, frame.height);
    }
    if (layer == GuestLayer::Ui) {
      appendWrites(frame.batches.size());
    }
    return true;
  }

  // Validates the complete mesh and enrolls it as a static GPU mesh. Staging
  // memory is released either way.
  bool finalizeMesh(const RetainedMesh& mesh)
  {
    RetainedMesh::Upload& upload = *mesh.upload;
    const std::uint32_t stride =
      GuestMeshRequest::stride(static_cast<std::uint32_t>(mesh.style));
    const std::uint32_t vertexCount = mesh.vertexBytes / stride;
    upload.indexCount = mesh.indexBytes / 4u;
    bool valid = vertexCount > 0 && upload.indexCount > 0;
    for (std::uint32_t vertex = 0; valid && vertex < vertexCount; ++vertex) {
      std::array<float, 3> position{};
      std::memcpy(position.data(),
                  upload.vertices.data() +
                    static_cast<std::size_t>(vertex) * stride,
                  sizeof(position));
      for (float value : position) {
        valid = valid && std::isfinite(value);
      }
      const glm::vec3 point(position[0], position[1], position[2]);
      if (vertex == 0) {
        upload.bounds.minimum = point;
        upload.bounds.maximum = point;
      } else {
        upload.bounds.include(point);
      }
    }
    for (std::uint32_t index = 0; valid && index < upload.indexCount; ++index) {
      std::uint32_t value = 0;
      std::memcpy(&value,
                  upload.indices.data() + static_cast<std::size_t>(index) * 4,
                  sizeof(value));
      valid = value < vertexCount;
    }
    if (valid) {
      upload.handle = renderer.enrollMesh(upload.vertices.data(),
                                          upload.vertices.size(),
                                          upload.indices.data(),
                                          upload.indices.size(),
                                          layout(mesh.style),
                                          false);
      valid = upload.handle.isValid();
    }
    upload.vertices.clear();
    upload.vertices.shrink_to_fit();
    upload.indices.clear();
    upload.indices.shrink_to_fit();
    upload.ready = valid;
    upload.failed = !valid;
    return valid;
  }

  Renderer& renderer;
  std::weak_ptr<const void> lifetime;
  GuestFrameLimits limits;
  std::shared_ptr<Budget> budget = std::make_shared<Budget>();
  WasmResourceTable<Texture, GuestResourceKind::Texture> textures;
  WasmResourceTable<RetainedMesh, GuestResourceKind::Mesh> retainedMeshes;
  std::array<std::vector<Mesh>, kStylePools> pools;
  std::vector<Slot> batchSlots;
  std::vector<std::unique_ptr<Surface>> surfaces;
  // Dynamic retained meshes with written spans awaiting upload, and those
  // whose upload tokens the current submission still reads.
  std::vector<std::shared_ptr<const RetainedMesh>> dirtyMeshes;
  std::vector<std::shared_ptr<const RetainedMesh>> uploadingMeshes;
  GuestFrame frame;
  std::vector<PreparedBatch> payloads;
  std::vector<std::shared_ptr<const Texture>> uploadTextures;
  std::size_t nextWrite = 0;
  std::uint64_t uploadedSerial = 0;
  std::map<std::uint32_t, RenderStyleHandle> derivedStyles;
  std::string error;
  WasmFrameCounters counters;
  bool retired = false;
  bool changingResources = false;
  Layer world;
  Layer ui;
};

WasmFrameRenderer::WasmFrameRenderer(Renderer& renderer,
                                     std::uint64_t owner,
                                     GuestFrameLimits limits)
  : m_state(std::make_unique<State>(renderer, owner, limits))
{
}
WasmFrameRenderer::~WasmFrameRenderer() = default;

GuestResourceId
WasmFrameRenderer::createTexture(std::span<const std::byte> pixels,
                                 std::uint32_t width,
                                 std::uint32_t height,
                                 std::uint32_t channels,
                                 bool linear)
try {
  State& state = *m_state;
  const std::uint64_t bytes =
    static_cast<std::uint64_t>(width) * height * channels;
  if (state.retired || state.lifetime.expired() || width == 0 || height == 0 ||
      width > 8192 || height > 8192 ||
      (channels != 1 && channels != 3 && channels != 4) ||
      pixels.size() != bytes ||
      bytes > State::Budget::Maximum - state.budget->bytes ||
      !state.textures.hasCapacity()) {
    state.error = "Invalid or over-budget guest texture";
    return {};
  }
  std::shared_ptr<State::Texture> texture =
    std::make_shared<State::Texture>(state.renderer, state.budget, bytes);
  texture->width = width;
  texture->height = height;
  texture->channels = channels;
  TextureOptions options;
  options.filter = linear ? TextureFilter::Linear : TextureFilter::Nearest;
  texture->handle = state.renderer.enrollTexture(
    reinterpret_cast<const unsigned char*>(pixels.data()),
    static_cast<int>(width),
    static_cast<int>(height),
    static_cast<int>(channels),
    options);
  if (!texture->handle.isValid()) {
    state.error = "Guest texture allocation failed";
    return {};
  }
  return state.textures.insert(std::move(texture));
} catch (const std::exception& exception) {
  m_state->error = exception.what();
  return {};
}

GuestResourceId
WasmFrameRenderer::createCubemap(std::span<const std::byte> faces,
                                 std::uint32_t size)
try {
  State& state = *m_state;
  const std::uint64_t bytes = GuestCubemapRequest::bytesFor(size);
  if (state.retired || state.lifetime.expired() || size == 0 || size > 2048 ||
      faces.size() != bytes ||
      bytes > State::Budget::Maximum - state.budget->bytes ||
      !state.textures.hasCapacity()) {
    state.error = "Invalid or over-budget guest cubemap";
    return {};
  }
  std::shared_ptr<State::Texture> cubemap =
    std::make_shared<State::Texture>(state.renderer, state.budget, bytes);
  cubemap->width = size;
  cubemap->height = size;
  cubemap->channels = 4;
  cubemap->cubemap = true;
  const std::size_t face = static_cast<std::size_t>(bytes / 6u);
  std::array<const unsigned char*, 6> pointers{};
  for (std::size_t index = 0; index < pointers.size(); ++index) {
    pointers[index] =
      reinterpret_cast<const unsigned char*>(faces.data() + index * face);
  }
  cubemap->handle = state.renderer.enrollCubemap(
    pointers, static_cast<int>(size), static_cast<int>(size), 4);
  if (!cubemap->handle.isValid()) {
    state.error = "Guest cubemap allocation failed";
    return {};
  }
  return state.textures.insert(std::move(cubemap));
} catch (const std::exception& exception) {
  m_state->error = exception.what();
  return {};
}

GuestResourceId
WasmFrameRenderer::createMesh(const GuestMeshRequest& request)
try {
  State& state = *m_state;
  const std::uint64_t bytes =
    static_cast<std::uint64_t>(request.vertexBytes) + request.indexBytes;
  if (state.retired || state.lifetime.expired() ||
      bytes > State::Budget::Maximum - state.budget->bytes ||
      !state.retainedMeshes.hasCapacity()) {
    state.error = "Invalid or over-budget retained guest mesh";
    return {};
  }
  std::shared_ptr<State::RetainedMesh> mesh =
    std::make_shared<State::RetainedMesh>(
      state.renderer, state.budget, request);
  mesh->upload->vertices.resize(request.vertexBytes);
  mesh->upload->indices.resize(request.indexBytes);
  if (request.dynamic) {
    // Writable in place from the first frame: zero-filled, so every index
    // (zero) is inside the vertex capacity. The zeroed vertices upload with
    // the next frame's dirty spans.
    State::RetainedMesh::Upload& upload = *mesh->upload;
    upload.dynamic = true;
    upload.handle = state.renderer.enrollDynamicMesh(
      request.vertexBytes,
      upload.indices.data(),
      request.indexBytes,
      State::layout(static_cast<GuestBatchStyle>(request.style)));
    if (!upload.handle.isValid()) {
      state.error = "Dynamic guest mesh allocation failed";
      return {};
    }
    upload.indexCount = request.indexBytes / 4u;
    upload.ready = true;
    upload.vertexDirtyEnd = request.vertexBytes;
    const GuestResourceId id = state.retainedMeshes.insert(mesh);
    if (id.owner != 0) {
      state.queueDirty(mesh);
    }
    return id;
  }
  return state.retainedMeshes.insert(std::move(mesh));
} catch (const std::exception& exception) {
  m_state->error = exception.what();
  return {};
}

bool
WasmFrameRenderer::writeMesh(const GuestMeshWrite& write)
try {
  State& state = *m_state;
  const std::shared_ptr<const State::RetainedMesh> mesh =
    state.retainedMeshes.resolve(write.mesh);
  // Dynamic meshes are written only through frame mesh writes.
  if (!mesh || mesh->upload->ready || mesh->upload->failed ||
      mesh->upload->dynamic) {
    return false;
  }
  State::RetainedMesh::Upload& upload = *mesh->upload;
  std::vector<std::byte>& target =
    write.indices ? upload.indices : upload.vertices;
  std::uint32_t& written =
    write.indices ? upload.indexWritten : upload.vertexWritten;
  // Bytes arrive strictly in order, so completion is a simple count.
  if (write.offset != written || write.bytes.size() > target.size() - written) {
    upload.failed = true;
    return false;
  }
  std::memcpy(target.data() + written, write.bytes.data(), write.bytes.size());
  written += static_cast<std::uint32_t>(write.bytes.size());
  if (upload.vertexWritten == mesh->vertexBytes &&
      upload.indexWritten == mesh->indexBytes) {
    return state.finalizeMesh(*mesh);
  }
  return true;
} catch (const std::exception& exception) {
  m_state->error = exception.what();
  return false;
}

bool
WasmFrameRenderer::releaseMesh(const GuestResourceId& id)
{
  return m_state->retainedMeshes.release(id);
}

bool
WasmFrameRenderer::releaseTexture(const GuestResourceId& id)
{
  return m_state->textures.release(id);
}
bool
WasmFrameRenderer::accept(std::span<const std::byte> packet)
try {
  return m_state->accept(packet);
} catch (const std::exception& exception) {
  m_state->error = exception.what();
  if (m_state->changingResources) {
    m_state->frame.batches.clear();
    m_state->frame.textureWrites.clear();
    m_state->uploadTextures.clear();
    m_state->payloads.clear();
    m_state->changingResources = false;
    m_state->renderer.reportFrameError(m_state->error);
  }
  return false;
}
void
WasmFrameRenderer::dispatch(Scene& scene)
{
  // Shadow fitting and world-camera consumers follow the guest's camera.
  if (m_state->frame.hasCamera && !m_state->retired &&
      !m_state->lifetime.expired()) {
    m_state->renderer.setNextWorldViewProjection(m_state->frame.camera);
  }
  scene.AddDrawable(&m_state->world, RenderLayerId::World);
  scene.AddDrawable(&m_state->ui, RenderLayerId::UI);
}
std::vector<WasmSurfaceContent>
WasmFrameRenderer::surfaces() const
{
  std::vector<WasmSurfaceContent> result;
  for (const std::unique_ptr<State::Surface>& surface : m_state->surfaces) {
    result.push_back(
      { surface->id, surface->width, surface->height, surface->revision });
  }
  return result;
}
DrawableBase*
WasmFrameRenderer::surfaceDrawable(std::uint32_t surface)
{
  State::Surface* found = m_state->findSurface(surface);
  return found != nullptr ? found->drawable.get() : nullptr;
}
void
WasmFrameRenderer::retire()
{
  m_state->retired = true;
  m_state->textures.retire();
  m_state->retainedMeshes.retire();
  m_state->dirtyMeshes.clear();
}
const std::string&
WasmFrameRenderer::error() const
{
  return m_state->error;
}
const WasmFrameCounters&
WasmFrameRenderer::counters() const
{
  return m_state->counters;
}
