#include "WasmProfile.h"

#include <wasmtime.h>

#include <array>
#include <cstring>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#endif

// Site kinds; they match ILLUMO_PROFILE_SITE_* in
// <Illumo/Foundation/Profile.h>.
static constexpr std::int32_t kSiteZone = 0;
static constexpr std::int32_t kSitePlot = 1;
static constexpr std::int32_t kSiteFrameMark = 2;

#ifdef TRACY_ENABLE

// Tracy keeps source-location and name pointers for the rest of the process,
// so every instance's sites are interned here and never released. Identical
// sites from reloaded guests or parallel workers share one entry; the totals
// bound what guests can make the host retain.
class WasmProfileRegistry
{
public:
  static constexpr std::size_t kEntryLimit = 16384u;
  static constexpr std::size_t kTextByteLimit = 4u * 1024u * 1024u;

  const void* intern(std::int32_t kind,
                     std::string_view name,
                     std::string_view function,
                     std::string_view file,
                     std::uint32_t line)
  {
    std::string key;
    key.reserve(name.size() + function.size() + file.size() + 24u);
    key.append(std::to_string(kind))
      .append(1, '\n')
      .append(name)
      .append(1, '\n')
      .append(function)
      .append(1, '\n')
      .append(file)
      .append(1, '\n')
      .append(std::to_string(line));
    const std::lock_guard<std::mutex> lock(m_mutex);
    const std::unordered_map<std::string, const void*>::const_iterator found =
      m_entries.find(key);
    if (found != m_entries.end()) {
      return found->second;
    }
    const std::size_t bytes = name.size() + function.size() + file.size();
    if (m_entries.size() >= kEntryLimit || bytes > kTextByteLimit - m_bytes) {
      return nullptr;
    }
    m_bytes += bytes;
    const char* storedName = store(name);
    const void* data = storedName;
    if (kind == kSiteZone) {
      ___tracy_source_location_data& location = m_locations.emplace_back();
      location.name = storedName;
      location.function = store(function);
      location.file = store(file);
      location.line = line;
      // Guest zones share one colour so they stand apart from host zones.
      location.color = kGuestZoneColor;
      data = &location;
    }
    m_entries.emplace(std::move(key), data);
    return data;
  }

private:
  static constexpr std::uint32_t kGuestZoneColor = 0x3FA37Au;

  const char* store(std::string_view text)
  {
    return m_text.emplace_back(text).c_str();
  }

  std::mutex m_mutex;
  std::unordered_map<std::string, const void*> m_entries;
  // Deques keep element addresses stable as they grow.
  std::deque<std::string> m_text;
  std::deque<___tracy_source_location_data> m_locations;
  std::size_t m_bytes = 0;
};

static WasmProfileRegistry&
profileRegistry()
{
  // Deliberately leaked: Tracy may read these pointers during its own static
  // shutdown, after function-local statics would be destroyed.
  static WasmProfileRegistry* registry = new WasmProfileRegistry();
  return *registry;
}

// Reads one bounded guest string; false for an out-of-range or oversized span.
static bool
guestText(const std::uint8_t* memory,
          std::size_t memorySize,
          std::int32_t offset,
          std::int32_t length,
          std::string_view& text)
{
  if (offset < 0 || length < 0 ||
      static_cast<std::size_t>(length) > kWasmProfileTextLimit) {
    return false;
  }
  const std::size_t start = static_cast<std::size_t>(offset);
  const std::size_t size = static_cast<std::size_t>(length);
  if (start > memorySize || size > memorySize - start) {
    return false;
  }
  text = std::string_view(reinterpret_cast<const char*>(memory) + start, size);
  return true;
}

#endif

WasmProfileState::~WasmProfileState()
{
  closeOpenZones();
}

void
WasmProfileState::closeOpenZones()
{
#ifdef TRACY_ENABLE
  while (!m_open.empty()) {
    const OpenZone open = m_open.back();
    m_open.pop_back();
    ___tracy_emit_zone_end(TracyCZoneCtx{ open.id, open.active });
  }
#endif
  m_open.clear();
  m_overflow = 0;
}

