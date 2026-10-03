#pragma once
#include <Illumo/Rendering/RenderCommand.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// A program's uniforms by UniformKey (D-R37): an open-addressed table whose
// entries confirm their full name, so a hit costs one masked probe and one
// short compare instead of hashing the name. Absent uniforms are cached as
// well (the caller stores its "none" value). A token whose key was not built
// by the renderer simply misses; callers then retry with uniformKeyOf(name).
class UniformNameTable
{
public:
  // The value stored for name under key, or null.
  const std::int32_t* find(const UniformKey& key, const char* name) const
  {
    if (m_entries.empty() || name == nullptr) {
      return nullptr;
    }
    const std::size_t mask = m_entries.size() - 1u;
    for (std::size_t index = start(key) & mask;; index = (index + 1u) & mask) {
      const Entry& entry = m_entries[index];
      if (!entry.used) {
        return nullptr;
      }
      if (entry.key == key && entry.name == name) {
        return &entry.value;
      }
    }
  }

  void insert(const UniformKey& key, const char* name, std::int32_t value)
  {
    if (name == nullptr) {
      return;
    }
    if ((m_count + 1u) * 2u > m_entries.size()) {
      grow();
    }
    place(Entry{ true, key, std::string(name), value });
    m_count += 1u;
  }

  void clear()
  {
    m_entries.clear();
    m_count = 0u;
  }

private:
  struct Entry
  {
    bool used = false;
    UniformKey key{ 0u };
    std::string name;
    std::int32_t value = 0;
  };

  static std::size_t start(const UniformKey& key)
  {
    return static_cast<std::size_t>(key.hash);
  }

  void place(Entry entry)
  {
    const std::size_t mask = m_entries.size() - 1u;
    std::size_t index = start(entry.key) & mask;
    while (m_entries[index].used) {
      index = (index + 1u) & mask;
    }
    m_entries[index] = std::move(entry);
  }

  void grow()
  {
    std::vector<Entry> previous = std::move(m_entries);
    m_entries.clear();
    m_entries.resize(previous.empty() ? 16u : previous.size() * 2u);
    for (Entry& entry : previous) {
      if (entry.used) {
        place(std::move(entry));
      }
    }
  }

  std::vector<Entry> m_entries;
  std::size_t m_count = 0u;
};

// The value for a token's uniform: by its carried key, then by the key its
// name yields, then resolved once through resolve(name) and cached (entries
// only ever hold true keys).
template<typename Resolve>
std::int32_t
lookupUniform(UniformNameTable& table,
              const UniformKey& carried,
              const char* name,
              const Resolve& resolve)
{
  if (const std::int32_t* found = table.find(carried, name)) {
    return *found;
  }
  const UniformKey derived = uniformKeyOf(name);
  if (!(derived == carried)) {
    if (const std::int32_t* found = table.find(derived, name)) {
      return *found;
    }
  }
  const std::int32_t value = resolve(name);
  table.insert(derived, name, value);
  return value;
}
