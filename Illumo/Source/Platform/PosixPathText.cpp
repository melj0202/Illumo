#include <Illumo/Platform/PathText.h>
#include <cstdint>

// POSIX paths are bytes; the native text is used as UTF-8 as it is.

std::string
pathToUtf8(const std::filesystem::path& path)
{
  return path.native();
}

std::string
pathToGenericUtf8(const std::filesystem::path& path)
{
  return path.generic_string();
}

// Checks the UTF-8 encoding rules (shortest form, no surrogates, at most
// U+10FFFF) without decoding.
static bool
validUtf8(std::string_view text)
{
  std::size_t index = 0;
  while (index < text.size()) {
    const std::uint8_t lead = static_cast<std::uint8_t>(text[index]);
    std::size_t extra = 0;
    std::uint32_t code = 0;
    if (lead < 0x80u) {
      ++index;
      continue;
    } else if (lead >= 0xC2u && lead <= 0xDFu) {
      extra = 1;
      code = lead & 0x1Fu;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
      extra = 2;
      code = lead & 0x0Fu;
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
      extra = 3;
      code = lead & 0x07u;
    } else {
      return false;
    }
    if (text.size() - index <= extra) {
      return false;
    }
    for (std::size_t offset = 1; offset <= extra; ++offset) {
      const std::uint8_t next = static_cast<std::uint8_t>(text[index + offset]);
      if ((next & 0xC0u) != 0x80u) {
        return false;
      }
      code = (code << 6u) | (next & 0x3Fu);
    }
    if ((extra == 2 &&
         (code < 0x800u || (code >= 0xD800u && code <= 0xDFFFu))) ||
        (extra == 3 && (code < 0x10000u || code > 0x10FFFFu))) {
      return false;
    }
    index += extra + 1;
  }
  return true;
}

bool
pathFromUtf8(std::string_view text, std::filesystem::path* path)
{
  if (path == nullptr || !validUtf8(text)) {
    return false;
  }
  *path = std::filesystem::path(std::string(text));
  return true;
}
