#pragma once
#include "Rulesets/RuleSetRegistry.h"
#include <IllumoGuest/Files.h>
#include <array>

// Product policy runs in the control guest. A registry becomes visible only
// after the packaged pair and every present user overlay have been validated.
class CSimCatalogBootstrap
{
public:
  explicit CSimCatalogBootstrap(GuestFiles& files);
  ~CSimCatalogBootstrap();
  CSimCatalogBootstrap(const CSimCatalogBootstrap&) = delete;
  CSimCatalogBootstrap& operator=(const CSimCatalogBootstrap&) = delete;
  CSimCatalogBootstrap(CSimCatalogBootstrap&&) = delete;
  CSimCatalogBootstrap& operator=(CSimCatalogBootstrap&&) = delete;
  void pump();
  bool ready() const { return m_phase == 4; }
  bool failed() const { return !m_error.empty(); }
  const std::string& error() const { return m_error; }
  const RuleSetRegistry& registry() const { return m_staged; }

private:
  struct Read
  {
    std::uint64_t task = 0;
    bool complete = false;
    std::string text;
  };
  GuestFiles& m_files;
  std::array<Read, 4> m_reads;
  RuleSetRegistry m_staged;
  std::string m_error;
  unsigned int m_phase = 0;
};
