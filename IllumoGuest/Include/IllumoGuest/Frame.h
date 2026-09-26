#pragma once

#include <IllumoGuest/ResourceId.h>
#include <array>
#include <cmath>
#include <string>

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

// Frame schema v6 (HostRender): one change to the guest's host render world.
// Ids are guest-chosen and nonzero, except Environment, whose id is zero.
enum class GuestWorldOp : std::uint32_t
{
  MaterialCreate = 1,
  MaterialUpdate = 2,
  MaterialDestroy = 3,
  InstanceCreate = 4,
  // Tint and visibility; mesh and material stay as created.
  InstanceUpdate = 5,
  InstanceDestroy = 6,
  InstanceTransform = 7,
  Environment = 8
};

struct GuestWorldMaterial
{
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };
  bool receivesShadow = true;
  bool castsShadow = true;
  bool blend = false;
};

struct GuestWorldEnvironment
{
  std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
  std::array<float, 3> lightColor{ 1.0f, 1.0f, 1.0f };
  std::array<float, 3> ambientColor{ 0.2f, 0.2f, 0.2f };
  bool shadowsEnabled = false;
  bool shadowPcf = false;
  float shadowBias = 0.002f;
  float shadowSlopeScale = 0.01f;
  float shadowNormalOffset = 0.02f;
  std::uint32_t shadowMapSize = 1024;
  float shadowMinimumRadius = 2.5f;
  float shadowLightDistance = 8.0f;
  float shadowCasterDistance = 100.0f;
};

// Fields beyond an operation's own are ignored when encoding and default
// after decoding. An instance culls with its retained mesh's host bounds.
struct GuestWorldOperation
{
  GuestWorldOp op = GuestWorldOp::MaterialCreate;
  std::uint32_t id = 0;
  GuestWorldMaterial material;  // MaterialCreate/Update
  GuestResourceId mesh;         // InstanceCreate: a lit mesh
  std::uint32_t firstIndex = 0; // InstanceCreate
  std::uint32_t indexCount = 0; // InstanceCreate
  std::uint32_t materialId = 0; // InstanceCreate
  std::array<float, 16> transform{ 1, 0, 0, 0, 0, 1, 0, 0,
                                   0, 0, 1, 0, 0, 0, 0, 1 }; // Create/Transform
  std::array<float, 4> tint{ 1.0f, 1.0f, 1.0f, 1.0f };       // Create/Update
  bool visible = true;                                       // Create/Update
  GuestWorldEnvironment environment;                         // Environment
};

// Frame schema v7 (HostRender): one change to a host-retained 2D visual.
// Ids are guest-chosen and nonzero.
enum class GuestVisualOp : std::uint32_t
{
  Create = 1,
  Destroy = 2,
  // Space, layer, transform, opacity, clip and visibility.
  Set = 3,
  // Replaces item `index`, or appends at the visual's item count.
  ItemSet = 4,
  // Removes `count` items from `index`.
  ItemRemove = 5,
  ItemsClear = 6
};

struct GuestTransform2D
{
  float x = 0.0f;
  float y = 0.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float rotation = 0.0f;
  float pivotX = 0.0f;
  float pivotY = 0.0f;
};

struct GuestVisualProperties
{
  bool worldSpace = false; // logical pixels otherwise
  GuestLayer layer = GuestLayer::Ui;
  GuestTransform2D transform;
  float opacity = 1.0f;
  bool clipped = false;
  std::array<float, 4> clip{}; // logical top-left x, y, width, height
  bool visible = true;
};

enum class GuestItemKind : std::uint32_t
{
  Shape = 1,
  Sprite = 2,
  Text = 3
};

// One GameVisual item. Colours pack as r | g << 8 | b << 16 | a << 24. Items
// always use the built-in Shape and Sprite styles.
struct GuestVisualItem
{
  GuestItemKind kind = GuestItemKind::Shape;
  std::int32_t drawOrder = 0;
  bool visible = true;
  GuestTransform2D transform;  // Shape and Sprite
  std::uint32_t shape = 0;     // ShapeKind value
  std::array<float, 4> rect{}; // Shape and Sprite bounds; Text x, y
  std::array<float, 8> points{};
  std::uint32_t rgba = UINT32_MAX; // Shape colour, Sprite tint, Text colour
  std::array<std::uint32_t, 4> vertexColors{};
  float lineWidth = 1.0f;
  GuestResourceId texture; // Sprite
  std::array<float, 4> region{ 0.0f, 0.0f, 1.0f, 1.0f };
  bool flipX = false;
  bool flipY = false;
  // Text: fonts are the atlas textures LoadFont returned.
  GuestResourceId font;
  GuestResourceId heavyFont;
  float heavyBlend = 0.0f;
  float sizePt = 12.0f;
  float stretchX = 1.0f;
  float stretchY = 1.0f;
  std::string text;
};

