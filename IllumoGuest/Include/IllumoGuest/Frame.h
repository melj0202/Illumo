#pragma once

#include <IllumoGuest/ResourceId.h>
#include <array>
#include <cmath>

enum class GuestBatchStyle : std::uint32_t
{
  Shape = 1,
  Sprite = 2,
  Canvas = 3,
  // Version 2: world mesh with normals, lit by the host's shared light and
  // shadow pass. Carries a GuestLighting record.
  LitMesh = 4,
  // Version 3: world cube sampled from a cubemap texture with the built-in
  // skybox style. Carries the tint; mvp is the rotation-only view projection.
  Skybox = 5
};

enum class GuestLayer : std::uint32_t
{
  World = 1,
  Ui = 2
};

// Version 2. Lines are valid only for Shape batches.
enum class GuestPrimitive : std::uint32_t
{
  Triangles = 1,
  Lines = 2
};

struct GuestVertex
{
  std::array<float, 3> position{};
  std::uint32_t rgba = UINT32_MAX;
  std::array<float, 2> uv{};
  std::array<float, 3> normal{}; // LitMesh only
};

// Product-chosen lighting for one LitMesh batch. The host supplies the light
// space and shadow map from its own shared pass; these values mirror the
// MeshVisual uniforms that do not depend on that pass.
struct GuestLighting
{
  std::array<float, 16> model{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
  std::array<float, 3> lightColor{ 1.0f, 1.0f, 1.0f };
  std::array<float, 3> ambientColor{ 0.2f, 0.2f, 0.2f };
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
  float shadowBias = 0.0f;
  float shadowSlopeScale = 0.0f;
  float shadowNormalOffset = 0.0f;
  bool shadowPcf = false;
  bool receivesShadow = false;
  bool castsShadow = false;
};

struct GuestBatch
{
  GuestBatchStyle style = GuestBatchStyle::Shape;
  GuestLayer layer = GuestLayer::Ui;
  GuestResourceId texture;
  std::array<float, 16> mvp{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  bool clipped = false;
  std::array<float, 4> clip{}; // logical top-left x, y, width, height
  GuestPrimitive primitive = GuestPrimitive::Triangles; // version 2
  bool depthTest = false;                               // version 2
  // Version 3: the guest pipeline's alpha blending. Frames of versions 1
  // and 2 carry none and keep each style's historical default.
  bool blend = false;
  bool hasBlend = false; // decoded only; true for version 3
  // Version 3: draw indexCount indices from firstIndex of a retained host
  // mesh instead of inline geometry (vertices and indices stay empty).
  GuestResourceId mesh;
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
  GuestLighting lighting; // LitMesh; Skybox uses its tint only
  std::vector<GuestVertex> vertices;
  std::vector<std::uint32_t> indices;

  bool retained() const { return mesh.owner != 0; }
  std::uint32_t drawCount() const
  {
    return retained() ? indexCount : static_cast<std::uint32_t>(indices.size());
  }
};

// Version 2: one registered shadow caster, as the product requested it.
struct GuestShadowCaster
{
  std::array<float, 3> boundsMin{};
  std::array<float, 3> boundsMax{};
  std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
  std::uint32_t mapSize = 1024;
  float minimumRadius = 2.5f;
  float lightDistance = 8.0f;
  float casterDistance = 100.0f;
};

struct GuestFrameLimits
{
  std::uint32_t bytes = 64u * 1024u * 1024u;
  // Batches, vertices and indices bound the main frame and every surface
  // together.
  std::uint32_t batches = 4096;
  std::uint32_t vertices = 1000000;
  std::uint32_t indices = 3000000;
  std::uint32_t textureWrites = 256;
  std::uint32_t uploadBytes = 16u * 1024u * 1024u;
  std::uint32_t shadowCasters = 256;
  std::uint32_t meshWrites = 4096;
  std::uint32_t meshWriteBytes = 32u * 1024u * 1024u;
};

// Version 4: a byte range written into a dynamic retained host mesh. Every
// write in a frame applies before any of its batches draw.
struct GuestFrameMeshWrite
{
  GuestResourceId mesh;
  bool indices = false;
  std::uint32_t offset = 0;
  std::vector<std::byte> bytes;
};

struct GuestTextureWrite
{
  GuestResourceId texture;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t channels = 0;
  std::vector<std::byte> pixels; // tightly packed copied rectangle
  std::uint32_t beforeBatch = 0; // ordered between draws; batches.size() is end
};

// Version 5: the content of one surface window (Windows capability). Its
// batches are UI only (Shape, Sprite, Canvas) in the surface's own logical
// space, drawn after every main batch and texture write. A surface whose
// content did not change since its last revision is sent as `same`.
struct GuestSurfaceFrame
{
  std::uint32_t surface = 0;
  float width = 0;
  float height = 0;
  std::uint64_t revision = 0;
  bool same = false;
  std::vector<GuestBatch> batches;
};

struct GuestFrame
{
  static constexpr std::uint32_t Magic = 0x31465249u; // IRF1
  // Version 1 carries 2D batches only. Version 2 adds the world camera,
  // primitive/depth flags, lit meshes and shadow casters. Version 3 adds the
  // blend flag, retained host meshes and the cubemap skybox. Version 4 adds
  // in-place writes to dynamic retained meshes. Version 5 adds surface
  // windows. The host accepts all five.
  static constexpr std::uint32_t Version = 5;
  static constexpr std::uint32_t MaximumSurfaces = 8;
  static constexpr std::uint32_t MaximumSurfaceId = 0x7fffffffu;
  float width = 1280;
  float height = 720;
  // World view-projection used for host shadow fitting (version 2).
  bool hasCamera = false;
  std::array<float, 16> camera{
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
  };
  std::vector<GuestBatch> batches;
  std::vector<GuestTextureWrite> textureWrites;
  std::vector<GuestShadowCaster> shadowCasters;
  std::vector<GuestFrameMeshWrite> meshWrites;
  std::vector<GuestSurfaceFrame> surfaces;

