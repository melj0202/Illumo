#include <Illumo/Rendering/Font.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmRenderServices.h>
#include <IllumoGuest/Protocol.h>
#include <exception>

static const char*
fontPath(const std::string& name)
{
  if (name == "default" || name == "mono-bold" ||
      name == "Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf") {
    return "Fonts/Space_Mono/SpaceMono-Bold.ttf";
  }
  if (name == "mono-regular" ||
      name == "Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf") {
    return "Fonts/Space_Mono/SpaceMono-Regular.ttf";
  }
  if (name == "handjet-bold" ||
      name == "Assets/Fonts/Handjet/static/Handjet-Bold.ttf") {
    return "Fonts/Handjet/static/Handjet-Bold.ttf";
  }
  if (name == "handjet-regular" ||
      name == "Assets/Fonts/Handjet/static/Handjet-Regular.ttf") {
    return "Fonts/Handjet/static/Handjet-Regular.ttf";
  }
  return nullptr;
}

// An engine font request: a catalog path plus how to sample it.
struct EngineFontRequest
{
  const char* path = nullptr;
  FontFaceOptions options;
};

// Resolves `family[:weight[:glyphs]]`. Fixed faces take the plain names
// above. The variable family "kikuta" takes an integer weight of 1..1000
// (default 400) and an optional subset of 1..32 printable ASCII glyphs; the
// characters it lacks come from Space Mono of a similar weight. Anything else
// is outside the engine catalog.
static bool
resolveEngineFont(const std::string& name, EngineFontRequest& request)
{
  const std::size_t colon = name.find(':');
  const std::string family = name.substr(0, colon);
  if (family != "kikuta") {
    request.path = colon == std::string::npos ? fontPath(name) : nullptr;
    return request.path != nullptr;
  }
  int weight = 400;
  std::string glyphs;
  if (colon != std::string::npos) {
    const std::size_t second = name.find(':', colon + 1);
    const std::string digits = name.substr(
      colon + 1,
      second == std::string::npos ? std::string::npos : second - colon - 1);
    if (digits.empty() || digits.size() > 4 ||
        digits.find_first_not_of("0123456789") != std::string::npos) {
      return false;
    }
    weight = std::stoi(digits);
    if (weight < 1 || weight > 1000) {
      return false;
    }
    if (second != std::string::npos) {
      glyphs = name.substr(second + 1);
      if (glyphs.empty() || glyphs.size() > 32) {
        return false;
      }
      for (char character : glyphs) {
        if (character < 32 || character > 126) {
          return false;
        }
      }
    }
  }
  request.path = "Fonts/Kikuta/Kikuta-Variable.ttf";
  request.options.weight = static_cast<float>(weight);
  request.options.glyphs = glyphs;
  request.options.fallbackPath = weight >= 600
                                   ? "Fonts/Space_Mono/SpaceMono-Bold.ttf"
                                   : "Fonts/Space_Mono/SpaceMono-Regular.ttf";
  return true;
}

WasmRenderServices::WasmRenderServices(WasmFrameRenderer& frames,
                                       std::uint32_t grants,
                                       std::filesystem::path engineAssets)
  : m_frames(frames)
  , m_grants(grants)
  , m_engineAssets(std::move(engineAssets))
{
}

bool
WasmRenderServices::process(std::span<const std::byte> requests,
                            std::vector<std::byte>& completions)
