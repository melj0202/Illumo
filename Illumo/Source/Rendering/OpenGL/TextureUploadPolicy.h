#pragma once

#include <array>
#include <cstddef>
#include <limits>

enum class TextureUploadSlotState
{
  Unused,
  Signaled,
  Busy
};

class TextureUploadPolicy
{
public:
  static constexpr int kPboCount = 3;
  static constexpr std::size_t kDirectUploadThresholdBytes = 64u * 1024u;

  static bool validLayout(int textureWidth,
                          int textureHeight,
                          int x,
                          int y,
                          int width,
                          int height,
                          int channels,
                          int rowStride)
  {
    if (textureWidth <= 0 || textureHeight <= 0 || x < 0 || y < 0 ||
        width <= 0 || height <= 0 || x > textureWidth || y > textureHeight ||
        width > textureWidth - x || height > textureHeight - y ||
        (channels != 1 && channels != 3 && channels != 4) || rowStride < 0 ||
        (rowStride != 0 && rowStride < width)) {
      return false;
    }
    // PBO offsets/sizes and host pointer differences must be representable.
    const std::size_t limit =
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    const std::size_t bytesPerPixel = static_cast<std::size_t>(channels);
    const std::size_t fullRow = static_cast<std::size_t>(textureWidth);
    const std::size_t sourceRow =
      static_cast<std::size_t>(rowStride == 0 ? width : rowStride);
    if (fullRow > limit / bytesPerPixel || sourceRow > limit / bytesPerPixel) {
      return false;
    }
    return static_cast<std::size_t>(textureHeight) <=
             limit / (fullRow * bytesPerPixel) &&
           static_cast<std::size_t>(height) <=
             limit / (sourceRow * bytesPerPixel);
  }

  static bool useDirectUpload(std::size_t packedBytes)
  {
    return packedBytes <= kDirectUploadThresholdBytes;
  }

  static int selectAvailableSlot(
    int currentSlot,
    const std::array<TextureUploadSlotState, kPboCount>& states)
  {
    for (int attempt = 0; attempt < kPboCount; ++attempt) {
      const int candidate = (currentSlot + 1 + attempt) % kPboCount;
      if (states[static_cast<std::size_t>(candidate)] !=
          TextureUploadSlotState::Busy) {
        return candidate;
      }
    }
    return -1;
  }
};
