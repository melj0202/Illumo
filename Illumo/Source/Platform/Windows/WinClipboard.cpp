#include <Illumo/Platform/Clipboard.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

std::string
Clipboard::GetText()
{
  if (!OpenClipboard(nullptr)) {
    return std::string();
  }

  std::string result;
  HANDLE unicodeHandle = GetClipboardData(CF_UNICODETEXT);
  if (unicodeHandle != nullptr) {
    const wchar_t* wide =
      static_cast<const wchar_t*>(GlobalLock(unicodeHandle));
    if (wide != nullptr) {
      const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
      if (bytes > 1) {
        std::vector<char> buffer(static_cast<std::size_t>(bytes));
        WideCharToMultiByte(
          CP_UTF8, 0, wide, -1, buffer.data(), bytes, nullptr, nullptr);
        result.assign(buffer.data());
      }
      GlobalUnlock(unicodeHandle);
    }
  } else {
    HANDLE ansiHandle = GetClipboardData(CF_TEXT);
    if (ansiHandle != nullptr) {
      const char* ansi = static_cast<const char*>(GlobalLock(ansiHandle));
      if (ansi != nullptr) {
        result = ansi;
        GlobalUnlock(ansiHandle);
      }
    }
  }

  CloseClipboard();
  return result;
}

bool
Clipboard::SetText(const std::string& text)
{
  const int wideCount =
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (wideCount <= 0) {
    return false;
  }

  HGLOBAL memory = GlobalAlloc(
    GMEM_MOVEABLE, static_cast<SIZE_T>(wideCount) * sizeof(wchar_t));
  if (memory == nullptr) {
    return false;
  }

  wchar_t* wide = static_cast<wchar_t*>(GlobalLock(memory));
  if (wide == nullptr) {
    GlobalFree(memory);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide, wideCount);
  GlobalUnlock(memory);

  if (!OpenClipboard(nullptr)) {
    GlobalFree(memory);
    return false;
  }
  EmptyClipboard();
  if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    CloseClipboard();
    GlobalFree(memory);
    return false;
  }
  CloseClipboard();
  return true;
}
