#pragma once

#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Platform/SaveLoad.h>
#include <functional>
#include <string>
#include <vector>

struct CSimReadResult
{
  bool success = false;
  // True when the location does not exist; other failures leave it false.
  bool missing = false;
  std::string location;
  std::string bytes;
  std::string error;
};

// Product I/O seam for dialogs, files, clipboard and the user rule overlay.
// Completions may run before the request returns (native test oracle) or on a
// later update (WASM guest services); callers must handle both and guard
// their own lifetime. A location is a path natively and a storage name or a
// selected-file capability name in the guest; product code never builds paths
// from it beyond appending an extension to a user-typed name.
class CSimPlatform
{
public:
  using LocationCallback = std::function<void(const std::string& location)>;
  using ReadCallback = std::function<void(const CSimReadResult& result)>;
  using WriteCallback =
    std::function<void(bool success, const std::string& error)>;
  using TextCallback =
    std::function<void(bool success, const std::string& text)>;

  CSimPlatform() = default;
  virtual ~CSimPlatform() = default;
  CSimPlatform(const CSimPlatform&) = delete;
  CSimPlatform& operator=(const CSimPlatform&) = delete;
  CSimPlatform(CSimPlatform&&) = delete;
  CSimPlatform& operator=(CSimPlatform&&) = delete;

  // An empty location reports cancellation.
  virtual void chooseLoadLocation(const SaveLoadDialogSpec& specification,
                                  LocationCallback done) = 0;
  virtual void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                                  LocationCallback done) = 0;
  // Reads the first candidate that exists; `missing` means none did.
  virtual void readFirst(std::vector<std::string> locations,
                         ReadCallback done) = 0;
  // Atomic replacement: a failed write leaves the previous content intact.
  virtual void writeFile(const std::string& location,
                         std::string bytes,
                         WriteCallback done) = 0;
  virtual void readClipboard(TextCallback done) = 0;
  virtual void writeClipboard(const std::string& text) = 0;
  // Merges definitions into the user overlay catalogs next to the product's
  // storage, validating against the active registry.
  virtual void saveUserCatalog(std::vector<RuleFamilyDefinition> families,
                               std::vector<RuleSetDefinition> rules,
                               WriteCallback done) = 0;

  // Defined once per link: the native oracle or the guest service adapter.
  static CSimPlatform& current();
};