// Fields beyond an operation's own are ignored when encoding and default
// after decoding.
struct GuestVisualOperation
{
  GuestVisualOp op = GuestVisualOp::Create;
  std::uint32_t id = 0;
  GuestVisualProperties properties; // Set
  std::uint32_t index = 0;          // ItemSet, ItemRemove
  std::uint32_t count = 0;          // ItemRemove
  GuestVisualItem item;             // ItemSet
};

enum class GuestCompositionKind : std::uint32_t
{
  // A visual, by id.
  Visual = 1,
  // `count` of the target's batches from `first`.
  Batches = 2,
  // The guest's host render world (main frame only).
  World = 3
};

struct GuestCompositionEntry
{
  GuestCompositionKind kind = GuestCompositionKind::Visual;
  std::uint32_t first = 0; // visual id, or first batch
  std::uint32_t count = 0; // Batches
};

// Frame schema v7: the painter order of one target. Target 0 is the main
// frame; others are surface ids. World-layer entries come first, and batch
// ranges cover the target's batches once, in order. `same` repeats the
// target's previous composition, size included.
struct GuestComposition
{
  std::uint32_t target = 0;
  bool same = false;
  // The logical size that pixel-space visuals lay out in: the guest's
  // window divided by its UI scale, or a surface's own size.
  float width = 1280.0f;
  float height = 720.0f;
  std::vector<GuestCompositionEntry> entries;
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
  // Version 6 world operations and version 7 visual operations together.
  std::uint32_t worldOperations = 65536;
  std::uint32_t textBytes = 1024u * 1024u;  // version 7
  std::uint32_t compositionEntries = 16384; // version 7, per target
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
  // windows. Version 6 adds host render world operations (HostRender).
  // Version 7 adds host-retained visuals and compositions. The host accepts
  // all seven.
  static constexpr std::uint32_t Version = 7;
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
  std::vector<GuestWorldOperation> worldOperations;
  std::vector<GuestVisualOperation> visualOperations;
  std::vector<GuestComposition> compositions;
  // Not encoded: text buffers of operations a shorter frame dropped, reused
  // by decode so steady frames with changing text allocate nothing.
  std::vector<std::string> spareText;
  std::vector<std::vector<GuestCompositionEntry>> spareEntries;

  // Empties the frame but keeps every container's capacity for reuse.
  void clear()
  {
    batches.clear();
    textureWrites.clear();
    shadowCasters.clear();
    meshWrites.clear();
    surfaces.clear();
    worldOperations.clear();
    visualOperations.clear();
    compositions.clear();
    hasCamera = false;
  }

  static void countInline(const std::vector<GuestBatch>& list,
                          std::uint64_t& vertices,
                          std::uint64_t& indices)
  {
    for (const GuestBatch& batch : list) {
      if (!batch.retained()) {
        vertices += batch.vertices.size();
        indices += batch.indices.size();
      }
    }
  }

