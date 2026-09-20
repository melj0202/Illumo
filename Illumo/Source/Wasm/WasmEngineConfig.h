#pragma once
#include <wasmtime.h>

// Compilation and deserialization must use identical feature/instrumentation
// settings. Only the bundled compiler produces native artifacts for the host.
inline wasm_engine_t*
createWasmEngine()
{
  wasm_config_t* config = wasm_config_new();
  wasmtime_config_consume_fuel_set(config, true);
  wasmtime_config_epoch_interruption_set(config, true);
  wasmtime_config_wasm_exceptions_set(config, true);
  wasmtime_config_wasm_threads_set(config, false);
  wasmtime_config_wasm_memory64_set(config, false);
  wasmtime_config_wasm_multi_memory_set(config, false);
#ifdef _DEBUG
  // Windows ASan's lazy shadow-page handler can recurse into Wasmtime's
  // vectored exception handler. Sanitizer builds use explicit guest checks.
  // Release retains fault-based checks and speculative-execution mitigations.
  wasmtime_config_signals_based_traps_set(config, false);
  wasmtime_config_cranelift_flag_set(
    config, "enable_heap_access_spectre_mitigation", "false");
  wasmtime_config_cranelift_flag_set(
    config, "enable_table_access_spectre_mitigation", "false");
#endif
  return wasm_engine_new_with_config(config);
}