  // Empties the frame but keeps every container's capacity for reuse.
  void clear()
  {
    batches.clear();
    textureWrites.clear();
    shadowCasters.clear();
    meshWrites.clear();
    surfaces.clear();
    hasCamera = false;
  }

  static void writeFloats(GuestWireWriter& output, const float* values, int n)
  {
    for (int index = 0; index < n; ++index) {
      output.f32(values[index]);
    }
  }
  static bool readFloats(GuestWireReader& reader, float* values, int n)
  {
    for (int index = 0; index < n; ++index) {
      values[index] = reader.f32();
      if (!std::isfinite(values[index])) {
        return false;
      }
    }
    return reader.valid();
  }

  static void writeBatch(GuestWireWriter& output, const GuestBatch& batch)
  {
    if (batch.vertices.size() > UINT32_MAX ||
        batch.indices.size() > UINT32_MAX ||
        (batch.retained() &&
         (!batch.vertices.empty() || !batch.indices.empty()))) {
      throw std::length_error("Guest geometry exceeds ABI range");
    }
    output.u32(static_cast<std::uint32_t>(batch.style));
    output.u32(static_cast<std::uint32_t>(batch.layer));
    batch.texture.write(output);
    output.u32(batch.clipped ? 1 : 0);
    writeFloats(output, batch.clip.data(), 4);
    writeFloats(output, batch.mvp.data(), 16);
    output.u32(static_cast<std::uint32_t>(batch.primitive));
    output.u32(batch.depthTest ? 1 : 0);
    output.u32(batch.blend ? 1 : 0);
    batch.mesh.write(output);
    output.u32(batch.firstIndex);
    const bool lit = batch.style == GuestBatchStyle::LitMesh;
    if (batch.style == GuestBatchStyle::Skybox) {
      writeFloats(output, batch.lighting.tint.data(), 4);
    }
    if (lit) {
      const GuestLighting& lighting = batch.lighting;
      writeFloats(output, lighting.model.data(), 16);
      writeFloats(output, lighting.lightDirection.data(), 3);
      writeFloats(output, lighting.lightColor.data(), 3);
      writeFloats(output, lighting.ambientColor.data(), 3);
      writeFloats(output, lighting.tint.data(), 4);
      output.f32(lighting.shadowBias);
      output.f32(lighting.shadowSlopeScale);
      output.f32(lighting.shadowNormalOffset);
      output.u32((lighting.shadowPcf ? 1u : 0u) |
                 (lighting.receivesShadow ? 2u : 0u) |
                 (lighting.castsShadow ? 4u : 0u));
    }
    output.u32(static_cast<std::uint32_t>(batch.vertices.size()));
    output.u32(batch.drawCount());
    for (const GuestVertex& vertex : batch.vertices) {
      writeFloats(output, vertex.position.data(), 3);
      output.u32(vertex.rgba);
      writeFloats(output, vertex.uv.data(), 2);
      if (lit) {
        writeFloats(output, vertex.normal.data(), 3);
      }
    }
    for (std::uint32_t index : batch.indices) {
      output.u32(index);
    }
  }

