#pragma once

#include <Illumo/Wasm/WasmInstance.h>
#include <IllumoGuest/Protocol.h>
#include <vector>

// Protocol/lifecycle boundary shared by games and mods. The caller supplies
// grants; descriptor requests can only narrow them. Payloads are owned copies.
// Service/render decoders must validate their schemas before acting on a reply.
class WasmGuest
{
public:
  explicit WasmGuest(WasmLimits limits = {},
                     std::uint32_t messageLimit = 64u * 1024u * 1024u);
  ~WasmGuest();
  WasmGuest(const WasmGuest&) = delete;
  WasmGuest& operator=(const WasmGuest&) = delete;
  WasmGuest(WasmGuest&&) = delete;
  WasmGuest& operator=(WasmGuest&&) = delete;

  bool start(std::span<const std::byte> module,
             GuestRole role,
             std::uint32_t grants,
             std::span<const std::byte> startup,
             std::vector<std::byte>& response);
  bool invoke(GuestCall call,
              std::span<const std::byte> input,
              std::vector<std::byte>& output);
  void retire(std::string reason);
  void shutdown();
  bool isAlive() const;
  std::uint64_t session() const;
  std::uint32_t capabilities() const;
  const GuestDescriptor& descriptor() const;
  const std::string& error() const;

private:
  bool exchange(GuestCall call,
                std::span<const std::byte> input,
                std::vector<std::byte>& output);
  bool readResult(std::int32_t address,
                  std::vector<std::byte>& output,
                  std::uint32_t maximum);
  bool fail(std::string reason);
  WasmLimits m_limits;
  std::uint32_t m_messageLimit;
  std::unique_ptr<WasmInstance> m_instance;
  GuestDescriptor m_descriptor;
  std::string m_error;
  std::uint64_t m_session = 0;
  std::uint64_t m_sequence = 0;
  std::uint32_t m_capabilities = 0;
  bool m_started = false;
  bool m_attempted = false;
};
