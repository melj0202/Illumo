#pragma once

#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <IllumoGuest/Services.h>
#include <filesystem>

// Native service executor scoped to one renderer owner. Validates the whole
// request batch before side effects; each accepted operation has its own
// result.
class WasmRenderServices
{
public:
  WasmRenderServices(WasmFrameRenderer& frames,
                     std::uint32_t grants,
                     std::filesystem::path engineAssets);
  WasmRenderServices(const WasmRenderServices&) = delete;
  WasmRenderServices& operator=(const WasmRenderServices&) = delete;
  WasmRenderServices(WasmRenderServices&&) = delete;
  WasmRenderServices& operator=(WasmRenderServices&&) = delete;
  bool process(std::span<const std::byte> requests,
               std::vector<std::byte>& completions);
  const std::string& error() const { return m_error; }
  std::size_t pendingRequests() const { return m_pending.size(); }

private:
  WasmFrameRenderer& m_frames;
  std::uint32_t m_grants;
  std::filesystem::path m_engineAssets;
  std::deque<GuestServiceRecord> m_pending;
  std::size_t m_pendingBytes = 0;
  std::uint64_t m_lastRequest = 0;
  std::string m_error;
};
