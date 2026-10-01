#include "Platform/AtomicFileInternal.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::error_code
replaceAtomicFile(const std::filesystem::path& source,
                  const std::filesystem::path& destination)
{
  if (MoveFileExW(
        source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) {
    return {};
  }
  return { static_cast<int>(GetLastError()), std::system_category() };
}
