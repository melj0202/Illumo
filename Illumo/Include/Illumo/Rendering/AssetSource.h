#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Byte access for AssetManager. Paths are product-relative names; the source
// decides what they mean. The native default reads the filesystem; a WASM
// guest serves bytes its package preloaded, since it has no file system.
// read() and stamp() may run on AssetManager's worker thread.
class IAssetSource
{
public:
  IAssetSource() = default;
  virtual ~IAssetSource() = default;
  IAssetSource(const IAssetSource&) = delete;
  IAssetSource& operator=(const IAssetSource&) = delete;
  IAssetSource(IAssetSource&&) = delete;
  IAssetSource& operator=(IAssetSource&&) = delete;

  // Stable identity used for caching and reload matching.
  virtual std::string canonical(const std::string& path) const = 0;
  // Whole-asset bytes; false when absent or unreadable.
  virtual bool read(const std::string& canonical,
                    std::vector<unsigned char>& bytes) const = 0;
  // Change stamp for hot reload; 0 when unknown or immutable.
  virtual std::int64_t stamp(const std::string& canonical) const = 0;
  // Whether shader and mesh files may be opened by path (the native file
  // system); package sources supply bytes only.
  virtual bool hasFileSystem() const { return false; }
};

// Process-wide filesystem source used when no other source is supplied.
// Unavailable (nullptr) in serial WASM guests.
IAssetSource*
DefaultAssetSource();
