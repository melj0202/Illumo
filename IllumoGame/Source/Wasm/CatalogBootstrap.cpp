#include "CatalogBootstrap.h"

CSimCatalogBootstrap::CSimCatalogBootstrap(GuestFiles& files)
  : m_files(files)
{
}

CSimCatalogBootstrap::~CSimCatalogBootstrap()
{
  for (const Read& read : m_reads) {
    m_files.cancel(read.task);
  }
}

void
CSimCatalogBootstrap::pump()
{
  if (ready() || failed()) {
    return;
  }
  constexpr std::array<const char*, 4> paths{
    "families.json", "rulesets.json", "families.user.json", "rulesets.user.json"
  };
  if (m_phase == 0) {
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
                                 4u * 1024u * 1024u);
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
        return;
      }
      read.text.assign(reinterpret_cast<const char*>(result.bytes.data()),
                       result.bytes.size());
      // An existing empty overlay is malformed, not an absent optional file.
      if (read.text.empty()) {
        m_error = std::string("Empty catalog: ") + paths[index];
        return;
      }
    }
    if (complete) {
      m_phase = 1;
    }
    return;
  }
  bool accepted = false;
  if (m_phase == 1) {
    accepted = m_staged.loadFromCatalogTexts(m_reads[0].text, m_reads[1].text);
  } else if (m_phase == 2) {
    accepted =
      m_reads[2].text.empty() || m_staged.loadFamiliesFromText(m_reads[2].text);
  } else {
    accepted =
      m_reads[3].text.empty() || m_staged.loadFromText(m_reads[3].text);
  }
  if (!accepted) {
    m_error = m_phase == 1 ? "Packaged rule catalogs are invalid"
                           : "User rule catalog is invalid";
    return;
  }
  ++m_phase;
}
