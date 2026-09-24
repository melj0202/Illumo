#include <Illumo/Content/SceneAssetRefs.h>
#include <IllumoGuest/SceneFetches.h>
#include <cstddef>
#include <utility>

static bool
isObjPath(const std::string& path)
{
  if (path.size() < 4) {
    return false;
  }
  std::string suffix = path.substr(path.size() - 4);
  for (char& character : suffix) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return suffix == ".obj";
}

GuestSceneFetches::GuestSceneFetches(GuestVfsAssets& cache)
  : m_cache(cache)
{
}

GuestSceneFetches::~GuestSceneFetches()
{
  release();
  for (const Fetch& fetch : m_fetches) {
    m_cache.release(fetch.set);
  }
}

void
GuestSceneFetches::fetch(const std::vector<std::string>& paths, Callback done)
{
  Fetch fetch;
  fetch.set = m_cache.fetch(paths);
  fetch.paths = paths;
  fetch.done = std::move(done);
  m_fetches.push_back(std::move(fetch));
}

void
GuestSceneFetches::release()
{
  for (std::uint64_t set : m_held) {
    m_cache.release(set);
  }
  m_held.clear();
}

void
GuestSceneFetches::pump()
{
  for (std::size_t index = 0; index < m_fetches.size();) {
    std::vector<std::string> missing;
    if (!m_cache.fetched(m_fetches[index].set, &missing)) {
      ++index;
      continue;
    }
    Fetch fetch = std::move(m_fetches[index]);
    m_fetches.erase(m_fetches.begin() + static_cast<std::ptrdiff_t>(index));
    m_held.push_back(fetch.set);
    if (fetch.materials) {
      // Materials are optional: an OBJ without its MTL still loads.
      missing.clear();
    }
    missing.insert(missing.end(), fetch.missing.begin(), fetch.missing.end());
    // OBJ files name their material libraries; fetch those too before the
    // mesh loads, so AssetManager reads them beside the OBJ.
    std::vector<std::string> libraries;
    if (!fetch.materials) {
      for (const std::string& path : fetch.paths) {
        std::vector<unsigned char> bytes;
        const std::string canonical = m_cache.canonical(path);
        if (isObjPath(path) && m_cache.read(canonical, bytes)) {
          for (const std::string& library : objMaterialLibraries(
                 canonical, std::string(bytes.begin(), bytes.end()))) {
            libraries.push_back(library);
          }
        }
      }
    }
    if (!libraries.empty()) {
      Fetch next;
      next.set = m_cache.fetch(libraries);
      next.paths = libraries;
      next.materials = true;
      next.missing = std::move(missing);
      next.done = std::move(fetch.done);
      m_fetches.push_back(std::move(next));
      continue;
    }
    fetch.done(std::move(missing));
  }
}
