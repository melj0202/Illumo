#pragma once

#include "SparseCellGrid.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

struct IllumoDocument
{
  int version = 4;
  std::string familyString;
  std::string ruleString;
  double cameraX = 0.0;
  double cameraY = 0.0;
  double cameraZoom = 1.0;
  bool restoreCamera = false;
  std::int64_t worldChunkWidth = 0;
  std::int64_t worldChunkHeight = 0;
  std::unique_ptr<SparseCellGrid> grid;
  const SparseCellGrid* sourceGrid = nullptr;
};

class IllumoCodec
{
public:
  static constexpr int kVersion = 4;

  // Shared format implementation for native files and capability-provided
  // guest bytes. Legacy readers require a seekable stream beginning at zero.
  static bool readStream(std::istream& stream,
                         IllumoDocument* document,
                         std::string* error = nullptr);
  static bool writeStream(std::ostream& stream,
                          const IllumoDocument& document,
                          std::string* error = nullptr);

  static bool readFile(const std::string& path,
                       IllumoDocument* document,
                       std::string* error = nullptr);

  static bool writeFile(const std::string& path,
                        const IllumoDocument& document,
                        std::string* error = nullptr);

  static std::string withCSimExtension(const std::string& filename);

private:
  static void setError(std::string* error, const std::string& message);
};
