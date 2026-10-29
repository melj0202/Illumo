#include "../../Wasm/WasmEngineConfig.h"
#include <Illumo/Foundation/ParseNumber.h>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>

// This helper compiles but never instantiates untrusted WASM. Arguments: the
// shared buffer's descriptor, the engine options mask of the host engine that
// will deserialize the artifact, and the memory limit in bytes. Before it
// reads the module it limits its private writable memory (RLIMIT_DATA, the
// closest match to the Windows job's process memory limit), disables core
// dumps and asks to be killed with its parent. Exit codes: 0 success, 2
// invalid invocation, 3 the module did not compile, 4 the artifact exceeds
// the shared buffer. Anything else is an abnormal termination (for example
// an allocation refused by the memory limit).
static bool
parseDecimal(const char* text, std::uint64_t* value)
{
  const std::string_view digits(text);
  return !digits.empty() &&
         digits.find_first_not_of("0123456789") == std::string_view::npos &&
         parseWholeInteger(digits, value);
}

int
main(int argc, char** argv)
{
  WasmEngineOptions options;
  std::uint64_t descriptorValue = 0;
  std::uint64_t mask = 0;
  std::uint64_t memoryLimit = 0;
  if (argc != 4 || !parseDecimal(argv[1], &descriptorValue) ||
      !parseDecimal(argv[2], &mask) || !parseDecimal(argv[3], &memoryLimit) ||
      descriptorValue > 1024u || mask > UINT32_MAX ||
      !decodeWasmEngineOptions(static_cast<std::uint32_t>(mask), options) ||
      memoryLimit < 16u * 1024u * 1024u) {
    return 2;
  }
  const rlimit memory{ static_cast<rlim_t>(memoryLimit),
                       static_cast<rlim_t>(memoryLimit) };
  const rlimit noCore{ 0, 0 };
  if (setrlimit(RLIMIT_DATA, &memory) != 0 ||
      setrlimit(RLIMIT_CORE, &noCore) != 0 ||
      prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
    return 2;
  }
  constexpr std::size_t kCapacity = 256u * 1024u * 1024u;
  constexpr std::size_t kHeaderBytes = 16u;
  void* view = mmap(nullptr,
                    kCapacity,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    static_cast<int>(descriptorValue),
                    0);
  if (view == MAP_FAILED) {
    return 2;
  }
  std::uint8_t* shared = static_cast<std::uint8_t*>(view);
  std::uint64_t inputSize = 0;
  std::memcpy(&inputSize, shared, sizeof(inputSize));
  if (inputSize == 0 || inputSize > kCapacity - kHeaderBytes) {
    munmap(view, kCapacity);
    return 2;
  }
  wasm_engine_t* engine = createWasmEngine(options);
  if (engine == nullptr) {
    munmap(view, kCapacity);
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
  munmap(view, kCapacity);
  return status;
}
