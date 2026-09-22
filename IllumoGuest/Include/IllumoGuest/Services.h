#pragma once

#include <IllumoGuest/ResourceId.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>

enum class GuestService : std::uint32_t
{
  CreateTexture = 1,
  ReleaseTexture = 2,
  LoadFont = 3,
  Log = 4,
  Job = 5,
  File = 6,
  Display = 7,
  Clipboard = 8,
  Console = 9,
  Dialog = 10,
  // Frame schema v3 resources.
  CreateMesh = 11,
  WriteMesh = 12,
  ReleaseMesh = 13,
  CreateCubemap = 14
};

enum class GuestServiceStatus : std::uint32_t
{
  Request = 0,
  Complete = 1,
  Rejected = 2
};

struct GuestServiceRecord
{
  std::uint64_t request = 0;
  GuestService operation = GuestService::CreateTexture;
  GuestServiceStatus status = GuestServiceStatus::Request;
  std::vector<std::byte> payload;
};

// One copied, bounded exchange at a frame boundary. Results arrive on a later
// update, never through a callback while the requesting store is executing.
struct GuestServices
{
  static constexpr std::size_t MaximumRecords = 32;
  static constexpr std::size_t MaximumBytes = 16 * 1024 * 1024;
  static constexpr std::size_t MaximumJobBytes = 15 * 1024 * 1024;
  std::vector<GuestServiceRecord> records;

  void write(GuestWireWriter& output) const
  {
    output.u32(0x31565349); // ISV1
    output.u32(1);
    output.u32(static_cast<std::uint32_t>(records.size()));
    for (const GuestServiceRecord& record : records) {
      output.u64(record.request);
      output.u32(static_cast<std::uint32_t>(record.operation));
      output.u32(static_cast<std::uint32_t>(record.status));
      output.u32(static_cast<std::uint32_t>(record.payload.size()));
      output.bytes(record.payload);
    }
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestServices& output,
                   bool requests)
  {
    if (bytes.size() > MaximumBytes) {
      return false;
    }
    GuestWireReader reader(bytes);
    if (reader.u32() != 0x31565349 || reader.u32() != 1) {
      return false;
    }
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || count > MaximumRecords) {
      return false;
    }
    GuestServices candidate;
    for (std::uint32_t index = 0; index < count; ++index) {
      GuestServiceRecord record;
      record.request = reader.u64();
      const std::uint32_t operation = reader.u32();
      const std::uint32_t status = reader.u32();
      const std::span<const std::byte> payload = reader.bytes(reader.u32());
      if (!reader.valid() || record.request == 0 || operation < 1 ||
          operation > static_cast<std::uint32_t>(GuestService::CreateCubemap) ||
          (requests ? status != 0 : status < 1 || status > 2)) {
        return false;
      }
      for (const GuestServiceRecord& prior : candidate.records) {
        if (prior.request == record.request) {
          return false;
        }
      }
      record.operation = static_cast<GuestService>(operation);
      record.status = static_cast<GuestServiceStatus>(status);
      record.payload.assign(payload.begin(), payload.end());
      candidate.records.push_back(std::move(record));
    }
    if (!reader.finished()) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

struct GuestTextureRequest
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t channels = 0;
  bool linear = false;
  std::vector<std::byte> pixels;
  void write(GuestWireWriter& output) const
  {
    output.u32(width);
    output.u32(height);
    output.u32(channels);
    output.u32(linear ? 1 : 0);
    output.bytes(pixels);
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestTextureRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestTextureRequest candidate;
    candidate.width = reader.u32();
    candidate.height = reader.u32();
    candidate.channels = reader.u32();
    const std::uint32_t linear = reader.u32();
    if (!reader.valid() || candidate.width == 0 || candidate.width > 8192 ||
        candidate.height == 0 || candidate.height > 8192 || linear > 1 ||
        (candidate.channels != 1 && candidate.channels != 3 &&
         candidate.channels != 4)) {
      return false;
    }
    const std::uint64_t count = static_cast<std::uint64_t>(candidate.width) *
                                candidate.height * candidate.channels;
    if (count != reader.remaining() ||
        count > GuestServices::MaximumBytes - 64) {
      return false;
    }
    const std::span<const std::byte> pixels =
      reader.bytes(static_cast<std::size_t>(count));
    candidate.pixels.assign(pixels.begin(), pixels.end());
    candidate.linear = linear != 0;
    output = std::move(candidate);
    return true;
  }
};

