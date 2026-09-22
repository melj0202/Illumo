#pragma once
#include <IllumoGuest/Services.h>
#include <filesystem>
#include <memory>

struct WasmFileLimits
{
  std::uint64_t fileBytes = 512ull * 1024ull * 1024ull;
  std::uint64_t stagedBytes = 512ull * 1024ull * 1024ull;
  std::uint64_t storageBytes = 1024ull * 1024ull * 1024ull;
  std::uint32_t openFiles = 64;
};

struct WasmFileRoots
{
  std::filesystem::path package;
  std::filesystem::path storage;
  // A document the user named at launch (--open). Granted before the first
  // update as the selection "launch"; editable grants may be saved in place.
  std::filesystem::path launch;
  bool launchEditable = false;
};

// A dedicated native I/O worker owns streams and staging files. No guest call
// waits for disk work. Cancellation drops unpublished completions; a commit
// already executing may complete, as specified by the service contract.
class WasmFileServices
{
public:
  WasmFileServices(std::uint64_t owner,
                   std::uint32_t grants,
                   std::filesystem::path packageRoot,
                   std::filesystem::path storageRoot,
                   WasmFileLimits limits = {});
  ~WasmFileServices();
  WasmFileServices(const WasmFileServices&) = delete;
  WasmFileServices& operator=(const WasmFileServices&) = delete;
  WasmFileServices(WasmFileServices&&) = delete;
  WasmFileServices& operator=(WasmFileServices&&) = delete;
  bool submit(const GuestServices& requests);
  GuestServices poll(std::size_t byteBudget = GuestServices::MaximumBytes);
  // A readable grant requires an existing file; a writable-only grant needs
  // only its parent directory. Editable grants are both, for save-in-place.
  bool grantSelected(const std::filesystem::path& path,
                     bool writable,
                     std::string& name,
                     std::uint64_t& size);
  bool grantEditable(const std::filesystem::path& path,
                     std::string& name,
                     std::uint64_t& size);
  // Grants the launch document under the fixed selection name "launch".
  bool grantLaunch(const std::filesystem::path& path,
                   bool editable,
                   std::uint64_t& size);
  std::size_t pendingRequests() const;
  void cancel();
  const std::string& error() const;

private:
  class State;
  std::unique_ptr<State> m_state;
};