std::int32_t
WasmProfileState::registerSite(std::int32_t kind,
                               const std::uint8_t* memory,
                               std::size_t memorySize,
                               const std::int32_t* ranges,
                               std::int32_t line)
{
#ifdef TRACY_ENABLE
  if ((kind != kSiteZone && kind != kSitePlot && kind != kSiteFrameMark) ||
      m_sites.size() >= kWasmProfileSiteLimit || memory == nullptr ||
      line < 0) {
    return -1;
  }
  std::string_view name;
  std::string_view function;
  std::string_view file;
  if (!guestText(memory, memorySize, ranges[0], ranges[1], name) ||
      !guestText(memory, memorySize, ranges[2], ranges[3], function) ||
      !guestText(memory, memorySize, ranges[4], ranges[5], file) ||
      name.empty()) {
    return -1;
  }
  const void* data = profileRegistry().intern(
    kind, name, function, file, static_cast<std::uint32_t>(line));
  if (data == nullptr) {
    return -1;
  }
  m_sites.push_back(Site{ kind, data });
  return static_cast<std::int32_t>(m_sites.size() - 1u);
#else
  (void)kind;
  (void)memory;
  (void)memorySize;
  (void)ranges;
  (void)line;
  return -1;
#endif
}

void
WasmProfileState::beginZone(std::int32_t site)
{
#ifdef TRACY_ENABLE
  if (site < 0 || static_cast<std::size_t>(site) >= m_sites.size() ||
      m_sites[static_cast<std::size_t>(site)].kind != kSiteZone) {
    return;
  }
  if (m_open.size() >= kWasmProfileDepthLimit) {
    ++m_overflow;
    return;
  }
  const TracyCZoneCtx context =
    ___tracy_emit_zone_begin(static_cast<const ___tracy_source_location_data*>(
                               m_sites[static_cast<std::size_t>(site)].data),
                             1);
  m_open.push_back(OpenZone{ context.id, context.active });
#else
  (void)site;
#endif
}

void
WasmProfileState::endZone()
{
#ifdef TRACY_ENABLE
  if (m_overflow > 0u) {
    --m_overflow;
    return;
  }
  if (m_open.empty()) {
    return;
  }
  const OpenZone open = m_open.back();
  m_open.pop_back();
  ___tracy_emit_zone_end(TracyCZoneCtx{ open.id, open.active });
#endif
}

void
WasmProfileState::plot(std::int32_t site, double value)
{
#ifdef TRACY_ENABLE
  if (site >= 0 && static_cast<std::size_t>(site) < m_sites.size() &&
      m_sites[static_cast<std::size_t>(site)].kind == kSitePlot) {
    ___tracy_emit_plot(
      static_cast<const char*>(m_sites[static_cast<std::size_t>(site)].data),
      value);
  }
#else
  (void)site;
  (void)value;
#endif
}

void
WasmProfileState::frameMark(std::int32_t site)
{
#ifdef TRACY_ENABLE
  if (site >= 0 && static_cast<std::size_t>(site) < m_sites.size() &&
      m_sites[static_cast<std::size_t>(site)].kind == kSiteFrameMark) {
    ___tracy_emit_frame_mark(
      static_cast<const char*>(m_sites[static_cast<std::size_t>(site)].data));
  }
#else
  (void)site;
#endif
}

static wasm_trap_t*
profileRegister(void* environment,
                wasmtime_caller_t* caller,
                wasmtime_val_raw_t* values,
                std::size_t)
{
  WasmProfileState& state = *static_cast<WasmProfileState*>(environment);
  const std::array<std::int32_t, 6> ranges{
    values[1].i32, values[2].i32, values[3].i32,
    values[4].i32, values[5].i32, values[6].i32,
  };
  const std::uint8_t* memory = nullptr;
  std::size_t memorySize = 0;
  wasmtime_extern_t exported{};
  const bool hasMemory =
    wasmtime_caller_export_get(caller, "memory", 6, &exported);
  if (hasMemory && exported.kind == WASMTIME_EXTERN_MEMORY) {
    wasmtime_context_t* context = wasmtime_caller_context(caller);
    memory = wasmtime_memory_data(context, &exported.of.memory);
    memorySize = wasmtime_memory_data_size(context, &exported.of.memory);
  }
  if (hasMemory) {
    wasmtime_extern_delete(&exported);
  }
  // Guest memory stays valid here: nothing below can grow it.
  values[0].i32 = state.registerSite(
    values[0].i32, memory, memorySize, ranges.data(), values[7].i32);
  return nullptr;
}