  void write(GuestWireWriter& output) const
  {
    if (batches.size() > UINT32_MAX || shadowCasters.size() > UINT32_MAX) {
      throw std::length_error("Too many guest batches");
    }
    output.u32(Magic);
    output.u32(Version);
    output.f32(width);
    output.f32(height);
    output.u32(hasCamera ? 1 : 0);
    writeFloats(output, camera.data(), 16);
    output.u32(static_cast<std::uint32_t>(batches.size()));
    for (const GuestBatch& batch : batches) {
      writeBatch(output, batch);
    }
    output.u32(static_cast<std::uint32_t>(textureWrites.size()));
    for (const GuestTextureWrite& write : textureWrites) {
      write.texture.write(output);
      output.u32(write.x);
      output.u32(write.y);
      output.u32(write.width);
      output.u32(write.height);
      output.u32(write.channels);
      output.u32(write.beforeBatch);
      output.u32(static_cast<std::uint32_t>(write.pixels.size()));
      output.bytes(write.pixels);
    }
    output.u32(static_cast<std::uint32_t>(shadowCasters.size()));
    for (const GuestShadowCaster& caster : shadowCasters) {
      writeFloats(output, caster.boundsMin.data(), 3);
      writeFloats(output, caster.boundsMax.data(), 3);
      writeFloats(output, caster.lightDirection.data(), 3);
      output.u32(caster.mapSize);
      output.f32(caster.minimumRadius);
      output.f32(caster.lightDistance);
      output.f32(caster.casterDistance);
    }
    if (meshWrites.size() > UINT32_MAX) {
      throw std::length_error("Too many guest mesh writes");
    }
    output.u32(static_cast<std::uint32_t>(meshWrites.size()));
    for (const GuestFrameMeshWrite& write : meshWrites) {
      if (write.bytes.size() > UINT32_MAX) {
        throw std::length_error("Guest mesh write exceeds ABI range");
      }
      write.mesh.write(output);
      output.u32(write.indices ? 1 : 0);
      output.u32(write.offset);
      output.u32(static_cast<std::uint32_t>(write.bytes.size()));
      output.bytes(write.bytes);
    }
    if (surfaces.size() > MaximumSurfaces) {
      throw std::length_error("Too many guest surfaces");
    }
    output.u32(static_cast<std::uint32_t>(surfaces.size()));
    for (const GuestSurfaceFrame& surface : surfaces) {
      output.u32(surface.surface);
      output.f32(surface.width);
      output.f32(surface.height);
      output.u64(surface.revision);
      output.u32(surface.same ? 1 : 0);
      if (surface.same) {
        continue;
      }
      if (surface.batches.size() > UINT32_MAX) {
        throw std::length_error("Too many guest surface batches");
      }
      output.u32(static_cast<std::uint32_t>(surface.batches.size()));
      for (const GuestBatch& batch : surface.batches) {
        writeBatch(output, batch);
      }
    }
  }

  // Running totals shared by the main batches and every surface's batches.
  struct Totals
  {
    std::uint32_t batches = 0;
    std::uint32_t vertices = 0;
    std::uint32_t indices = 0;
  };

