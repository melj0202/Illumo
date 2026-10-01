#pragma once

#include <string>

// Specification strings and returned filesystem paths use UTF-8.
// Cancellation or failure returns an empty path.
struct SaveLoadDialogSpec
{
  std::string fileDescription{ "Illumo File Format" };
  std::string defaultFilename{ "MyCanvas.illumo" };
  std::string extensionPattern{ "*.ILLUMO" };
};

class SaveLoad
{
public:
  static std::string GetLoadLocation(
    const SaveLoadDialogSpec& specification = SaveLoadDialogSpec{});
  static std::string GetSaveLocation(
    const SaveLoadDialogSpec& specification = SaveLoadDialogSpec{});
};
