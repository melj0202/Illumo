#include "IllumoCodec.h"
#include "CanvasCoordinatePolicy.h"
#include "CellContext.h"
#include "Rulesets/RuleSet.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Platform/AtomicFile.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

static std::string
boundedRuleTag(const char* tag, std::size_t capacity)
{
  if (tag == nullptr) {
    return std::string();
  }
  std::size_t length = 0;
  while (length < capacity && tag[length] != '\0') {
    length += 1;
  }
  return std::string(tag, length);
}

void
IllumoCodec::setError(std::string* error, const std::string& message)
{
  if (error != nullptr) {
    *error = message;
  }
}

std::string
IllumoCodec::withIllumoExtension(const std::string& filename)
{
  if (filename.empty()) {
    return filename;
  }
  if (filename.length() >= 7 &&
      filename.compare(filename.length() - 7, 7, ".illumo") == 0) {
    return filename;
  }
  return filename + ".illumo";
}

bool
IllumoCodec::writeFile(const std::string& path,
                       const IllumoDocument& document,
                       std::string* error)
{
  if (path.empty()) {
    setError(error, "Save path is empty");
    return false;
  }
  if (!CanvasCoordinatePolicy::validPosition(document.cameraX,
                                             document.cameraY) ||
      !std::isfinite(document.cameraZoom) || document.cameraZoom < 0.1 ||
      document.cameraZoom > 100.0) {
    setError(error, "Camera metadata is outside the supported range");
    return false;
  }
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(document.ruleString);
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(document.familyString);
  if (rule == nullptr || family == nullptr ||
      rule->familyId != document.familyString) {
    setError(error, "Save requires a matching ruleset and family");
    return false;
  }
  if (document.familyString.size() >= MAX_RULETAG_SIZE ||
      document.ruleString.size() >= MAX_RULETAG_SIZE ||
      !SparseCellGrid::isValidTopology(document.worldChunkWidth,
                                       document.worldChunkHeight)) {
    setError(
      error,
      "Save identity or topology metadata is outside the supported range");
    return false;
  }
  const SparseCellGrid* grid =
    document.sourceGrid != nullptr ? document.sourceGrid : document.grid.get();
  std::vector<SparseChunkRecord> records;
  if (grid != nullptr) {
    records = grid->collectChunkRecords();
    for (const SparseChunkRecord& record : records) {
      for (const unsigned char state : record.cells) {
        if (static_cast<unsigned int>(state) >= family->stateCount) {
          setError(error,
                   "Save contains a state that is invalid for its family");
          return false;
        }
      }
    }
  }

  return AtomicFile::write(
    path,
    [&document, &records](std::ostream& file, std::string*) {
      const char magic[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '4', '\0' };
      const std::uint32_t version = 4;
      char familyTag[MAX_RULETAG_SIZE] = {};
      char ruleTag[MAX_RULETAG_SIZE] = {};
      const std::size_t familyBytes =
        std::min(document.familyString.size(),
                 static_cast<std::size_t>(MAX_RULETAG_SIZE - 1));
      const std::string activeTag = document.ruleString;
      const std::size_t tagBytes = std::min(
        activeTag.size(), static_cast<std::size_t>(MAX_RULETAG_SIZE - 1));
      std::memcpy(familyTag, document.familyString.data(), familyBytes);
      std::memcpy(ruleTag, activeTag.data(), tagBytes);

      const double cameraX = document.cameraX;
      const double cameraY = document.cameraY;
      const double cameraZoom = document.cameraZoom;
      const std::int64_t worldChunkWidth = document.worldChunkWidth;
      const std::int64_t worldChunkHeight = document.worldChunkHeight;
      const std::uint64_t chunkCount =
        static_cast<std::uint64_t>(records.size());

      file.write(magic, sizeof(magic));
      file.write(reinterpret_cast<const char*>(&version), sizeof(version));
      file.write(familyTag, sizeof(familyTag));
      file.write(ruleTag, sizeof(ruleTag));
      file.write(reinterpret_cast<const char*>(&cameraX), sizeof(cameraX));
      file.write(reinterpret_cast<const char*>(&cameraY), sizeof(cameraY));
      file.write(reinterpret_cast<const char*>(&cameraZoom),
                 sizeof(cameraZoom));
      file.write(reinterpret_cast<const char*>(&worldChunkWidth),
                 sizeof(worldChunkWidth));
      file.write(reinterpret_cast<const char*>(&worldChunkHeight),
                 sizeof(worldChunkHeight));
      file.write(reinterpret_cast<const char*>(&chunkCount),
                 sizeof(chunkCount));
      for (const SparseChunkRecord& record : records) {
        file.write(reinterpret_cast<const char*>(&record.chunkX),
                   sizeof(record.chunkX));
        file.write(reinterpret_cast<const char*>(&record.chunkY),
                   sizeof(record.chunkY));
        file.write(reinterpret_cast<const char*>(record.cells.data()),
                   static_cast<std::streamsize>(record.cells.size()));
      }
      return file.good();
    },
    error);
}

