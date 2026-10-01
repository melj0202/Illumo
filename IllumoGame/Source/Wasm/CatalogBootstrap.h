#pragma once
#include "Rulesets/RuleSetRegistry.h"
#include <IllumoGuest/Files.h>
#include <array>
#include <string>
#include <vector>

// Product policy runs in the control guest. A registry becomes visible only
// after the packaged pair and every present user overlay have been validated.
//
// Content packages extend the catalogs: every *.json under
// /packages/<id>/csim/ (packages in id order, files in name order) merges
// after the packaged pair and before the user overlays, so a player's own
// edits still win. Files named families*.json add families; any other name
// is a rule package. A package catalog that does not validate is skipped with
// a warning instead of stopping the game.
class CSimCatalogBootstrap
{
public:
  static constexpr std::size_t kMaximumPackageCatalogs = 64;

  explicit CSimCatalogBootstrap(GuestFiles& files);
  ~CSimCatalogBootstrap();
  CSimCatalogBootstrap(const CSimCatalogBootstrap&) = delete;
  CSimCatalogBootstrap& operator=(const CSimCatalogBootstrap&) = delete;
  CSimCatalogBootstrap(CSimCatalogBootstrap&&) = delete;
  CSimCatalogBootstrap& operator=(CSimCatalogBootstrap&&) = delete;
  void pump();
  bool ready() const { return m_phase == Phase::Ready; }
  bool failed() const { return !m_error.empty(); }
  const std::string& error() const { return m_error; }
  const RuleSetRegistry& registry() const { return m_staged; }
  // Package catalogs that were merged, as virtual paths.
  const std::vector<std::string>& merged() const { return m_merged; }
  // Package catalogs that were skipped, with the reason.
  const std::vector<std::string>& warnings() const { return m_warnings; }

private:
  enum class Phase
  {
    ReadPackaged,
    ListPackages,
    ListCatalogs,
    ReadCatalogs,
    Apply,
    Ready
  };
  struct Read
  {
    std::uint64_t task = 0;
    bool complete = false;
    std::string text;
  };
  struct PackageCatalog
  {
    std::string path;
    std::string text;
  };
  bool readPackaged();
  bool listPackages();
  bool listCatalogs();
  bool readCatalogs();
  void apply();

  GuestFiles& m_files;
  std::array<Read, 4> m_reads;
  RuleSetRegistry m_staged;
  std::string m_error;
  Phase m_phase = Phase::ReadPackaged;
  std::uint64_t m_task = 0;
  std::vector<std::string> m_packages;
  std::size_t m_nextPackage = 0;
  std::vector<PackageCatalog> m_catalogs;
  std::size_t m_nextCatalog = 0;
  std::vector<std::string> m_merged;
  std::vector<std::string> m_warnings;
};
