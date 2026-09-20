#pragma once

#include <IllumoGuest/ResourceId.h>
#include <array>
#include <cmath>

enum class GuestBatchStyle : std::uint32_t
{
  Shape = 1,
  Sprite = 2,
  Canvas = 3
};

enum class GuestLayer : std::uint32_t
{
  World = 1,
  Ui = 2
};

struct GuestVertex
{
  std::array<float, 3> position{};
  std::uint32_t rgba = UINT32_MAX;
  std::array<float, 2> uv{};
};

struct GuestBatch
{
  GuestBatchStyle style = GuestBatchStyle::Shape;
  GuestLayer layer = GuestLayer::Ui;
  GuestResourceId texture;
  std::array<float, 16> mvp{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  bool clipped = false;
  std::array<float, 4> clip{}; // logical top-left x, y, width, height
  std::vector<GuestVertex> vertices;
  std::vector<std::uint32_t> indices;
};

struct GuestFrameLimits
{
  std::uint32_t bytes = 64u * 1024u * 1024u;
  std::uint32_t batches = 4096;
  std::uint32_t vertices = 1000000;
  std::uint32_t indices = 3000000;
  std::uint32_t textureWrites = 256;
  std::uint32_t uploadBytes = 16u * 1024u * 1024u;
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

struct GuestFrame
{
  static constexpr std::uint32_t Magic = 0x31465249u; // IRF1
  float width = 1280;
  float height = 720;
  std::vector<GuestBatch> batches;
  std::vector<GuestTextureWrite> textureWrites;

  void write(GuestWireWriter& output) const
  {
    if (batches.size() > UINT32_MAX) {
      throw std::length_error("Too many guest batches");
    }
    output.u32(Magic);
    output.u32(1);
    output.f32(width);
    output.f32(height);
    output.u32(static_cast<std::uint32_t>(batches.size()));
    for (const GuestBatch& batch : batches) {
      if (batch.vertices.size() > UINT32_MAX ||
          batch.indices.size() > UINT32_MAX) {
        throw std::length_error("Guest geometry exceeds ABI range");
      }
      output.u32(static_cast<std::uint32_t>(batch.style));
      output.u32(static_cast<std::uint32_t>(batch.layer));
      batch.texture.write(output);
      output.u32(batch.clipped ? 1 : 0);
      for (float value : batch.clip) {
        output.f32(value);
      }
      for (float value : batch.mvp) {
        output.f32(value);
      }
      output.u32(static_cast<std::uint32_t>(batch.vertices.size()));
      output.u32(static_cast<std::uint32_t>(batch.indices.size()));
      for (const GuestVertex& vertex : batch.vertices) {
        for (float value : vertex.position) {
          output.f32(value);
        }
        output.u32(vertex.rgba);
        for (float value : vertex.uv) {
          output.f32(value);
        }
      }
      for (std::uint32_t index : batch.indices) {
        output.u32(index);
      }
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
    const std::uint32_t count = reader.u32();
    if (magic != Magic || version != 1 || !reader.valid() ||
        !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
        frame.width < 1 || frame.height < 1 || frame.width > 65536 ||
        frame.height > 65536 || count > limits.batches ||
        count > reader.remaining() / 120u) {
      return false;
    }
    std::uint32_t totalVertices = 0;
    std::uint32_t totalIndices = 0;
    std::uint32_t lastLayer = 1;
    frame.batches.reserve(count);
    for (std::uint32_t batchIndex = 0; batchIndex < count; ++batchIndex) {
      GuestBatch batch;
      const std::uint32_t style = reader.u32();
      const std::uint32_t layer = reader.u32();
      batch.style = static_cast<GuestBatchStyle>(style);
      batch.layer = static_cast<GuestLayer>(layer);
      batch.texture = GuestResourceId::read(reader);
      const std::uint32_t clipped = reader.u32();
      batch.clipped = clipped != 0;
      if (style < 1 || style > 3 || layer < 1 || layer > 2 || clipped > 1) {
        return false;
      }
      if (layer < lastLayer) {
        return false;
      }
      lastLayer = layer;
      if (style == 1) {
        if (batch.texture.owner != 0 || batch.texture.slot != 0 ||
            batch.texture.generation != 0) {
          return false;
        }
      } else if (batch.texture.owner == 0 || batch.texture.slot == 0 ||
                 batch.texture.generation == 0 ||
                 batch.texture.kind != GuestResourceKind::Texture) {
        return false;
      }
      for (float& value : batch.clip) {
        value = reader.f32();
        if (!std::isfinite(value) || std::abs(value) > 65536) {
          return false;
        }
      }
      if (batch.clip[2] < 0 || batch.clip[3] < 0) {
        return false;
      }
      for (float& value : batch.mvp) {
        value = reader.f32();
        if (!std::isfinite(value)) {
          return false;
        }
      }
      const std::uint32_t vertices = reader.u32();
      const std::uint32_t indices = reader.u32();
      if (!reader.valid() || vertices == 0 || indices == 0 ||
          indices % 3 != 0 || vertices > limits.vertices - totalVertices ||
          indices > limits.indices - totalIndices ||
          vertices > reader.remaining() / 24u) {
        return false;
      }
      totalVertices += vertices;
      totalIndices += indices;
      batch.vertices.resize(vertices);
      for (GuestVertex& vertex : batch.vertices) {
        for (float& value : vertex.position) {
          value = reader.f32();
          if (!std::isfinite(value)) {
            return false;
          }
        }
        vertex.rgba = reader.u32();
        for (float& value : vertex.uv) {
          value = reader.f32();
          if (!std::isfinite(value)) {
            return false;
          }
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
      frame.batches.push_back(std::move(batch));
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
    if (!reader.finished()) {
      return false;
    }
    output = std::move(frame);
    return true;
  }
};
