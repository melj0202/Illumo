#include <Illumo/Services/Logger.h>
#include <IllumoGuest/VfsAssets.h>
#include <algorithm>

GuestVfsAssets::GuestVfsAssets(GuestFiles& files, std::string base)
  : m_files(files)
  , m_base(std::move(base))
{
}

void
GuestVfsAssets::setBudget(std::size_t bytes)
{
  m_budget = bytes;
  evict();
}

std::string
GuestVfsAssets::canonical(const std::string& path) const
{
  std::string name = path;
  std::replace(name.begin(), name.end(), '\\', '/');
  const std::string joined =
    !name.empty() && name.front() == '/' ? name : m_base + "/" + name;
  // Collapse empty and "." components; ".." never resolves.
  std::string result;
  std::size_t start = 0;
  while (start <= joined.size()) {
    std::size_t end = joined.find('/', start);
    if (end == std::string::npos) {
      end = joined.size();
    }
    const std::string component = joined.substr(start, end - start);
    start = end + 1;
    if (component.empty() || component == ".") {
      continue;
    }
    if (component == "..") {
      return std::string();
    }
    for (char character : component) {
      if (static_cast<unsigned char>(character) < 0x20) {
        return std::string();
      }
    }
    result += "/" + component;
  }
  return result.empty() ? std::string("/") : result;
}

bool
GuestVfsAssets::contains(const std::string& path) const
{
  return m_entries.find(canonical(path)) != m_entries.end();
}

bool
GuestVfsAssets::join(const std::string& path, bool pinned, std::uint64_t set)
{
  for (Load& load : m_waiting) {
    if (load.path == path) {
      load.pinned = load.pinned || pinned;
      if (set != 0) {
        load.sets.push_back(set);
      }
      return true;
    }
  }
  for (std::pair<const std::uint64_t, Load>& entry : m_loads) {
    if (entry.second.path == path) {
      entry.second.pinned = entry.second.pinned || pinned;
      if (set != 0) {
        entry.second.sets.push_back(set);
      }
      return true;
    }
  }
  return false;
}

void
GuestVfsAssets::preload(const std::vector<std::string>& names)
{
  for (const std::string& name : names) {
    const std::string key = canonical(name);
    if (key.empty() || key == "/") {
      Logger::LogError("Package asset has an invalid name: " + name);
      continue;
    }
    std::map<std::string, Entry>::iterator found = m_entries.find(key);
    if (found != m_entries.end()) {
      found->second.pinned = true;
      continue;
    }
    ++m_preloads;
    if (!join(key, true, 0)) {
      Load load;
      load.path = key;
      load.pinned = true;
      m_waiting.push_back(std::move(load));
    }
  }
  pump();
}

bool
GuestVfsAssets::ready()
{
  pump();
  return m_preloads == 0;
}

std::uint64_t
GuestVfsAssets::fetch(const std::vector<std::string>& paths)
{
  const std::uint64_t id = m_nextSet++;
  Set set;
  for (const std::string& path : paths) {
    const std::string key = canonical(path);
    if (key.empty() || key == "/") {
      set.missing.push_back(path);
      continue;
    }
    if (std::find(set.paths.begin(), set.paths.end(), key) != set.paths.end()) {
      continue;
    }
    set.paths.push_back(key);
    std::map<std::string, Entry>::iterator found = m_entries.find(key);
    if (found != m_entries.end()) {
      // Already cached: hold it so the set's own loads cannot evict it.
      ++found->second.holds;
      continue;
    }
    ++set.pending;
    if (!join(key, false, id)) {
      Load load;
      load.path = key;
      load.sets.push_back(id);
      m_waiting.push_back(std::move(load));
    }
  }
  m_sets.emplace(id, std::move(set));
  pump();
  return id;
}

bool
GuestVfsAssets::fetched(std::uint64_t set, std::vector<std::string>* missing)
{
  pump();
  const std::map<std::uint64_t, Set>::const_iterator found = m_sets.find(set);
  if (found == m_sets.end() || found->second.pending != 0) {
    return false;
  }
  if (missing != nullptr) {
    *missing = found->second.missing;
  }
  return true;
}

void
GuestVfsAssets::release(std::uint64_t set)
{
  const std::map<std::uint64_t, Set>::iterator found = m_sets.find(set);
  if (found == m_sets.end()) {
    return;
  }
  for (const std::string& path : found->second.paths) {
    std::map<std::string, Entry>::iterator entry = m_entries.find(path);
    if (entry != m_entries.end() && entry->second.holds > 0) {
      --entry->second.holds;
    }
  }
  // Loads still in flight for a released set no longer report to it.
  for (Load& load : m_waiting) {
    load.sets.erase(std::remove(load.sets.begin(), load.sets.end(), set),
                    load.sets.end());
  }
  for (std::pair<const std::uint64_t, Load>& load : m_loads) {
    load.second.sets.erase(
      std::remove(load.second.sets.begin(), load.second.sets.end(), set),
      load.second.sets.end());
  }
  m_sets.erase(found);
  evict();
}

