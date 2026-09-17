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
#include <optional>
#include <stdexcept>

static TestCounters g;

static std::optional<std::string>
readTestEnvironmentVariable(const char* name)
{
#ifdef _MSC_VER
  char* value = nullptr;
  std::size_t length = 0u;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
    std::free(value);
    return std::nullopt;
  }
  const std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

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
    writeFile(directory / "rulesets.json", text);
  }

  static void writeFile(const std::filesystem::path& path,
                        const std::string& text)
  {
    std::ofstream file(path, std::ios::binary);
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
    const std::optional<std::string> expectedRule =
      readTestEnvironmentVariable("ILLUMO_TEST_CATALOG_RULE");
    const std::optional<std::string> directLaunch =
      readTestEnvironmentVariable("ILLUMO_TEST_CATALOG_DIRECT");
    if (expectedRule.has_value()) {
      testTrue(g,
               !RuleSetRegistry::instance().isKnownRule(expectedRule.value()),
               "custom catalog is not loaded by registry construction");
    }
    const IllumoApplicationDefinition application = CreateIllumoApplication();
    CatalogFixture fixture;
    EnvVars environment(fixture.root / "environment.json");
    environment.setVar("LaunchDirect", directLaunch.has_value() ? "1" : "0");
    const std::unique_ptr<IModule> module =
      application.createRequiredModule(&environment);
    testTrue(g, module != nullptr, "required module factory succeeds");
    testTrue(g,
             RuleSetRegistry::instance().createRuleSet(
               expectedRule.has_value() ? expectedRule.value()
                                        : "GAME_OF_LIFE") != nullptr,
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
             "executable catalog takes precedence over lower priority files");
    CatalogFixture::write(fixture.executable, "malformed");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.isKnownRule("WORKING"),
             "malformed executable catalog falls back to working directory");
    std::filesystem::remove(fixture.working / "rulesets.json");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.isKnownRule("SUBDIRECTORY"),
             "missing working catalog falls back to product subdirectory");
    CatalogFixture::write(fixture.executable, "[]");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               !rules.isKnownRule("SUBDIRECTORY"),
             "valid empty catalog stops fallback");
    CatalogFixture::write(fixture.executable,
                          R"([{"id":"BASE_FOR_OVERLAY","rule":"B3/S23"}])");
    CatalogFixture::write(fixture.working,
                          R"([{"id":"LOWER_PRIORITY","rule":"B2/S"}])");
    CatalogFixture::writeFile(fixture.working / "rulesets.user.json",
                              R"([{"id":"USER_RULE","rule":"B3/S23"}])");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.isKnownRule("BASE_FOR_OVERLAY") &&
               !rules.isKnownRule("LOWER_PRIORITY") &&
               rules.isKnownRule("USER_RULE"),
             "user overlay is merged after the selected base catalog");
    CatalogFixture::writeFile(fixture.working / "rulesets.user.json",
                              R"([{"id":"BROKEN","rule":"B0/S"}])");
    testTrue(g,
             !RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.isKnownRule("BASE_FOR_OVERLAY") &&
               rules.isKnownRule("USER_RULE"),
             "invalid overlay preserves the previously published catalog");
    return g.failures;
  });
  registry.add("IllumoGame.Catalog.TransactionalFiles", []() {
    g = {};
    CatalogFixture fixture;
    RuleSetRegistry rules;
    testTrue(g,
             rules.loadFromText(R"([{"id":"GAME_OF_LIFE","rule":"B3/S23"}])"),
             "test base catalog loads");
    const std::vector<std::string> names = rules.getKnownRules();
    testTrue(g,
             !RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.getKnownRules() == names,
             "missing catalogs preserve the current catalog");
    CatalogFixture::write(
      fixture.executable,
      R"([{"id":"GAME_OF_LIFE","rule":"B2/S"},{"id":"INVALID","rule":"B0/S"}])");
    testTrue(g,
             !RuleCatalogLoader::loadFromFile(
               rules, fixture.executable / "rulesets.json"),
             "invalid later definition rejects entire file");
    testTrue(g,
             rules.getKnownRules() == names &&
               rules.getRuleSetDefinition("GAME_OF_LIFE")->birthMask ==
                 (1u << 3),
             "failed file leaves the original definition intact");
    CatalogFixture::write(fixture.working,
                          R"([{"id":"CUSTOM","rule":"B2/S"}])");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.createRuleSet("CUSTOM") != nullptr,
             "fallback catalog supplies usable rule factory");
    RuleSetDefinition custom;
    custom.id = "SAVED_RULE";
    custom.name = "Saved rule";
    RuleFamilyDefinition customFamily = *rules.getFamilyDefinition(
      rules.getRuleSetDefinition("CUSTOM")->familyId);
    customFamily.id = "SAVED_FAMILY";
    customFamily.name = "Saved family";
    customFamily.builtIn = false;
    custom.familyId = customFamily.id;
    custom.birthMask = 1u << 2u;
    RuleSetRegistry previousGlobal = RuleSetRegistry::instance();
    RuleSetRegistry::instance() = rules;
    testTrue(g,
             RuleCatalogLoader::saveUserFamily(fixture.working, customFamily) &&
               RuleCatalogLoader::saveUserRule(fixture.working, custom),
             "user family and rule are written through atomic catalog paths");
    testTrue(g,
             RuleCatalogLoader::loadFromLocations(
               rules, fixture.executable, fixture.working) &&
               rules.createRuleSet("SAVED_RULE") != nullptr &&
               rules.getFamilyDefinition("SAVED_FAMILY") != nullptr,
             "saved user family and rule layer over the startup catalog");
    const std::filesystem::path exported =
      fixture.working / "exported-rule.json";
    const RuleFamilyDefinition* family =
      rules.getFamilyDefinition(custom.familyId);
    testTrue(g,
             family != nullptr &&
               RuleCatalogLoader::saveCatalog(exported, *family, custom),
             "individual rule export writes a valid catalog file");
    RuleSetRegistry imported;
    testTrue(g,
             RuleCatalogLoader::loadFromFile(imported, exported) &&
               imported.createRuleSet("SAVED_RULE") != nullptr,
             "exported rule can be imported as a validated catalog");
    testTrue(g,
             !RuleCatalogLoader::loadFromFile(rules, fixture.working),
             "directory cannot be read as a catalog");
    RuleSetRegistry::instance() = std::move(previousGlobal);
    testTrue(g,
             !RuleCatalogLoader::loadFromLocations(rules, {}, {}),
             "absent directory inputs do not probe ambient paths");
    return g.failures;
  });
}
