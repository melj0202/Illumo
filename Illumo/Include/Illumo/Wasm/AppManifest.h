#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Host ceilings for manifest requests. A package asks; the runtime grants at
// most these, and command-line options are validated against the same range.
struct AppManifestCeilings
{
  std::uint64_t memoryMiB = 4095u;
  std::uint64_t fuelPerCall = 100000000000ull;
  std::uint64_t deadlineMilliseconds = 600000u;
  std::uint64_t workers = 8u;
};

// app.json is generic package metadata: identity, member modules, window
// title, launch-document access and requested budgets. Product policy stays
// in the guest.
struct AppManifest
{
  std::string id;
  std::string module;
  std::string worker;
  std::string title;
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

// Decodes and validates one manifest. Unknown members are ignored; a present
// member with the wrong type or an invalid value rejects the whole manifest.
bool
decodeAppManifest(std::string_view json,
                  const AppManifestCeilings& ceilings,
                  AppManifest& manifest,
                  std::string& error);
