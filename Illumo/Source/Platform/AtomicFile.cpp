#include "AtomicFileInternal.h"
#include <atomic>
#include <chrono>
#include <stdexcept>

struct AtomicStagingFile
{
  std::filesystem::path path;
  bool owned = false;
  AtomicStagingFile() = default;
  AtomicStagingFile(const AtomicStagingFile&) = delete;
  AtomicStagingFile& operator=(const AtomicStagingFile&) = delete;
  ~AtomicStagingFile()
  {
    if (owned) {
      std::error_code error;
      std::filesystem::remove(path, error);
    }
  }
};

std::filesystem::path
AtomicFileOperations::stagingPath(const std::filesystem::path& destination,
                                  unsigned int attempt)
{
  (void)attempt;
  static std::atomic<unsigned long long> sequence{ 0 };
  // Keep the sibling basename bounded even for maximum-length destinations.
  const std::string name =
    ".illumo-tmp-" +
    std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()) +
    "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
  return destination.parent_path() / name;
}

void
AtomicFileOperations::flush(std::ofstream& stream)
{
  stream.flush();
}
void
AtomicFileOperations::close(std::ofstream& stream)
{
  stream.close();
}
std::error_code
AtomicFileOperations::publish(const std::filesystem::path& source,
                              const std::filesystem::path& destination)
{
  return replaceAtomicFile(source, destination);
}

bool
writeAtomicFile(const std::filesystem::path& destination,
                const AtomicFile::Writer& writer,
                std::string* error,
                AtomicFileOperations& operations)
{
  if (error != nullptr) {
    error->clear();
  }
  try {
    if (destination.empty() || !writer) {
      throw std::runtime_error("Empty save path or writer");
    }
    const std::filesystem::path target = std::filesystem::absolute(destination);
    AtomicStagingFile staging;
    std::ofstream stream;
    for (unsigned int attempt = 0; attempt < 32u; ++attempt) {
      staging.path = operations.stagingPath(target, attempt);
      stream.clear();
      stream.open(staging.path,
                  std::ios::binary | std::ios::out | std::ios::noreplace);
      if (stream.is_open()) {
        staging.owned = true;
        break;
      }
      std::error_code existsError;
      if (!std::filesystem::exists(staging.path, existsError) || existsError) {
        throw std::runtime_error("Cannot create sibling save staging file");
      }
    }
    if (!staging.owned) {
      throw std::runtime_error("Save staging name collisions exhausted");
    }
    if (!writer(stream, error) || !stream.good()) {
      if (error != nullptr && !error->empty()) {
        return false;
      }
      throw std::runtime_error("Save writer failed");
    }
    operations.flush(stream);
    if (!stream.good()) {
      throw std::runtime_error("Save flush failed");
    }
    operations.close(stream);
    if (stream.fail() || stream.is_open()) {
      throw std::runtime_error("Save close failed");
    }
    const std::error_code publicationError =
      operations.publish(staging.path, target);
    if (publicationError) {
      throw std::runtime_error("Save publication failed: " +
                               publicationError.message());
    }
    staging.owned = false;
    return true;
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = exception.what();
    }
  } catch (...) {
    if (error != nullptr) {
      *error = "Unknown save failure";
    }
  }
  return false;
}

bool
AtomicFile::write(const std::filesystem::path& destination,
                  const Writer& writer,
                  std::string* error)
{
  AtomicFileOperations operations;
  return writeAtomicFile(destination, writer, error, operations);
}
