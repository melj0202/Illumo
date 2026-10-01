#include <Illumo/Content/VirtualPath.h>

#include <utility>

// Well-formed UTF-8 without overlong forms, surrogates or values past U+10FFFF.
static bool
wellFormedUtf8(std::string_view text)
{
  std::size_t index = 0;
  while (index < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80u) {
      ++index;
      continue;
    }
    std::size_t length = 0;
    unsigned char minimum = 0x80u;
    unsigned char maximum = 0xbfu;
    if (lead >= 0xc2u && lead <= 0xdfu) {
      length = 2;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
      length = 3;
      minimum = lead == 0xe0u ? 0xa0u : 0x80u;
      maximum = lead == 0xedu ? 0x9fu : 0xbfu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
      length = 4;
      minimum = lead == 0xf0u ? 0x90u : 0x80u;
      maximum = lead == 0xf4u ? 0x8fu : 0xbfu;
    } else {
      return false;
    }
    if (text.size() - index < length) {
      return false;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
      const unsigned char next =
        static_cast<unsigned char>(text[index + offset]);
      const unsigned char low = offset == 1 ? minimum : 0x80u;
      const unsigned char high = offset == 1 ? maximum : 0xbfu;
      if (next < low || next > high) {
        return false;
      }
    }
    index += length;
  }
  return true;
}

bool
VirtualPath::validComponent(std::string_view component)
{
  if (component.empty() || component.size() > kMaximumComponentBytes ||
      component == "." || component == ".." || component.back() == '.' ||
      component.back() == ' ' ||
      component.find_first_of("\\/:*?\"<>|") != std::string_view::npos) {
    return false;
  }
  for (const char value : component) {
    const unsigned char character = static_cast<unsigned char>(value);
    if (character < 32u || character == 127u) {
      return false;
    }
  }
  if (!wellFormedUtf8(component)) {
    return false;
  }
  std::string upper(component);
  for (char& character : upper) {
    if (character >= 'a' && character <= 'z') {
      character = static_cast<char>(character - 'a' + 'A');
    }
  }
  if (upper.starts_with(".ILLUMO-")) {
    return false;
  }
  const std::string stem = upper.substr(0, upper.find('.'));
  return !(stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
           stem == "CONIN$" || stem == "CONOUT$" || stem == "CLOCK$" ||
           (stem.size() == 4 &&
            (stem.starts_with("COM") || stem.starts_with("LPT")) &&
            stem[3] >= '0' && stem[3] <= '9'));
}

bool
VirtualPath::validRelative(std::string_view path)
{
  if (path.empty() || path.size() > kMaximumPathBytes) {
    return false;
  }
  std::size_t start = 0;
  while (true) {
    const std::size_t separator = path.find('/', start);
    const std::string_view part =
      path.substr(start,
                  separator == std::string_view::npos ? std::string_view::npos
                                                      : separator - start);
    if (!validComponent(part)) {
      return false;
    }
    if (separator == std::string_view::npos) {
      return true;
    }
    start = separator + 1;
  }
}

bool
VirtualPath::normalize(std::string_view path, std::string& output)
{
  if (path.empty() || path.front() != '/') {
    return false;
  }
  std::string normalized;
  normalized.reserve(path.size());
  std::size_t start = 0;
  while (start < path.size()) {
    while (start < path.size() && path[start] == '/') {
      ++start;
    }
    if (start >= path.size()) {
      break;
    }
    std::size_t end = path.find('/', start);
    if (end == std::string_view::npos) {
      end = path.size();
    }
    const std::string_view part = path.substr(start, end - start);
    if (!validComponent(part)) {
      return false;
    }
    normalized.push_back('/');
    normalized.append(part);
    start = end;
  }
  if (normalized.empty()) {
    normalized = "/";
  }
  if (normalized.size() > kMaximumPathBytes) {
    return false;
  }
  output = std::move(normalized);
  return true;
}

bool
VirtualPath::join(std::string_view baseDirectory,
                  std::string_view reference,
                  std::string& output)
{
  if (!reference.empty() && reference.front() == '/') {
    return normalize(reference, output);
  }
  if (reference.empty()) {
    return false;
  }
  std::string combined(baseDirectory);
  combined.push_back('/');
  combined.append(reference);
  return normalize(combined, output);
}

std::string
VirtualPath::parent(std::string_view normalized)
{
  const std::size_t separator = normalized.rfind('/');
  if (separator == std::string_view::npos || separator == 0) {
    return "/";
  }
  return std::string(normalized.substr(0, separator));
}

std::string_view
VirtualPath::fileName(std::string_view normalized)
{
  const std::size_t separator = normalized.rfind('/');
  return separator == std::string_view::npos ? normalized
                                             : normalized.substr(separator + 1);
}

std::string_view
VirtualPath::mountName(std::string_view normalized)
{
  if (normalized.size() < 2 || normalized.front() != '/') {
    return {};
  }
  const std::size_t separator = normalized.find('/', 1);
  return normalized.substr(1,
                           separator == std::string_view::npos
                             ? std::string_view::npos
                             : separator - 1);
}

bool
VirtualPath::isWithin(std::string_view normalized, std::string_view prefix)
{
  if (prefix == "/") {
    return !normalized.empty() && normalized.front() == '/';
  }
  return normalized.starts_with(prefix) &&
         (normalized.size() == prefix.size() ||
          normalized[prefix.size()] == '/');
}

std::string_view
VirtualPath::relativeTo(std::string_view normalized, std::string_view prefix)
{
  if (prefix == "/") {
    return normalized.substr(1);
  }
  if (normalized.size() <= prefix.size()) {
    return {};
  }
  return normalized.substr(prefix.size() + 1);
}
