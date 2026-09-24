#include "CatalogBootstrap.h"
#include <algorithm>

static constexpr std::size_t kCatalogBytes = 4u * 1024u * 1024u;

CSimCatalogBootstrap::CSimCatalogBootstrap(GuestFiles& files)
  : m_files(files)
{
}

CSimCatalogBootstrap::~CSimCatalogBootstrap()
{
  for (const Read& read : m_reads) {
    m_files.cancel(read.task);
  }
  m_files.cancel(m_task);
}

bool
CSimCatalogBootstrap::readPackaged()
{
  constexpr std::array<const char*, 4> paths{
    "families.json", "rulesets.json", "families.user.json", "rulesets.user.json"
  };
  bool complete = true;
  for (std::size_t index = 0; index < m_reads.size(); ++index) {
    Read& read = m_reads[index];
    if (read.complete) {
      continue;
    }
    if (read.task == 0) {
      read.task = m_files.read(index < 2 ? GuestFileArea::Package
                                         : GuestFileArea::Storage,
                               paths[index],
                               kCatalogBytes);
    }
    GuestFileResult result;
    if (read.task == 0 || !m_files.take(read.task, result)) {
      complete = false;
      continue;
    }
    read.task = 0;
    read.complete = true;
    if (result.outcome == GuestFileOutcome::NotFound && index != 1) {
      continue;
    }
    if (result.outcome != GuestFileOutcome::Success) {
      m_error = std::string("Cannot read catalog: ") + paths[index];
      return false;
    }
    read.text.assign(reinterpret_cast<const char*>(result.bytes.data()),
                     result.bytes.size());
    // An existing empty overlay is malformed, not an absent optional file.
    if (read.text.empty()) {
      m_error = std::string("Empty catalog: ") + paths[index];
      return false;
    }
  }
  return complete;
}

// One listing page is plenty: a runtime rarely mounts hundreds of packages,
// and a catalog directory holds a handful of files.
static bool
takeListing(GuestFiles& files,
            std::uint64_t& task,
            const std::string& path,
            bool* done,
            GuestFileListing* listing)
{
  *done = false;
  if (task == 0) {
    task = files.list(path, 0);
    return task != 0;
  }
  GuestFileResult result;
  if (!files.take(task, result)) {
    return true;
  }
  task = 0;
  *done = true;
  // No /packages mount, or no csim directory: nothing to merge.
  if (result.outcome != GuestFileOutcome::Success ||
      !GuestFileListing::read(result.bytes, *listing)) {
    listing->entries.clear();
  }
  return true;
}

bool
CSimCatalogBootstrap::listPackages()
{
  bool done = false;
  GuestFileListing listing;
  if (!takeListing(m_files, m_task, "/packages", &done, &listing) || !done) {
    return false;
  }
  for (const GuestFileEntry& entry : listing.entries) {
    if (entry.directory) {
      m_packages.push_back(entry.name);
    }
  }
  std::sort(m_packages.begin(), m_packages.end());
  return true;
}

bool
CSimCatalogBootstrap::listCatalogs()
{
  while (m_nextPackage < m_packages.size()) {
    const std::string directory =
      "/packages/" + m_packages[m_nextPackage] + "/csim";
    bool done = false;
    GuestFileListing listing;
    if (!takeListing(m_files, m_task, directory, &done, &listing) || !done) {
      return false;
    }
    std::vector<std::string> names;
    for (const GuestFileEntry& entry : listing.entries) {
      if (!entry.directory && entry.name.size() > 5 &&
          entry.name.compare(entry.name.size() - 5, 5, ".json") == 0) {
        names.push_back(entry.name);
      }
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
      if (m_catalogs.size() < kMaximumPackageCatalogs) {
        m_catalogs.push_back({ directory + "/" + name, std::string() });
      }
    }
    ++m_nextPackage;
  }
  return true;
}

bool
CSimCatalogBootstrap::readCatalogs()
{
  while (m_nextCatalog < m_catalogs.size()) {
    PackageCatalog& catalog = m_catalogs[m_nextCatalog];
    if (m_task == 0) {
      m_task =
        m_files.read(GuestFileArea::Mounted, catalog.path, kCatalogBytes);
      if (m_task == 0) {
        return false;
      }
    }
    GuestFileResult result;
    if (!m_files.take(m_task, result)) {
      return false;
    }
    m_task = 0;
    if (result.outcome == GuestFileOutcome::Success) {
      catalog.text.assign(reinterpret_cast<const char*>(result.bytes.data()),
                          result.bytes.size());
    } else {
      m_warnings.push_back("Cannot read package catalog " + catalog.path);
    }
    ++m_nextCatalog;
  }
  return true;
}

void
CSimCatalogBootstrap::apply()
{
  if (!m_staged.loadFromCatalogTexts(m_reads[0].text, m_reads[1].text)) {
    m_error = "Packaged rule catalogs are invalid";
    return;
  }
  for (const PackageCatalog& catalog : m_catalogs) {
    if (catalog.text.empty()) {
      continue;
    }
    // Merge into a copy so a rejected catalog leaves no partial state.
    RuleSetRegistry candidate = m_staged;
    const std::size_t slash = catalog.path.rfind('/');
    const std::string name = catalog.path.substr(slash + 1);
    const bool families = name.rfind("families", 0) == 0;
    const bool accepted = families
                            ? candidate.loadFamiliesFromText(catalog.text)
                            : candidate.loadFromText(catalog.text);
    if (accepted) {
      m_staged = std::move(candidate);
      m_merged.push_back(catalog.path);
    } else {
      m_warnings.push_back("Skipping invalid package catalog " + catalog.path);
    }
  }
  if ((!m_reads[2].text.empty() &&
       !m_staged.loadFamiliesFromText(m_reads[2].text)) ||
      (!m_reads[3].text.empty() && !m_staged.loadFromText(m_reads[3].text))) {
    m_error = "User rule catalog is invalid";
    return;
  }
  m_phase = Phase::Ready;
}

void
CSimCatalogBootstrap::pump()
{
  if (ready() || failed()) {
    return;
  }
  switch (m_phase) {
    case Phase::ReadPackaged:
      if (readPackaged()) {
        m_phase = Phase::ListPackages;
      }
      return;
    case Phase::ListPackages:
      if (listPackages()) {
        m_phase = Phase::ListCatalogs;
      }
      return;
    case Phase::ListCatalogs:
      if (listCatalogs()) {
        m_phase = Phase::ReadCatalogs;
      }
      return;
    case Phase::ReadCatalogs:
      if (readCatalogs()) {
        m_phase = Phase::Apply;
      }
      return;
    case Phase::Apply:
      apply();
      return;
    case Phase::Ready:
      return;
  }
}
