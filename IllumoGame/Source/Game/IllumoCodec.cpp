#include "IllumoCodec.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Platform/PathText.h>
#include <filesystem>
#include <fstream>

bool
IllumoCodec::writeFile(const std::string& path,
                       const IllumoDocument& document,
                       std::string* error)
{
  ILLUMO_PROFILE_ZONE("IllumoCodec.writeFile");
  if (path.empty()) {
    setError(error, "Save path is empty");
    return false;
  }
  std::filesystem::path target;
  if (!pathFromUtf8(path, &target)) {
    setError(error, "Invalid or inaccessible UTF-8 file path");
    return false;
  }
  return AtomicFile::write(
    target,
    [&document](std::ostream& file, std::string* streamError) {
      return writeStream(file, document, streamError);
    },
    error);
}

bool
IllumoCodec::readFile(const std::string& path,
                      IllumoDocument* document,
                      std::string* error)
{
  ILLUMO_PROFILE_ZONE("IllumoCodec.readFile");
  if (document == nullptr) {
    setError(error, "Document pointer is null");
    return false;
  }
  if (path.empty()) {
    setError(error, "Load path is empty");
    return false;
  }
  std::filesystem::path source;
  if (!pathFromUtf8(path, &source)) {
    setError(error, "Invalid or inaccessible UTF-8 file path");
    return false;
  }
  std::ifstream file(source, std::ios::binary);
  if (!file.is_open()) {
    setError(error, "Failed to open for loading: " + path);
    return false;
  }
  return readStream(file, document, error);
}
