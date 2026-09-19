#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Services/Logger.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <windows.h>

static std::wstring
wideText(const std::string& text)
{
  if (text.empty() ||
      text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8,
                                       MB_ERR_INVALID_CHARS,
                                       text.data(),
                                       static_cast<int>(text.size()),
                                       nullptr,
                                       0);
  if (size == 0) {
    return {};
  }
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      MB_ERR_INVALID_CHARS,
                      text.data(),
                      static_cast<int>(text.size()),
                      result.data(),
                      size);
  return result;
}

static std::string
chooseFile(const SaveLoadDialogSpec& specification, bool save)
{
  const std::wstring description = wideText(
    specification.fileDescription.empty() ? "Illumo File Format"
                                          : specification.fileDescription);
  const std::wstring pattern = wideText(specification.extensionPattern.empty()
                                          ? "*.ILLUMO"
                                          : specification.extensionPattern);
  const std::wstring filename = wideText(specification.defaultFilename.empty()
                                           ? "MyCanvas.illumo"
                                           : specification.defaultFilename);
  std::array<wchar_t, 32768> file{};
  if (description.empty() || pattern.empty() || filename.empty() ||
      filename.size() >= file.size()) {
    Logger::LogError(
      "SaveLoad: invalid UTF-8 dialog specification or filename too long");
    return {};
  }
  std::copy(filename.begin(), filename.end(), file.begin());
  std::wstring filter = description + L" (" + pattern + L")";
  filter.push_back(L'\0');
  filter += pattern;
  filter.push_back(L'\0');
  filter.push_back(L'\0');
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.lpstrFile = file.data();
  dialog.nMaxFile = static_cast<DWORD>(file.size());
  dialog.lpstrFilter = filter.c_str();
  dialog.nFilterIndex = 1;
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                 (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
    if (CommDlgExtendedError() != 0) {
      Logger::LogError("SaveLoad: native file dialog failed");
    }
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8,
                                       WC_ERR_INVALID_CHARS,
                                       file.data(),
                                       -1,
                                       nullptr,
                                       0,
                                       nullptr,
                                       nullptr);
  if (size <= 1) {
    return {};
  }
  std::string result(static_cast<size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8,
                          WC_ERR_INVALID_CHARS,
                          file.data(),
                          -1,
                          result.data(),
                          size,
                          nullptr,
                          nullptr) == 0) {
    return {};
  }
  result.pop_back();
  return result;
}

std::string
SaveLoad::GetLoadLocation(const SaveLoadDialogSpec& specification)
{
  return chooseFile(specification, false);
}

std::string
SaveLoad::GetSaveLocation(const SaveLoadDialogSpec& specification)
{
  return chooseFile(specification, true);
}
