#include <Illumo/Rendering/WindowIcon.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdint>
#include <vector>

// A 2x2 RGBA PNG: red, green / blue, half-transparent white.
static const std::vector<std::uint8_t> kPng = {
  0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49,
  0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06,
  0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52,
  0x47, 0x42, 0x00, 0xAE, 0xCE, 0x1C, 0xE9, 0x00, 0x00, 0x00, 0x04, 0x67, 0x41,
  0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00, 0x00,
  0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0E, 0xC3, 0x00, 0x00, 0x0E, 0xC3,
  0x01, 0xC7, 0x6F, 0xA8, 0x64, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41, 0x54,
  0x18, 0x57, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x0C, 0x19, 0x18, 0xFE, 0x83,
  0x40, 0x03, 0x00, 0x49, 0x49, 0x09, 0x78, 0xCE, 0xD7, 0x63, 0xF7, 0x00, 0x00,
  0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

static void
putU16(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

static void
putU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
  putU16(bytes, value & 0xFFFFu);
  putU16(bytes, value >> 16);
}

// An uncompressed 2x2 bitmap entry of kPng's picture: bottom row first, BGRA.
// The alpha is alphaScale, and half of it for the white pixel. A
// bitsPerPixel other than 32 makes an entry the decoder skips.
static std::vector<std::uint8_t>
bitmapEntry(std::uint32_t bitsPerPixel, std::uint8_t alphaScale)
{
  std::vector<std::uint8_t> bytes;
  putU32(bytes, 40);
  putU32(bytes, 2);
  putU32(bytes, 4); // colour rows plus the AND mask's
  putU16(bytes, 1);
  putU16(bytes, bitsPerPixel);
  putU32(bytes, 0);
  putU32(bytes, 0);
  putU32(bytes, 0);
  putU32(bytes, 0);
  putU32(bytes, 0);
  putU32(bytes, 0);
  const std::uint8_t pixels[16] = { // Bottom row: blue, white.
                                    255,
                                    0,
                                    0,
                                    alphaScale,
                                    255,
                                    255,
                                    255,
                                    static_cast<std::uint8_t>(alphaScale / 2),
                                    // Top row: red, green.
                                    0,
                                    0,
                                    255,
                                    alphaScale,
                                    0,
                                    255,
                                    0,
                                    alphaScale
  };
  bytes.insert(bytes.end(), pixels, pixels + 16);
  // The AND mask: two rows of one padded 32-bit word.
  putU32(bytes, 0);
  putU32(bytes, 0);
  return bytes;
}

static std::vector<std::uint8_t>
makeIcon(const std::vector<std::vector<std::uint8_t>>& entries,
         std::uint32_t type = 1)
{
  std::vector<std::uint8_t> bytes;
  putU16(bytes, 0);
  putU16(bytes, type);
  putU16(bytes, static_cast<std::uint32_t>(entries.size()));
  std::uint32_t offset = 6 + 16 * static_cast<std::uint32_t>(entries.size());
  for (const std::vector<std::uint8_t>& entry : entries) {
    bytes.push_back(2);
    bytes.push_back(2);
    bytes.push_back(0);
    bytes.push_back(0);
    putU16(bytes, 1);
    putU16(bytes, 32);
    putU32(bytes, static_cast<std::uint32_t>(entry.size()));
    putU32(bytes, offset);
    offset += static_cast<std::uint32_t>(entry.size());
  }
  for (const std::vector<std::uint8_t>& entry : entries) {
    bytes.insert(bytes.end(), entry.begin(), entry.end());
  }
  return bytes;
}

static bool
pixelIs(const WindowIconImage& image,
        int x,
        int y,
        std::uint8_t r,
        std::uint8_t g,
        std::uint8_t b,
        int a)
{
  const std::size_t at = (static_cast<std::size_t>(y) * 2u + x) * 4u;
  return image.rgba.size() == 16u && image.rgba[at] == r &&
         image.rgba[at + 1] == g && image.rgba[at + 2] == b &&
         (a < 0 || image.rgba[at + 3] == a);
}

static int
testWindowIconDecode()
{
  TestCounters counters;
  const std::vector<WindowIconImage> png =
    WindowIcon::decode(makeIcon({ kPng }));
  testTrue(counters,
           png.size() == 1 && png[0].width == 2 && png[0].height == 2,
           "a PNG entry decodes at its size");
  testTrue(counters,
           png.size() == 1 && pixelIs(png[0], 0, 0, 255, 0, 0, 255) &&
             pixelIs(png[0], 1, 0, 0, 255, 0, 255) &&
             pixelIs(png[0], 0, 1, 0, 0, 255, 255) &&
             pixelIs(png[0], 1, 1, 255, 255, 255, 128),
           "PNG pixels keep their colour and alpha, top row first");

  const std::vector<WindowIconImage> bitmap =
    WindowIcon::decode(makeIcon({ bitmapEntry(32, 255) }));
  testTrue(counters,
           bitmap.size() == 1 && bitmap[0].width == 2 && bitmap[0].height == 2,
           "a 32-bit bitmap entry decodes at its size");
  testTrue(counters,
           bitmap.size() == 1 && pixelIs(bitmap[0], 0, 0, 255, 0, 0, 255) &&
             pixelIs(bitmap[0], 1, 0, 0, 255, 0, 255) &&
             pixelIs(bitmap[0], 0, 1, 0, 0, 255, 255) &&
             pixelIs(bitmap[0], 1, 1, 255, 255, 255, 127),
           "bitmap rows are flipped upright and BGRA becomes RGBA");

  const std::vector<WindowIconImage> opaque =
    WindowIcon::decode(makeIcon({ bitmapEntry(32, 0) }));
  testTrue(counters,
           opaque.size() == 1 && pixelIs(opaque[0], 0, 0, 255, 0, 0, 255) &&
             pixelIs(opaque[0], 1, 1, 255, 255, 255, 255),
           "a bitmap with no alpha anywhere is opaque");

  std::vector<std::vector<std::uint8_t>> entries = { kPng,
                                                     bitmapEntry(24, 255),
                                                     bitmapEntry(32, 255) };
  std::vector<std::uint8_t> mixed = makeIcon(entries);
  const std::vector<WindowIconImage> kept = WindowIcon::decode(mixed);
  testTrue(counters,
           kept.size() == 2 && pixelIs(kept[0], 0, 0, 255, 0, 0, 255) &&
             pixelIs(kept[1], 1, 0, 0, 255, 0, 255),
           "unsupported entries are skipped and the rest keep their order");

  // Entry 0's offset points past the end of the file.
  std::vector<std::uint8_t> outside = makeIcon({ kPng, kPng });
  outside[6 + 12] = 0xFF;
  outside[6 + 13] = 0xFF;
  outside[6 + 14] = 0xFF;
  outside[6 + 15] = 0x7F;
  testTrue(counters,
           WindowIcon::decode(outside).size() == 1,
           "an entry outside the file is skipped");

  std::vector<std::uint8_t> truncated = makeIcon({ kPng });
  truncated.resize(truncated.size() - 20);
  testTrue(counters,
           WindowIcon::decode(truncated).empty(),
           "an entry running past the end of the file is skipped");

  testTrue(counters,
           WindowIcon::decode(nullptr, 0).empty() &&
             WindowIcon::decode(std::vector<std::uint8_t>{}).empty() &&
             WindowIcon::decode(std::vector<std::uint8_t>{ 0, 0, 1 }).empty(),
           "nothing, or a header cut short, has no images");
  testTrue(counters,
           WindowIcon::decode(makeIcon({ kPng }, 2)).empty() &&
             WindowIcon::decode(kPng).empty(),
           "a cursor, or a file that is not an icon, has no images");
  std::vector<std::uint8_t> crowded = makeIcon({ kPng });
  crowded[4] = 0xFF;
  crowded[5] = 0xFF;
  testTrue(counters,
           WindowIcon::decode(crowded).empty(),
           "a count the file cannot hold has no images");
  return counters.failures;
}

void
registerWindowIconTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Rendering.WindowIconDecode",
               []() { return testWindowIconDecode(); });
}
