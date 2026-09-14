#include "Game/RuleCatalogLoader.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

static TestCounters g;

class CatalogFixture
{
public:
  const std::filesystem::path root;
  const std::filesystem::path executable;
  const std::filesystem::path working;

  CatalogFixture()
    : root(std::filesystem::temp_directory_path() /
           ("illumo-catalog-" +
            std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count())))
    , executable(root / "executable")
    , working(root / "working")
  {
    if (!std::filesystem::create_directory(root)) {
      throw std::runtime_error("Catalog test directory already exists");
    }
    std::filesystem::create_directories(executable);
    std::filesystem::create_directories(working / "IllumoGame");
  }
  ~CatalogFixture()
  {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  CatalogFixture(const CatalogFixture&) = delete;
  CatalogFixture& operator=(const CatalogFixture&) = delete;
  CatalogFixture(CatalogFixture&&) = delete;
  CatalogFixture& operator=(CatalogFixture&&) = delete;

  static void write(const std::filesystem::path& directory,
                    const std::string& text)
  {
    std::ofstream file(directory / "rulesets.json", std::ios::binary);
    file << text;
    file.close();
    if (!file) {
      throw std::runtime_error("Cannot write catalog fixture");
    }
  }
};

void
registerRuleCatalogLoaderTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Catalog.StartupComposition", []() {
    g = {};
    const char* expectedRule = std::getenv("ILLUMO_TEST_CATALOG_RULE");
    const char* directLaunch = std::getenv("ILLUMO_TEST_CATALOG_DIRECT");
    if (expectedRule != nullptr) {
      testTrue(g,
               !RuleSetRegistry::instance().isKnownRule(expectedRule),
               "custom catalog is not loaded by registry construction");
    }
    const IllumoApplicationDefinition application = CreateIllumoApplication();
    CatalogFixture fixture;
    EnvVars environment(fixture.root / "environment.json");
    environment.setVar("LaunchDirect", directLaunch != nullptr ? "1" : "0");
    const std::unique_ptr<IModule> module =
      application.createRequiredModule(&environment);
    testTrue(g, module != nullptr, "required module factory succeeds");
    testTrue(g,
             RuleSetRegistry::instance().createRuleSet(
               expectedRule != nullptr ? expectedRule : "GAME_OF_LIFE") !=
               nullptr,
             "catalog rule is available before required module startup");
    return g.failures;
  });
  registry.add("IllumoGame.Catalog.PrecedenceAndFallback", []() {
    g = {};
    CatalogFixture fixture;
    CatalogFixture::write(fixture.executable,
                          R"([{"id":"EXECUTABLE","rule":"B3/S23"}])");
    CatalogFixture::write(fixture.working,
                          R"([{"id":"WORKING","rule":"B3/S23"}])");
    CatalogFixture::write(fixture.working / "IllumoGame",
                          R"([{"id":"SUBDIRECTORY","rule":"B3/S23"}])");
    RuleSetRegistry rules;
    testTrue(g,
             !rules.isKnownRule("EXECUTABLE"),
             "constructing registry does not load catalogs");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working),
             "loads first valid catalog");
    testTrue(g,
             rules.isKnownRule("EXECUTABLE") && !rules.isKnownRule("WORKING") &&
               !rules.isKnownRule("SUBDIRECTORY"),
             "executable catalog takes precedence without merging lower "
             "priority files");
    testTrue(
      g, rules.isKnownRule("GAME_OF_LIFE"), "overlay preserves built-ins");
    CatalogFixture::write(fixture.executable, "malformed");
    rules.loadBuiltinDefaults();
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.isKnownRule("WORKING"),
             "malformed executable catalog falls back to working directory");
    std::filesystem::remove(fixture.working / "rulesets.json");
    rules.loadBuiltinDefaults();
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.isKnownRule("SUBDIRECTORY"),
             "missing working catalog falls back to product subdirectory");
    CatalogFixture::write(fixture.executable, "[]");
    rules.loadBuiltinDefaults();
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               !rules.isKnownRule("SUBDIRECTORY"),
             "valid empty catalog stops fallback");
    return g.failures;
  });
  registry.add("IllumoGame.Catalog.TransactionalFiles", []() {
    g = {};
    CatalogFixture fixture;
    RuleSetRegistry rules;
    const std::vector<std::string> names = rules.getKnownRules();
    testTrue(g,
             !RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.getKnownRules() == names,
             "missing catalogs preserve built-ins");
    CatalogFixture::write(
      fixture.executable,
      R"([{"id":"GAME_OF_LIFE","rule":"B2/S"},{"id":"INVALID","rule":"B0/S"}])");
    testTrue(g,
             !RuleCatalogLoader::loadFromFile(
               rules, fixture.executable / "rulesets.json"),
             "invalid later definition rejects entire file");
    testTrue(g,
             rules.getKnownRules() == names &&
               rules.getRuleDefinition("GAME_OF_LIFE")->birthMask == (1u << 3),
             "failed file leaves original definition intact");
    CatalogFixture::write(fixture.working,
                          R"([{"id":"CUSTOM","rule":"B2/S"}])");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.createRuleSet("CUSTOM") != nullptr,
             "fallback catalog supplies usable rule factory");
    testTrue(g,
             !RuleCatalogLoader::loadFromFile(rules, fixture.working),
             "directory cannot be read as a catalog");
    testTrue(g,
             !RuleCatalogLoader::loadFromLocations(rules, {}, {}),
             "absent directory inputs do not probe ambient paths");
    return g.failures;
  });
}
