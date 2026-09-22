#pragma once

#include <Illumo/Platform/SaveLoad.h>
#include <functional>
#include <string>

// A scene document's location and what the editor shows for it. Natively
// both are the file path; in the WASM package the location is an opaque file
// grant and the label is only the file's base name.
struct IllEdLocation
{
  std::string location;
  std::string label;
  bool empty() const { return location.empty(); }
};

// Editor I/O seam for dialogs and scene files. Completions may run before the
// request returns (native test oracle) or on a later update (WASM guest
// services); callers must handle both and guard their own lifetime.
class IllEdPlatform
{
public:
  using LocationCallback = std::function<void(const IllEdLocation& chosen)>;
  using ReadCallback = std::function<
    void(bool success, const std::string& text, const std::string& error)>;
  using WriteCallback =
    std::function<void(bool success, const std::string& error)>;

  IllEdPlatform() = default;
  virtual ~IllEdPlatform() = default;
  IllEdPlatform(const IllEdPlatform&) = delete;
  IllEdPlatform& operator=(const IllEdPlatform&) = delete;
  IllEdPlatform(IllEdPlatform&&) = delete;
  IllEdPlatform& operator=(IllEdPlatform&&) = delete;

  // An empty location reports cancellation. Opened documents stay writable,
  // so Save can replace them in place.
  virtual void chooseOpenLocation(const SaveLoadDialogSpec& specification,
                                  LocationCallback done) = 0;
  // The chosen location carries the .ilsc extension.
  virtual void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                                  LocationCallback done) = 0;
  virtual void read(const std::string& location, ReadCallback done) = 0;
  // Atomic replacement: a failed write leaves the previous content intact.
  virtual void write(const std::string& location,
                     std::string text,
                     WriteCallback done) = 0;
  // The document named when the product launched, if any.
  virtual IllEdLocation launchDocument() const { return {}; }

  // Defined once per link: the native oracle or the guest service adapter.
  static IllEdPlatform& current();
};