static wasm_trap_t*
profileZoneBegin(void* environment,
                 wasmtime_caller_t*,
                 wasmtime_val_raw_t* values,
                 std::size_t)
{
  static_cast<WasmProfileState*>(environment)->beginZone(values[0].i32);
  return nullptr;
}

static wasm_trap_t*
profileZoneEnd(void* environment,
               wasmtime_caller_t*,
               wasmtime_val_raw_t*,
               std::size_t)
{
  static_cast<WasmProfileState*>(environment)->endZone();
  return nullptr;
}

static wasm_trap_t*
profilePlot(void* environment,
            wasmtime_caller_t*,
            wasmtime_val_raw_t* values,
            std::size_t)
{
  static_cast<WasmProfileState*>(environment)
    ->plot(values[0].i32, values[1].f64);
  return nullptr;
}

static wasm_trap_t*
profileFrameMark(void* environment,
                 wasmtime_caller_t*,
                 wasmtime_val_raw_t* values,
                 std::size_t)
{
  static_cast<WasmProfileState*>(environment)->frameMark(values[0].i32);
  return nullptr;
}

static wasmtime_error_t*
defineProfileFunction(wasmtime_linker_t* linker,
                      std::string_view name,
                      std::span<const wasm_valkind_t> parameters,
                      bool returnsI32,
                      wasmtime_func_unchecked_callback_t callback,
                      WasmProfileState* state)
{
  wasm_valtype_vec_t parameterTypes{};
  wasm_valtype_vec_new_uninitialized(&parameterTypes, parameters.size());
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    parameterTypes.data[index] = wasm_valtype_new(parameters[index]);
  }
  wasm_valtype_vec_t resultTypes{};
  wasm_valtype_vec_new_uninitialized(&resultTypes, returnsI32 ? 1u : 0u);
  if (returnsI32) {
    resultTypes.data[0] = wasm_valtype_new_i32();
  }
  wasm_functype_t* type = wasm_functype_new(&parameterTypes, &resultTypes);
  constexpr std::string_view kModule = "illumo_profile";
  wasmtime_error_t* error =
    wasmtime_linker_define_func_unchecked(linker,
                                          kModule.data(),
                                          kModule.size(),
                                          name.data(),
                                          name.size(),
                                          type,
                                          callback,
                                          state,
                                          nullptr);
  wasm_functype_delete(type);
  return error;
}

wasmtime_error_t*
defineWasmProfileImports(wasmtime_linker_t* linker, WasmProfileState* state)
{
  constexpr std::array<wasm_valkind_t, 8> kRegister{
    WASM_I32, WASM_I32, WASM_I32, WASM_I32,
    WASM_I32, WASM_I32, WASM_I32, WASM_I32,
  };
  constexpr std::array<wasm_valkind_t, 1> kSite{ WASM_I32 };
  constexpr std::array<wasm_valkind_t, 2> kPlot{ WASM_I32, WASM_F64 };
  wasmtime_error_t* error = defineProfileFunction(
    linker, "register", kRegister, true, profileRegister, state);
  if (error == nullptr) {
    error = defineProfileFunction(
      linker, "zone_begin", kSite, false, profileZoneBegin, state);
  }
  if (error == nullptr) {
    error = defineProfileFunction(
      linker, "zone_end", {}, false, profileZoneEnd, state);
  }
  if (error == nullptr) {
    error =
      defineProfileFunction(linker, "plot", kPlot, false, profilePlot, state);
  }
  if (error == nullptr) {
    error = defineProfileFunction(
      linker, "frame_mark", kSite, false, profileFrameMark, state);
  }
  return error;
}