bool
GuestVfsAssets::putLocal(const std::string& name,
                         std::vector<unsigned char> bytes,
                         std::string* path)
{
  const std::string key = canonical("/local/" + name);
  if (key.size() <= 7 || key.compare(0, 7, "/local/") != 0 ||
      bytes.size() > kMaximumAssetBytes) {
    return false;
  }
  Entry& entry = m_entries[key];
  m_bytes -= entry.bytes.size();
  entry.bytes = std::move(bytes);
  entry.pinned = true;
  entry.used = ++m_clock;
  m_bytes += entry.bytes.size();
  if (path != nullptr) {
    *path = key;
  }
  evict();
  return true;
}

void
GuestVfsAssets::removeLocal(const std::string& name)
{
  const std::string key = canonical("/local/" + name);
  const std::map<std::string, Entry>::iterator found = m_entries.find(key);
  if (found != m_entries.end() && key.compare(0, 7, "/local/") == 0) {
    m_bytes -= found->second.bytes.size();
    m_entries.erase(found);
  }
}

void
GuestVfsAssets::finish(Load& load, GuestFileResult& result)
{
  const bool loaded = result.outcome == GuestFileOutcome::Success;
  if (loaded) {
    Entry& entry = m_entries[load.path];
    m_bytes -= entry.bytes.size();
    entry.bytes.resize(result.bytes.size());
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
      entry.bytes[index] = static_cast<unsigned char>(result.bytes[index]);
    }
    entry.pinned = entry.pinned || load.pinned;
    entry.used = ++m_clock;
    m_bytes += entry.bytes.size();
  } else if (load.pinned) {
    Logger::LogError("Package asset is missing or unreadable: " + load.path);
  }
  if (load.pinned && m_preloads > 0) {
    --m_preloads;
    if (m_preloads == 0) {
      // One summary rather than a line per asset: guest log lines share the
      // bounded service queue with file requests.
      Logger::LogTrace("Package asset preloads settled; the asset cache "
                       "holds " +
                       std::to_string(m_entries.size()) + " entries (" +
                       std::to_string(m_bytes) + " bytes)");
    }
  }
  for (std::uint64_t id : load.sets) {
    const std::map<std::uint64_t, Set>::iterator set = m_sets.find(id);
    if (set == m_sets.end()) {
      continue;
    }
    if (set->second.pending > 0) {
      --set->second.pending;
    }
    if (loaded) {
      ++m_entries[load.path].holds;
    } else {
      set->second.missing.push_back(load.path);
    }
  }
}

void
GuestVfsAssets::pump()
{
  while (!m_waiting.empty()) {
    const std::uint64_t task = m_files.read(
      GuestFileArea::Mounted, m_waiting.front().path, kMaximumAssetBytes);
    if (task == 0) {
      break;
    }
    m_loads.emplace(task, std::move(m_waiting.front()));
    m_waiting.pop_front();
  }
  bool finished = false;
  for (std::map<std::uint64_t, Load>::iterator it = m_loads.begin();
       it != m_loads.end();) {
    GuestFileResult result;
    if (!m_files.take(it->first, result)) {
      ++it;
      continue;
    }
    finish(it->second, result);
    it = m_loads.erase(it);
    finished = true;
  }
  if (finished) {
    evict();
  }
}

// Whether the last eviction pass ended over budget with nothing evictable,
// so that state is reported once rather than on every pass.
static bool overBudgetReported = false;

void
GuestVfsAssets::evict()
{
  std::size_t evicted = 0;
  std::size_t freed = 0;
  while (m_bytes > m_budget) {
    std::map<std::string, Entry>::iterator oldest = m_entries.end();
    for (std::map<std::string, Entry>::iterator it = m_entries.begin();
         it != m_entries.end();
         ++it) {
      if (!it->second.pinned && it->second.holds == 0 &&
          (oldest == m_entries.end() ||
           it->second.used < oldest->second.used)) {
        oldest = it;
      }
    }
    if (oldest == m_entries.end()) {
      if (!overBudgetReported) {
        overBudgetReported = true;
        Logger::LogWarning("Asset cache holds " + std::to_string(m_bytes) +
                           " bytes, over its " + std::to_string(m_budget) +
                           "-byte budget; every entry is pinned or in use");
      }
      break;
    }
    ++evicted;
    freed += oldest->second.bytes.size();
    m_bytes -= oldest->second.bytes.size();
    m_entries.erase(oldest);
  }
  if (m_bytes <= m_budget) {
    overBudgetReported = false;
  }
  if (evicted > 0) {
    Logger::LogTrace("Asset cache evicted " + std::to_string(evicted) +
                     " entries (" + std::to_string(freed) + " bytes)");
  }
}

bool
GuestVfsAssets::read(const std::string& canonical,
                     std::vector<unsigned char>& bytes) const
{
  const std::map<std::string, Entry>::const_iterator found =
    m_entries.find(canonical);
  if (found == m_entries.end()) {
    return false;
  }
  found->second.used = ++m_clock;
  bytes = found->second.bytes;
  return true;
}

std::int64_t
GuestVfsAssets::stamp(const std::string& canonical) const
{
  (void)canonical;
  return 0;
}