  // Decodes one batch (without layer ordering, which callers check).
  static bool readBatch(GuestWireReader& reader,
                        std::uint32_t version,
                        const GuestFrameLimits& limits,
                        Totals& totals,
                        GuestBatch& batch)
  {
    const std::uint32_t style = reader.u32();
    const std::uint32_t layer = reader.u32();
    batch.style = static_cast<GuestBatchStyle>(style);
    batch.layer = static_cast<GuestLayer>(layer);
    batch.texture = GuestResourceId::read(reader);
    const std::uint32_t clipped = reader.u32();
    batch.clipped = clipped != 0;
    // Version 1: Shape to Canvas; 2 adds LitMesh; 3 and later add Skybox.
    const std::uint32_t maximumStyle = version >= 3 ? 5u : version + 2u;
    if (style < 1 || style > maximumStyle || layer < 1 || layer > 2 ||
        clipped > 1) {
      return false;
    }
    const bool lit = batch.style == GuestBatchStyle::LitMesh;
    const bool sky = batch.style == GuestBatchStyle::Skybox;
    if (style == 1 || lit) {
      if (batch.texture.owner != 0 || batch.texture.slot != 0 ||
          batch.texture.generation != 0) {
        return false;
      }
    } else if (batch.texture.owner == 0 || batch.texture.slot == 0 ||
               batch.texture.generation == 0 ||
               batch.texture.kind != GuestResourceKind::Texture) {
      return false;
    }
    if (!readFloats(reader, batch.clip.data(), 4)) {
      return false;
    }
    for (float value : batch.clip) {
      if (std::abs(value) > 65536) {
        return false;
      }
    }
    if (batch.clip[2] < 0 || batch.clip[3] < 0 ||
        !readFloats(reader, batch.mvp.data(), 16)) {
      return false;
    }
    if (version >= 2) {
      const std::uint32_t primitive = reader.u32();
      const std::uint32_t depthTest = reader.u32();
      if (primitive < 1 || primitive > 2 || depthTest > 1 ||
          (primitive == 2 && style != 1) ||
          ((lit || sky) && batch.layer != GuestLayer::World)) {
        return false;
      }
      batch.primitive = static_cast<GuestPrimitive>(primitive);
      batch.depthTest = depthTest != 0;
    }
    if (version >= 3) {
      const std::uint32_t blend = reader.u32();
      batch.mesh = GuestResourceId::read(reader);
      batch.firstIndex = reader.u32();
      if (!reader.valid() || blend > 1) {
        return false;
      }
      batch.blend = blend != 0;
      batch.hasBlend = true;
      if (batch.retained()
            ? (batch.mesh.kind != GuestResourceKind::Mesh ||
               batch.mesh.slot == 0 || batch.mesh.generation == 0 || sky)
            : (batch.mesh.slot != 0 || batch.mesh.generation != 0 ||
               batch.firstIndex != 0)) {
        return false;
      }
    }
    if (sky && !readFloats(reader, batch.lighting.tint.data(), 4)) {
      return false;
    }
    if (lit) {
      GuestLighting& lighting = batch.lighting;
      if (!readFloats(reader, lighting.model.data(), 16) ||
          !readFloats(reader, lighting.lightDirection.data(), 3) ||
          !readFloats(reader, lighting.lightColor.data(), 3) ||
          !readFloats(reader, lighting.ambientColor.data(), 3) ||
          !readFloats(reader, lighting.tint.data(), 4)) {
        return false;
      }
      lighting.shadowBias = reader.f32();
      lighting.shadowSlopeScale = reader.f32();
      lighting.shadowNormalOffset = reader.f32();
      const std::uint32_t flags = reader.u32();
      if (!reader.valid() || flags > 7 || !std::isfinite(lighting.shadowBias) ||
          !std::isfinite(lighting.shadowSlopeScale) ||
          !std::isfinite(lighting.shadowNormalOffset)) {
        return false;
      }
      lighting.shadowPcf = (flags & 1u) != 0;
      lighting.receivesShadow = (flags & 2u) != 0;
      lighting.castsShadow = (flags & 4u) != 0;
    }
    const std::uint32_t vertices = reader.u32();
    const std::uint32_t indices = reader.u32();
    const std::uint32_t stride = lit ? 36u : 24u;
    const std::uint32_t group =
      batch.primitive == GuestPrimitive::Lines ? 2u : 3u;
    if (batch.retained()) {
      // Only the index range travels; the host resolves and bounds it
      // against the retained mesh.
      if (!reader.valid() || vertices != 0 || indices == 0 ||
          indices % group != 0) {
        return false;
      }
      batch.indexCount = indices;
      return true;
    }
    if (!reader.valid() || vertices == 0 || indices == 0 ||
        indices % group != 0 || vertices > limits.vertices - totals.vertices ||
        indices > limits.indices - totals.indices ||
        vertices > reader.remaining() / stride) {
      return false;
    }
    totals.vertices += vertices;
    totals.indices += indices;
    batch.vertices.resize(vertices);
    for (GuestVertex& vertex : batch.vertices) {
      if (!readFloats(reader, vertex.position.data(), 3)) {
        return false;
      }
      vertex.rgba = reader.u32();
      if (!readFloats(reader, vertex.uv.data(), 2) ||
          (lit && !readFloats(reader, vertex.normal.data(), 3))) {
        return false;
      }
    }
    if (indices > reader.remaining() / 4u) {
      return false;
    }
    batch.indices.resize(indices);
    for (std::uint32_t& index : batch.indices) {
      index = reader.u32();
      if (index >= vertices) {
        return false;
      }
    }
    return true;
  }

