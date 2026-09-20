#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmResourceTable.h>

#include <algorithm>
#include <cstring>

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
    TextureHandle handle{};
  };
  struct PreparedBatch
  {
    std::vector<std::byte> vertices;
    std::shared_ptr<const Texture> texture;
  };
  struct Mesh
  {
    MeshHandle handle{};
    MeshVertexLayout layout = MeshVertexLayout::Pos3Color4U8;
    std::size_t vertexCapacity = 0;
    std::size_t indexCapacity = 0;
  };
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
    State& state;
    GuestLayer layer;
  };
  State(Renderer& value, std::uint64_t owner, GuestFrameLimits quotas)
    : renderer(value)
    , lifetime(value.getLifetimeIdentity())
    , limits(quotas)
    , textures(owner)
    , world(*this, GuestLayer::World)
    , ui(*this, GuestLayer::Ui)
  {
  }
  ~State()
  {
    if (!lifetime.expired()) {
      for (const Mesh& mesh : meshes) {
        if (mesh.handle.isValid()) {
          renderer.destroyMesh(mesh.handle);
        }
      }
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  static MeshVertexLayout layout(GuestBatchStyle style)
  {
    if (style == GuestBatchStyle::Sprite) {
      return MeshVertexLayout::Pos3Color4U8Uv2;
    }
    if (style == GuestBatchStyle::Canvas) {
      return MeshVertexLayout::Pos3Color3Uv2;
    }
    return MeshVertexLayout::Pos3Color4U8;
  }

  static void appendFloat(std::vector<std::byte>& output, float value)
  {
    const std::size_t offset = output.size();
    output.resize(offset + sizeof(value));
    std::memcpy(output.data() + offset, &value, sizeof(value));
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
    std::vector<PreparedBatch> prepared(proposed.batches.size());
    std::vector<std::shared_ptr<const Texture>> preparedWrites;
    for (const GuestTextureWrite& write : proposed.textureWrites) {
      std::shared_ptr<const Texture> texture = textures.resolve(write.texture);
      if (!texture || texture->channels != write.channels ||
          write.x > texture->width || write.y > texture->height ||
          write.width > texture->width - write.x ||
          write.height > texture->height - write.y) {
        error = "Invalid guest texture upload authority or region";
        return false;
      }
      preparedWrites.push_back(std::move(texture));
    }
    for (std::size_t index = 0; index < proposed.batches.size(); ++index) {
      const GuestBatch& batch = proposed.batches[index];
      PreparedBatch& ready = prepared[index];
      if (batch.style != GuestBatchStyle::Shape) {
        ready.texture = textures.resolve(batch.texture);
        if (!ready.texture) {
          error = "Invalid guest texture authority";
          return false;
        }
      }
      const std::size_t stride = batch.style == GuestBatchStyle::Shape    ? 16u
                                 : batch.style == GuestBatchStyle::Sprite ? 24u
                                                                          : 32u;
      ready.vertices.reserve(batch.vertices.size() * stride);
      for (const GuestVertex& vertex : batch.vertices) {
        for (float value : vertex.position) {
          appendFloat(ready.vertices, value);
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
    // Validation/lease acquisition completes before mutating renderer state.
    std::uint64_t growth = 0;
    std::uint64_t replacementPeak = 0;
    for (std::size_t index = 0; index < proposed.batches.size(); ++index) {
      const std::size_t vertices = prepared[index].vertices.size();
      const std::size_t indices =
        proposed.batches[index].indices.size() * sizeof(std::uint32_t);
      if (index < meshes.size()) {
        growth += std::max(meshes[index].vertexCapacity, vertices) -
                  meshes[index].vertexCapacity;
        growth += std::max(meshes[index].indexCapacity, indices) -
                  meshes[index].indexCapacity;
        if (meshes[index].layout != layout(proposed.batches[index].style) ||
            meshes[index].vertexCapacity < vertices ||
            meshes[index].indexCapacity < indices) {
          replacementPeak =
            std::max(replacementPeak,
                     static_cast<std::uint64_t>(
                       std::max(meshes[index].vertexCapacity, vertices) +
                       std::max(meshes[index].indexCapacity, indices)));
        }
      } else {
        growth += vertices + indices;
      }
    }
    if (growth > Budget::Maximum - budget->bytes ||
        replacementPeak > Budget::Maximum - budget->bytes - growth) {
      error = "Guest geometry exceeds the resident resource quota";
      return false;
    }
    meshes.resize(std::max(meshes.size(), proposed.batches.size()));
    changingResources = true;
    for (std::size_t index = 0; index < proposed.batches.size(); ++index) {
      Mesh& mesh = meshes[index];
      const MeshVertexLayout requested = layout(proposed.batches[index].style);
      const std::size_t vertexBytes = prepared[index].vertices.size();
      const std::size_t indexBytes =
        proposed.batches[index].indices.size() * sizeof(std::uint32_t);
      if (!mesh.handle.isValid() || mesh.layout != requested ||
          mesh.vertexCapacity < vertexBytes ||
          mesh.indexCapacity < indexBytes) {
        const std::size_t vertexCapacity =
          std::max(mesh.vertexCapacity, vertexBytes);
        const std::size_t indexCapacity =
          std::max(mesh.indexCapacity, indexBytes);
        bool ready = true;
        if (mesh.handle.isValid()) {
          ready = renderer.replaceDynamicMesh(
            mesh.handle, vertexCapacity, nullptr, indexCapacity, requested);
        } else {
          mesh.handle = renderer.enrollDynamicMesh(
            vertexCapacity, nullptr, indexCapacity, requested);
          ready = mesh.handle.isValid();
        }
        if (!ready) {
          error = "Guest frame mesh allocation failed";
          renderer.reportFrameError(error);
          frame.batches.clear();
          frame.textureWrites.clear();
          uploadTextures.clear();
          payloads.clear();
          changingResources = false;
          return false;
        }
        mesh.layout = requested;
        budget->bytes += vertexCapacity - mesh.vertexCapacity + indexCapacity -
                         mesh.indexCapacity;
        mesh.vertexCapacity = vertexCapacity;
        mesh.indexCapacity = indexCapacity;
      }
    }
    frame = std::move(proposed);
    payloads = std::move(prepared);
    uploadTextures = std::move(preparedWrites);
    changingResources = false;
    error.clear();
    return true;
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

  bool append(Renderer* target, GuestLayer layer)
  {
    if (target != &renderer || lifetime.expired()) {
      return true;
    }
    if (layer == GuestLayer::World) {
      nextWrite = 0;
    }
    for (std::size_t index = 0; index < frame.batches.size(); ++index) {
      const GuestBatch& batch = frame.batches[index];
      if (batch.layer != layer) {
        continue;
      }
      appendWrites(index);
      const PreparedBatch& payload = payloads[index];
      const Mesh& mesh = meshes[index];
      const RenderStyleId style =
        batch.style == GuestBatchStyle::Shape    ? RenderStyleId::Shape
        : batch.style == GuestBatchStyle::Sprite ? RenderStyleId::Sprite
                                                 : RenderStyleId::Canvas;
      if (!renderer.pushUpdateBuffer(
            mesh.handle,
            0,
            static_cast<unsigned int>(payload.vertices.size()),
            payload.vertices.data()) ||
          !renderer.pushUpdateIndexBuffer(
            mesh.handle,
            0,
            static_cast<unsigned int>(batch.indices.size() *
                                      sizeof(std::uint32_t)),
            batch.indices.data()) ||
          !renderer.bindStyle(style)) {
        renderer.reportFrameError("Guest frame upload or style rejected");
        return true;
      }
      if (batch.clipped) {
        const std::array<int, 4> viewport = renderer.getCurrentPassViewport();
        const double left = std::clamp(
          static_cast<double>(batch.clip[0]) / frame.width, 0.0, 1.0);
        const double right = std::clamp(
          static_cast<double>(batch.clip[0] + batch.clip[2]) / frame.width,
          left,
          1.0);
        const double top = std::clamp(
          static_cast<double>(batch.clip[1]) / frame.height, 0.0, 1.0);
        const double bottom = std::clamp(
          static_cast<double>(batch.clip[1] + batch.clip[3]) / frame.height,
          top,
          1.0);
        const int x0 = static_cast<int>(std::floor(left * viewport[2]));
        const int x1 = static_cast<int>(std::ceil(right * viewport[2]));
        const int y0 = static_cast<int>(std::floor(top * viewport[3]));
        const int y1 = static_cast<int>(std::ceil(bottom * viewport[3]));
        renderer.pushClipRect(
          viewport[0] + x0, viewport[1] + viewport[3] - y1, x1 - x0, y1 - y0);
      }
      renderer.pushSetMesh(mesh.handle);
      renderer.pushUniformVec2(
        WorldLook::kResolutionUniform, frame.width, frame.height);
      renderer.pushUniformMat4(WorldLook::kMvpUniform, batch.mvp.data());
      if (payload.texture) {
        renderer.pushUniformInt(WorldLook::kTextureUniform, 0);
        renderer.pushSetTexture(payload.texture->handle, 0);
      }
      renderer.pushDrawIndexed(static_cast<unsigned int>(batch.indices.size()));
      if (batch.clipped) {
        renderer.popClipRect();
      }
    }
    if (layer == GuestLayer::Ui) {
      appendWrites(frame.batches.size());
    }
    return true;
  }

  Renderer& renderer;
  std::weak_ptr<const void> lifetime;
  GuestFrameLimits limits;
  std::shared_ptr<Budget> budget = std::make_shared<Budget>();
  WasmResourceTable<Texture, GuestResourceKind::Texture> textures;
  std::vector<Mesh> meshes;
  GuestFrame frame;
  std::vector<PreparedBatch> payloads;
  std::vector<std::shared_ptr<const Texture>> uploadTextures;
  std::size_t nextWrite = 0;
  std::string error;
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
  scene.AddDrawable(&m_state->world, RenderLayerId::World);
  scene.AddDrawable(&m_state->ui, RenderLayerId::UI);
}
void
WasmFrameRenderer::retire()
{
  m_state->retired = true;
  m_state->textures.retire();
}
const std::string&
WasmFrameRenderer::error() const
{
  return m_state->error;
}