bool
IllumoCodec::readFile(const std::string& path,
                      IllumoDocument* document,
                      std::string* error)
{
  if (document == nullptr) {
    setError(error, "Document pointer is null");
    return false;
  }
  if (path.empty()) {
    setError(error, "Load path is empty");
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    setError(error, "Failed to open for loading: " + path);
    return false;
  }

  const char expectedMagicV4[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '4', '\0' };
  const char expectedMagicV3[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '3', '\0' };
  const char expectedMagicV2[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '2', '\0' };
  char magic[sizeof(expectedMagicV3)] = {};
  if (!file.read(magic, sizeof(magic))) {
    setError(error, "Invalid or truncated Illumo save header");
    return false;
  }

  std::string ruleString;
  std::unique_ptr<SparseCellGrid> loadedGrid =
    std::make_unique<SparseCellGrid>();
  std::int64_t loadedWorldChunkWidth = 0;
  std::int64_t loadedWorldChunkHeight = 0;
  bool restoreCamera = false;
  double savedCameraX = 0.0;
  double savedCameraY = 0.0;
  double savedCameraZoom = 1.0;
  int fileVersion = 1;

  const bool sparseV4 = std::memcmp(magic, expectedMagicV4, sizeof(magic)) == 0;
  const bool sparseV3 = std::memcmp(magic, expectedMagicV3, sizeof(magic)) == 0;
  const bool sparseV2 = std::memcmp(magic, expectedMagicV2, sizeof(magic)) == 0;
  std::string familyString;
  if (sparseV4 || sparseV3 || sparseV2) {
    fileVersion = sparseV4 ? 4 : sparseV3 ? 3 : 2;
    std::uint32_t version = 0;
    char familyTag[MAX_RULETAG_SIZE] = {};
    char ruleTag[MAX_RULETAG_SIZE] = {};
    std::uint64_t chunkCount = 0;
    if (!file.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
        (sparseV4 && !file.read(familyTag, sizeof(familyTag))) ||
        !file.read(ruleTag, sizeof(ruleTag)) ||
        !file.read(reinterpret_cast<char*>(&savedCameraX),
                   sizeof(savedCameraX)) ||
        !file.read(reinterpret_cast<char*>(&savedCameraY),
                   sizeof(savedCameraY)) ||
        !file.read(reinterpret_cast<char*>(&savedCameraZoom),
                   sizeof(savedCameraZoom))) {
      setError(error, "Invalid or truncated sparse save header");
      return false;
    }
    if ((sparseV3 || sparseV4) &&
        (!file.read(reinterpret_cast<char*>(&loadedWorldChunkWidth),
                    sizeof(loadedWorldChunkWidth)) ||
         !file.read(reinterpret_cast<char*>(&loadedWorldChunkHeight),
                    sizeof(loadedWorldChunkHeight)))) {
      setError(error, "Invalid or truncated topology metadata");
      return false;
    }
    if (!file.read(reinterpret_cast<char*>(&chunkCount), sizeof(chunkCount))) {
      setError(error, "Invalid or truncated sparse save header");
      return false;
    }
    const std::uint32_t expectedVersion = sparseV4 ? 4u : sparseV3 ? 3u : 2u;
    if (version != expectedVersion || chunkCount > 10000000ULL ||
        !CanvasCoordinatePolicy::validPosition(savedCameraX, savedCameraY) ||
        !std::isfinite(savedCameraZoom) || savedCameraZoom < 0.1 ||
        savedCameraZoom > 100.0 ||
        !SparseCellGrid::isValidTopology(loadedWorldChunkWidth,
                                         loadedWorldChunkHeight)) {
      setError(error, "Sparse save contains invalid metadata");
      return false;
    }
    loadedGrid = std::make_unique<SparseCellGrid>(loadedWorldChunkWidth,
                                                  loadedWorldChunkHeight);
    if (sparseV4) {
      familyString = boundedRuleTag(familyTag, sizeof(familyTag));
    }
    ruleString = boundedRuleTag(ruleTag, sizeof(ruleTag));
    bool havePreviousChunk = false;
    std::int64_t previousChunkX = 0;
    std::int64_t previousChunkY = 0;
    for (std::uint64_t i = 0; i < chunkCount; ++i) {
      SparseChunkRecord record{};
      if (!file.read(reinterpret_cast<char*>(&record.chunkX),
                     sizeof(record.chunkX)) ||
          !file.read(reinterpret_cast<char*>(&record.chunkY),
                     sizeof(record.chunkY)) ||
          !file.read(reinterpret_cast<char*>(record.cells.data()),
                     static_cast<std::streamsize>(record.cells.size()))) {
        setError(error, "Sparse save is truncated");
        return false;
      }
      bool hasCell = false;
      for (unsigned char state : record.cells) {
        if (state != SparseCellGrid::BackgroundState) {
          hasCell = true;
          break;
        }
      }
      if (!hasCell) {
        setError(error, "Sparse save contains an empty chunk");
        return false;
      }
      if (havePreviousChunk && (record.chunkY < previousChunkY ||
                                (record.chunkY == previousChunkY &&
                                 record.chunkX <= previousChunkX))) {
        setError(error, "Sparse save chunk records are not strictly sorted");
        return false;
      }
      havePreviousChunk = true;
      previousChunkX = record.chunkX;
      previousChunkY = record.chunkY;
      if (!loadedGrid->assignChunk(record)) {
        setError(error, "Sparse save contains an invalid chunk");
        return false;
      }
    }
    restoreCamera = true;
  } else {
    fileVersion = 1;
    file.clear();
    file.seekg(0, std::ios::beg);
    char legacyRuleTag[MAX_RULETAG_SIZE] = {};
    int fileWidth = 0;
    int fileHeight = 0;
    if (!file.read(legacyRuleTag, sizeof(legacyRuleTag)) ||
        !file.read(reinterpret_cast<char*>(&fileWidth), sizeof(fileWidth)) ||
        !file.read(reinterpret_cast<char*>(&fileHeight), sizeof(fileHeight))) {
      setError(error, "Invalid or truncated legacy save header");
      return false;
    }
    ruleString = boundedRuleTag(legacyRuleTag, sizeof(legacyRuleTag));
    const long long fileCellCount =
      static_cast<long long>(fileWidth) * static_cast<long long>(fileHeight);
    if (fileWidth < 1 || fileHeight < 1 || fileCellCount < 1 ||
        fileCellCount > 100000000LL) {
      setError(error, "Legacy save contains invalid dimensions");
      return false;
    }
    const std::size_t cellBytes = static_cast<std::size_t>(fileCellCount);
    std::vector<unsigned char> loadedCells;
    try {
      loadedCells.resize(cellBytes);
    } catch (const std::bad_alloc&) {
      setError(error, "Legacy save is too large to load");
      return false;
    }
    if (!file.read(reinterpret_cast<char*>(loadedCells.data()),
                   static_cast<std::streamsize>(cellBytes))) {
      setError(error, "Legacy save is truncated");
      return false;
    }
    const std::int64_t originX = static_cast<std::int64_t>(fileWidth / 2);
    const std::int64_t originY = static_cast<std::int64_t>(fileHeight / 2);
    for (int y = 0; y < fileHeight; ++y) {
      for (int x = 0; x < fileWidth; ++x) {
        const unsigned char state =
          loadedCells[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(fileWidth) +
                      static_cast<std::size_t>(x)];
        if (state != SparseCellGrid::BackgroundState) {
          loadedGrid->setCell(
            CellAddress{ static_cast<std::int64_t>(x) - originX,
                         static_cast<std::int64_t>(y) - originY },
            state);
        }
      }
    }
  }

  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(ruleString);
  if (rule == nullptr) {
    setError(error, "Save uses unsupported ruleset: " + ruleString);
    return false;
  }
  if (!sparseV4) {
    familyString = rule->familyId;
  } else if (familyString != rule->familyId) {
    setError(error, "Save ruleset does not belong to its recorded family");
    return false;
  }
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(familyString);
  if (family == nullptr) {
    setError(error, "Save uses unsupported family: " + familyString);
    return false;
  }
  std::vector<SparseChunkRecord> records;
  loadedGrid->collectChunkRecords(&records);
  for (const SparseChunkRecord& record : records) {
    for (const unsigned char state : record.cells) {
      if (static_cast<unsigned int>(state) >= family->stateCount) {
        setError(error, "Save contains a state that is invalid for its family");
        return false;
      }
    }
  }

  document->version = fileVersion;
  document->familyString = familyString;
  document->ruleString = ruleString;
  document->cameraX = savedCameraX;
  document->cameraY = savedCameraY;
  document->cameraZoom = savedCameraZoom;
  document->restoreCamera = restoreCamera;
  document->worldChunkWidth = loadedWorldChunkWidth;
  document->worldChunkHeight = loadedWorldChunkHeight;
  document->grid = std::move(loadedGrid);
  return true;
}
