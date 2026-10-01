#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Content/VirtualPath.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

bool
PackageManifest::targetsApplication(std::string_view applicationId) const
{
  for (const std::string& target : targets) {
    if (target == "*" || target == applicationId) {
      return true;
    }
  }
  return false;
}

bool
validPackageId(std::string_view id)
{
  if (id.empty() || id.size() > 64 || id == "." || id == "..") {
    return false;
  }
  for (const char character : id) {
    if (!((character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '.' ||
          character == '-' || character == '_')) {
      return false;
    }
  }
  return true;
}

// Plain file names inside the package root; nested paths and parent
// references are not module members.
static bool
moduleMember(const std::string& name)
{
  return name.size() <= 128 && name.find('/') == std::string::npos &&
         VirtualPath::validComponent(name);
}

// Printable single-line text without control characters.
static bool
plainText(const std::string& text, std::size_t maximum)
{
  if (text.size() > maximum) {
    return false;
  }
  for (const char value : text) {
    const unsigned char character = static_cast<unsigned char>(value);
    if (character < 32u || character == 127u) {
      return false;
    }
  }
  return true;
}

static bool
onlyKeys(const nlohmann::json& object,
         std::initializer_list<const char*> allowed,
         std::string& error,
         const char* where)
{
  for (nlohmann::json::const_iterator item = object.begin();
       item != object.end();
       ++item) {
    bool known = false;
    for (const char* key : allowed) {
      known = known || item.key() == key;
    }
    if (!known) {
      error = std::string("Unknown key \"") + item.key() + "\" in " + where;
      return false;
    }
  }
  return true;
}

static bool
readString(const nlohmann::json& object,
           const char* key,
           bool required,
           std::string& output,
           std::string& error,
           const char* where)
{
  if (!object.contains(key)) {
    if (required) {
      error = std::string(where) + " requires string " + key;
    }
    return !required;
  }
  if (!object[key].is_string()) {
    error = std::string(where) + " field " + key + " must be a string";
    return false;
  }
  output = object[key].get<std::string>();
  return true;
}

static bool
decodeApp(const nlohmann::json& section,
          const PackageCeilings& ceilings,
          PackageAppSection& app,
          std::string& error)
{
  if (!section.is_object() ||
      !onlyKeys(section,
                { "module",
                  "worker",
                  "launchAccess",
                  "metering",
                  "memoryMiB",
                  "fuelPerCall",
                  "deadlineMilliseconds",
                  "workers",
                  "workerMemoryMiB",
                  "workerDeadlineMilliseconds" },
                error,
                "app") ||
      !readString(section, "module", true, app.module, error, "app") ||
      !readString(section, "worker", false, app.worker, error, "app")) {
    if (error.empty()) {
      error = "app must be an object";
    }
    return false;
  }
  if (!moduleMember(app.module) ||
      (!app.worker.empty() && !moduleMember(app.worker))) {
    error = "app names an invalid module or worker";
    return false;
  }
  if (section.contains("launchAccess")) {
    const nlohmann::json& access = section["launchAccess"];
    if (!access.is_string() || (access != "read" && access != "edit")) {
      error = "app launchAccess must be \"read\" or \"edit\"";
      return false;
    }
    app.launchEditable = access == "edit";
  }
  if (section.contains("metering")) {
    const nlohmann::json& metering = section["metering"];
    if (!metering.is_string() || (metering != "fuel" && metering != "epoch")) {
      error = "app metering must be \"fuel\" or \"epoch\"";
      return false;
    }
    app.meterFuel = metering == "fuel";
  }
  if (!app.meterFuel && section.contains("fuelPerCall")) {
    error = "An epoch-metered app cannot request fuelPerCall";
    return false;
  }
  const bool workerLimits = section.contains("workers") ||
                            section.contains("workerMemoryMiB") ||
                            section.contains("workerDeadlineMilliseconds");
  if (workerLimits && app.worker.empty()) {
    error = "Worker budgets need a worker module";
    return false;
  }
  // Requests are clamped to host ceilings; packages cannot raise them.
  const std::pair<const char*, std::uint64_t*> limits[] = {
    { "memoryMiB", &app.memoryMiB },
    { "fuelPerCall", &app.fuelPerCall },
    { "deadlineMilliseconds", &app.deadlineMilliseconds },
    { "workers", &app.workers },
    { "workerMemoryMiB", &app.workerMemoryMiB },
    { "workerDeadlineMilliseconds", &app.workerDeadlineMilliseconds }
  };
  const std::uint64_t maximums[] = {
    ceilings.memoryMiB, ceilings.fuelPerCall, ceilings.deadlineMilliseconds,
    ceilings.workers,   ceilings.memoryMiB,   ceilings.deadlineMilliseconds
  };
  for (std::size_t index = 0; index < std::size(limits); ++index) {
    if (!section.contains(limits[index].first)) {
      continue;
    }
    const nlohmann::json& value = section[limits[index].first];
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0) {
      error = std::string("Invalid app limit ") + limits[index].first;
      return false;
    }
    *limits[index].second =
      std::min(value.get<std::uint64_t>(), maximums[index]);
  }
  return true;
}

