#include "WasmCompiler.h"
#include "WasmEngineConfig.h"
#include <Illumo/Wasm/WasmInstance.h>

#include <wasmtime.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>

struct WasmInstance::State
{
  explicit State(WasmLimits value)
    : limits(value)
  {
  }
  ~State()
  {
    if (ticker.joinable()) {
      ticker.request_stop();
      ticker.join();
    }
    if (store != nullptr) {
      wasmtime_store_delete(store);
    }
    if (engine != nullptr) {
      wasm_engine_delete(engine);
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  WasmLimits limits;
  wasm_engine_t* engine = nullptr;
  wasmtime_store_t* store = nullptr;
  wasmtime_context_t* context = nullptr;
  wasmtime_instance_t instance{};
  wasmtime_memory_t memory{};
  bool alive = false;
  bool attempted = false;
  std::string failure;
  WasmFailure failureKind = WasmFailure::None;
  std::thread::id owner = std::this_thread::get_id();
  std::jthread ticker;

  bool fail(std::string message)
  {
    alive = false;
    if (failureKind == WasmFailure::None) {
      failureKind = WasmFailure::Rejected;
    }
    failure = std::move(message);
    return false;
  }

  bool check(wasmtime_error_t* error, wasm_trap_t* trap = nullptr)
  {
    if (error == nullptr && trap == nullptr) {
      return true;
    }
    wasm_byte_vec_t message{};
    if (error != nullptr) {
      wasmtime_error_message(error, &message);
      wasmtime_error_delete(error);
    } else {
      wasm_trap_message(trap, &message);
      failureKind = WasmFailure::Trap;
      wasmtime_trap_code_t code{};
      if (wasmtime_trap_code(trap, &code)) {
        if (code == WASMTIME_TRAP_CODE_OUT_OF_FUEL) {
          failureKind = WasmFailure::FuelExhausted;
        } else if (code == WASMTIME_TRAP_CODE_INTERRUPT) {
          failureKind = WasmFailure::DeadlineExceeded;
        }
      }
    }
    std::string text(message.data, message.size);
    wasm_byte_vec_delete(&message);
    if (trap != nullptr) {
      wasm_trap_delete(trap);
    }
    return fail(std::move(text));
  }

  bool budget()
  {
    wasmtime_context_set_epoch_deadline(
      context,
      (static_cast<std::uint64_t>(limits.deadlineMilliseconds) + 9u) / 10u);
    return check(wasmtime_context_set_fuel(context, limits.fuelPerCall));
  }

  bool bounds(std::uint32_t offset, std::size_t size)
  {
    if (!alive || std::this_thread::get_id() != owner) {
      return false;
    }
    const std::size_t available = wasmtime_memory_data_size(context, &memory);
    if (offset > available || size > available - offset) {
      return fail("Guest memory range is outside linear memory");
    }
    return true;
  }
};

static wasm_trap_t*
denyFile(void*,
         wasmtime_caller_t*,
         const wasmtime_val_t*,
         std::size_t,
         wasmtime_val_t* results,
         std::size_t)
{
  // libc pulls these symbols in even without file use. No descriptors exist.
  results[0].kind = WASMTIME_I32;
  results[0].of.i32 = 8; // WASI EBADF
  return nullptr;
}

static wasm_trap_t*
emptyEnvironment(void*,
                 wasmtime_caller_t*,
                 const wasmtime_val_t*,
                 std::size_t,
                 wasmtime_val_t* results,
                 std::size_t)
{
  results[0].kind = WASMTIME_I32;
  results[0].of.i32 = 0;
  return nullptr;
}

static wasm_trap_t*
emptyEnvironmentSizes(void*,
                      wasmtime_caller_t* caller,
                      const wasmtime_val_t* args,
                      std::size_t,
                      wasmtime_val_t* results,
                      std::size_t)
{
  wasmtime_extern_t memory{};
  if (!wasmtime_caller_export_get(caller, "memory", 6, &memory)) {
    return wasmtime_trap_new("Missing guest memory", 20);
  }
  wasmtime_context_t* context = wasmtime_caller_context(caller);
  if (memory.kind != WASMTIME_EXTERN_MEMORY) {
    wasmtime_extern_delete(&memory);
    return wasmtime_trap_new("Invalid guest memory", 20);
  }
  const std::size_t size =
    wasmtime_memory_data_size(context, &memory.of.memory);
  const std::uint32_t count = static_cast<std::uint32_t>(args[0].of.i32);
  const std::uint32_t bytes = static_cast<std::uint32_t>(args[1].of.i32);
  if (size < 4u || count > size - 4u || bytes > size - 4u) {
    wasmtime_extern_delete(&memory);
    return wasmtime_trap_new("Invalid environment output range", 32);
  }
  std::uint8_t* data = wasmtime_memory_data(context, &memory.of.memory);
  std::memset(data + count, 0, 4);
  std::memset(data + bytes, 0, 4);
  wasmtime_extern_delete(&memory);
  results[0].kind = WASMTIME_I32;
  results[0].of.i32 = 0;
  return nullptr;
}

static wasm_trap_t*
guestClock(void*,
           wasmtime_caller_t* caller,
           const wasmtime_val_t* args,
           std::size_t,
           wasmtime_val_t* results,
           std::size_t)
{
  results[0].kind = WASMTIME_I32;
  results[0].of.i32 = 28; // WASI EINVAL for unsupported CPU clocks.
  const std::int32_t clock = args[0].of.i32;
  if (clock != 0 && clock != 1) {
    return nullptr;
  }
  wasmtime_extern_t memory{};
  if (!wasmtime_caller_export_get(caller, "memory", 6, &memory)) {
    return wasmtime_trap_new("Missing guest memory", 20);
  }
  wasmtime_context_t* context = wasmtime_caller_context(caller);
  if (memory.kind != WASMTIME_EXTERN_MEMORY) {
    wasmtime_extern_delete(&memory);
    return wasmtime_trap_new("Invalid guest memory", 20);
  }
  const std::size_t size =
    wasmtime_memory_data_size(context, &memory.of.memory);
  const std::uint32_t offset = static_cast<std::uint32_t>(args[2].of.i32);
  if (size < 8u || offset > size - 8u) {
    wasmtime_extern_delete(&memory);
    return wasmtime_trap_new("Invalid clock output range", 26);
  }
  const std::uint64_t nanos = static_cast<std::uint64_t>(
    clock == 0 ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()
               : std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
  std::uint8_t* output =
    wasmtime_memory_data(context, &memory.of.memory) + offset;
  for (unsigned int index = 0; index < 8u; ++index) {
    output[index] = static_cast<std::uint8_t>(nanos >> (index * 8u));
  }
  wasmtime_extern_delete(&memory);
  results[0].of.i32 = 0;
  return nullptr;
}

static wasm_trap_t*
exitGuest(void*,
          wasmtime_caller_t*,
          const wasmtime_val_t*,
          std::size_t,
          wasmtime_val_t*,
          std::size_t)
{
  constexpr char kMessage[] = "Guest requested process exit";
  return wasmtime_trap_new(kMessage, sizeof(kMessage) - 1u);
}

static wasmtime_error_t*
defineLibcFunction(wasmtime_linker_t* linker,
                   const char* name,
                   std::span<const wasm_valkind_t> kinds,
                   bool exits = false,
                   wasmtime_func_callback_t callback = denyFile)
{
  wasm_valtype_vec_t parameters{};
  wasm_valtype_vec_new_uninitialized(&parameters, kinds.size());
  for (std::size_t index = 0; index < kinds.size(); ++index) {
    parameters.data[index] = wasm_valtype_new(kinds[index]);
  }
  wasm_valtype_vec_t results{};
  wasm_valtype_vec_new_uninitialized(&results, exits ? 0u : 1u);
  if (!exits) {
    results.data[0] = wasm_valtype_new_i32();
  }
  wasm_functype_t* type = wasm_functype_new(&parameters, &results);
  constexpr char kModule[] = "wasi_snapshot_preview1";
  wasmtime_error_t* error =
    wasmtime_linker_define_func(linker,
                                kModule,
                                sizeof(kModule) - 1u,
                                name,
                                std::strlen(name),
                                type,
                                exits ? exitGuest : callback,
                                nullptr,
                                nullptr);
  wasm_functype_delete(type);
  return error;
}

WasmInstance::WasmInstance(WasmLimits limits)
  : m_state(std::make_unique<State>(limits))
{
}

WasmInstance::~WasmInstance() = default;

bool
WasmInstance::load(std::span<const std::byte> bytes)
{
  State& state = *m_state;
  if (std::this_thread::get_id() != state.owner || state.attempted) {
    return false;
  }
  state.attempted = true;
  if (bytes.empty() || bytes.size() > state.limits.moduleBytes ||
      state.limits.memoryBytes == 0u || state.limits.memoryBytes > UINT32_MAX ||
      state.limits.fuelPerCall == 0u ||
      state.limits.deadlineMilliseconds == 0u) {
    return state.fail("Invalid guest module size or execution limits");
  }
  std::vector<std::byte> compiled;
  std::string compileError;
  if (!compileWasmIsolated(bytes,
                           state.limits.compilerMemoryBytes,
                           state.limits.compilerDeadlineMilliseconds,
                           compiled,
                           compileError)) {
    return state.fail(std::move(compileError));
  }
  state.engine = createWasmEngine();
  if (state.engine == nullptr) {
    return state.fail("Cannot create WASM engine");
  }
  state.store = wasmtime_store_new(state.engine, nullptr, nullptr);
  state.context = wasmtime_store_context(state.store);
  wasmtime_store_limiter(state.store,
                         static_cast<std::int64_t>(state.limits.memoryBytes),
                         state.limits.tableElements,
                         1,
                         1,
                         1);
  state.ticker = std::jthread([engine = state.engine](std::stop_token stop) {
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      wasmtime_engine_increment_epoch(engine);
    }
  });
  wasmtime_module_t* module = nullptr;
  if (!state.check(wasmtime_module_deserialize(
        state.engine,
        reinterpret_cast<const std::uint8_t*>(compiled.data()),
        compiled.size(),
        &module))) {
    return false;
  }
  wasmtime_linker_t* linker = wasmtime_linker_new(state.engine);
  constexpr std::array<wasm_valkind_t, 1> kOne{ WASM_I32 };
  constexpr std::array<wasm_valkind_t, 2> kTwo{ WASM_I32, WASM_I32 };
  constexpr std::array<wasm_valkind_t, 3> kThree{ WASM_I32,
                                                  WASM_I32,
                                                  WASM_I32 };
  constexpr std::array<wasm_valkind_t, 3> kClock{ WASM_I32,
                                                  WASM_I64,
                                                  WASM_I32 };
  constexpr std::array<wasm_valkind_t, 4> kFour{
    WASM_I32, WASM_I32, WASM_I32, WASM_I32
  };
  constexpr std::array<wasm_valkind_t, 4> kSeek{
    WASM_I32, WASM_I64, WASM_I32, WASM_I32
  };
  const bool linked =
    state.check(defineLibcFunction(linker, "fd_close", kOne)) &&
    state.check(defineLibcFunction(linker, "fd_prestat_get", kTwo)) &&
    state.check(defineLibcFunction(linker, "fd_prestat_dir_name", kThree)) &&
    state.check(defineLibcFunction(linker, "fd_seek", kSeek)) &&
    state.check(defineLibcFunction(linker, "fd_write", kFour)) &&
    state.check(defineLibcFunction(linker, "proc_exit", kOne, true)) &&
    state.check(defineLibcFunction(
      linker, "environ_get", kTwo, false, emptyEnvironment)) &&
    state.check(defineLibcFunction(
      linker, "environ_sizes_get", kTwo, false, emptyEnvironmentSizes)) &&
    state.check(
      defineLibcFunction(linker, "clock_time_get", kClock, false, guestClock));
  wasm_trap_t* trap = nullptr;
  bool instantiated = false;
  if (linked && state.budget()) {
    wasmtime_error_t* error = wasmtime_linker_instantiate(
      linker, state.context, module, &state.instance, &trap);
    instantiated = state.check(error, trap);
  }
  wasmtime_linker_delete(linker);
  wasmtime_module_delete(module);
  if (!instantiated) {
    return false;
  }
  wasmtime_extern_t exported{};
  if (!wasmtime_instance_export_get(
        state.context, &state.instance, "memory", 6, &exported)) {
    return state.fail("Guest must export linear memory");
  }
  const bool isMemory = exported.kind == WASMTIME_EXTERN_MEMORY;
  if (isMemory) {
    state.memory = exported.of.memory;
  }
  wasmtime_extern_delete(&exported);
  if (!isMemory) {
    return state.fail("Guest memory export has the wrong type");
  }
  if (wasmtime_instance_export_get(
        state.context, &state.instance, "_initialize", 11, &exported)) {
    bool initialized = false;
    if (exported.kind == WASMTIME_EXTERN_FUNC && state.budget()) {
      trap = nullptr;
      wasmtime_error_t* error = wasmtime_func_call(
        state.context, &exported.of.func, nullptr, 0, nullptr, 0, &trap);
      initialized = state.check(error, trap);
    }
    wasmtime_extern_delete(&exported);
    if (!initialized) {
      return state.fail("Guest reactor initialization failed: " +
                        state.failure);
    }
  }
  state.alive = true;
  return true;
}

bool
WasmInstance::call(std::string_view name,
                   std::span<const std::int32_t> arguments,
                   std::int32_t& result)
{
  State& state = *m_state;
  if (!state.alive || std::this_thread::get_id() != state.owner) {
    return false;
  }
  if (arguments.size() > 8u) {
    return state.fail("Too many guest arguments");
  }
  wasmtime_extern_t exported{};
  if (!wasmtime_instance_export_get(
        state.context, &state.instance, name.data(), name.size(), &exported)) {
    return state.fail("Missing guest export: " + std::string(name));
  }
  if (exported.kind != WASMTIME_EXTERN_FUNC) {
    wasmtime_extern_delete(&exported);
    return state.fail("Guest export is not a function");
  }
  std::array<wasmtime_val_t, 8> values{};
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    values[index].kind = WASMTIME_I32;
    values[index].of.i32 = arguments[index];
  }
  wasmtime_val_t returned{};
  wasm_trap_t* trap = nullptr;
  bool succeeded = false;
  if (state.budget()) {
    wasmtime_error_t* error = wasmtime_func_call(state.context,
                                                 &exported.of.func,
                                                 values.data(),
                                                 arguments.size(),
                                                 &returned,
                                                 1,
                                                 &trap);
    succeeded = state.check(error, trap);
  }
  wasmtime_extern_delete(&exported);
  if (!succeeded) {
    return false;
  }
  const bool isInteger = returned.kind == WASMTIME_I32;
  if (isInteger) {
    result = returned.of.i32;
  }
  wasmtime_val_unroot(&returned);
  return isInteger || state.fail("Guest result must be i32");
}

bool
WasmInstance::copyFromMemory(std::uint32_t offset, std::span<std::byte> output)
{
  if (!m_state->bounds(offset, output.size())) {
    return false;
  }
  if (!output.empty()) {
    std::memcpy(output.data(),
                wasmtime_memory_data(m_state->context, &m_state->memory) +
                  offset,
                output.size());
  }
  return true;
}

bool
WasmInstance::copyToMemory(std::uint32_t offset,
                           std::span<const std::byte> input)
{
  if (!m_state->bounds(offset, input.size())) {
    return false;
  }
  if (!input.empty()) {
    std::memcpy(wasmtime_memory_data(m_state->context, &m_state->memory) +
                  offset,
                input.data(),
                input.size());
  }
  return true;
}

bool
WasmInstance::isAlive() const
{
  return m_state->alive;
}

const std::string&
WasmInstance::error() const
{
  return m_state->failure;
}

WasmFailure
WasmInstance::failure() const
{
  return m_state->failureKind;
}