  // Minimum encoded batch: 120 bytes (v1), 128 (v2), 156 (v3 and later).
  static std::uint32_t minimumBatchBytes(std::uint32_t version)
  {
    return version == 1 ? 120u : (version == 2 ? 128u : 156u);
  }

  static bool readSurfaces(GuestWireReader& reader,
                           const GuestFrameLimits& limits,
                           Totals& totals,
                           GuestFrame& frame)
  {
    const std::uint32_t count = reader.u32();
    // Minimum encoded surface: id, size, revision, same flag (24 bytes).
    if (!reader.valid() || count > MaximumSurfaces ||
        count > reader.remaining() / 24u) {
      return false;
    }
    frame.surfaces.resize(count);
    for (std::size_t index = 0; index < frame.surfaces.size(); ++index) {
      GuestSurfaceFrame& surface = frame.surfaces[index];
      surface.surface = reader.u32();
      surface.width = reader.f32();
      surface.height = reader.f32();
      surface.revision = reader.u64();
      const std::uint32_t same = reader.u32();
      surface.same = same != 0;
      if (!reader.valid() || surface.surface == 0 ||
          surface.surface > MaximumSurfaceId || surface.revision == 0 ||
          same > 1 || !std::isfinite(surface.width) ||
          !std::isfinite(surface.height) || surface.width < 1 ||
          surface.height < 1 || surface.width > 65536 ||
          surface.height > 65536) {
        return false;
      }
      for (std::size_t earlier = 0; earlier < index; ++earlier) {
        if (frame.surfaces[earlier].surface == surface.surface) {
          return false;
        }
      }
      if (surface.same) {
        continue;
      }
      const std::uint32_t batches = reader.u32();
      if (!reader.valid() || batches > limits.batches - totals.batches ||
          batches > reader.remaining() / minimumBatchBytes(5)) {
        return false;
      }
      totals.batches += batches;
      surface.batches.resize(batches);
      for (GuestBatch& batch : surface.batches) {
        if (!readBatch(reader, 5, limits, totals, batch) ||
            batch.layer != GuestLayer::Ui ||
            batch.style == GuestBatchStyle::LitMesh ||
            batch.style == GuestBatchStyle::Skybox || batch.depthTest) {
          return false;
        }
      }
    }
    return true;
  }