// A retained host mesh (frame schema v3). The style fixes the vertex layout:
// Shape 16, Sprite 24, Canvas 32 and LitMesh 36 bytes per vertex. Bytes then
// arrive in order through WriteMesh; the host validates the whole mesh when
// the last byte lands and rejects that write if it is malformed.
struct GuestMeshRequest
{
  static constexpr std::uint32_t MaximumVertexBytes = 192u * 1024u * 1024u;
  static constexpr std::uint32_t MaximumIndexBytes = 64u * 1024u * 1024u;
  std::uint32_t style = 0;
  std::uint32_t vertexBytes = 0;
  std::uint32_t indexBytes = 0;
  static std::uint32_t stride(std::uint32_t style)
  {
    return style == 1 ? 16u : style == 2 ? 24u : style == 3 ? 32u : 36u;
  }
  void write(GuestWireWriter& output) const
  {
    output.u32(style);
    output.u32(vertexBytes);
    output.u32(indexBytes);
  }
  static bool read(std::span<const std::byte> bytes, GuestMeshRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestMeshRequest candidate;
    candidate.style = reader.u32();
    candidate.vertexBytes = reader.u32();
    candidate.indexBytes = reader.u32();
    if (!reader.finished() || candidate.style < 1 || candidate.style > 4 ||
        candidate.vertexBytes == 0 ||
        candidate.vertexBytes > MaximumVertexBytes ||
        candidate.vertexBytes % stride(candidate.style) != 0 ||
        candidate.indexBytes == 0 || candidate.indexBytes > MaximumIndexBytes ||
        candidate.indexBytes % 4 != 0) {
      return false;
    }
    output = candidate;
    return true;
  }
};

struct GuestMeshWrite
{
  static constexpr std::uint32_t MaximumChunk = 1024u * 1024u;
  GuestResourceId mesh;
  bool indices = false;
  std::uint32_t offset = 0;
  std::vector<std::byte> bytes;
  void write(GuestWireWriter& output) const
  {
    mesh.write(output);
    output.u32(indices ? 1 : 0);
    output.u32(offset);
    output.u32(static_cast<std::uint32_t>(bytes.size()));
    output.bytes(bytes);
  }
  static bool read(std::span<const std::byte> input, GuestMeshWrite& output)
  {
    GuestWireReader reader(input);
    GuestMeshWrite candidate;
    candidate.mesh = GuestResourceId::read(reader);
    const std::uint32_t target = reader.u32();
    candidate.offset = reader.u32();
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || target > 1 || count == 0 || count > MaximumChunk ||
        candidate.mesh.owner == 0 || candidate.mesh.slot == 0 ||
        candidate.mesh.generation == 0 ||
        candidate.mesh.kind != GuestResourceKind::Mesh) {
      return false;
    }
    const std::span<const std::byte> data = reader.bytes(count);
    if (!reader.finished()) {
      return false;
    }
    candidate.indices = target == 1;
    candidate.bytes.assign(data.begin(), data.end());
    output = std::move(candidate);
    return true;
  }
};

// Six square RGBA faces in +X, -X, +Y, -Y, +Z, -Z order (frame schema v3).
struct GuestCubemapRequest
{
  std::uint32_t size = 0;
  std::vector<std::byte> faces;
  static std::uint64_t bytesFor(std::uint32_t size)
  {
    return static_cast<std::uint64_t>(size) * size * 4u * 6u;
  }
  void write(GuestWireWriter& output) const
  {
    output.u32(size);
    output.bytes(faces);
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestCubemapRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestCubemapRequest candidate;
    candidate.size = reader.u32();
    if (!reader.valid() || candidate.size == 0 || candidate.size > 2048 ||
        bytesFor(candidate.size) != reader.remaining() ||
        bytesFor(candidate.size) > GuestServices::MaximumBytes - 64) {
      return false;
    }
    const std::span<const std::byte> faces =
      reader.bytes(static_cast<std::size_t>(bytesFor(candidate.size)));
    candidate.faces.assign(faces.begin(), faces.end());
    output = std::move(candidate);
    return true;
  }
};