  // The first count quota `decode` would reject this frame for, or nullptr.
  // Guests check before sending, because the host retires a guest whose frame
  // breaks its quotas. The encoded byte total is not estimated here.
  const char* exceededLimit(const GuestFrameLimits& limits = {}) const
  {
    std::uint64_t batchCount = batches.size();
    std::uint64_t vertexCount = 0;
    std::uint64_t indexCount = 0;
    countInline(batches, vertexCount, indexCount);
    for (const GuestSurfaceFrame& surface : surfaces) {
      if (!surface.same) {
        batchCount += surface.batches.size();
        countInline(surface.batches, vertexCount, indexCount);
      }
    }
    std::uint64_t uploadBytes = 0;
    for (const GuestTextureWrite& write : textureWrites) {
      uploadBytes += write.pixels.size();
    }
    std::uint64_t meshWriteBytes = 0;
    for (const GuestFrameMeshWrite& write : meshWrites) {
      meshWriteBytes += write.bytes.size();
    }
    if (batchCount > limits.batches) {
      return "the frame has more batches than the host accepts";
    }
    if (vertexCount > limits.vertices || indexCount > limits.indices) {
      return "the frame has more inline geometry than the host accepts";
    }
    if (textureWrites.size() > limits.textureWrites ||
        uploadBytes > limits.uploadBytes) {
      return "the frame has more texture uploads than the host accepts";
    }
    if (shadowCasters.size() > limits.shadowCasters) {
      return "the frame has more shadow casters than the host accepts";
    }
    if (meshWrites.size() > limits.meshWrites ||
        meshWriteBytes > limits.meshWriteBytes) {
      return "the frame has more mesh writes than the host accepts";
    }
    if (worldOperations.size() + visualOperations.size() >
        limits.worldOperations) {
      return "the frame has more world and visual operations than the host "
             "accepts";
    }
    std::uint64_t textBytes = 0;
    for (const GuestVisualOperation& operation : visualOperations) {
      if (operation.op == GuestVisualOp::ItemSet &&
          operation.item.kind == GuestItemKind::Text) {
        textBytes += operation.item.text.size();
      }
    }
    if (textBytes > limits.textBytes) {
      return "the frame has more visual text than the host accepts";
    }
    if (compositions.size() > MaximumSurfaces + 1u) {
      return "the frame has more compositions than the host accepts";
    }
    for (const GuestComposition& composition : compositions) {
      if (composition.entries.size() > limits.compositionEntries) {
        return "a composition has more entries than the host accepts";
      }
    }
    return nullptr;
  }

  // Version 7 wire forms. Colours and ids are checked against the host's
  // resources when applied; here only structure and finite values.
  static void writeTransform(GuestWireWriter& output,
                             const GuestTransform2D& value)
  {
    const float values[] = { value.x,      value.y,        value.scaleX,
                             value.scaleY, value.rotation, value.pivotX,
                             value.pivotY };
    writeFloats(output, values, 7);
  }
  static bool readTransform(GuestWireReader& reader, GuestTransform2D& value)
  {
    float values[7] = {};
    if (!readFloats(reader, values, 7)) {
      return false;
    }
    value = { values[0], values[1], values[2], values[3],
              values[4], values[5], values[6] };
    return true;
  }
  static bool validTexture(const GuestResourceId& id)
  {
    return id.owner != 0 && id.slot != 0 && id.generation != 0 &&
           id.kind == GuestResourceKind::Texture;
  }
  static bool emptyId(const GuestResourceId& id)
  {
    return id.owner == 0 && id.slot == 0 && id.generation == 0;
  }

  static constexpr std::uint32_t MaximumItemText = 65536;

  static void writeVisualItem(GuestWireWriter& output,
                              const GuestVisualItem& item)
  {
    output.u32(static_cast<std::uint32_t>(item.kind));
    output.u32(static_cast<std::uint32_t>(item.drawOrder));
    output.u32(item.visible ? 1u : 0u);
    switch (item.kind) {
      case GuestItemKind::Shape:
        writeTransform(output, item.transform);
        output.u32(item.shape);
        writeFloats(output, item.rect.data(), 4);
        writeFloats(output, item.points.data(), 8);
        output.u32(item.rgba);
        for (std::uint32_t color : item.vertexColors) {
          output.u32(color);
        }
        output.f32(item.lineWidth);
        break;
      case GuestItemKind::Sprite:
        writeTransform(output, item.transform);
        writeFloats(output, item.rect.data(), 4);
        item.texture.write(output);
        writeFloats(output, item.region.data(), 4);
        output.u32(item.rgba);
        output.u32((item.flipX ? 1u : 0u) | (item.flipY ? 2u : 0u));
        break;
      case GuestItemKind::Text:
        if (item.text.size() > MaximumItemText) {
          throw std::length_error("Guest visual text exceeds ABI range");
        }
        writeFloats(output, item.rect.data(), 2);
        output.f32(item.sizePt);
        output.u32(item.rgba);
        item.font.write(output);
        item.heavyFont.write(output);
        output.f32(item.heavyBlend);
        output.f32(item.stretchX);
        output.f32(item.stretchY);
        output.u32(static_cast<std::uint32_t>(item.text.size()));
        output.bytes(std::as_bytes(std::span(item.text)));
        break;
    }
  }