  // Decode transactionally before resource resolution or renderer calls.
  // Counts are checked against both byte availability and aggregate quotas.
  static bool read(std::span<const std::byte> input,
                   GuestFrame& output,
                   GuestFrameLimits limits = {})
  {
    if (input.size() > limits.bytes) {
      return false;
    }
    GuestWireReader reader(input);
    const std::uint32_t magic = reader.u32();
    const std::uint32_t version = reader.u32();
    GuestFrame frame;
    frame.width = reader.f32();
    frame.height = reader.f32();
    if (magic != Magic || version < 1 || version > Version || !reader.valid() ||
        !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
        frame.width < 1 || frame.height < 1 || frame.width > 65536 ||
        frame.height > 65536) {
      return false;
    }
    if (version >= 2) {
      const std::uint32_t hasCamera = reader.u32();
      if (hasCamera > 1 || !readFloats(reader, frame.camera.data(), 16)) {
        return false;
      }
      frame.hasCamera = hasCamera != 0;
    }
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || count > limits.batches ||
        count > reader.remaining() / minimumBatchBytes(version)) {
      return false;
    }
    Totals totals;
    totals.batches = count;
    std::uint32_t lastLayer = 1;
    frame.batches.resize(count);
    for (GuestBatch& batch : frame.batches) {
      if (!readBatch(reader, version, limits, totals, batch)) {
        return false;
      }
      const std::uint32_t layer = static_cast<std::uint32_t>(batch.layer);
      if (layer < lastLayer) {
        return false;
      }
      lastLayer = layer;
    }
    const std::uint32_t writes = reader.u32();
    if (!reader.valid() || writes > limits.textureWrites ||
        writes > reader.remaining() / 48u) {
      return false;
    }
    std::uint32_t uploadBytes = 0;
    std::uint32_t lastWritePosition = 0;
    for (std::uint32_t index = 0; index < writes; ++index) {
      GuestTextureWrite write;
      write.texture = GuestResourceId::read(reader);
      write.x = reader.u32();
      write.y = reader.u32();
      write.width = reader.u32();
      write.height = reader.u32();
      write.channels = reader.u32();
      write.beforeBatch = reader.u32();
      const std::uint32_t bytes = reader.u32();
      if (!reader.valid() || write.beforeBatch < lastWritePosition ||
          write.beforeBatch > count || write.texture.owner == 0 ||
          write.texture.slot == 0 || write.texture.generation == 0 ||
          write.texture.kind != GuestResourceKind::Texture || write.x > 8192 ||
          write.y > 8192 || write.width == 0 || write.width > 8192 ||
          write.height == 0 || write.height > 8192 ||
          (write.channels != 1 && write.channels != 3 && write.channels != 4) ||
          bytes != static_cast<std::uint64_t>(write.width) * write.height *
                     write.channels ||
          bytes > limits.uploadBytes - uploadBytes ||
          bytes > reader.remaining()) {
        return false;
      }
      uploadBytes += bytes;
      lastWritePosition = write.beforeBatch;
      const std::span<const std::byte> pixels = reader.bytes(bytes);
      write.pixels.assign(pixels.begin(), pixels.end());
      frame.textureWrites.push_back(std::move(write));
    }
    if (version >= 2) {
      const std::uint32_t casters = reader.u32();
      if (!reader.valid() || casters > limits.shadowCasters ||
          casters > reader.remaining() / 52u) {
        return false;
      }
      for (std::uint32_t index = 0; index < casters; ++index) {
        GuestShadowCaster caster;
        if (!readFloats(reader, caster.boundsMin.data(), 3) ||
            !readFloats(reader, caster.boundsMax.data(), 3) ||
            !readFloats(reader, caster.lightDirection.data(), 3)) {
          return false;
        }
        caster.mapSize = reader.u32();
        caster.minimumRadius = reader.f32();
        caster.lightDistance = reader.f32();
        caster.casterDistance = reader.f32();
        if (!reader.valid() || caster.mapSize < 64 || caster.mapSize > 8192 ||
            !std::isfinite(caster.minimumRadius) ||
            !std::isfinite(caster.lightDistance) ||
            !std::isfinite(caster.casterDistance)) {
          return false;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
          if (caster.boundsMin[axis] > caster.boundsMax[axis]) {
            return false;
          }
        }
        frame.shadowCasters.push_back(caster);
      }
    }
    if (version >= 4) {
      // Minimum encoded write: 20-byte id, target, offset, count, one byte.
      const std::uint32_t meshWrites = reader.u32();
      if (!reader.valid() || meshWrites > limits.meshWrites ||
          meshWrites > reader.remaining() / 33u) {
        return false;
      }
      std::uint32_t writeBytes = 0;
      frame.meshWrites.reserve(meshWrites);
      for (std::uint32_t index = 0; index < meshWrites; ++index) {
        GuestFrameMeshWrite write;
        write.mesh = GuestResourceId::read(reader);
        const std::uint32_t target = reader.u32();
        write.offset = reader.u32();
        const std::uint32_t bytes = reader.u32();
        if (!reader.valid() || write.mesh.owner == 0 || write.mesh.slot == 0 ||
            write.mesh.generation == 0 ||
            write.mesh.kind != GuestResourceKind::Mesh || target > 1 ||
            bytes == 0 || bytes > limits.meshWriteBytes - writeBytes ||
            bytes > reader.remaining()) {
          return false;
        }
        writeBytes += bytes;
        write.indices = target == 1;
        const std::span<const std::byte> data = reader.bytes(bytes);
        write.bytes.assign(data.begin(), data.end());
        frame.meshWrites.push_back(std::move(write));
      }
    }
    if (version >= 5 && !readSurfaces(reader, limits, totals, frame)) {
      return false;
    }
    if (!reader.finished()) {
      return false;
    }
    output = std::move(frame);
    return true;
  }
};
