#include <Illumo/Wasm/AppManifest.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <utility>

// Plain file names inside the package; paths and parent references are not
// package members.
static bool
packageMember(const std::string& name)
{
  return !name.empty() && name.size() <= 128 &&
         name.find_first_of("/\\:") == std::string::npos && name != "." &&
         name != "..";
}

// Package ids and application names share one conservative alphabet, so an
// application name is always a single directory under apps/.
static bool
packageId(const std::string& id)
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

bool
decodeAppManifest(std::string_view json,
                  const AppManifestCeilings& ceilings,
                  AppManifest& output,
                  std::string& error)
{
  const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
  if (!document.is_object() || !document.contains("id") ||
      !document["id"].is_string() || !document.contains("module") ||
      !document["module"].is_string()) {
    error = "Package manifest requires string id and module";
    return false;
  }
  AppManifest manifest;
  manifest.id = document["id"].get<std::string>();
  manifest.module = document["module"].get<std::string>();
  const std::pair<const char*, std::string*> strings[] = {
    { "worker", &manifest.worker }, { "title", &manifest.title }
  };
  for (const std::pair<const char*, std::string*>& field : strings) {
    if (!document.contains(field.first)) {
      continue;
    }
    if (!document[field.first].is_string()) {
      error = std::string("Package manifest field ") + field.first +
              " must be a string";
      return false;
    }
    *field.second = document[field.first].get<std::string>();
  }
  if (document.contains("launchAccess")) {
    const nlohmann::json& access = document["launchAccess"];
    if (!access.is_string() || (access != "read" && access != "edit")) {
      error = "Package launchAccess must be \"read\" or \"edit\"";
      return false;
    }
    manifest.launchEditable = access == "edit";
  }
  if (document.contains("metering")) {
    const nlohmann::json& metering = document["metering"];
    if (!metering.is_string() || (metering != "fuel" && metering != "epoch")) {
      error = "Package metering must be \"fuel\" or \"epoch\"";
      return false;
    }
    manifest.meterFuel = metering == "fuel";
  }
  if (!manifest.meterFuel && document.contains("fuelPerCall")) {
    error = "An epoch-metered package cannot request fuelPerCall";
    return false;
  }
  const bool workerLimits = document.contains("workers") ||
                            document.contains("workerMemoryMiB") ||
                            document.contains("workerDeadlineMilliseconds");
  if (workerLimits && manifest.worker.empty()) {
    error = "Worker budgets need a worker module";
    return false;
  }
  // Requests are clamped to host ceilings; packages cannot raise them.
  const std::pair<const char*, std::uint64_t*> limits[] = {
    { "memoryMiB", &manifest.memoryMiB },
    { "fuelPerCall", &manifest.fuelPerCall },
    { "deadlineMilliseconds", &manifest.deadlineMilliseconds },
    { "workers", &manifest.workers },
    { "workerMemoryMiB", &manifest.workerMemoryMiB },
    { "workerDeadlineMilliseconds", &manifest.workerDeadlineMilliseconds }
  };
  const std::uint64_t maximums[] = {
    ceilings.memoryMiB, ceilings.fuelPerCall, ceilings.deadlineMilliseconds,
    ceilings.workers,   ceilings.memoryMiB,   ceilings.deadlineMilliseconds
  };
  for (std::size_t index = 0; index < std::size(limits); ++index) {
    if (!document.contains(limits[index].first)) {
      continue;
    }
    const nlohmann::json& value = document[limits[index].first];
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0) {
      error = std::string("Invalid package limit ") + limits[index].first;
      return false;
    }
    *limits[index].second =
      std::min(value.get<std::uint64_t>(), maximums[index]);
  }
  if (!packageId(manifest.id) || !packageMember(manifest.module) ||
      (!manifest.worker.empty() && !packageMember(manifest.worker)) ||
      manifest.title.size() > 128) {
    error = "Package manifest names an invalid id, module or title";
    return false;
  }
  output = std::move(manifest);
  error.clear();
  return true;
}
