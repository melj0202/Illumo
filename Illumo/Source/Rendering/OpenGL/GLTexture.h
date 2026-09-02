#pragma once

#include "GL/glew.h"
#include "TextureUploadPolicy.h"
#include <Illumo/Rendering/ITexture.h>
#include <array>
#include <cstring>
#include <string>
#include <tracy/Tracy.hpp>

class GLTexture : public ITexture
{
public:
  GLTexture()
    : m_path("")
    , m_id(0)
    , m_size({ 0, 0 })
    , m_channels(4)
    , m_pbo{ 0, 0, 0 }
    , m_pboFence{ nullptr, nullptr, nullptr }
    , m_pboIndex(0)
    , m_pboBytes(0)
  {
  }

  // channels: 1 = R8, 3 = RGB, 4 = RGBA (default)
  GLTexture(const unsigned char* data,
            int width,
            int height,
            int channels = 4,
            TextureFilter filter = TextureFilter::Nearest)
    : GLTexture(data,
                width,
                height,
                channels,
                TextureOptions{ filter,
                                TextureWrap::ClampToEdge,
                                TextureWrap::ClampToEdge,
                                false })
  {
  }

  GLTexture(const unsigned char* data,
            int width,
            int height,
            int channels,
            const TextureOptions& options)
    : m_path("")
    , m_id(0)
    , m_size({ width, height })
    , m_channels(normalizeChannels(channels))
    , m_pbo{ 0, 0, 0 }
    , m_pboFence{ nullptr, nullptr, nullptr }
    , m_pboIndex(0)
    , m_pboBytes(0)
  {
    UploadToGPU(data, width, height, m_channels, options);
  }

  static std::unique_ptr<GLTexture> CreateRenderTargetTexture(
    int width,
    int height,
    TextureFormat format,
    TextureFilter filter = TextureFilter::Nearest,
    TextureWrap wrap = TextureWrap::ClampToEdge)
  {
    auto tex = std::make_unique<GLTexture>();
    tex->m_size = { width, height };

    GLint internalFormat = GL_RGBA8;
    GLenum glFormat = GL_RGBA;
    GLenum glType = GL_UNSIGNED_BYTE;
    int channels = 4;

    switch (format) {
      case TextureFormat::RGBA8:
        internalFormat = GL_RGBA8;
        glFormat = GL_RGBA;
        glType = GL_UNSIGNED_BYTE;
        channels = 4;
        break;
      case TextureFormat::RGB8:
        internalFormat = GL_RGB8;
        glFormat = GL_RGB;
        glType = GL_UNSIGNED_BYTE;
        channels = 3;
        break;
      case TextureFormat::R8:
        internalFormat = GL_R8;
        glFormat = GL_RED;
        glType = GL_UNSIGNED_BYTE;
        channels = 1;
        break;
      case TextureFormat::RGBA16F:
        internalFormat = GL_RGBA16F;
        glFormat = GL_RGBA;
        glType = GL_FLOAT;
        channels = 4;
        break;
      case TextureFormat::RG16F:
        internalFormat = GL_RG16F;
        glFormat = GL_RG;
        glType = GL_FLOAT;
        channels = 2;
        break;
      case TextureFormat::R16F:
        internalFormat = GL_R16F;
        glFormat = GL_RED;
        glType = GL_FLOAT;
        channels = 1;
        break;
      case TextureFormat::Depth24:
        internalFormat = GL_DEPTH_COMPONENT24;
        glFormat = GL_DEPTH_COMPONENT;
        glType = GL_FLOAT;
        channels = 1;
        break;
      case TextureFormat::Depth24Stencil8:
        internalFormat = GL_DEPTH24_STENCIL8;
        glFormat = GL_DEPTH_STENCIL;
        glType = GL_UNSIGNED_INT_24_8;
        channels = 2;
        break;
      default:
        break;
    }

    tex->m_channels = channels;
    glGenTextures(1, &tex->m_id);
    glBindTexture(GL_TEXTURE_2D, tex->m_id);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 internalFormat,
                 width,
                 height,
                 0,
                 glFormat,
                 glType,
                 nullptr);

