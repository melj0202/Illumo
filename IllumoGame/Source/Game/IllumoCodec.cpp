#include "IllumoCodec.h"
#include <Illumo/Platform/AtomicFile.h>
#include <filesystem>
#include <fstream>
#include <system_error>

bool
IllumoCodec::writeFile(const std::string& path,
                       const IllumoDocument& document,
                       std::string* error)
try {
  if (path.empty()) {
    setError(error, "Save path is empty");
    return false;
  }
  return AtomicFile::write(
    std::filesystem::path(std::u8string(path.begin(), path.end())),
    [&document](std::ostream& file, std::string* streamError) {
      return writeStream(file, document, streamError);
    },
    error);
} catch (const std::system_error&) {
  setError(error, "Invalid or inaccessible UTF-8 file path");
  return false;
}

bool
IllumoCodec::readFile(const std::string& path,
                      IllumoDocument* document,
                      std::string* error)
try {
  if (document == nullptr) {
    setError(error, "Document pointer is null");
    return false;
  }
  if (path.empty()) {
    setError(error, "Load path is empty");
    return false;
  }
  std::ifstream file(
    std::filesystem::path(std::u8string(path.begin(), path.end())),
    std::ios::binary);
  if (!file.is_open()) {
    setError(error, "Failed to open for loading: " + path);
    return false;
  }
  return readStream(file, document, error);
} catch (const std::system_error&) {
  setError(error, "Invalid or inaccessible UTF-8 file path");
  return false;
}
