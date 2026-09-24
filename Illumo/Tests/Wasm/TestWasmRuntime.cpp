#include <Illumo/Wasm/WasmGuest.h>
#include <Illumo/Wasm/WasmInstance.h>
#include <Illumo/Wasm/WasmResourceTable.h>
#include <Illumo/Wasm/WasmWorker.h>
#include <IllumoGuest/Wire.h>

// Narrow private policy headers: engine options and the isolated compiler.
#include "../../Source/Wasm/WasmCompiler.h"
#include "../../Source/Wasm/WasmEngineConfig.h"

#include <wasmtime.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>

static bool
require(bool success,
        const char* message,
        const WasmInstance* instance = nullptr)
{
  if (!success) {
    std::fprintf(stderr,
                 "FAIL: %s: %s\n",
                 message,
                 instance == nullptr ? "" : instance->error().c_str());
  }
  return success;
}

static std::vector<std::byte>
readGuest(const char* path = ILLUMO_WASM_TEST_GUEST)
{
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> contents{ std::istreambuf_iterator<char>(stream),
                                    {} };
  std::vector<std::byte> bytes(contents.size());
  std::memcpy(bytes.data(), contents.data(), contents.size());
  return bytes;
}

static bool
waitWorker(WasmWorker& worker, WasmWorkerStatus expected)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (worker.status() == expected) {
      return true;
    }
    if (worker.status() == WasmWorkerStatus::Failed &&
        expected != WasmWorkerStatus::Failed) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

// True when an artifact compiled with compileOptions deserializes into an
// engine created with engineOptions.
static bool
deserializes(const std::vector<std::byte>& bytes,
             std::uint32_t compileOptions,
             std::uint32_t engineOptions)
{
  std::vector<std::byte> artifact;
  std::string error;
  WasmEngineOptions options;
  if (!compileWasmIsolated(bytes,
                           1024ull * 1024ull * 1024ull,
                           30000u,
                           compileOptions,
                           artifact,
                           error) ||
      !decodeWasmEngineOptions(engineOptions, options)) {
    return false;
  }
  wasm_engine_t* engine = createWasmEngine(options);
  wasmtime_module_t* module = nullptr;
  wasmtime_error_t* failure = wasmtime_module_deserialize(
    engine,
    reinterpret_cast<const std::uint8_t*>(artifact.data()),
    artifact.size(),
    &module);
  const bool accepted = failure == nullptr && module != nullptr;
  if (failure != nullptr) {
    wasmtime_error_delete(failure);
  }
  if (module != nullptr) {
    wasmtime_module_delete(module);
  }
  wasm_engine_delete(engine);
  return accepted;
}

static bool
engineModes(const std::vector<std::byte>& bytes)
{
  const std::uint32_t metered = wasmHostEngineOptions(true);
  const std::uint32_t epoch = wasmHostEngineOptions(false);
#if defined(ILLUMO_ASAN) || defined(__SANITIZE_ADDRESS__)
  const bool sanitized = true;
#else
  const bool sanitized = false;
#endif
  WasmEngineOptions decoded;
  if (!require(decodeWasmEngineOptions(metered, decoded) && decoded.meterFuel &&
                 decoded.explicitBounds == sanitized &&
                 decodeWasmEngineOptions(epoch, decoded) &&
                 !decoded.meterFuel && decoded.explicitBounds == sanitized,
               "Host engine options follow metering and the ASan build") ||
      !require(!decodeWasmEngineOptions(4u, decoded),
               "Unknown engine option bits are rejected")) {
    return false;
  }
  std::vector<std::byte> artifact;
  std::string error;
  if (!require(
        !compileWasmIsolated(
          bytes, 1024ull * 1024ull * 1024ull, 30000u, 4u, artifact, error),
        "The compiler refuses an invalid options mask") ||
      !require(deserializes(bytes, metered, metered) &&
                 deserializes(bytes, epoch, epoch),
               "Artifacts load under the options they were compiled with") ||
      !require(!deserializes(bytes, metered, epoch) &&
                 !deserializes(bytes, epoch, metered),
               "Mismatched artifacts fail closed at deserialization")) {
    return false;
  }
  WasmLimits limits;
  limits.meterFuel = false;
  limits.fuelPerCall = 0;
  WasmInstance instance(limits);
  std::int32_t result = 0;
  return require(instance.load(bytes) &&
                   instance.call("compatibility", {}, result) && result == 4,
                 "An epoch-only store needs no fuel budget",
                 &instance);
}

