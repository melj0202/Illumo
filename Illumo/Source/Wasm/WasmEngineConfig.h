#pragma once
#include <cstdint>
#include <wasmtime.h>

// Engine settings that shape compiled code. Compilation and deserialization
// must use identical options: the host decides them and hands them to the
// out-of-process compiler, whose own build configuration never matters, and
// Wasmtime rejects an artifact compiled under different settings.
struct WasmEngineOptions
{
  // Fuel instrumentation in every compiled block. Without it the epoch
  // deadline alone bounds a call.
  bool meterFuel = true;
  // Explicit bounds checks instead of signal-based traps. Required when the
  // host runs under AddressSanitizer: Windows ASan's lazy shadow-page handler
  // can recurse into Wasmtime's vectored exception handler.
  bool explicitBounds = false;
};

inline constexpr std::uint32_t kWasmEngineMeterFuel = 1u;
inline constexpr std::uint32_t kWasmEngineExplicitBounds = 2u;

inline std::uint32_t
encodeWasmEngineOptions(const WasmEngineOptions& options)
{
  return (options.meterFuel ? kWasmEngineMeterFuel : 0u) |
         (options.explicitBounds ? kWasmEngineExplicitBounds : 0u);
}

inline bool
decodeWasmEngineOptions(std::uint32_t mask, WasmEngineOptions& options)
{
  if ((mask & ~(kWasmEngineMeterFuel | kWasmEngineExplicitBounds)) != 0u) {
    return false;
  }
  options.meterFuel = (mask & kWasmEngineMeterFuel) != 0u;
  options.explicitBounds = (mask & kWasmEngineExplicitBounds) != 0u;
  return true;
}

inline wasm_engine_t*
createWasmEngine(const WasmEngineOptions& options)
{
  wasm_config_t* config = wasm_config_new();
  wasmtime_config_consume_fuel_set(config, options.meterFuel);
  wasmtime_config_epoch_interruption_set(config, true);
  wasmtime_config_wasm_exceptions_set(config, true);
  wasmtime_config_wasm_threads_set(config, false);
  wasmtime_config_wasm_memory64_set(config, false);
  wasmtime_config_wasm_multi_memory_set(config, false);
  if (options.explicitBounds) {
    // Sanitizer hosts use explicit guest checks. Fault-based configurations
    // retain speculative-execution mitigations.
    wasmtime_config_signals_based_traps_set(config, false);
    wasmtime_config_cranelift_flag_set(
      config, "enable_heap_access_spectre_mitigation", "false");
    wasmtime_config_cranelift_flag_set(
      config, "enable_table_access_spectre_mitigation", "false");
  }
  return wasm_engine_new_with_config(config);
}
