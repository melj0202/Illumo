#pragma once

#include <Illumo/Wasm/WasmInstance.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

enum class WasmWorkerStatus
{
  Loading,
  Idle,
  Working,
  Completed,
  Failed
};

struct WasmJobResult
{
  std::uint64_t requestId = 0;
  std::vector<std::byte> bytes;
  std::string error;
};

// A persistent store entered only on its owning worker thread. One outstanding
// job; IDs originate here, never from guest-provided response metadata.
class WasmWorker
{
public:
  explicit WasmWorker(std::vector<std::byte> module,
                      WasmLimits limits = {},
                      std::uint32_t messageLimit = 64u * 1024u * 1024u);
  ~WasmWorker();
  WasmWorker(const WasmWorker&) = delete;
  WasmWorker& operator=(const WasmWorker&) = delete;
  WasmWorker(WasmWorker&&) = delete;
  WasmWorker& operator=(WasmWorker&&) = delete;

  WasmWorkerStatus status() const;
  bool submit(std::span<const std::byte> bytes, std::uint64_t& requestId);
  bool poll(WasmJobResult& result);
  void requestStop();

private:
  struct State;
  std::unique_ptr<State> m_state;
};
