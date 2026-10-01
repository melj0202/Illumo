#include "DomainFixture.h"
#include <Illumo/Wasm/WasmInstance.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::string
read(const char* path)
{
  std::ifstream stream(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(stream), {} };
}

static bool
upload(WasmInstance& instance, const std::string& text, std::int32_t& address)
{
  const std::array<std::int32_t, 1> args{ static_cast<std::int32_t>(
    text.size()) };
  return instance.call("allocate", args, address) &&
         instance.copyToMemory(
           static_cast<std::uint32_t>(address),
           std::as_bytes(std::span(text.data(), text.size())));
}

static bool
compare(WasmInstance& instance, const DomainFixture& reference)
{
  const std::array<std::int32_t, 1> lowArg{ 0 };
  const std::array<std::int32_t, 1> highArg{ 1 };
  std::int32_t low = 0;
  std::int32_t high = 0;
  if (!instance.call("domainHash", lowArg, low) ||
      !instance.call("domainHash", highArg, high)) {
    return false;
  }
  const std::uint64_t hash =
    static_cast<std::uint32_t>(low) |
    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32u);
  return hash == reference.hash();
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("IllumoGame.Wasm.DomainParity");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      std::string(argv[2]) != "IllumoGame.Wasm.DomainParity") {
    return 2;
  }
  const std::string module = read(ILLUMO_DOMAIN_GUEST);
  const std::string families = read(ILLUMO_FAMILIES);
  const std::string rules = read(ILLUMO_RULES);
  WasmLimits limits;
  limits.fuelPerCall = 1000000000u;
  limits.deadlineMilliseconds = 10000u;
  limits.memoryBytes = 512u * 1024u * 1024u;
  WasmInstance instance(limits);
  DomainFixture reference;
  std::int32_t familiesAddress = 0;
  std::int32_t rulesAddress = 0;
  std::int32_t result = 0;
  if (!instance.load(std::as_bytes(std::span(module.data(), module.size()))) ||
      !upload(instance, families, familiesAddress) ||
      !upload(instance, rules, rulesAddress)) {
    std::fprintf(
      stderr, "Guest load/upload failed: %s\n", instance.error().c_str());
    return 1;
  }
  const std::array<std::int32_t, 4> initialize{
    familiesAddress,
    static_cast<std::int32_t>(families.size()),
    rulesAddress,
    static_cast<std::int32_t>(rules.size())
  };
  if (!instance.call("domainInitialize", initialize, result) ||
      !reference.initialize(families, rules) ||
      result != reference.ruleCount()) {
    std::fprintf(
      stderr, "Catalog initialization failed: %s\n", instance.error().c_str());
    return 1;
  }
  for (int rule = 0; rule < reference.ruleCount(); ++rule) {
    for (int topology = 0; topology < 2; ++topology) {
      for (int workload = 0; workload < 3; ++workload) {
        const std::array<std::int32_t, 3> args{ rule, topology, workload };
        if (!reference.select(rule, topology, workload) ||
            !instance.call("domainCase", args, result) || result != 1) {
          return 1;
        }
        for (int generation = 0; generation < 8; ++generation) {
          if (!compare(instance, reference) || !reference.advance() ||
              !instance.call("domainAdvance", {}, result) || result != 1) {
            std::fprintf(stderr,
                         "Parity failed: rule=%d topology=%d workload=%d "
                         "generation=%d %s\n",
                         rule,
                         topology,
                         workload,
                         generation,
                         instance.error().c_str());
            return 1;
          }
        }
        if (!compare(instance, reference)) {
          return 1;
        }
        const std::string saved = reference.save();
        std::int32_t address = 0;
        std::int32_t size = 0;
        if (saved.empty() || !instance.call("domainSave", {}, address) ||
            !instance.call("domainSaveSize", {}, size) || size < 0 ||
            static_cast<std::size_t>(size) != saved.size()) {
          std::fprintf(stderr, "Save failed: %s\n", instance.error().c_str());
          return 1;
        }
        std::string guestSave(saved.size(), '\0');
        if (!instance.copyFromMemory(static_cast<std::uint32_t>(address),
                                     std::as_writable_bytes(std::span(
                                       guestSave.data(), guestSave.size()))) ||
            guestSave != saved || !reference.restore(saved) ||
            !instance.call("domainRestore", {}, result) || result != 1 ||
            !compare(instance, reference)) {
          std::fprintf(stderr,
                       "Save byte/restore parity failed: %s\n",
                       instance.error().c_str());
          return 1;
        }
      }
    }
  }
  std::printf("PASS: %d shipped rules, two topologies, three workloads, eight "
              "generations, identical v4 save bytes and restore\n",
              reference.ruleCount());
  return 0;
}