static bool
decodeMod(const nlohmann::json& section,
          PackageModSection& mod,
          std::string& error)
{
  if (!section.is_object()) {
    error = "mod must be an object";
    return false;
  }
  if (!onlyKeys(section, { "module", "extensionApi" }, error, "mod") ||
      !readString(section, "module", true, mod.module, error, "mod") ||
      !readString(
        section, "extensionApi", true, mod.extensionApi, error, "mod")) {
    return false;
  }
  if (!moduleMember(mod.module) || !validPackageId(mod.extensionApi)) {
    error = "mod names an invalid module or extensionApi";
    return false;
  }
  return true;
}

bool
decodePackageManifest(std::string_view json,
                      const PackageCeilings& ceilings,
                      PackageManifest& output,
                      std::string& error)
{
  error.clear();
  const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
  if (!document.is_object()) {
    error = "Package manifest is not a JSON object";
    return false;
  }
  if (!onlyKeys(document,
                { "format",
                  "format_version",
                  "id",
                  "version",
                  "title",
                  "kind",
                  "targets",
                  "dependencies",
                  "overlays",
                  "app",
                  "mod" },
                error,
                "package manifest")) {
    return false;
  }
  if (!document.contains("format") || document["format"] != "ilpk") {
    error = "Package manifest requires \"format\": \"ilpk\"";
    return false;
  }
  if (!document.contains("format_version") ||
      !document["format_version"].is_number_unsigned()) {
    error = "Package manifest requires an unsigned format_version";
    return false;
  }
  if (document["format_version"].get<std::uint64_t>() !=
      PackageManifest::kFormatVersion) {
    error = "Package manifest format_version " +
            std::to_string(document["format_version"].get<std::uint64_t>()) +
            " is not supported (expected 1)";
    return false;
  }
  PackageManifest manifest;
  const char* where = "package manifest";
  if (!readString(document, "id", true, manifest.id, error, where) ||
      !readString(document, "version", false, manifest.version, error, where) ||
      !readString(document, "title", false, manifest.title, error, where)) {
    return false;
  }
  if (!validPackageId(manifest.id)) {
    error = "Package id must be 1-64 characters of [a-z0-9._-]";
    return false;
  }
  if (!plainText(manifest.version, 64) || !plainText(manifest.title, 128)) {
    error = "Package version or title is too long or has control characters";
    return false;
  }
  std::string kind;
  if (!readString(document, "kind", true, kind, error, where)) {
    return false;
  }
  if (kind == "app") {
    manifest.kind = PackageKind::App;
  } else if (kind == "content") {
    manifest.kind = PackageKind::Content;
  } else if (kind == "mod") {
    manifest.kind = PackageKind::Mod;
  } else {
    error = "Package kind must be \"app\", \"content\" or \"mod\"";
    return false;
  }
  const bool hasApp = document.contains("app");
  const bool hasMod = document.contains("mod");
  if (hasApp != (manifest.kind == PackageKind::App) ||
      hasMod != (manifest.kind == PackageKind::Mod)) {
    error = "Package kind \"" + kind +
            "\" does not match its sections (app needs \"app\", mod needs "
            "\"mod\", content has neither)";
    return false;
  }
  if (hasApp && !decodeApp(document["app"], ceilings, manifest.app, error)) {
    return false;
  }
  if (hasMod && !decodeMod(document["mod"], manifest.mod, error)) {
    return false;
  }

  if (document.contains("targets")) {
    const nlohmann::json& targets = document["targets"];
    if (!targets.is_array() || targets.size() > 64) {
      error = "Package targets must be an array of at most 64 ids";
      return false;
    }
    std::set<std::string> seen;
    for (const nlohmann::json& target : targets) {
      if (!target.is_string() ||
          (target != "*" && !validPackageId(target.get<std::string>())) ||
          !seen.insert(target.get<std::string>()).second) {
        error = "Package targets must be unique application ids or \"*\"";
        return false;
      }
      manifest.targets.push_back(target.get<std::string>());
    }
  } else {
    manifest.targets.push_back("*");
  }

  if (document.contains("dependencies")) {
    const nlohmann::json& dependencies = document["dependencies"];
    if (!dependencies.is_array() || dependencies.size() > 64) {
      error = "Package dependencies must be an array of at most 64 entries";
      return false;
    }
    std::set<std::string> seen;
    for (const nlohmann::json& entry : dependencies) {
      PackageDependency dependency;
      const char* dependencyWhere = "dependency";
      if (!entry.is_object() ||
          !onlyKeys(entry, { "id", "version" }, error, dependencyWhere) ||
          !readString(
            entry, "id", true, dependency.id, error, dependencyWhere) ||
          !readString(entry,
                      "version",
                      false,
                      dependency.version,
                      error,
                      dependencyWhere)) {
        if (error.empty()) {
          error = "Package dependency must be an object";
        }
        return false;
      }
      if (!validPackageId(dependency.id) || dependency.id == manifest.id ||
          !plainText(dependency.version, 64) ||
          !seen.insert(dependency.id).second) {
        error = "Package dependency \"" + dependency.id +
                "\" is invalid, duplicated or the package itself";
        return false;
      }
      manifest.dependencies.push_back(std::move(dependency));
    }
  }

  if (document.contains("overlays")) {
    const nlohmann::json& overlays = document["overlays"];
    if (!overlays.is_array() || overlays.size() > 8) {
      error = "Package overlays must be an array of at most 8 entries";
      return false;
    }
    std::set<std::string> seen;
    for (const nlohmann::json& entry : overlays) {
      PackageOverlay overlay;
      if (!entry.is_object() ||
          !onlyKeys(entry, { "target", "priority" }, error, "overlay") ||
          !readString(
            entry, "target", true, overlay.target, error, "overlay")) {
        if (error.empty()) {
          error = "Package overlay must be an object";
        }
        return false;
      }
      if (overlay.target != "/app") {
        error = "Package overlays may only target \"/app\" in format 1";
        return false;
      }
      if (entry.contains("priority")) {
        const nlohmann::json& priority = entry["priority"];
        if (!priority.is_number_integer() ||
            priority.get<std::int64_t>() < -1000000 ||
            priority.get<std::int64_t>() > 1000000) {
          error = "Package overlay priority must be an integer in "
                  "[-1000000, 1000000]";
          return false;
        }
        overlay.priority =
          static_cast<std::int32_t>(priority.get<std::int64_t>());
      }
      if (!seen.insert(overlay.target).second) {
        error = "Package overlays name the same target twice";
        return false;
      }
      manifest.overlays.push_back(std::move(overlay));
    }
  }

  output = std::move(manifest);
  return true;
}

