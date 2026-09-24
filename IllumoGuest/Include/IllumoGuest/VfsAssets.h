#pragma once

#include <Illumo/Rendering/AssetSource.h>
#include <IllumoGuest/Files.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

// Guest asset bytes from the host's virtual file tree, served synchronously
// to AssetManager (IAssetSource::read stays synchronous because mesh loads
// are synchronous and guest texture jobs run inline).
//
// Canonical names are absolute virtual paths: relative names join the base
// directory ("/app"), so product paths such as "Assets/IllEd/x.jpg" become
// "/app/Assets/IllEd/x.jpg". Bytes arrive three ways:
//  - preload: pinned bootstrap files, never evicted;
//  - fetch sets: a load collects its references, fetches them, then
//    instantiates; a set holds its entries until released;
//  - putLocal: bytes the app supplies itself under /local/<name> (for example
//    an .obj opened through a dialog), pinned until removed.
// Unpinned, unheld entries are evicted least recently used first once the
// cache passes its byte budget.
class GuestVfsAssets final : public IAssetSource
{
public:
  static constexpr std::size_t kDefaultBudget = 256u * 1024u * 1024u;
  static constexpr std::size_t kMaximumAssetBytes = 256u * 1024u * 1024u;

  explicit GuestVfsAssets(GuestFiles& files, std::string base = "/app");

  void setBudget(std::size_t bytes);
  std::size_t budget() const { return m_budget; }
  std::size_t cachedBytes() const { return m_bytes; }
  bool contains(const std::string& path) const;

  // Pinned bootstrap files. Missing ones are logged once and then behave as
  // absent assets.
  void preload(const std::vector<std::string>& names);
  // Pumps; true once every preload has completed.
  bool ready();

  // Starts loading every path that is not cached yet. Returns the set id.
  std::uint64_t fetch(const std::vector<std::string>& paths);
  // Pumps; true once the set completed. missing receives the canonical
  // paths that could not be read.
  bool fetched(std::uint64_t set, std::vector<std::string>* missing = nullptr);
  // Lets the set's entries be evicted again.
  void release(std::uint64_t set);

  // App-supplied bytes at /local/<name>; path receives the canonical name.
  bool putLocal(const std::string& name,
                std::vector<unsigned char> bytes,
                std::string* path = nullptr);
  void removeLocal(const std::string& name);

  // Advances transfers; called by ready() and fetched() too.
  void pump();

  std::string canonical(const std::string& path) const override;
  bool read(const std::string& canonical,
            std::vector<unsigned char>& bytes) const override;
  std::int64_t stamp(const std::string& canonical) const override;

private:
  struct Entry
  {
    std::vector<unsigned char> bytes;
    bool pinned = false;
    std::uint32_t holds = 0;
    // Recency is bookkeeping, not asset state, so const reads update it.
    mutable std::uint64_t used = 0;
  };
  struct Load
  {
    std::string path;
    bool pinned = false;
    std::vector<std::uint64_t> sets;
  };
  struct Set
  {
    std::vector<std::string> paths;
    std::vector<std::string> missing;
    std::size_t pending = 0;
    bool held = false;
  };

  GuestFiles& m_files;
  std::string m_base;
  std::size_t m_budget = kDefaultBudget;
  std::size_t m_bytes = 0;
  // Canonical path to entry.
  std::map<std::string, Entry> m_entries;
  // Loads waiting for a free file task, then in flight by task id.
  std::deque<Load> m_waiting;
  std::map<std::uint64_t, Load> m_loads;
  std::map<std::uint64_t, Set> m_sets;
  std::uint64_t m_nextSet = 1;
  std::size_t m_preloads = 0;
  mutable std::uint64_t m_clock = 0;

  // Joins a load onto one already waiting or in flight for the same path.
  bool join(const std::string& path, bool pinned, std::uint64_t set);
  void finish(Load& load, GuestFileResult& result);
  void evict();
};