    const GLint glFilter =
      (filter == TextureFilter::Linear) ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);

    const GLint glWrap =
      (wrap == TextureWrap::Repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrap);

    if (format == TextureFormat::Depth24 ||
        format == TextureFormat::Depth24Stencil8) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
      float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
      glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
  }

  static std::unique_ptr<GLTexture> CreateDepthTexture(int width, int height)
  {
    return CreateRenderTargetTexture(width,
                                     height,
                                     TextureFormat::Depth24,
                                     TextureFilter::Nearest,
                                     TextureWrap::ClampToEdge);
  }

  ~GLTexture() override { Destroy(); }

  void Bind(unsigned int slot) const override
  {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_id);
  }

  unsigned int getID() const override { return m_id; }
  std::array<int, 2> getSize() const override { return m_size; }
  int getChannels() const override { return m_channels; }

  // CPU → GPU subimage upload with optional row stride and PBO ping-pong (P4).
  // data is host pointer (not PBO). srcRowStride is in pixels (0 = tightly
  // packed width). Small dirty rects pack tightly into a partial PBO region
  // (D-P6) instead of re-orphaning a full-texture buffer every frame.
  void UpdateSubImage(int x,
                      int y,
                      int width,
                      int height,
                      int channels,
                      const void* data,
                      int srcRowStridePixels)
  {
    ZoneScopedN("GLTexture.UpdateSubImage");
    if (!data || width <= 0 || height <= 0 || m_id == 0) {
      return;
    }

    const int ch = (channels > 0) ? channels : m_channels;
    const GLenum format = formatForChannels(ch);
    const int rowStride = (srcRowStridePixels > 0) ? srcRowStridePixels : width;
    const size_t bytesPerPixel = static_cast<size_t>(ch);
    const size_t fullBytes = static_cast<size_t>(m_size[0]) *
                             static_cast<size_t>(m_size[1]) * bytesPerPixel;
    const size_t packedBytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerPixel;
    const size_t srcRowBytes = static_cast<size_t>(rowStride) * bytesPerPixel;
    const size_t copyRowBytes = static_cast<size_t>(width) * bytesPerPixel;
    const unsigned char* srcBase = static_cast<const unsigned char*>(data);

    // Prefer tight packing for partial rects (avoids full-texture map cost).
    const bool usePacked = (width < m_size[0] || height < m_size[1]);
    const size_t stageBytes = usePacked ? packedBytes : fullBytes;

    if (TextureUploadPolicy::useDirectUpload(packedBytes)) {
      directUpload(x, y, width, height, format, data, rowStride);
      return;
    }

    ensurePBOs(fullBytes);
    std::array<TextureUploadSlotState, TextureUploadPolicy::kPboCount>
      slotStates;
    {
      ZoneScopedN("GLTexture.selectPBO");
      for (int candidate = 0; candidate < TextureUploadPolicy::kPboCount;
           ++candidate) {
        if (m_pboFence[candidate] == nullptr) {
          slotStates[static_cast<std::size_t>(candidate)] =
            TextureUploadSlotState::Unused;
          continue;
        }
        const GLenum waitResult = glClientWaitSync(m_pboFence[candidate], 0, 0);
        if (waitResult == GL_ALREADY_SIGNALED ||
            waitResult == GL_CONDITION_SATISFIED) {
          slotStates[static_cast<std::size_t>(candidate)] =
            TextureUploadSlotState::Signaled;
        } else {
          slotStates[static_cast<std::size_t>(candidate)] =
            TextureUploadSlotState::Busy;
        }
      }
    }
    const int availablePbo =
      TextureUploadPolicy::selectAvailableSlot(m_pboIndex, slotStates);
    if (availablePbo < 0) {
      directUpload(x, y, width, height, format, data, rowStride);
      return;
    }
    if (m_pboFence[availablePbo] != nullptr) {
      glDeleteSync(m_pboFence[availablePbo]);
      m_pboFence[availablePbo] = nullptr;
    }

    m_pboIndex = availablePbo;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[m_pboIndex]);

    // Map only the staging range; invalidate to avoid GPU readback stalls.
    // Reuses the existing PBO allocation (no glBufferData orphan each frame).
    void* mapped = nullptr;
    {
      ZoneScopedN("GLTexture.mapPBO");
      mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
                                0,
                                static_cast<GLsizeiptr>(stageBytes),
                                GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
    }
    if (!mapped) {
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      directUpload(x, y, width, height, format, data, rowStride);
      return;
    }

    unsigned char* dstBase = static_cast<unsigned char*>(mapped);
    {
      ZoneScopedN("GLTexture.copyPBO");
      if (usePacked) {
        for (int row = 0; row < height; ++row) {
          unsigned char* dst =
            dstBase + static_cast<size_t>(row) * copyRowBytes;
          const unsigned char* src =
            srcBase + static_cast<size_t>(row) * srcRowBytes;
          std::memcpy(dst, src, copyRowBytes);
        }
      } else {
        const size_t dstRowBytes =
          static_cast<size_t>(m_size[0]) * bytesPerPixel;
        const size_t dstOrigin =
          (static_cast<size_t>(y) * static_cast<size_t>(m_size[0]) +
           static_cast<size_t>(x)) *
          bytesPerPixel;
        for (int row = 0; row < height; ++row) {
          unsigned char* dst =
            dstBase + dstOrigin + static_cast<size_t>(row) * dstRowBytes;
          const unsigned char* src =
            srcBase + static_cast<size_t>(row) * srcRowBytes;
          std::memcpy(dst, src, copyRowBytes);
        }
      }
    }
    {
      ZoneScopedN("GLTexture.unmapPBO");
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    {
      ZoneScopedN("GLTexture.submitPBO");
      glBindTexture(GL_TEXTURE_2D, m_id);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      if (usePacked) {
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        x,
                        y,
                        width,
                        height,
                        format,
                        GL_UNSIGNED_BYTE,
                        reinterpret_cast<const GLvoid*>(0));
      } else {
        const size_t dstOrigin =
          (static_cast<size_t>(y) * static_cast<size_t>(m_size[0]) +
           static_cast<size_t>(x)) *
          bytesPerPixel;
        const GLvoid* pboOffset = reinterpret_cast<const GLvoid*>(dstOrigin);
        if (m_size[0] != width) {
          glPixelStorei(GL_UNPACK_ROW_LENGTH, m_size[0]);
        }
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        x,
                        y,
                        width,
                        height,
                        format,
                        GL_UNSIGNED_BYTE,
                        pboOffset);
        if (m_size[0] != width) {
          glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
      }
      m_pboFence[m_pboIndex] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }

  void Destroy() override
  {
    for (int index = 0; index < kPboCount; ++index) {
      if (m_pboFence[index] != nullptr) {
        glDeleteSync(m_pboFence[index]);
        m_pboFence[index] = nullptr;
      }
    }
    if (m_pbo[0] != 0) {
      glDeleteBuffers(kPboCount, m_pbo);
      m_pbo[0] = 0;
      m_pbo[1] = 0;
      m_pbo[2] = 0;
      m_pboBytes = 0;
    }
    if (m_id != 0) {
      glDeleteTextures(1, &m_id);
      m_id = 0;
    }
  }

