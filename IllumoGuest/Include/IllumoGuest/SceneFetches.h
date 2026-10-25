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
// reported. Completed sets stay held (not evictable) until released: every
// fetch returns a ticket that release(ticket) frees (an editor with several
// open documents keeps one per document), and release() frees them all.
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

  // Returns the ticket that holds the fetched entries (never 0).
  std::uint64_t fetch(const std::vector<std::string>& paths, Callback done);
  // Lets the ticket's completed entries be evicted again. A ticket whose
  // fetch is still in flight keeps its entries until release().
  void release(std::uint64_t ticket);
  // Lets every completed set's entries be evicted again.
  void release();
  // Advances fetches and runs completions; call once per update.
  void pump();
  bool idle() const { return m_fetches.empty(); }

private:
  struct Fetch
  {
    std::uint64_t set = 0;
    std::uint64_t ticket = 0;
    std::vector<std::string> paths;
    // The second stage: an OBJ's material libraries.
    bool materials = false;
    std::vector<std::string> missing;
    Callback done;
  };

  struct Held
  {
    std::uint64_t ticket = 0;
    std::uint64_t set = 0;
  };

  GuestVfsAssets& m_cache;
  std::vector<Fetch> m_fetches;
  std::vector<Held> m_held;
  std::uint64_t m_nextTicket = 1;
};
