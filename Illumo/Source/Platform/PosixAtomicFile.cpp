#include "AtomicFileInternal.h"

std::error_code
replaceAtomicFile(const std::filesystem::path& source,
                  const std::filesystem::path& destination)
{
  std::error_code error;
  std::filesystem::rename(source, destination, error);
  return error;
}
