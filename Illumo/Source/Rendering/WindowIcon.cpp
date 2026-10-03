#include "thirdparty/stb/stb_image.h"
#include <Illumo/Rendering/WindowIcon.h>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

static std::uint32_t
readU16(const std::uint8_t* bytes)
{
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8);
}

static std::uint32_t
readU32(const std::uint8_t* bytes)
{
  return readU16(bytes) | (readU16(bytes + 2) << 16);
}

static bool
isPng(const std::uint8_t* bytes, std::size_t size)
{
  static const std::uint8_t kSignature[8] = { 0x89, 'P',  'N',  'G',
                                              0x0D, 0x0A, 0x1A, 0x0A };
  return size >= sizeof(kSignature) &&
         std::memcmp(bytes, kSignature, sizeof(kSignature)) == 0;
}

static bool
edgeInRange(int edge)
{
  return edge >= 1 && edge <= WindowIcon::kMaximumEdge;
}

static bool
decodePng(const std::uint8_t* bytes, std::size_t size, WindowIconImage& image)
{
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  int width = 0;
  int height = 0;
  int sourceChannels = 0;
  std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
    stbi_load_from_memory(bytes,
                          static_cast<int>(size),
                          &width,
                          &height,
                          &sourceChannels,
                          STBI_rgb_alpha),
    stbi_image_free);
  if (!decoded || !edgeInRange(width) || !edgeInRange(height)) {
    return false;
  }
  image.width = width;
  image.height = height;
  image.rgba.assign(decoded.get(),
                    decoded.get() + static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 4u);
  return true;
}

// A BITMAPINFOHEADER image whose height counts the colour rows and the AND
// mask. Only 32-bit BI_RGB is read: it carries its own alpha, so the mask
// is not needed.
static bool
decodeBitmap(const std::uint8_t* bytes,
             std::size_t size,
             WindowIconImage& image)
{
  constexpr std::size_t kHeaderBytes = 40;
  if (size < kHeaderBytes || readU32(bytes) != kHeaderBytes ||
      readU16(bytes + 12) != 1u || readU16(bytes + 14) != 32u ||
      readU32(bytes + 16) != 0u) {
    return false;
  }
  const std::int64_t width = static_cast<std::int32_t>(readU32(bytes + 4));
  const std::int64_t doubled = static_cast<std::int32_t>(readU32(bytes + 8));
  const std::int64_t height = doubled / 2;
  if (width < 1 || height < 1 || width > WindowIcon::kMaximumEdge ||
      height > WindowIcon::kMaximumEdge) {
    return false;
  }
  const std::size_t pixels =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (size - kHeaderBytes < pixels * 4u) {
    return false;
  }
  image.width = static_cast<int>(width);
  image.height = static_cast<int>(height);
  image.rgba.resize(pixels * 4u);
  const std::uint8_t* source = bytes + kHeaderBytes;
  bool anyAlpha = false;
  for (std::int64_t row = 0; row < height; ++row) {
    // Bottom-up rows of BGRA.
    const std::uint8_t* line = source + static_cast<std::size_t>(row) *
                                          static_cast<std::size_t>(width) * 4u;
    std::uint8_t* target =
      image.rgba.data() + static_cast<std::size_t>(height - 1 - row) *
                            static_cast<std::size_t>(width) * 4u;
    for (std::int64_t column = 0; column < width; ++column) {
      target[0] = line[2];
      target[1] = line[1];
      target[2] = line[0];
      target[3] = line[3];
      anyAlpha = anyAlpha || line[3] != 0;
      line += 4;
      target += 4;
    }
  }
  if (!anyAlpha) {
    // Written without an alpha channel: the colour rows are the picture.
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
      image.rgba[pixel * 4u + 3u] = 255;
    }
  }
  return true;
}

std::vector<WindowIconImage>
WindowIcon::decode(const std::uint8_t* data, std::size_t size)
{
  constexpr std::size_t kDirectoryBytes = 6;
  constexpr std::size_t kEntryBytes = 16;
  std::vector<WindowIconImage> images;
  // Reserved 0, type 1 (icon; 2 is a cursor), then the entry count.
  if (data == nullptr || size < kDirectoryBytes || readU16(data) != 0u ||
      readU16(data + 2) != 1u) {
    return images;
  }
  const std::size_t count = readU16(data + 4);
  if (size - kDirectoryBytes < count * kEntryBytes) {
    return images;
  }
  for (std::size_t index = 0; index < count; ++index) {
    const std::uint8_t* entry = data + kDirectoryBytes + index * kEntryBytes;
    const std::size_t length = readU32(entry + 8);
    const std::size_t offset = readU32(entry + 12);
    if (offset > size || length > size - offset) {
      continue;
    }
    WindowIconImage image;
    const std::uint8_t* payload = data + offset;
    const bool decoded = isPng(payload, length)
                           ? decodePng(payload, length, image)
                           : decodeBitmap(payload, length, image);
    if (decoded) {
      images.push_back(std::move(image));
    }
  }
  return images;
}