  static bool readVisualItem(GuestWireReader& reader, GuestVisualItem& item)
  {
    // Reused items keep their text capacity.
    std::string text = std::move(item.text);
    text.clear();
    item = GuestVisualItem{};
    item.text = std::move(text);
    const std::uint32_t kind = reader.u32();
    item.drawOrder = static_cast<std::int32_t>(reader.u32());
    const std::uint32_t visible = reader.u32();
    if (!reader.valid() || kind < 1 || kind > 3 || visible > 1) {
      return false;
    }
    item.kind = static_cast<GuestItemKind>(kind);
    item.visible = visible != 0;
    std::uint32_t flags = 0;
    switch (item.kind) {
      case GuestItemKind::Shape:
        if (!readTransform(reader, item.transform)) {
          return false;
        }
        item.shape = reader.u32();
        if (!reader.valid() || item.shape > 5 ||
            !readFloats(reader, item.rect.data(), 4) ||
            !readFloats(reader, item.points.data(), 8)) {
          return false;
        }
        item.rgba = reader.u32();
        for (std::uint32_t& color : item.vertexColors) {
          color = reader.u32();
        }
        item.lineWidth = reader.f32();
        return reader.valid() && std::isfinite(item.lineWidth);
      case GuestItemKind::Sprite:
        if (!readTransform(reader, item.transform) ||
            !readFloats(reader, item.rect.data(), 4)) {
          return false;
        }
        item.texture = GuestResourceId::read(reader);
        if (!reader.valid() || !validTexture(item.texture) ||
            !readFloats(reader, item.region.data(), 4)) {
          return false;
        }
        item.rgba = reader.u32();
        flags = reader.u32();
        item.flipX = (flags & 1u) != 0;
        item.flipY = (flags & 2u) != 0;
        return reader.valid() && flags <= 3u;
      case GuestItemKind::Text: {
        if (!readFloats(reader, item.rect.data(), 2)) {
          return false;
        }
        item.sizePt = reader.f32();
        item.rgba = reader.u32();
        item.font = GuestResourceId::read(reader);
        item.heavyFont = GuestResourceId::read(reader);
        item.heavyBlend = reader.f32();
        item.stretchX = reader.f32();
        item.stretchY = reader.f32();
        const std::uint32_t bytes = reader.u32();
        if (!reader.valid() || !std::isfinite(item.sizePt) ||
            item.sizePt <= 0.0f || item.sizePt > 4096.0f ||
            !validTexture(item.font) ||
            !(emptyId(item.heavyFont) || validTexture(item.heavyFont)) ||
            !std::isfinite(item.heavyBlend) || item.heavyBlend < 0.0f ||
            item.heavyBlend > 1.0f || !std::isfinite(item.stretchX) ||
            !std::isfinite(item.stretchY) || item.stretchX <= 0.0f ||
            item.stretchY <= 0.0f || bytes > MaximumItemText ||
            bytes > reader.remaining()) {
          return false;
        }
        const std::span<const std::byte> text = reader.bytes(bytes);
        item.text.assign(reinterpret_cast<const char*>(text.data()),
                         text.size());
        return reader.valid();
      }
    }
    return false;
  }

  static void writeVisualOperation(GuestWireWriter& output,
                                   const GuestVisualOperation& operation)
  {
    output.u32(static_cast<std::uint32_t>(operation.op));
    output.u32(operation.id);
    switch (operation.op) {
      case GuestVisualOp::Set: {
        const GuestVisualProperties& properties = operation.properties;
        output.u32((properties.worldSpace ? 1u : 0u) |
                   (properties.clipped ? 2u : 0u) |
                   (properties.visible ? 4u : 0u));
        output.u32(static_cast<std::uint32_t>(properties.layer));
        writeTransform(output, properties.transform);
        output.f32(properties.opacity);
        writeFloats(output, properties.clip.data(), 4);
        break;
      }
      case GuestVisualOp::ItemSet:
        output.u32(operation.index);
        writeVisualItem(output, operation.item);
        break;
      case GuestVisualOp::ItemRemove:
        output.u32(operation.index);
        output.u32(operation.count);
        break;
      case GuestVisualOp::Create:
      case GuestVisualOp::Destroy:
      case GuestVisualOp::ItemsClear:
        break;
    }
  }