struct GuestFontRequest
{
  // Engine-packaged font names only; package fonts require a separate asset
  // grant.
  std::string name = "default";
  float pixelSize = 32;
  void write(GuestWireWriter& output) const
  {
    output.text(name);
    output.f32(pixelSize);
  }
  static bool read(std::span<const std::byte> bytes, GuestFontRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestFontRequest candidate;
    candidate.name = reader.text(128);
    candidate.pixelSize = reader.f32();
    if (!reader.finished() || !std::isfinite(candidate.pixelSize) ||
        candidate.pixelSize < 8 || candidate.pixelSize > 256) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

struct GuestGlyph
{
  std::uint32_t codepoint = 0;
  std::array<float, 9>
    values{}; // UV rectangle, width/height, bearings, advance.
  bool visible = false;
};

struct GuestFont
{
  GuestResourceId atlas;
  std::array<float, 5>
    metrics{}; // pixel size, ascender, descender, line height, max advance.
  std::vector<GuestGlyph> glyphs;
  void write(GuestWireWriter& output) const
  {
    atlas.write(output);
    for (float value : metrics) {
      output.f32(value);
    }
    output.u32(static_cast<std::uint32_t>(glyphs.size()));
    for (const GuestGlyph& glyph : glyphs) {
      output.u32(glyph.codepoint);
      for (float value : glyph.values) {
        output.f32(value);
      }
      output.u32(glyph.visible ? 1 : 0);
    }
  }
  static bool read(std::span<const std::byte> bytes, GuestFont& output)
  {
    GuestWireReader reader(bytes);
    GuestFont candidate;
    candidate.atlas = GuestResourceId::read(reader);
    if (candidate.atlas.owner == 0 || candidate.atlas.slot == 0 ||
        candidate.atlas.generation == 0 ||
        candidate.atlas.kind != GuestResourceKind::Texture) {
      return false;
    }
    for (float& value : candidate.metrics) {
      value = reader.f32();
      if (!std::isfinite(value) || std::abs(value) > 1024) {
        return false;
      }
    }
    if (candidate.metrics[0] <= 0 || candidate.metrics[3] <= 0) {
      return false;
    }
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || count > 4096) {
      return false;
    }
    std::uint32_t previous = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
      GuestGlyph glyph;
      glyph.codepoint = reader.u32();
      if (glyph.codepoint <= previous || glyph.codepoint > 0x10ffff ||
          (glyph.codepoint >= 0xd800 && glyph.codepoint <= 0xdfff)) {
        return false;
      }
      previous = glyph.codepoint;
      for (float& value : glyph.values) {
        value = reader.f32();
        if (!std::isfinite(value) || std::abs(value) > 8192) {
          return false;
        }
      }
      const std::uint32_t visible = reader.u32();
      if (visible > 1 || glyph.values[0] < 0 || glyph.values[1] < 0 ||
          glyph.values[2] > 1 || glyph.values[3] > 1 ||
          glyph.values[2] < glyph.values[0] ||
          glyph.values[3] < glyph.values[1] || glyph.values[4] < 0 ||
          glyph.values[5] < 0) {
        return false;
      }
      glyph.visible = visible != 0;
      candidate.glyphs.push_back(glyph);
    }
    if (!reader.finished()) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// Guest-local request ownership. Consumers retain request IDs and poll their
// completion; no closures or pointers are sent to the host.
class GuestServiceQueue
{
public:
  std::uint64_t enqueue(GuestService operation, std::vector<std::byte> payload)
  {
    if (m_next == UINT64_MAX ||
        m_pending.size() >= GuestServices::MaximumRecords ||
        payload.size() > GuestServices::MaximumBytes - 64 ||
        m_queuedBytes + payload.size() + 20 > GuestServices::MaximumBytes) {
      return 0;
    }
    // Allocate both owners before publishing a request or charging its bytes.
    // A caught allocation failure must not strand an unobservable pending ID.
    m_outgoing.records.reserve(GuestServices::MaximumRecords);
    const std::uint64_t id = m_next++;
    m_pending.emplace(id, operation);
    const std::size_t bytes = payload.size() + 20;
    m_outgoing.records.push_back(
      { id, operation, GuestServiceStatus::Request, std::move(payload) });
    m_queuedBytes += bytes;
    return id;
  }
  bool exchange(std::span<const std::byte> input,
                std::vector<std::byte>& output)
  {
    GuestServices completed;
    if (!GuestServices::read(input, completed, false)) {
      return false;
    }
    for (const GuestServiceRecord& record : completed.records) {
      const std::map<std::uint64_t, GuestService>::const_iterator found =
        m_pending.find(record.request);
      if (found == m_pending.end() || found->second != record.operation ||
          m_completed.contains(record.request)) {
        return false;
      }
    }
    for (GuestServiceRecord& record : completed.records) {
      if (record.operation == GuestService::Log) {
        m_pending.erase(record.request);
      } else {
        m_completed.emplace(record.request, std::move(record));
      }
    }
    GuestWireWriter writer;
    m_outgoing.write(writer);
    output = writer.take();
    m_outgoing.records.clear();
    m_queuedBytes = 12;
    return true;
  }
  bool take(std::uint64_t id, GuestServiceRecord& output)
  {
    std::map<std::uint64_t, GuestServiceRecord>::iterator found =
      m_completed.find(id);
    if (found == m_completed.end()) {
      return false;
    }
    output = std::move(found->second);
    m_completed.erase(found);
    m_pending.erase(id);
    return true;
  }

private:
  std::uint64_t m_next = 1;
  std::size_t m_queuedBytes = 12;
  GuestServices m_outgoing;
  std::map<std::uint64_t, GuestService> m_pending;
  std::map<std::uint64_t, GuestServiceRecord> m_completed;
};
