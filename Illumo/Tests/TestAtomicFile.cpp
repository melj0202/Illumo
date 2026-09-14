#include "Platform/AtomicFileInternal.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <iterator>
#include <stdexcept>

class AtomicFailureOperations : public AtomicFileOperations
{
public:
  int failure = 0;
  int publications = 0;
  std::filesystem::path stagingPath(const std::filesystem::path& destination,
                                    unsigned int attempt) override
  {
    std::filesystem::path path = destination;
    path += ".stage-" + std::to_string(attempt);
    return path;
  }
  void flush(std::ofstream& stream) override
  {
    AtomicFileOperations::flush(stream);
    if (failure == 4) {
      stream.setstate(std::ios::badbit);
    }
  }
  void close(std::ofstream& stream) override
  {
    AtomicFileOperations::close(stream);
    if (failure == 5) {
      stream.setstate(std::ios::failbit);
    }
  }
  std::error_code publish(const std::filesystem::path& source,
                          const std::filesystem::path& destination) override
  {
    ++publications;
    return AtomicFileOperations::publish(source, destination);
  }
};

static std::string
readAtomicFixture(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(stream),
           std::istreambuf_iterator<char>() };
}

static int
testAtomicFileFailures()
{
  TestCounters counters;
  const std::filesystem::path path = "atomic-save-test.bin";
  const std::filesystem::path collision = "atomic-save-test.bin.stage-0";
  const std::filesystem::path ownedStage = "atomic-save-test.bin.stage-1";
  for (int failure = 0; failure <= 5; ++failure) {
    {
      std::ofstream old(path, std::ios::binary);
      old << "previous valid file";
      std::ofstream occupied(collision, std::ios::binary);
      occupied << "foreign staging file";
    }
    AtomicFailureOperations operations;
    operations.failure = failure;
    std::string error;
    const bool saved = writeAtomicFile(
      path,
      [failure](std::ostream& output, std::string*) {
        output << "replacement bytes";
        if (failure == 1) {
          return false;
        }
        if (failure == 2) {
          throw std::runtime_error("encoding failure");
        }
        if (failure == 3) {
          output.setstate(std::ios::badbit);
        }
        return true;
      },
      &error,
      operations);
    testTrue(
      counters, saved == (failure == 0), "only fully finalized save succeeds");
    testTrue(counters,
             readAtomicFixture(path) ==
               (failure == 0 ? "replacement bytes" : "previous valid file"),
             "failed write/flush/close preserves prior bytes");
    testTrue(counters,
             readAtomicFixture(collision) == "foreign staging file",
             "colliding staging file is never modified");
    testTrue(counters,
             !std::filesystem::exists(ownedStage),
             "owned staging file is cleaned after success or failure");
    testEqInt(counters,
              operations.publications,
              failure == 0 ? 1 : 0,
              "failed finalization never attempts publication");
    testTrue(counters, error.empty() == saved, "failure is observable");
  }
  std::filesystem::remove(path);
  std::filesystem::remove(collision);
  std::string error;
  const AtomicFile::Writer writer = [](std::ostream& output, std::string*) {
    output << "created";
    return true;
  };
  testTrue(counters,
           AtomicFile::write(path, writer, &error) &&
             readAtomicFixture(path) == "created",
           "new file publication succeeds");
  std::filesystem::remove(path);
  std::filesystem::path longPath =
    std::filesystem::absolute(std::string(240, 'a') + ".bin");
#ifdef _WIN32
  // Exercise the component limit independently of the legacy MAX_PATH limit.
  longPath = std::filesystem::path(L"\\\\?\\" + longPath.wstring());
#endif
  {
    std::ofstream previous(longPath, std::ios::binary);
    previous << "previous";
  }
  testTrue(counters,
           readAtomicFixture(longPath) == "previous",
           "long destination is a valid existing file");
  testTrue(counters,
           AtomicFile::write(longPath, writer, &error) &&
             readAtomicFixture(longPath) == "created",
           "long basename replacement uses a bounded sibling name");
  std::filesystem::remove(longPath);
  std::filesystem::create_directory(path);
  AtomicFailureOperations operations;
  testTrue(counters,
           !writeAtomicFile(path, writer, &error, operations),
           "directory destination rejects publication");
  testTrue(counters,
           std::filesystem::is_directory(path) &&
             !std::filesystem::exists(collision),
           "publication failure preserves destination and removes staging");
  std::filesystem::remove(path);
  return counters.failures;
}

void
registerAtomicFileTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Platform.AtomicFile",
               []() { return testAtomicFileFailures(); });
}
