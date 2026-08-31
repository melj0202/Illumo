#include <Illumo/Platform/SaveLoad.h>

#include <cstdio>
#include <string>
#include <windows.h>

static std::string
buildDialogFilter(const SaveLoadDialogSpec& specification)
{
  const std::string description = specification.fileDescription.empty()
                                    ? "Illumo File Format"
                                    : specification.fileDescription;
  const std::string pattern = specification.extensionPattern.empty()
                                ? "*.ILLUMO"
                                : specification.extensionPattern;
  std::string filter = description + " (" + pattern + ")";
  filter.push_back('\0');
  filter += pattern;
  filter.push_back('\0');
  filter.push_back('\0');
  return filter;
}

static void
seedDialogFilename(char* destination,
                   std::size_t destinationSize,
                   const SaveLoadDialogSpec& specification)
{
  const std::string filename = specification.defaultFilename.empty()
                                 ? "MyCanvas.illumo"
                                 : specification.defaultFilename;
  std::snprintf(destination, destinationSize, "%s", filename.c_str());
}

std::string
SaveLoad::GetLoadLocation(const SaveLoadDialogSpec& specification)
{
  OPENFILENAMEA ofn;
  char file[4096] = {};
  seedDialogFilename(file, sizeof(file), specification);
  const std::string filter = buildDialogFilter(specification);

  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL;
  ofn.lpstrFile = file;
  ofn.nMaxFile = static_cast<DWORD>(sizeof(file));
  ofn.lpstrFilter = filter.c_str();
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = NULL;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = NULL;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

  if (!GetOpenFileNameA(&ofn)) {
    return "";
  }
  return std::string{ file };
}

std::string
SaveLoad::GetSaveLocation(const SaveLoadDialogSpec& specification)
{
  OPENFILENAMEA ofn;
  char file[4096] = {};
  seedDialogFilename(file, sizeof(file), specification);
  const std::string filter = buildDialogFilter(specification);

  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL;
  ofn.lpstrFile = file;
  ofn.nMaxFile = static_cast<DWORD>(sizeof(file));
  ofn.lpstrFilter = filter.c_str();
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = NULL;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = NULL;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

  if (!GetSaveFileNameA(&ofn)) {
    return "";
  }
  return std::string{ file };
}
