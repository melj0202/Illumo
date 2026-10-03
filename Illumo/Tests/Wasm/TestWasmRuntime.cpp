#include <Illumo/Wasm/WasmGuest.h>
#include <Illumo/Wasm/WasmInstance.h>
#include <Illumo/Wasm/WasmModuleCache.h>
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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
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

// Loads bytes into a fresh instance that shares cache, and checks it runs.
static bool
loadsThrough(const std::vector<std::byte>& bytes,
             const std::shared_ptr<WasmModuleCache>& cache,
             bool meterFuel = true,
             std::uint32_t compilerDeadline = 30000u)
{
  WasmLimits limits;
  limits.meterFuel = meterFuel;
  limits.compilerDeadlineMilliseconds = compilerDeadline;
  limits.compiledCode = cache;
  WasmInstance instance(limits);
  std::int32_t abi = 0;
  return require(instance.load(bytes) &&
                   instance.call("illumo_guest_describe", {}, abi),
                 "Module loads through the compiled-code cache",
                 &instance);
}

static std::size_t
cacheFileCount(const std::filesystem::path& directory)
{
  std::size_t count = 0u;
  std::error_code error;
  for (std::filesystem::directory_iterator it(directory, error);
       !error && it != std::filesystem::directory_iterator();
       it.increment(error)) {
    count += it->path().extension() == ".cwasm" ? 1u : 0u;
  }
  return count;
}

