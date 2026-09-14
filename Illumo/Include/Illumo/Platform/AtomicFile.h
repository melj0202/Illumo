#pragma once
#include <filesystem>
#include <functional>
#include <ostream>
#include <string>

class AtomicFile
{
public:
  using Writer = std::function<bool(std::ostream&, std::string*)>;

  // Synchronously stage beside destination, check flush/close, then replace.
  // Reported failures preserve the previous destination. Not a power-loss
  // durability guarantee. The writer must not close or retain the stream.
  static bool write(const std::filesystem::path& destination,
                    const Writer& writer,
                    std::string* error = nullptr);
};