static bool
run(const std::string& name, const std::vector<std::byte>& bytes)
{
  if (name == "EngineModes") {
    return engineModes(bytes);
  }
  if (name == "Resources") {
    WasmResourceTable<int, GuestResourceKind::Texture> first(1, 1);
    WasmResourceTable<int, GuestResourceKind::Texture> second(2, 1);
    const GuestResourceId id = first.insert(std::make_shared<const int>(10));
    if (!require(id.owner == 1 && first.resolve(id) && !second.resolve(id),
                 "Resource IDs are owner scoped")) {
      return false;
    }
    GuestResourceId wrongType = id;
    wrongType.kind = GuestResourceKind::Mesh;
    if (first.resolve(wrongType)) {
      return false;
    }
    std::shared_ptr<const int> acceptedFrame = first.resolve(id);
    if (!first.release(id) || first.resolve(id) || first.hasCapacity() ||
        *acceptedFrame != 10) {
      return false;
    }
    acceptedFrame.reset();
    const GuestResourceId replacement =
      first.insert(std::make_shared<const int>(20));
    if (replacement.slot != id.slot ||
        replacement.generation == id.generation || first.resolve(id)) {
      return false;
    }
    acceptedFrame = first.resolve(replacement);
    first.retire();
    return require(
      !first.resolve(replacement) && !first.hasCapacity() &&
        *acceptedFrame == 20,
      "Revocation invalidates authority and preserves accepted-frame leases");
  }
  if (name == "Lifecycle") {
    const std::vector<std::byte> module =
      readGuest(ILLUMO_WASM_LIFECYCLE_GUEST);
    GuestWireWriter startup;
    startup.text("startup");
    const std::uint32_t render =
      static_cast<std::uint32_t>(GuestCapability::Render);
    std::vector<std::byte> response;
    WasmGuest guest;
    if (!require(guest.start(
                   module, GuestRole::Game, render, startup.data(), response),
                 "Start lifecycle guest") ||
        !require(GuestWireReader(response).u32() == 42,
                 "Copied startup reply")) {
      std::fprintf(stderr, "%s\n", guest.error().c_str());
      return false;
    }
    const std::vector<std::byte> preserved = response;
    if (!guest.invoke(GuestCall::Close, {}, response) ||
        GuestWireReader(response).u32() != 0) {
      return false;
    }
    GuestWireWriter update;
    update.u32(0);
    if (!guest.invoke(GuestCall::Update, update.data(), response) ||
        GuestWireReader(response).u32() != 1 ||
        !guest.invoke(GuestCall::Frame, {}, response) ||
        GuestWireReader(response).u32() != 1 ||
        !guest.invoke(GuestCall::Close, {}, response) ||
        GuestWireReader(response).u32() != 1 ||
        GuestWireReader(preserved).u32() != 42) {
      return false;
    }
    WasmGuest other;
    if (!other.start(
          module, GuestRole::Game, render, startup.data(), response) ||
        other.session() == guest.session()) {
      return false;
    }
    update.clear();
    update.u32(1);
    if (!require(!guest.invoke(GuestCall::Update, update.data(), response) &&
                   response.empty() && !guest.isAlive() &&
                   guest.capabilities() == 0 && other.isAlive(),
                 "Trap retires only its guest and discards output")) {
      return false;
    }
    other.shutdown();
    if (!require(!other.isAlive() &&
                   !other.invoke(GuestCall::Frame, {}, response),
                 "Shutdown retires the session")) {
      return false;
    }
    for (const std::uint32_t mode : { 2u, 3u }) {
      WasmGuest corrupt;
      if (!corrupt.start(
            module, GuestRole::Game, render, startup.data(), response)) {
        return false;
      }
      update.clear();
      update.u32(mode);
      if (!require(
            !corrupt.invoke(GuestCall::Update, update.data(), response) &&
              response.empty() && !corrupt.isAlive(),
            "Reject stale sequence or forged session")) {
        return false;
      }
    }
    WasmGuest denied;
    WasmGuest wrongRole;
    return require(
      !denied.start(module, GuestRole::Game, 0, startup.data(), response) &&
        !wrongRole.start(
          module, GuestRole::Mod, render, startup.data(), response),
      "Requirements do not grant authority or change role");
  }
  if (name == "Protocol") {
    GuestWireWriter writer;
    GuestEnvelope{ GuestCall::Update, 12, 34, {} }.write(writer);
    GuestEnvelope decoded;
    if (!GuestEnvelope::read(writer.data(), decoded) || decoded.session != 12 ||
        decoded.sequence != 34) {
      return false;
    }
    for (std::size_t size = 0; size < writer.data().size(); ++size) {
      if (GuestEnvelope::read(std::span(writer.data()).first(size), decoded)) {
        return false;
      }
    }
    std::vector<std::byte> invalid = writer.data();
    invalid.push_back(std::byte{ 0 });
    if (GuestEnvelope::read(invalid, decoded)) {
      return false;
    }
    writer.clear();
    GuestDescriptor{ GuestRole::Game, UINT32_MAX, "test" }.write(writer);
    GuestDescriptor descriptor;
    if (GuestDescriptor::read(writer.data(), descriptor)) {
      return false;
    }
    writer.clear();
    GuestDescriptor{ GuestRole::Game, 0, "../outside" }.write(writer);
    return require(!GuestDescriptor::read(writer.data(), descriptor),
                   "Strict descriptor and envelope grammar");
  }
  if (name == "Worker") {
    WasmWorker worker(bytes);
    GuestWireWriter request;
    request.u32(0);
    request.text("opaque game job");
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    WasmJobResult result;
    if (!require(waitWorker(worker, WasmWorkerStatus::Idle),
                 "Worker initializes") ||
        !require(worker.submit(request.data(), first) &&
                   !worker.submit(request.data(), second),
                 "One outstanding job") ||
        !require(waitWorker(worker, WasmWorkerStatus::Completed) &&
                   worker.poll(result) && result.requestId == first &&
                   result.error.empty(),
                 "Worker publishes copied result")) {
      return false;
    }
    GuestWireReader response(result.bytes);
    if (!require(response.u32() == 1 && response.text() == "opaque game job" &&
                   response.finished(),
                 "Guest evaluates the job")) {
      return false;
    }
    if (!worker.submit(request.data(), second) || second <= first ||
        !waitWorker(worker, WasmWorkerStatus::Completed) ||
        !worker.poll(result)) {
      return false;
    }
    GuestWireReader persistent(result.bytes);
    if (!require(persistent.u32() == 2, "Worker preserves guest state")) {
      return false;
    }
    request.clear();
    request.u32(1);
    if (!worker.submit(request.data(), second) ||
        !waitWorker(worker, WasmWorkerStatus::Failed) || !worker.poll(result)) {
      return false;
    }
    return require(!result.error.empty() && result.requestId == second &&
                     !worker.submit(request.data(), first) &&
                     !worker.poll(result),
                   "Trapped worker is retired and failure delivered once");
  }
  if (name == "Wire") {
    GuestWireWriter packet;
    packet.u32(0x01020304u);
    packet.u64(UINT64_MAX);
    packet.f32(1.25f);
    packet.f64(-123.5);
    packet.text("bytes");
    GuestWireReader good(packet.data());
    if (!require(packet.data()[0] == std::byte{ 4 } &&
                   good.u32() == 0x01020304u && good.u64() == UINT64_MAX &&
                   good.f32() == 1.25f && good.f64() == -123.5 &&
                   good.text() == "bytes" && good.finished(),
                 "Explicit little-endian values")) {
      return false;
    }
    GuestWireReader bad(std::span(packet.data()).first(3));
    bad.u32();
    GuestWireReader oversize(packet.data());
    oversize.text(4);
    bool rejected = false;
    try {
      GuestWireWriter tiny(3);
      tiny.u32(1);
    } catch (const std::length_error&) {
      rejected = true;
    }
    return require(!bad.valid() && bad.bytes(SIZE_MAX).empty() &&
                     !oversize.valid() && rejected,
                   "Truncation, length and quota checks");
  }
  WasmLimits limits;
  if (name == "CompilerLimits") {
    limits.compilerDeadlineMilliseconds = 1;
    WasmInstance compilerLimited(limits);
    return require(!compilerLimited.load(bytes) && !compilerLimited.isAlive() &&
                     compilerLimited.error().find("deadline") !=
                       std::string::npos,
                   "Compiler subprocess deadline",
                   &compilerLimited);
  }
  if (name == "Fuel") {
    limits.meterFuel = true;
  }
  if (name == "Epoch") {
    // Epoch-only: no fuel instrumentation, so the deadline must interrupt.
    limits.meterFuel = false;
    limits.fuelPerCall = 0;
    limits.deadlineMilliseconds = 50;
  }
  WasmInstance instance(limits);
  if (name == "InvalidModule") {
    return require(!instance.load({}) && !instance.isAlive(),
                   "Empty module rejected");
  }
  if (name == "DeniedImports") {
    constexpr char kWat[] =
      "(module (import \"wasi_snapshot_preview1\" \"path_open\""
      " (func (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))"
      " (memory (export \"memory\") 1))";
    wasm_byte_vec_t module{};
    wasmtime_error_t* error =
      wasmtime_wat2wasm(kWat, sizeof(kWat) - 1u, &module);
    if (error != nullptr) {
      wasmtime_error_delete(error);
      return false;
    }
    const bool accepted =
      instance.load(std::as_bytes(std::span(module.data, module.size)));
    wasm_byte_vec_delete(&module);
    return require(!accepted && !instance.isAlive() &&
                     instance.error().find("unknown import") !=
                       std::string::npos,
                   "Unapproved import rejected");
  }
  if (!require(instance.load(bytes), "Guest loads", &instance)) {
    return false;
  }
  std::int32_t result = 0;
  if (name == "Compatibility") {
    const std::array<std::int32_t, 1> size{ 16 };
    if (!require(instance.call("compatibility", {}, result) && result == 4,
                 "C++23 containers, exceptions, SIMD and constructors",
                 &instance) ||
        !require(instance.call("allocationFailure", {}, result) && result == 1,
                 "Guest allocation failure is catchable",
                 &instance) ||
        !require(instance.call("allocate", size, result),
                 "Guest allocation",
                 &instance)) {
      return false;
    }
    const std::uint32_t address = static_cast<std::uint32_t>(result);
    const std::array<std::byte, 3> sent{ std::byte{ 1 },
                                         std::byte{ 2 },
                                         std::byte{ 3 } };
    std::array<std::byte, 3> received{};
    const std::array<std::int32_t, 1> allocation{ result };
    return require(instance.copyToMemory(address, sent) &&
                     instance.copyFromMemory(address, received) &&
                     received == sent &&
                     instance.call("release", allocation, result),
                   "Memory copies and deallocation",
                   &instance);
  }
  if (name == "Isolation") {
    WasmInstance other;
    return require(other.load(bytes) && instance.call("counter", {}, result) &&
                     result == 1 && instance.call("counter", {}, result) &&
                     result == 2 && other.call("counter", {}, result) &&
                     result == 1 && !instance.call("crash", {}, result) &&
                     !instance.isAlive() &&
                     !instance.call("counter", {}, result) &&
                     other.call("counter", {}, result) && result == 2,
                   "Independent state and trap retirement",
                   &instance);
  }
  if (name == "Fuel" || name == "Epoch") {
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    const bool interrupted = !instance.call("spin", {}, result);
    const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
    const WasmFailure expected = name == "Fuel" ? WasmFailure::FuelExhausted
                                                : WasmFailure::DeadlineExceeded;
    return require(interrupted && !instance.isAlive() && seconds < 5.0 &&
                     instance.failure() == expected,
                   "Infinite loop interrupted",
                   &instance);
  }
  if (name == "Memory") {
    const std::array<std::int32_t, 1> pages{ 1024 };
    std::array<std::byte, 1> output{};
    return require(instance.call("grow", pages, result) && result == -1 &&
                     !instance.copyFromMemory(UINT32_MAX, output) &&
                     !instance.isAlive(),
                   "Memory growth limit and overflow-safe bounds",
                   &instance);
  }
  return require(false, "Unknown test");
}

int
main(int argc, char** argv)
{
  constexpr std::array<const char*, 15> kCases{
    "Compatibility", "Isolation",      "Fuel",          "Epoch",
    "Memory",        "DeniedImports",  "InvalidModule", "Worker",
    "Wire",          "CompilerLimits", "Lifecycle",     "Protocol",
    "Resources",     "EngineModes"
  };
  if (argc == 2 && std::string(argv[1]) == "--list") {
    for (const char* name : kCases) {
      std::printf("Illumo.Wasm.%s\n", name);
    }
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  const std::string fullName(argv[2]);
  if (!fullName.starts_with("Illumo.Wasm.")) {
    return 2;
  }
  const bool passed = run(fullName.substr(12), readGuest());
  std::printf("%s: %s\n", fullName.c_str(), passed ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