static bool
moduleCache(const std::vector<std::byte>& bytes)
{
  const std::filesystem::path directory =
    std::filesystem::temp_directory_path() /
    ("illumo-module-cache-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleared;
  std::filesystem::remove_all(directory, cleared);

  // Lanes: concurrent loads of one module compile it once.
  std::shared_ptr<WasmModuleCache> first =
    std::make_shared<WasmModuleCache>(directory);
  std::array<bool, 4> loaded{};
  {
    std::array<std::jthread, 4> lanes;
    for (std::size_t lane = 0u; lane < lanes.size(); ++lane) {
      lanes[lane] = std::jthread([&bytes, &first, &loaded, lane]() {
        loaded[lane] = loadsThrough(bytes, first);
      });
    }
  }
  const WasmModuleCache::Counters shared = first->counters();
  if (!require(loaded[0] && loaded[1] && loaded[2] && loaded[3] &&
                 shared.compiles == 1u && shared.memoryHits == 3u &&
                 shared.diskWrites == 1u && cacheFileCount(directory) == 1u,
               "Concurrent loads share one compile and one stored entry")) {
    return false;
  }

  // A later launch reads the stored artifact, skipping the compiler (and its
  // limits).
  std::shared_ptr<WasmModuleCache> relaunch =
    std::make_shared<WasmModuleCache>(directory);
  if (!require(loadsThrough(bytes, relaunch, true, 1u) &&
                 relaunch->counters().diskHits == 1u &&
                 relaunch->counters().compiles == 0u,
               "A later launch loads the stored artifact without compiling")) {
    return false;
  }

  // Other engine options are another entry.
  if (!require(loadsThrough(bytes, relaunch, false) &&
                 relaunch->counters().compiles == 1u &&
                 cacheFileCount(directory) == 2u,
               "Engine options key separate artifacts")) {
    return false;
  }

  // A damaged entry is a miss and is rewritten.
  std::filesystem::path entry;
  for (std::filesystem::directory_iterator it(directory, cleared);
       !cleared && it != std::filesystem::directory_iterator();
       it.increment(cleared)) {
    if (it->path().filename().string().ends_with(
          "-" + std::to_string(wasmHostEngineOptions(true)) + ".cwasm")) {
      entry = it->path();
    }
  }
  {
    // Invert one byte in the middle of the stored artifact.
    const std::streamoff middle = static_cast<std::streamoff>(
      std::filesystem::file_size(entry, cleared) / 2u);
    std::fstream damage(entry, std::ios::binary | std::ios::in | std::ios::out);
    damage.seekg(middle);
    const int original = damage.get();
    damage.seekp(middle);
    damage.put(static_cast<char>(original ^ 0xff));
  }
  std::shared_ptr<WasmModuleCache> damaged =
    std::make_shared<WasmModuleCache>(directory);
  std::shared_ptr<WasmModuleCache> repaired =
    std::make_shared<WasmModuleCache>(directory);
  if (!require(
        loadsThrough(bytes, damaged) && damaged->counters().diskRejects == 1u &&
          damaged->counters().compiles == 1u && loadsThrough(bytes, repaired) &&
          repaired->counters().diskHits == 1u,
        "A damaged entry recompiles and is rewritten")) {
    return false;
  }

  // An intact entry the engine refuses is discarded and rebuilt once.
  std::shared_ptr<WasmModuleCache> poisoner =
    std::make_shared<WasmModuleCache>(directory);
  WasmModuleCache::Artifact poison;
  WasmModuleCache::Source source = WasmModuleCache::Source::Compiled;
  std::string error;
  poisoner->discard(bytes, wasmHostEngineOptions(true));
  const bool poisoned = poisoner->acquire(
    bytes,
    wasmHostEngineOptions(true),
    [](std::vector<std::byte>& artifact, std::string&) {
      artifact.assign(4096u, std::byte{ 0x42 });
      return true;
    },
    poison,
    source,
    error);
  std::shared_ptr<WasmModuleCache> refused =
    std::make_shared<WasmModuleCache>(directory);
  if (!require(poisoned && loadsThrough(bytes, refused) &&
                 refused->counters().diskHits == 1u &&
                 refused->counters().compiles == 1u,
               "A stored artifact the engine refuses is rebuilt")) {
    return false;
  }

  // A failed compile is reported to every waiter and not remembered.
  std::shared_ptr<WasmModuleCache> memoryOnly =
    std::make_shared<WasmModuleCache>();
  WasmModuleCache::Artifact artifact;
  const bool failed = memoryOnly->acquire(
    bytes,
    7u,
    [](std::vector<std::byte>&, std::string& reason) {
      reason = "refused";
      return false;
    },
    artifact,
    source,
    error);
  if (!require(!failed && error == "refused" &&
                 memoryOnly->acquire(
                   bytes,
                   7u,
                   [](std::vector<std::byte>& output, std::string&) {
                     output.assign(16u, std::byte{ 1 });
                     return true;
                   },
                   artifact,
                   source,
                   error) &&
                 source == WasmModuleCache::Source::Compiled &&
                 memoryOnly->directory().empty(),
               "Compile failures are not remembered")) {
    return false;
  }

  // The directory keeps only the newest entries.
  std::shared_ptr<WasmModuleCache> pruned =
    std::make_shared<WasmModuleCache>(directory, 1u);
  const bool prunedLoad = loadsThrough(bytes, pruned, true);
  pruned->discard(bytes, wasmHostEngineOptions(true));
  const bool rewritten = loadsThrough(bytes, pruned, true);
  const std::size_t remaining = cacheFileCount(directory);
  std::filesystem::remove_all(directory, cleared);
  return require(prunedLoad && rewritten && remaining == 1u,
                 "Writing an entry prunes the directory to its limit");
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

// The illumo_profile imports link for every guest, validate guest ranges and
// site ids, bound registration, and survive unbalanced and trapping zones.
// Registration succeeds only in hosts built with Tracy.
static bool
profileImports(WasmInstance& instance)
{
  constexpr char kWat[] =
    "(module"
    " (import \"illumo_profile\" \"register\" (func $register"
    "  (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))"
    " (import \"illumo_profile\" \"zone_begin\" (func $begin (param i32)))"
    " (import \"illumo_profile\" \"zone_end\" (func $end))"
    " (import \"illumo_profile\" \"plot\" (func $plot (param i32 f64)))"
    " (import \"illumo_profile\" \"frame_mark\" (func $mark (param i32)))"
    " (memory (export \"memory\") 1)"
    " (data (i32.const 16) \"Test.zone\")"
    " (data (i32.const 32) \"run\")"
    " (data (i32.const 48) \"test.cpp\")"
    " (func (export \"registerKind\") (param i32) (result i32)"
    "  (call $register (local.get 0) (i32.const 16) (i32.const 9)"
    "   (i32.const 32) (i32.const 3) (i32.const 48) (i32.const 8)"
    "   (i32.const 7)))"
    " (func (export \"registerOutside\") (result i32)"
    "  (call $register (i32.const 0) (i32.const 65530) (i32.const 9)"
    "   (i32.const 32) (i32.const 3) (i32.const 48) (i32.const 8)"
    "   (i32.const 7)))"
    " (func (export \"registerNegative\") (result i32)"
    "  (call $register (i32.const 0) (i32.const 16) (i32.const -1)"
    "   (i32.const 32) (i32.const 3) (i32.const 48) (i32.const 8)"
    "   (i32.const 7)))"
    " (func (export \"fill\") (result i32) (local $i i32) (local $r i32)"
    "  (loop $again"
    "   (local.set $r (call $register (i32.const 0) (i32.const 16)"
    "    (i32.const 9) (i32.const 32) (i32.const 3) (i32.const 48)"
    "    (i32.const 8) (local.get $i)))"
    "   (local.set $i (i32.add (local.get $i) (i32.const 1)))"
    "   (br_if $again (i32.lt_u (local.get $i) (i32.const 5000))))"
    "  (local.get $r))"
    " (func (export \"unbalanced\") (param i32) (result i32) (local $i i32)"
    "  (call $begin (local.get 0)) (call $end) (call $end)"
    "  (loop $deeper"
    "   (call $begin (local.get 0))"
    "   (local.set $i (i32.add (local.get $i) (i32.const 1)))"
    "   (br_if $deeper (i32.lt_u (local.get $i) (i32.const 300))))"
    "  (call $end)"
    "  (call $begin (i32.const 4095)) (call $begin (i32.const -3))"
    "  (call $plot (local.get 0) (f64.const 1.5))"
    "  (call $plot (i32.const 99) (f64.const 2))"
    "  (call $mark (local.get 0)) (call $mark (i32.const -5))"
    "  (i32.const 1))"
    " (func (export \"trapInZone\") (param i32) (result i32)"
    "  (call $begin (local.get 0)) unreachable))";
  wasm_byte_vec_t module{};
  wasmtime_error_t* error = wasmtime_wat2wasm(kWat, sizeof(kWat) - 1u, &module);
  if (error != nullptr) {
    wasmtime_error_delete(error);
    return require(false, "Profile test module assembles");
  }
  const bool loaded =
    instance.load(std::as_bytes(std::span(module.data, module.size)));
  wasm_byte_vec_delete(&module);
  if (!require(loaded, "Profile imports link", &instance)) {
    return false;
  }
#ifdef TRACY_ENABLE
  const bool recording = true;
#else
  const bool recording = false;
#endif
  std::int32_t zone = 0;
  std::int32_t plot = 0;
  std::int32_t result = 0;
  const std::array<std::int32_t, 1> zoneKind{ 0 };
  const std::array<std::int32_t, 1> plotKind{ 1 };
  const std::array<std::int32_t, 1> unknownKind{ 7 };
  if (!require(
        instance.call("registerKind", zoneKind, zone) &&
          instance.call("registerKind", plotKind, plot) &&
          (recording ? zone >= 0 && plot > zone : zone == -1 && plot == -1),
        "Sites register only in Tracy hosts",
        &instance) ||
      !require(instance.call("registerKind", unknownKind, result) &&
                 result == -1 && instance.call("registerOutside", {}, result) &&
                 result == -1 &&
                 instance.call("registerNegative", {}, result) && result == -1,
               "Unknown kinds and out-of-range text are refused",
               &instance)) {
    return false;
  }
  // Unbalanced ends, depth overflow, unknown ids and a wrong-kind plot are
  // ignored; zones left open are closed when the call returns.
  const std::array<std::int32_t, 1> site{ zone };
  if (!require(instance.call("unbalanced", site, result) && result == 1 &&
                 instance.call("unbalanced", site, result) && result == 1,
               "Unbalanced markers are contained",
               &instance) ||
      !require(instance.call("fill", {}, result) && result == -1,
               "Registration is bounded per instance",
               &instance)) {
    return false;
  }
  return require(!instance.call("trapInZone", site, result) &&
                   !instance.isAlive() &&
                   instance.failure() == WasmFailure::Trap,
                 "A trap inside a zone retires the guest",
                 &instance);
}

static bool
run(const std::string& name, const std::vector<std::byte>& bytes)
{
  if (name == "EngineModes") {
    return engineModes(bytes);
  }
  if (name == "ModuleCache") {
    return moduleCache(bytes);
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
    GuestWireWriter tiny(3);
    tiny.u32(1);
    const bool rejected = tiny.failed() && tiny.data().empty();
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
  if (name == "ProfileImports") {
    return profileImports(instance);
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
                 "C++23 containers, destructors, SIMD and constructors",
                 &instance) ||
        !require(instance.call("allocationFailure", {}, result) && result == 1,
                 "Guest nothrow allocation reports failure",
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
  constexpr std::array<const char*, 16> kCases{
    "Compatibility", "Isolation",      "Fuel",           "Epoch",
    "Memory",        "DeniedImports",  "InvalidModule",  "Worker",
    "Wire",          "CompilerLimits", "Lifecycle",      "Protocol",
    "Resources",     "EngineModes",    "ProfileImports", "ModuleCache"
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
