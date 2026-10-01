#include "GpuTexels.h"

#include <cmath>
#include <cstring>
#include <limits>

bool
validTextureUpdate(int textureWidth,
                   int textureHeight,
                   int x,
                   int y,
                   int width,
                   int height,
                   int channels,
                   int rowStridePixels)
{
  if (textureWidth <= 0 || textureHeight <= 0 || x < 0 || y < 0 || width <= 0 ||
      height <= 0 || x > textureWidth || y > textureHeight ||
      width > textureWidth - x || height > textureHeight - y ||
      (channels != 1 && channels != 3 && channels != 4) ||
      rowStridePixels < 0 ||
      (rowStridePixels != 0 && rowStridePixels < width)) {
    return false;
  }
  const size_t limit =
    static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max());
  const size_t bytesPerPixel = static_cast<size_t>(channels);
  const size_t fullRow = static_cast<size_t>(textureWidth);
  const size_t sourceRow =
    static_cast<size_t>(rowStridePixels == 0 ? width : rowStridePixels);
  if (fullRow > limit / bytesPerPixel || sourceRow > limit / bytesPerPixel) {
    return false;
  }
  return static_cast<size_t>(textureHeight) <=
           limit / (fullRow * bytesPerPixel) &&
         static_cast<size_t>(height) <= limit / (sourceRow * bytesPerPixel);
}

void
convertTexelsForStorage(const unsigned char* source,
                        int width,
                        int height,
                        int sourceChannels,
                        int sourceRowPixels,
                        int storageBytes,
                        unsigned char* destination)
{
  const size_t channels = static_cast<size_t>(sourceChannels);
  const size_t rowPixels =
    static_cast<size_t>(sourceRowPixels > 0 ? sourceRowPixels : width);
  const size_t sourceRowBytes = rowPixels * channels;
  const size_t destinationRowBytes =
    static_cast<size_t>(width) * static_cast<size_t>(storageBytes);
  for (int row = 0; row < height; ++row) {
    const unsigned char* in =
      source + static_cast<size_t>(row) * sourceRowBytes;
    unsigned char* out =
      destination + static_cast<size_t>(row) * destinationRowBytes;
    if (storageBytes == sourceChannels) {
      std::memcpy(out, in, destinationRowBytes);
      continue;
    }
    for (int column = 0; column < width; ++column) {
      const unsigned char* texel = in + static_cast<size_t>(column) * channels;
      if (storageBytes == 1) {
        out[column] = texel[0];
        continue;
      }
      unsigned char* stored = out + static_cast<size_t>(column) * 4u;
      stored[0] = texel[0];
      stored[1] = channels >= 3 ? texel[1] : 0;
      stored[2] = channels >= 3 ? texel[2] : 0;
      stored[3] = channels >= 4 ? texel[3] : 255;
    }
  }
}

unsigned
storedTexelBytes(TextureFormat format)
{
  switch (format) {
    case TextureFormat::RGBA8:
    case TextureFormat::RGB8:
      return 4;
    case TextureFormat::R8:
      return 1;
    case TextureFormat::RGBA16F:
      return 8;
    case TextureFormat::RG16F:
      return 4;
    case TextureFormat::R16F:
      return 2;
    default:
      return 0;
  }
}

float
halfToFloat(uint16_t half)
{
  const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
  const uint32_t exponent = (half >> 10) & 0x1Fu;
  const uint32_t mantissa = half & 0x3FFu;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      // Subnormal: value is mantissa * 2^-24.
      const float value = static_cast<float>(mantissa) * (1.0f / 16777216.0f);
      return (half & 0x8000u) != 0 ? -value : value;
    }
  } else if (exponent == 31) {
    bits = sign | 0x7F800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
  }
  float result = 0.0f;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

static unsigned char
unorm8(float value)
{
  if (!(value > 0.0f)) {
    return 0;
  }
  if (value >= 1.0f) {
    return 255;
  }
  return static_cast<unsigned char>(std::lround(value * 255.0f));
}

void
convertReadbackTexels(const unsigned char* source,
                      size_t sourceRowBytes,
                      int width,
                      int height,
                      TextureFormat format,
                      unsigned char* destination)
{
  const size_t outputRowBytes = static_cast<size_t>(width) * 4u;
  for (int row = 0; row < height; ++row) {
    // OpenGL rows run bottom-up; callers get top-down rows.
    const unsigned char* in =
      source + static_cast<size_t>(height - 1 - row) * sourceRowBytes;
    unsigned char* out =
      destination + static_cast<size_t>(row) * outputRowBytes;
    if (format == TextureFormat::RGBA8) {
      std::memcpy(out, in, outputRowBytes);
      continue;
    }
    for (int column = 0; column < width; ++column) {
      unsigned char* texel = out + static_cast<size_t>(column) * 4u;
      if (format == TextureFormat::RGB8) {
        std::memcpy(texel, in + static_cast<size_t>(column) * 4u, 3);
        texel[3] = 255;
      } else if (format == TextureFormat::R8) {
        texel[0] = in[column];
        texel[1] = 0;
        texel[2] = 0;
        texel[3] = 255;
      } else {
        const unsigned components = storedTexelBytes(format) / 2u;
        uint16_t halves[4] = { 0, 0, 0, 0x3C00u };
        std::memcpy(halves,
                    in + static_cast<size_t>(column) * components * 2u,
                    components * 2u);
        if (components < 4) {
          halves[3] = 0x3C00u;
        }
        for (unsigned channel = components; channel < 3; ++channel) {
          halves[channel] = 0;
        }
        for (unsigned channel = 0; channel < 4; ++channel) {
          texel[channel] = unorm8(halfToFloat(halves[channel]));
        }
      }
    }
  }
}