try {
  completions.clear();
  m_error.clear();
  GuestServices incoming;
  if (!GuestServices::read(requests, incoming, true)) {
    m_error = "Malformed service request batch";
    return false;
  }
  std::uint64_t last = m_lastRequest;
  std::size_t queuedBytes = m_pendingBytes;
  if (incoming.records.size() + m_pending.size() >
      GuestServices::MaximumRecords) {
    m_error = "Too many outstanding resource requests";
    return false;
  }
  for (const GuestServiceRecord& record : incoming.records) {
    queuedBytes += record.payload.size() + 20;
    if (queuedBytes > GuestServices::MaximumBytes) {
      m_error = "Resource request backlog exceeds quota";
      return false;
    }
    if (record.request <= last) {
      m_error = "Replayed service request";
      return false;
    }
    last = record.request;
    bool valid = false;
    if (record.operation == GuestService::CreateTexture) {
      GuestTextureRequest texture;
      valid = GuestTextureRequest::read(record.payload, texture);
    } else if (record.operation == GuestService::ReleaseTexture) {
      GuestWireReader reader(record.payload);
      GuestResourceId::read(reader);
      valid = reader.finished();
    } else if (record.operation == GuestService::LoadFont) {
      GuestFontRequest font;
      valid = GuestFontRequest::read(record.payload, font);
    } else if (record.operation == GuestService::Log) {
      GuestWireReader reader(record.payload);
      const std::uint32_t level = reader.u32();
      reader.text(4096);
      valid = reader.finished() && level >= 1 && level <= 4;
    } else if (record.operation == GuestService::CreateMesh) {
      GuestMeshRequest mesh;
      valid = GuestMeshRequest::read(record.payload, mesh);
    } else if (record.operation == GuestService::WriteMesh) {
      GuestMeshWrite write;
      valid = GuestMeshWrite::read(record.payload, write);
    } else if (record.operation == GuestService::ReleaseMesh) {
      GuestWireReader reader(record.payload);
      const GuestResourceId id = GuestResourceId::read(reader);
      valid = reader.finished() && id.kind == GuestResourceKind::Mesh;
    } else if (record.operation == GuestService::CreateCubemap) {
      GuestCubemapRequest cubemap;
      valid = GuestCubemapRequest::read(record.payload, cubemap);
    }
    if (!valid) {
      m_error = "Malformed service payload";
      return false;
    }
  }
  for (GuestServiceRecord& record : incoming.records) {
    m_pending.push_back(std::move(record));
  }
  m_pendingBytes = queuedBytes;
  m_lastRequest = last;
  GuestServices results;
  std::size_t fontCount = 0;
  while (!m_pending.empty()) {
    if (m_pending.front().operation == GuestService::LoadFont &&
        fontCount == 1) {
      break;
    }
    GuestServiceRecord record = std::move(m_pending.front());
    m_pending.pop_front();
    m_pendingBytes -= record.payload.size() + 20;
    GuestServiceRecord result{
      record.request, record.operation, GuestServiceStatus::Rejected, {}
    };
    GuestWireWriter payload;
    const std::uint32_t needed =
      static_cast<std::uint32_t>(GuestCapability::Render) |
      (record.operation == GuestService::LoadFont
         ? static_cast<std::uint32_t>(GuestCapability::Assets)
         : 0u);
    if ((m_grants & needed) != needed) {
      results.records.push_back(std::move(result));
      continue;
    }
    if (record.operation == GuestService::Log) {
      GuestWireReader reader(record.payload);
      const std::uint32_t level = reader.u32();
      const std::string text = "WASM: " + reader.text(4096);
      if (level == 1) {
        Logger::LogError(text);
      } else if (level == 2) {
        Logger::LogWarning(text);
      } else if (level == 4) {
        Logger::LogTrace(text);
      } else {
        Logger::LogInfo(text);
      }
      result.status = GuestServiceStatus::Complete;
    } else if (record.operation == GuestService::CreateTexture) {
      GuestTextureRequest texture;
      GuestTextureRequest::read(record.payload, texture);
      const GuestResourceId id = m_frames.createTexture(texture.pixels,
                                                        texture.width,
                                                        texture.height,
                                                        texture.channels,
                                                        texture.linear);
      if (id.owner != 0) {
        id.write(payload);
        result.status = GuestServiceStatus::Complete;
      }
    } else if (record.operation == GuestService::ReleaseTexture) {
      GuestWireReader reader(record.payload);
      if (m_frames.releaseTexture(GuestResourceId::read(reader))) {
        result.status = GuestServiceStatus::Complete;
      }
    } else if (record.operation == GuestService::CreateMesh) {
      GuestMeshRequest request;
      GuestMeshRequest::read(record.payload, request);
      const GuestResourceId id = m_frames.createMesh(request);
      if (id.owner != 0) {
        id.write(payload);
        result.status = GuestServiceStatus::Complete;
      }
    } else if (record.operation == GuestService::WriteMesh) {
      GuestMeshWrite write;
      GuestMeshWrite::read(record.payload, write);
      if (m_frames.writeMesh(write)) {
        result.status = GuestServiceStatus::Complete;
      }
    } else if (record.operation == GuestService::ReleaseMesh) {
      GuestWireReader reader(record.payload);
      if (m_frames.releaseMesh(GuestResourceId::read(reader))) {
        result.status = GuestServiceStatus::Complete;
      }
    } else if (record.operation == GuestService::CreateCubemap) {
      GuestCubemapRequest request;
      GuestCubemapRequest::read(record.payload, request);
      const GuestResourceId id =
        m_frames.createCubemap(request.faces, request.size);
      if (id.owner != 0) {
        id.write(payload);
        result.status = GuestServiceStatus::Complete;
      }
    } else if (record.operation == GuestService::LoadFont) {
      ++fontCount;
      GuestFontRequest requested;
      GuestFontRequest::read(record.payload, requested);
      EngineFontRequest engineFont;
      const bool known = resolveEngineFont(requested.name, engineFont);
      // Do not use Font's process-global unbounded file cache for guest input.
      std::shared_ptr<Font> font = std::make_shared<Font>();
      if (known && m_engineAssets.is_absolute()) {
        FontFaceOptions options = engineFont.options;
        if (!options.fallbackPath.empty()) {
          options.fallbackPath =
            (m_engineAssets / options.fallbackPath).string();
        }
        if (!font->loadFile((m_engineAssets / engineFont.path).string(),
                            requested.pixelSize,
                            options)) {
          font = Font::createFallback(requested.pixelSize);
        }
      } else {
        font.reset();
      }
      if (font && font->isValid()) {
        GuestFont description;
        const FontMetrics& metrics = font->getMetrics();
        description.metrics = { metrics.pixelSize,
                                metrics.ascender,
                                metrics.descender,
                                metrics.lineHeight,
                                metrics.maxAdvance };
        for (char32_t codepoint = 32; codepoint <= 126; ++codepoint) {
          const GlyphInfo* glyph = font->getGlyph(codepoint);
          if (glyph != nullptr && glyph->codepoint == codepoint) {
            description.glyphs.push_back(
              { static_cast<std::uint32_t>(codepoint),
                { glyph->u0,
                  glyph->v0,
                  glyph->u1,
                  glyph->v1,
                  glyph->width,
                  glyph->height,
                  glyph->bearingX,
                  glyph->bearingY,
                  glyph->advanceX },
                glyph->visible });
          }
        }
        description.atlas = m_frames.createTexture(
          std::as_bytes(std::span(font->getAtlasPixels())),
          static_cast<std::uint32_t>(font->getAtlasWidth()),
          static_cast<std::uint32_t>(font->getAtlasHeight()),
          4,
          true);
        if (description.atlas.owner != 0) {
          description.write(payload);
          result.status = GuestServiceStatus::Complete;
        }
      }
    }
    result.payload = payload.take();
    results.records.push_back(std::move(result));
  }
  m_lastRequest = last;
  GuestWireWriter writer;
  results.write(writer);
  completions = writer.take();
  return true;
} catch (const std::exception& exception) {
  m_error = exception.what();
  completions.clear();
  return false;
}
