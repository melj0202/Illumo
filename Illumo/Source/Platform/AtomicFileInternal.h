#pragma once
#include <Illumo/Platform/AtomicFile.h>
#include <fstream>
#include <system_error>

// Private finalization boundary; per-call overrides support late-I/O tests.
class AtomicFileOperations
{
public:
  virtual ~AtomicFileOperations() = default;
  virtual std::filesystem::path stagingPath(
    const std::filesystem::path& destination,
    unsigned int attempt);
  virtual void flush(std::ofstream& stream);
  virtual void close(std::ofstream& stream);
  virtual std::error_code publish(const std::filesystem::path& source,
                                  const std::filesystem::path& destination);
};

bool
writeAtomicFile(const std::filesystem::path& destination,
                const AtomicFile::Writer& writer,
                std::string* error,
                AtomicFileOperations& operations);

std::error_code
replaceAtomicFile(const std::filesystem::path& source,
                  const std::filesystem::path& destination);
