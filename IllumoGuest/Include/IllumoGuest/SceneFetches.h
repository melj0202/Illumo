#pragma once

#include <IllumoGuest/VfsAssets.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The guest half of "collect references, fetch, then instantiate": fetches
// the virtual paths a scene needs into the asset cache, then the material
// libraries its OBJ files declare, so AssetManager later reads every byte
// synchronously. Material libraries are optional: a missing one is not
// reported. Completed sets stay held (not evictable) until release().
//
// Part of IllumoGuestContent because it reads OBJ material names through
// Illumo::Content's SceneAssetRefs.
class GuestSceneFetches
{
public:
  // missing: canonical paths that could not be read.
  using Callback = std::function<void(std::vector<std::string> missing)>;

  explicit GuestSceneFetches(GuestVfsAssets& cache);
  ~GuestSceneFetches();
  GuestSceneFetches(const GuestSceneFetches&) = delete;
  GuestSceneFetches& operator=(const GuestSceneFetches&) = delete;
  GuestSceneFetches(GuestSceneFetches&&) = delete;
  GuestSceneFetches& operator=(GuestSceneFetches&&) = delete;

  void fetch(const std::vector<std::string>& paths, Callback done);
  // Lets every completed set's entries be evicted again.
  void release();
  // Advances fetches and runs completions; call once per update.
  void pump();
  bool idle() const { return m_fetches.empty(); }

private:
  struct Fetch
  {
    std::uint64_t set = 0;
    std::vector<std::string> paths;
    // The second stage: an OBJ's material libraries.
    bool materials = false;
    std::vector<std::string> missing;
    Callback done;
  };

  GuestVfsAssets& m_cache;
  std::vector<Fetch> m_fetches;
  std::vector<std::uint64_t> m_held;
};