PackageOrderResult
orderPackages(const std::vector<PackageManifest>& packages)
{
  PackageOrderResult result;
  std::map<std::string, std::size_t> byId;
  std::vector<bool> excluded(packages.size(), false);
  for (std::size_t index = 0; index < packages.size(); ++index) {
    if (!byId.emplace(packages[index].id, index).second) {
      excluded[index] = true;
      result.rejected.push_back(index);
      result.reasons.push_back("Duplicate package id \"" + packages[index].id +
                               "\"");
    }
  }
  // A missing dependency excludes the package, which may in turn exclude its
  // dependents; iterate until no more packages drop out.
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0; index < packages.size(); ++index) {
      if (excluded[index]) {
        continue;
      }
      for (const PackageDependency& dependency : packages[index].dependencies) {
        const std::map<std::string, std::size_t>::const_iterator found =
          byId.find(dependency.id);
        if (found == byId.end() || excluded[found->second]) {
          excluded[index] = true;
          result.rejected.push_back(index);
          result.reasons.push_back("Package \"" + packages[index].id +
                                   "\" needs missing package \"" +
                                   dependency.id + "\"");
          changed = true;
          break;
        }
      }
    }
  }
  std::vector<std::size_t> remaining(packages.size(), 0);
  std::vector<std::vector<std::size_t>> dependents(packages.size());
  std::set<std::pair<std::string, std::size_t>> ready;
  for (std::size_t index = 0; index < packages.size(); ++index) {
    if (excluded[index]) {
      continue;
    }
    for (const PackageDependency& dependency : packages[index].dependencies) {
      const std::size_t provider = byId.at(dependency.id);
      dependents[provider].push_back(index);
      ++remaining[index];
    }
    if (remaining[index] == 0) {
      ready.emplace(packages[index].id, index);
    }
  }
  while (!ready.empty()) {
    const std::size_t index = ready.begin()->second;
    ready.erase(ready.begin());
    result.order.push_back(index);
    for (const std::size_t dependent : dependents[index]) {
      if (--remaining[dependent] == 0) {
        ready.emplace(packages[dependent].id, dependent);
      }
    }
  }
  for (std::size_t index = 0; index < packages.size(); ++index) {
    if (!excluded[index] && remaining[index] != 0) {
      result.rejected.push_back(index);
      result.reasons.push_back("Package \"" + packages[index].id +
                               "\" is on or depends on a dependency cycle");
    }
  }
  return result;
}
