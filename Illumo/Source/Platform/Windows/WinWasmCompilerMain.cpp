#include "../../Wasm/WasmEngineConfig.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Illumo/Foundation/ParseNumber.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <windows.h>

// This helper compiles but never instantiates untrusted WASM. The parent
// assigns it to a memory-limited, single-process job before resuming its
// initial thread. Arguments: the shared mapping handle and the engine options
// mask of the host engine that will deserialize the artifact. Exit codes: 0
// success, 2 invalid invocation, 3 the module did not compile, 4 the artifact
// exceeds the shared buffer. Anything else is an abnormal termination (for
// example the job's memory limit).
int
main(int argc, char** argv)
{
  WasmEngineOptions options;
  std::uint32_t mask = 0;
  std::uint64_t handleValue = 0;
  if (argc != 3 ||
      std::string(argv[2]).find_first_not_of("0123456789") !=
        std::string::npos ||
      !parseWholeInteger(std::string_view(argv[2]), &mask) ||
      !decodeWasmEngineOptions(mask, options) ||
      !parseWholeInteger(std::string_view(argv[1]), &handleValue)) {
    return 2;
  }
  constexpr std::size_t kCapacity = 256u * 1024u * 1024u;
  constexpr std::size_t kHeaderBytes = 16u;
  const HANDLE mapping = reinterpret_cast<HANDLE>(handleValue);
  std::uint8_t* shared = static_cast<std::uint8_t*>(
    MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kCapacity));
  if (shared == nullptr) {
    return 2;
  }
  std::uint64_t inputSize = 0;
  std::memcpy(&inputSize, shared, sizeof(inputSize));
  if (inputSize == 0 || inputSize > kCapacity - kHeaderBytes) {
    UnmapViewOfFile(shared);
    return 2;
  }
  wasm_engine_t* engine = createWasmEngine(options);
  if (engine == nullptr) {
    UnmapViewOfFile(shared);
    return 2;
  }
  wasmtime_module_t* module = nullptr;
  wasmtime_error_t* error =
    wasmtime_module_new(engine, shared + kHeaderBytes, inputSize, &module);
  wasm_byte_vec_t serialized{};
  if (error == nullptr) {
    error = wasmtime_module_serialize(module, &serialized);
  }
  int status = 0;
  if (error != nullptr) {
    status = 3;
  } else if (serialized.size > kCapacity - kHeaderBytes) {
    status = 4;
  } else {
    const std::uint64_t outputSize = serialized.size;
    std::memcpy(shared + kHeaderBytes, serialized.data, serialized.size);
    std::memcpy(shared + sizeof(inputSize), &outputSize, sizeof(outputSize));
  }
  if (error != nullptr) {
    wasmtime_error_delete(error);
  }
  wasm_byte_vec_delete(&serialized);
  if (module != nullptr) {
    wasmtime_module_delete(module);
  }
  wasm_engine_delete(engine);
  UnmapViewOfFile(shared);
  return status;
}
