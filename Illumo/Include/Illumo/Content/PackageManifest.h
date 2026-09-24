#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// illumo.json: the one manifest at the root of every package, loose directory
// or .ilpk archive. It names the package, what it is for, what it depends on
// and, for applications and mods, the modules and budgets they request.
// Product policy stays in the guest; this is generic package metadata.

enum class PackageKind
{
  App,
  Content,
  Mod
};

// Host ceilings for application budget requests. A package asks; the runtime
// grants at most these, and command-line options use the same range.
struct PackageCeilings
{
  std::uint64_t memoryMiB = 4095u;
  std::uint64_t fuelPerCall = 100000000000ull;
  std::uint64_t deadlineMilliseconds = 600000u;
  std::uint64_t workers = 8u;
};

// The "app" section: member modules, launch-document access and requested
// budgets, clamped to PackageCeilings.
struct PackageAppSection
{
  std::string module;
  std::string worker;
  bool launchEditable = false;
  std::uint64_t memoryMiB = 64u;
  // "metering": "fuel" (default) instruments guest code with fuel and bounds
  // each call by fuelPerCall; "epoch" bounds calls by the deadline alone.
  bool meterFuel = true;
  std::uint64_t fuelPerCall = 10000000u;
  std::uint64_t deadlineMilliseconds = 1000u;
  // Compute lanes: worker instances of the "worker" module, each with these
  // budgets and the package's metering. Valid only with a worker module.
  std::uint64_t workers = 1u;
  std::uint64_t workerMemoryMiB = 512u;
  std::uint64_t workerDeadlineMilliseconds = 10000u;
};

// The "mod" section: a WASM mod module and the game extension API it targets.
struct PackageModSection
{
  std::string module;
  std::string extensionApi;
};

struct PackageDependency
{
  std::string id;
  // Informational in format 1: recorded and shown, not compared.
  std::string version;
};

// Contributes this package's files into another mount's merged view. Format 1
// accepts only the "/app" target.
struct PackageOverlay
{
  std::string target;
  std::int32_t priority = 0;
};

struct PackageManifest
{
  static constexpr std::uint32_t kFormatVersion = 1;
  static constexpr const char* kFileName = "illumo.json";

  std::string id;
  std::string version;
  std::string title;
  PackageKind kind = PackageKind::Content;
  // Application ids this package applies to; "*" means every application.
  std::vector<std::string> targets;
  std::vector<PackageDependency> dependencies;
  std::vector<PackageOverlay> overlays;
  PackageAppSection app;
  PackageModSection mod;

  bool targetsApplication(std::string_view applicationId) const;
};

// Package ids and application names share one conservative alphabet
// ([a-z0-9._-], at most 64), so an id is always a single directory name.
bool
validPackageId(std::string_view id);

// Decodes and validates one manifest. Every object is strict: an unknown key,
// a wrong type or an invalid value rejects the whole manifest and leaves the
// output untouched, because the manifest requests budgets and authority.
bool
decodePackageManifest(std::string_view json,
                      const PackageCeilings& ceilings,
                      PackageManifest& manifest,
                      std::string& error);

// Orders packages so every dependency precedes its dependents (Kahn's
// algorithm, ties broken by id). Packages whose dependencies are missing or
// that sit on a cycle are left out of the order and reported in rejected with
// a reason each. Indices refer to the input vector.
struct PackageOrderResult
{
  std::vector<std::size_t> order;
  std::vector<std::size_t> rejected;
  std::vector<std::string> reasons;
};

PackageOrderResult
orderPackages(const std::vector<PackageManifest>& packages);