  static bool readVisualOperation(GuestWireReader& reader,
                                  GuestVisualOperation& operation)
  {
    std::string text = std::move(operation.item.text);
    text.clear();
    operation = GuestVisualOperation{};
    operation.item.text = std::move(text);
    const std::uint32_t op = reader.u32();
    operation.id = reader.u32();
    if (!reader.valid() || operation.id == 0 ||
        op < static_cast<std::uint32_t>(GuestVisualOp::Create) ||
        op > static_cast<std::uint32_t>(GuestVisualOp::ItemsClear)) {
      return false;
    }
    operation.op = static_cast<GuestVisualOp>(op);
    switch (operation.op) {
      case GuestVisualOp::Set: {
        GuestVisualProperties& properties = operation.properties;
        const std::uint32_t flags = reader.u32();
        const std::uint32_t layer = reader.u32();
        if (!reader.valid() || flags > 7u || layer < 1 || layer > 2 ||
            !readTransform(reader, properties.transform)) {
          return false;
        }
        properties.worldSpace = (flags & 1u) != 0;
        properties.clipped = (flags & 2u) != 0;
        properties.visible = (flags & 4u) != 0;
        properties.layer = static_cast<GuestLayer>(layer);
        properties.opacity = reader.f32();
        return reader.valid() && std::isfinite(properties.opacity) &&
               properties.opacity >= 0.0f && properties.opacity <= 1.0f &&
               readFloats(reader, properties.clip.data(), 4) &&
               properties.clip[2] >= 0.0f && properties.clip[3] >= 0.0f;
      }
      case GuestVisualOp::ItemSet:
        operation.index = reader.u32();
        return reader.valid() && readVisualItem(reader, operation.item);
      case GuestVisualOp::ItemRemove:
        operation.index = reader.u32();
        operation.count = reader.u32();
        return reader.valid() && operation.count != 0;
      case GuestVisualOp::Create:
      case GuestVisualOp::Destroy:
      case GuestVisualOp::ItemsClear:
        return true;
    }
    return false;
  }

  static void writeComposition(GuestWireWriter& output,
                               const GuestComposition& composition)
  {
    if (composition.entries.size() > UINT32_MAX) {
      throw std::length_error("Too many guest composition entries");
    }
    output.u32(composition.target);
    output.u32(composition.same ? 1u : 0u);
    if (composition.same) {
      return;
    }
    output.f32(composition.width);
    output.f32(composition.height);
    output.u32(static_cast<std::uint32_t>(composition.entries.size()));
    for (const GuestCompositionEntry& entry : composition.entries) {
      output.u32(static_cast<std::uint32_t>(entry.kind));
      output.u32(entry.first);
      output.u32(entry.count);
    }
  }