private:
  static constexpr int kPboCount = TextureUploadPolicy::kPboCount;

  static int normalizeChannels(int channels)
  {
    if (channels == 1) {
      return 1;
    }
    if (channels == 3) {
      return 3;
    }
    return 4;
  }

  static GLenum formatForChannels(int channels)
  {
    if (channels == 1) {
      return GL_RED;
    }
    if (channels == 3) {
      return GL_RGB;
    }
    return GL_RGBA;
  }

  static GLenum internalFormatForChannels(int channels)
  {
    if (channels == 1) {
      return GL_R8;
    }
    if (channels == 3) {
      return GL_RGB8;
    }
    return GL_RGBA8;
  }

  void ensurePBOs(size_t bytes)
  {
    if (bytes == 0) {
      return;
    }
    if (m_pbo[0] != 0 && m_pboBytes >= bytes) {
      return;
    }
    if (m_pbo[0] != 0) {
      for (int index = 0; index < kPboCount; ++index) {
        if (m_pboFence[index] != nullptr) {
          glDeleteSync(m_pboFence[index]);
          m_pboFence[index] = nullptr;
        }
      }
      glDeleteBuffers(kPboCount, m_pbo);
      m_pbo[0] = 0;
      m_pbo[1] = 0;
      m_pbo[2] = 0;
    }
    glGenBuffers(kPboCount, m_pbo);
    m_pboBytes = bytes;
    for (int i = 0; i < kPboCount; ++i) {
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[i]);
      glBufferData(GL_PIXEL_UNPACK_BUFFER,
                   static_cast<GLsizeiptr>(bytes),
                   nullptr,
                   GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    m_pboIndex = 0;
  }

  void directUpload(int x,
                    int y,
                    int width,
                    int height,
                    GLenum format,
                    const void* data,
                    int rowStride)
  {
    ZoneScopedN("GLTexture.directUpload");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (rowStride != width) {
      glPixelStorei(GL_UNPACK_ROW_LENGTH, rowStride);
    }
    glTexSubImage2D(
      GL_TEXTURE_2D, 0, x, y, width, height, format, GL_UNSIGNED_BYTE, data);
    if (rowStride != width) {
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
  }

  void UploadToGPU(const unsigned char* data,
                   int width,
                   int height,
                   int channels,
                   const TextureOptions& options)
  {
    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    const GLenum format = formatForChannels(channels);
    const GLenum internalFmt = internalFormatForChannels(channels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 static_cast<GLint>(internalFmt),
                 width,
                 height,
                 0,
                 format,
                 GL_UNSIGNED_BYTE,
                 data);

    const GLint magnificationFilter =
      (options.filter == TextureFilter::Linear) ? GL_LINEAR : GL_NEAREST;
    GLint minificationFilter = magnificationFilter;
    if (options.generateMipmaps) {
      minificationFilter = options.filter == TextureFilter::Linear
                             ? GL_LINEAR_MIPMAP_LINEAR
                             : GL_NEAREST_MIPMAP_NEAREST;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minificationFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magnificationFilter);
    const GLint wrapX =
      options.wrapX == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    const GLint wrapY =
      options.wrapY == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapX);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapY);
    if (options.generateMipmaps) {
      glGenerateMipmap(GL_TEXTURE_2D);
    }

    // R8 samples only .r; make .gba = r for any accidental RGB sampling.
    if (channels == 1) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }
  }

  std::string m_path;
  unsigned int m_id;
  std::array<int, 2> m_size;
  int m_channels;

  // Async upload: non-blocking triple-buffered pixel unpack buffers.
  GLuint m_pbo[kPboCount];
  GLsync m_pboFence[kPboCount];
  int m_pboIndex;
  size_t m_pboBytes;
};
