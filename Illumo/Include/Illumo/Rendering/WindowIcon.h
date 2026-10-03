#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// One candidate size of a window icon: straight (non-premultiplied) RGBA8,
// row 0 at the top.
struct WindowIconImage
{
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

// Reads a Windows .ico file into the images a window shows in its title bar,
// taskbar and Alt+Tab. An application package supplies one as app.ico at its
// root (D-E39); the runtime applies it over the engine's own icon.
class WindowIcon
{
public:
  // Largest edge accepted for one image.
  static constexpr int kMaximumEdge = 512;
  // Largest .ico file the runtime reads from a package.
  static constexpr std::size_t kMaximumBytes = 1024u * 1024u;

  // Every usable entry, in file order: PNG-compressed entries (any PNG the
  // engine's image decoder reads) and uncompressed 32-bit bitmap entries.
  // Entries of any other kind, or damaged ones, are skipped; a file with no
  // usable entry, or that is not an .ico, yields an empty list.
  static std::vector<WindowIconImage> decode(const std::uint8_t* data,
                                             std::size_t size);
  static std::vector<WindowIconImage> decode(
    const std::vector<std::uint8_t>& bytes)
  {
    return decode(bytes.data(), bytes.size());
  }
};
