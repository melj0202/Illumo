#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

struct WasmLimits
{
  std::uint64_t memoryBytes = 64u * 1024u * 1024u;
  // Fuel metering instruments every compiled block and bounds each call by
  // fuelPerCall. Without it (epoch-only) the deadline alone bounds a call and
  // fuelPerCall is ignored.
  bool meterFuel = true;
  std::uint64_t fuelPerCall = 10000000u;
  std::uint32_t deadlineMilliseconds = 1000u;
  std::uint32_t moduleBytes = 64u * 1024u * 1024u;
  std::uint32_t tableElements = 65536u;
  std::uint64_t compilerMemoryBytes = 1024u * 1024u * 1024u;
  std::uint32_t compilerDeadlineMilliseconds = 30000u;
};

// The engine options mask (see WasmEngineConfig.h) this host uses for a store
// with or without fuel metering. Exposed for diagnostics and tests.
std::uint32_t
wasmHostEngineOptions(bool meterFuel);

enum class WasmFailure
{
  None,
  Rejected,
  Trap,
  FuelExhausted,
  DeadlineExceeded
};

// One store and linear memory, affine to its creating thread. A failed call
// permanently retires the instance; recreate it from a known state to recover.
// Returned data is always copied. No runtime or guest pointer escapes this API.
class WasmInstance
{
public:
  explicit WasmInstance(WasmLimits limits = {});
  ~WasmInstance();
  WasmInstance(const WasmInstance&) = delete;
  WasmInstance& operator=(const WasmInstance&) = delete;
  WasmInstance(WasmInstance&&) = delete;
  WasmInstance& operator=(WasmInstance&&) = delete;

  bool load(std::span<const std::byte> module);
  bool call(std::string_view name,
            std::span<const std::int32_t> arguments,
            std::int32_t& result);
  bool copyFromMemory(std::uint32_t offset, std::span<std::byte> output);
  bool copyToMemory(std::uint32_t offset, std::span<const std::byte> input);
  bool isAlive() const;
  const std::string& error() const;
  WasmFailure failure() const;

private:
  struct State;
  std::unique_ptr<State> m_state;
};
