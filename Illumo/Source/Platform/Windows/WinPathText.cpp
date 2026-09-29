#include <Illumo/Platform/PathText.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <climits>
#include <windows.h>

static std::string
wideToUtf8(const std::wstring& text)
{
  if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX)) {
    return {};
  }
  // No WC_ERR_INVALID_CHARS: invalid UTF-16 becomes U+FFFD instead of failing.
  const int wideLength = static_cast<int>(text.size());
  const int length = WideCharToMultiByte(
    CP_UTF8, 0, text.data(), wideLength, nullptr, 0, nullptr, nullptr);
  if (length <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8,
                      0,
                      text.data(),
                      wideLength,
                      result.data(),
                      length,
                      nullptr,
                      nullptr);
  return result;
}

std::string
pathToUtf8(const std::filesystem::path& path)
{
  return wideToUtf8(path.native());
}

std::string
pathToGenericUtf8(const std::filesystem::path& path)
{
  std::wstring text = path.native();
  for (wchar_t& character : text) {
    if (character == L'\\') {
      character = L'/';
    }
  }
  return wideToUtf8(text);
}

bool
pathFromUtf8(std::string_view text, std::filesystem::path* path)
{
  if (path == nullptr || text.size() > static_cast<std::size_t>(INT_MAX)) {
    return false;
  }
  if (text.empty()) {
    *path = std::filesystem::path();
    return true;
  }
  // A terminated copy: the conversion reads exactly narrowLength bytes, but
  // a view's data() carries no terminator of its own.
  const std::string narrow(text);
  const int narrowLength = static_cast<int>(narrow.size());
  const int length = MultiByteToWideChar(
    CP_UTF8, MB_ERR_INVALID_CHARS, narrow.c_str(), narrowLength, nullptr, 0);
  if (length <= 0) {
    return false;
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          narrow.c_str(),
                          narrowLength,
                          wide.data(),
                          length) != length) {
    return false;
  }
  *path = std::filesystem::path(std::move(wide));
  return true;
}
