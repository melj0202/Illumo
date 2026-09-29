#ifndef ILLUMO_WASM_PROFILE_H
#define ILLUMO_WASM_PROFILE_H

#include <cstddef>
#include <cstdint>
#include <vector>

struct wasmtime_linker;
struct wasmtime_error;

// Per-instance state behind the `illumo_profile` guest imports
// (docs/tracy-profiling.md). Guests register each marker site once and then
// refer to it by index; the host turns those calls into Tracy events on the
// thread running the guest. Without TRACY_ENABLE the imports still link, but
// registration returns -1 so guests make no further profiling calls.
//
// Owned by one WasmInstance and used only on its owner thread. Imports never
// reenter a guest, so zones cannot interleave between instances on a thread.
class WasmProfileState
{
public:
  WasmProfileState() = default;
  ~WasmProfileState();
  WasmProfileState(const WasmProfileState&) = delete;
  WasmProfileState& operator=(const WasmProfileState&) = delete;
  WasmProfileState(WasmProfileState&&) = delete;
  WasmProfileState& operator=(WasmProfileState&&) = delete;

  // Ends zones a guest call left open (a trap, a deadline, or unbalanced
  // markers), so the thread's Tracy zone stack stays balanced.
  void closeOpenZones();

  // Sites this instance registered; bounded by kWasmProfileSiteLimit.
  std::size_t siteCount() const { return m_sites.size(); }
  std::size_t openZoneCount() const { return m_open.size(); }

  // Import bodies, public for the linker callbacks and tests.
  std::int32_t registerSite(std::int32_t kind,
                            const std::uint8_t* memory,
                            std::size_t memorySize,
                            const std::int32_t* ranges,
                            std::int32_t line);
  void beginZone(std::int32_t site);
  void endZone();
  void plot(std::int32_t site, double value);
  void frameMark(std::int32_t site);

private:
  struct Site
  {
    std::int32_t kind = 0;
    // A ___tracy_source_location_data for zones, the interned name otherwise.
    // Both live for the rest of the process, as Tracy requires.
    const void* data = nullptr;
  };
  struct OpenZone
  {
    std::uint32_t id = 0;
    std::int32_t active = 0;
  };

  std::vector<Site> m_sites;
  std::vector<OpenZone> m_open;
  // Begins past kWasmProfileDepthLimit record nothing but must still pair
  // with their ends.
  std::uint32_t m_overflow = 0;
};

inline constexpr std::size_t kWasmProfileSiteLimit = 4096u;
inline constexpr std::size_t kWasmProfileDepthLimit = 256u;
inline constexpr std::size_t kWasmProfileTextLimit = 512u;

// Defines `illumo_profile.register`, `zone_begin`, `zone_end`, `plot` and
// `frame_mark` on the linker, bound to `state`.
wasmtime_error*
defineWasmProfileImports(wasmtime_linker* linker, WasmProfileState* state);

#endif