  // Entry shapes only; ranges, layers and ids are checked by the host
  // against its batches, surfaces and visuals.
  static bool readComposition(GuestWireReader& reader,
                              const GuestFrameLimits& limits,
                              GuestComposition& composition)
  {
    composition.target = reader.u32();
    const std::uint32_t same = reader.u32();
    composition.same = same == 1;
    composition.entries.clear();
    // Entry counts vary a little from frame to frame; a small floor keeps
    // steady frames from growing the vector.
    if (composition.entries.capacity() < 32) {
      composition.entries.reserve(32);
    }
    if (!reader.valid() || same > 1 || composition.target > MaximumSurfaceId) {
      return false;
    }
    if (composition.same) {
      return true;
    }
    composition.width = reader.f32();
    composition.height = reader.f32();
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || !std::isfinite(composition.width) ||
        !std::isfinite(composition.height) || composition.width < 1 ||
        composition.height < 1 || composition.width > 65536 ||
        composition.height > 65536 || count > limits.compositionEntries ||
        count > reader.remaining() / 12u) {
      return false;
    }
    composition.entries.resize(count);
    for (GuestCompositionEntry& entry : composition.entries) {
      const std::uint32_t kind = reader.u32();
      entry.first = reader.u32();
      entry.count = reader.u32();
      if (!reader.valid() || kind < 1 || kind > 3) {
        return false;
      }
      entry.kind = static_cast<GuestCompositionKind>(kind);
      const bool valid =
        entry.kind == GuestCompositionKind::Visual
          ? entry.first != 0 && entry.count == 0
        : entry.kind == GuestCompositionKind::Batches
          ? entry.count != 0
          : composition.target == 0 && entry.first == 0 && entry.count == 0;
      if (!valid) {
        return false;
      }
    }
    return true;
  }

  static void writeWorldOperation(GuestWireWriter& output,
                                  const GuestWorldOperation& operation)
  {
    output.u32(static_cast<std::uint32_t>(operation.op));
    output.u32(operation.id);
    switch (operation.op) {
      case GuestWorldOp::MaterialCreate:
      case GuestWorldOp::MaterialUpdate:
        writeFloats(output, operation.material.tint.data(), 4);
        output.u32((operation.material.receivesShadow ? 1u : 0u) |
                   (operation.material.castsShadow ? 2u : 0u) |
                   (operation.material.blend ? 4u : 0u));
        break;
      case GuestWorldOp::InstanceCreate:
        operation.mesh.write(output);
        output.u32(operation.firstIndex);
        output.u32(operation.indexCount);
        output.u32(operation.materialId);
        writeFloats(output, operation.transform.data(), 16);
        writeFloats(output, operation.tint.data(), 4);
        output.u32(operation.visible ? 1u : 0u);
        break;
      case GuestWorldOp::InstanceUpdate:
        writeFloats(output, operation.tint.data(), 4);
        output.u32(operation.visible ? 1u : 0u);
        break;
      case GuestWorldOp::InstanceTransform:
        writeFloats(output, operation.transform.data(), 16);
        break;
      case GuestWorldOp::Environment: {
        const GuestWorldEnvironment& environment = operation.environment;
        writeFloats(output, environment.lightDirection.data(), 3);
        writeFloats(output, environment.lightColor.data(), 3);
        writeFloats(output, environment.ambientColor.data(), 3);
        output.u32((environment.shadowsEnabled ? 1u : 0u) |
                   (environment.shadowPcf ? 2u : 0u));
        output.f32(environment.shadowBias);
        output.f32(environment.shadowSlopeScale);
        output.f32(environment.shadowNormalOffset);
        output.u32(environment.shadowMapSize);
        output.f32(environment.shadowMinimumRadius);
        output.f32(environment.shadowLightDistance);
        output.f32(environment.shadowCasterDistance);
        break;
      }
      case GuestWorldOp::MaterialDestroy:
      case GuestWorldOp::InstanceDestroy:
        break;
    }
  }

  // Structure and values only; ids and meshes are checked against the host's
  // world before anything is applied.
  static bool readWorldOperation(GuestWireReader& reader,
                                 GuestWorldOperation& operation)
  {
    operation = GuestWorldOperation{};
    const std::uint32_t op = reader.u32();
    operation.id = reader.u32();
    if (!reader.valid() ||
        op < static_cast<std::uint32_t>(GuestWorldOp::MaterialCreate) ||
        op > static_cast<std::uint32_t>(GuestWorldOp::Environment)) {
      return false;
    }
    operation.op = static_cast<GuestWorldOp>(op);
    if ((operation.op == GuestWorldOp::Environment) != (operation.id == 0)) {
      return false;
    }
    std::uint32_t flags = 0;
    switch (operation.op) {
      case GuestWorldOp::MaterialCreate:
      case GuestWorldOp::MaterialUpdate:
        if (!readFloats(reader, operation.material.tint.data(), 4)) {
          return false;
        }
        flags = reader.u32();
        operation.material.receivesShadow = (flags & 1u) != 0;
        operation.material.castsShadow = (flags & 2u) != 0;
        operation.material.blend = (flags & 4u) != 0;
        return reader.valid() && flags <= 7u;
      case GuestWorldOp::InstanceCreate:
        operation.mesh = GuestResourceId::read(reader);
        operation.firstIndex = reader.u32();
        operation.indexCount = reader.u32();
        operation.materialId = reader.u32();
        if (!reader.valid() || operation.mesh.owner == 0 ||
            operation.mesh.slot == 0 || operation.mesh.generation == 0 ||
            operation.mesh.kind != GuestResourceKind::Mesh ||
            operation.indexCount == 0 || operation.indexCount % 3u != 0 ||
            operation.materialId == 0 ||
            !readFloats(reader, operation.transform.data(), 16) ||
            !readFloats(reader, operation.tint.data(), 4)) {
          return false;
        }
        flags = reader.u32();
        operation.visible = flags != 0;
        return reader.valid() && flags <= 1u;
      case GuestWorldOp::InstanceUpdate:
        if (!readFloats(reader, operation.tint.data(), 4)) {
          return false;
        }
        flags = reader.u32();
        operation.visible = flags != 0;
        return reader.valid() && flags <= 1u;
      case GuestWorldOp::InstanceTransform:
        return readFloats(reader, operation.transform.data(), 16);
      case GuestWorldOp::Environment: {
        GuestWorldEnvironment& environment = operation.environment;
        if (!readFloats(reader, environment.lightDirection.data(), 3) ||
            !readFloats(reader, environment.lightColor.data(), 3) ||
            !readFloats(reader, environment.ambientColor.data(), 3)) {
          return false;
        }
        flags = reader.u32();
        environment.shadowsEnabled = (flags & 1u) != 0;
        environment.shadowPcf = (flags & 2u) != 0;
        environment.shadowBias = reader.f32();
        environment.shadowSlopeScale = reader.f32();
        environment.shadowNormalOffset = reader.f32();
        environment.shadowMapSize = reader.u32();
        environment.shadowMinimumRadius = reader.f32();
        environment.shadowLightDistance = reader.f32();
        environment.shadowCasterDistance = reader.f32();
        const float scalars[] = {
          environment.shadowBias,          environment.shadowSlopeScale,
          environment.shadowNormalOffset,  environment.shadowMinimumRadius,
          environment.shadowLightDistance, environment.shadowCasterDistance
        };
        for (float value : scalars) {
          if (!std::isfinite(value)) {
            return false;
          }
        }
        return reader.valid() && flags <= 3u &&
               environment.shadowMapSize >= 64 &&
               environment.shadowMapSize <= 8192;
      }
      case GuestWorldOp::MaterialDestroy:
      case GuestWorldOp::InstanceDestroy:
        return true;
    }
    return false;
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
    if (worldOperations.size() > UINT32_MAX) {
      throw std::length_error("Too many guest world operations");
    }
    output.u32(static_cast<std::uint32_t>(worldOperations.size()));
    for (const GuestWorldOperation& operation : worldOperations) {
      writeWorldOperation(output, operation);
    }
    if (visualOperations.size() > UINT32_MAX ||
        compositions.size() > MaximumSurfaces + 1u) {
      throw std::length_error("Too many guest visual operations");
    }
    output.u32(static_cast<std::uint32_t>(visualOperations.size()));
    for (const GuestVisualOperation& operation : visualOperations) {
      writeVisualOperation(output, operation);
    }
    output.u32(static_cast<std::uint32_t>(compositions.size()));
    for (const GuestComposition& composition : compositions) {
      writeComposition(output, composition);
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

  // Resets a reused batch to its defaults, keeping its geometry capacity.
  static void recycleBatch(GuestBatch& batch)
  {
    std::vector<GuestVertex> vertices = std::move(batch.vertices);
    std::vector<std::uint32_t> indices = std::move(batch.indices);
    batch = GuestBatch{};
    vertices.clear();
    indices.clear();
    batch.vertices = std::move(vertices);
    batch.indices = std::move(indices);
  }

  // Decode transactionally before resource resolution or renderer calls.
  // Counts are checked against both byte availability and aggregate quotas.
  static bool read(std::span<const std::byte> input,
                   GuestFrame& output,
                   GuestFrameLimits limits = {})
  {
    GuestFrame frame;
    if (!decode(input, frame, limits)) {
      return false;
    }
    output = std::move(frame);
    return true;
  }

  // As read, but decodes in place into a retained scratch frame, reusing its
  // containers' capacity. On failure `frame` holds unspecified partial
  // contents; callers keep their live frame separate and swap on success.
  static bool decode(std::span<const std::byte> input,
                     GuestFrame& frame,
                     GuestFrameLimits limits = {})
  {
    if (input.size() > limits.bytes) {
      return false;
    }
    GuestWireReader reader(input);
    const std::uint32_t magic = reader.u32();
    const std::uint32_t version = reader.u32();
    const GuestFrame defaults;
    frame.hasCamera = false;
    frame.camera = defaults.camera;
    frame.shadowCasters.clear();
    frame.surfaces.clear();
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
      recycleBatch(batch);
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
    // Reused writes keep their pixel capacity; every field is overwritten.
    frame.textureWrites.resize(writes);
    for (std::uint32_t index = 0; index < writes; ++index) {
      GuestTextureWrite& write = frame.textureWrites[index];
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
    if (version < 4) {
      frame.meshWrites.clear();
    } else {
      // Minimum encoded write: 20-byte id, target, offset, count, one byte.
      const std::uint32_t meshWrites = reader.u32();
      if (!reader.valid() || meshWrites > limits.meshWrites ||
          meshWrites > reader.remaining() / 33u) {
        return false;
      }
      std::uint32_t writeBytes = 0;
      // Reused writes keep their byte capacity; every field is overwritten.
      frame.meshWrites.resize(meshWrites);
      for (std::uint32_t index = 0; index < meshWrites; ++index) {
        GuestFrameMeshWrite& write = frame.meshWrites[index];
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
      }
    }
    if (version >= 5 && !readSurfaces(reader, limits, totals, frame)) {
      return false;
    }
    frame.worldOperations.clear();
    if (version >= 6) {
      // Minimum encoded operation: op and id (8 bytes).
      const std::uint32_t operations = reader.u32();
      if (!reader.valid() || operations > limits.worldOperations ||
          operations > reader.remaining() / 8u) {
        return false;
      }
      frame.worldOperations.resize(operations);
      for (GuestWorldOperation& operation : frame.worldOperations) {
        if (!readWorldOperation(reader, operation)) {
          return false;
        }
      }
    }
    // Version 7 reuses the containers below (resize keeps what it can);
    // older frames carry neither section.
    if (version < 7) {
      frame.visualOperations.clear();
      frame.compositions.clear();
    } else {
      // Minimum encoded operation: op and id (8 bytes).
      const std::uint32_t operations = reader.u32();
      if (!reader.valid() ||
          operations > limits.worldOperations - frame.worldOperations.size() ||
          operations > reader.remaining() / 8u) {
        return false;
      }
      std::uint64_t textBytes = 0;
      std::vector<GuestVisualOperation>& visuals = frame.visualOperations;
      const std::size_t kept =
        std::min<std::size_t>(visuals.size(), operations);
      for (std::size_t index = kept; index < visuals.size(); ++index) {
        if (visuals[index].item.text.capacity() != 0 &&
            frame.spareText.size() < 64) {
          frame.spareText.push_back(std::move(visuals[index].item.text));
        }
      }
      visuals.resize(operations);
      for (std::size_t index = kept;
           index < visuals.size() && !frame.spareText.empty();
           ++index) {
        visuals[index].item.text = std::move(frame.spareText.back());
        frame.spareText.pop_back();
      }
      for (GuestVisualOperation& operation : frame.visualOperations) {
        if (!readVisualOperation(reader, operation)) {
          return false;
        }
        textBytes += operation.item.text.size();
        if (textBytes > limits.textBytes) {
          return false;
        }
      }
      // Minimum encoded composition: target and same flag (8 bytes).
      const std::uint32_t compositions = reader.u32();
      if (!reader.valid() || compositions > MaximumSurfaces + 1u ||
          compositions > reader.remaining() / 8u) {
        return false;
      }
      std::vector<GuestComposition>& targets = frame.compositions;
      const std::size_t reused =
        std::min<std::size_t>(targets.size(), compositions);
      for (std::size_t index = reused; index < targets.size(); ++index) {
        if (frame.spareEntries.size() < MaximumSurfaces + 1u) {
          frame.spareEntries.push_back(std::move(targets[index].entries));
        }
      }
      targets.resize(compositions);
      for (std::size_t index = reused;
           index < targets.size() && !frame.spareEntries.empty();
           ++index) {
        targets[index].entries = std::move(frame.spareEntries.back());
        frame.spareEntries.pop_back();
      }
      for (std::size_t index = 0; index < frame.compositions.size(); ++index) {
        if (!readComposition(reader, limits, frame.compositions[index])) {
          return false;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
          if (frame.compositions[earlier].target ==
              frame.compositions[index].target) {
            return false;
          }
        }
      }
    }
    return reader.finished();
  }
};
