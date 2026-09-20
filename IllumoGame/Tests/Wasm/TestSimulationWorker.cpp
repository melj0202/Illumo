#include "DomainFixture.h"
#include "Wasm/SimulationProtocol.h"
#include <Illumo/Wasm/WasmWorker.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

static std::string
snapshot(const SparseCellGrid& grid, const RuleSet& rule)
{
  IllumoDocument document;
  document.sourceGrid = &grid;
  document.familyString = rule.getFamilyTag();
  document.ruleString = rule.getRuleTag();
  document.worldChunkWidth = grid.getWorldChunkWidth();
  document.worldChunkHeight = grid.getWorldChunkHeight();
  std::ostringstream output(std::ios::binary);
  return IllumoCodec::writeStream(output, document) ? output.str()
                                                    : std::string{};
}

static bool
protocolCases(const std::string& families, const std::string& rules)
{
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(families, rules)) {
    return false;
  }
  RuleSetRegistry::instance() = registry;
  std::unique_ptr<RuleSet> rule = registry.createRuleSet("GAME_OF_LIFE");
  if (!rule) {
    return false;
  }
  for (int scenario = 0; scenario < 5; ++scenario) {
    SparseCellGrid base;
    if (scenario == 0) {
      for (std::int64_t index = 1; index <= 300; ++index) {
        for (std::int64_t y = 0; y < 2; ++y) {
          for (std::int64_t x = 0; x < 2; ++x) {
            base.setCell({ index * 64 + x, y }, 0);
          }
        }
      }
      for (std::int64_t x = 0; x < 3; ++x) {
        base.setCell({ x, 0 }, 0);
      }
    } else if (scenario == 1) {
      base.setCell({ -1, -1 }, 0);
    } else if (scenario == 3 || scenario == 4) {
      for (std::int64_t index = 0; index < 2100; ++index) {
        base.setCell({ index * 32, 8 }, 0);
        if (scenario == 3) {
          base.setCell({ index * 32 + 1, 8 }, 0);
          base.setCell({ index * 32 + 2, 8 }, 0);
        }
      }
    }
    SimulationRequest request;
    request.session = request.worldEpoch = request.ruleEpoch =
      request.operation = 1;
    request.synchronize = true;
    request.families = families;
    request.rules = rules;
    request.snapshot = snapshot(base, *rule);
    request.revision = base.getRevision();
    SimulationGuestWorker worker;
    GuestWireWriter input;
    request.write(input);
    std::vector<std::byte> output;
    std::string error;
    SimulationReply reply;
    if (!worker.execute(input.data(), output, error) ||
        !SimulationReply::read(output, request, reply)) {
      std::fprintf(
        stderr, "Protocol scenario %d: %s\n", scenario, error.c_str());
      return false;
    }
    const SimulationReply::Content expected =
      scenario == 2   ? SimulationReply::Content::Unchanged
      : scenario >= 3 ? SimulationReply::Content::Snapshot
                      : SimulationReply::Content::Chunks;
    if (reply.content != expected) {
      std::puts("Wrong reply representation");
      return false;
    }
    if (scenario == 0 && output.size() >= request.snapshot.size() / 10) {
      std::puts("Local change did not produce a bounded delta");
      return false;
    }
    SparseCellGrid reference;
    reference.copyStateFrom(base);
    if (!reference.advance(*rule)) {
      return false;
    }
    if (reply.changed) {
      std::unique_ptr<SparseCellGrid> restored =
        reply.restore(base, *rule, &error);
      if (!restored ||
          snapshot(*restored, *rule) != snapshot(reference, *rule)) {
        std::puts("Protocol private publication mismatch");
        return false;
      }
      SimulationReply malformed = reply;
      ++malformed.baseRevision;
      if (malformed.restore(base, *rule)) {
        return false;
      }
      if (reply.content == SimulationReply::Content::Chunks) {
        malformed = reply;
        malformed.chunks.push_back(malformed.chunks.front());
        GuestWireWriter duplicate;
        malformed.write(duplicate);
        SimulationReply ignored;
        if (SimulationReply::read(duplicate.data(), request, ignored) ||
            malformed.restore(base, *rule)) {
          return false;
        }
        malformed = reply;
        malformed.chunks[0].present = true;
        malformed.chunks[0].cells[0] = 255;
        if (malformed.restore(base, *rule)) {
          return false;
        }
        malformed = reply;
        malformed.chunks[0].address.x = INT64_MAX;
        if (malformed.restore(base, *rule)) {
          return false;
        }
        // A legal empty upsert is still an invalid delta record.
        malformed = reply;
        malformed.chunks[0].present = true;
        malformed.chunks[0].cells.fill(1);
        if (malformed.restore(base, *rule)) {
          return false;
        }
      } else {
        malformed = reply;
        malformed.snapshot += "trailing bytes";
        if (malformed.restore(base, *rule)) {
          return false;
        }
        SparseCellGrid invalidCoordinates;
        SparseChunkRecord invalidChunk;
        invalidChunk.chunkX = INT64_MAX;
        invalidChunk.cells.fill(1);
        invalidChunk.cells[0] = 0;
        if (!invalidCoordinates.assignChunk(invalidChunk)) {
          return false;
        }
        malformed.snapshot = snapshot(invalidCoordinates, *rule);
        if (malformed.restore(base, *rule)) {
          return false;
        }
        SparseCellGrid differentTopology(4, 4);
        malformed.snapshot = snapshot(differentTopology, *rule);
        if (malformed.restore(base, *rule)) {
          return false;
        }
        malformed.snapshot = snapshot(base, *rule);
        if (malformed.restore(base, *rule)) {
          return false;
        }
      }
    }
    // A store binds one session; resynchronization must advance an epoch.
    SimulationRequest invalid = request;
    invalid.operation = 2;
    invalid.session = 2;
    input.clear();
    invalid.write(input);
    if (worker.execute(input.data(), output, error)) {
      return false;
    }
    invalid.session = 1;
    input.clear();
    invalid.write(input);
    if (worker.execute(input.data(), output, error)) {
      return false;
    }
    invalid.worldEpoch = 2;
    input.clear();
    invalid.write(input);
    if (!worker.execute(input.data(), output, error)) {
      return false;
    }
    invalid.operation = 3;
    invalid.worldEpoch = 1;
    input.clear();
    invalid.write(input);
    if (worker.execute(input.data(), output, error)) {
      return false;
    }
  }
  std::puts("Simulation protocol delta, deletion, settled, replacement and "
            "rejection cases: PASS");
  return true;
}

static std::string
readText(const char* path)
{
  std::ifstream stream(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(stream), {} };
}

static bool
waitFor(WasmWorker& worker, WasmWorkerStatus status)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (worker.status() != status &&
         std::chrono::steady_clock::now() < deadline) {
    if (worker.status() == WasmWorkerStatus::Failed) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return worker.status() == status;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts(
      "IllumoGame.Wasm.WorkerParity\nIllumoGame.Wasm.SimulationProtocol");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      (std::string(argv[2]) != "IllumoGame.Wasm.WorkerParity" &&
       std::string(argv[2]) != "IllumoGame.Wasm.SimulationProtocol")) {
    return 2;
  }
  const std::string families = readText(ILLUMO_FAMILIES);
  const std::string rules = readText(ILLUMO_RULES);
  if (std::string(argv[2]) == "IllumoGame.Wasm.SimulationProtocol") {
    return protocolCases(families, rules) ? 0 : 1;
  }
  const std::string binary = readText(ILLUMO_SIMULATION_WORKER);
  std::vector<std::byte> module(binary.size());
  std::memcpy(module.data(), binary.data(), binary.size());
  WasmLimits limits;
  limits.memoryBytes = 512u * 1024u * 1024u;
  limits.fuelPerCall = 1000000000u;
  limits.deadlineMilliseconds = 10000;
  WasmWorker worker(std::move(module), limits);
  if (!waitFor(worker, WasmWorkerStatus::Idle)) {
    std::puts("Worker startup failed");
    return 1;
  }
  DomainFixture reference;
  DomainFixture published;
  if (!reference.initialize(families, rules) ||
      !published.initialize(families, rules)) {
    return 1;
  }
  SimulationRequest request;
  request.session = 7;
  std::uint64_t operation = 0;
  for (int rule = 0; rule < reference.ruleCount(); ++rule) {
    for (int topology = 0; topology < 2; ++topology) {
      for (int workload = 0; workload < 3; ++workload) {
        if (!reference.select(rule, topology, workload) ||
            !published.select(rule, topology, workload)) {
          return 1;
        }
        request.synchronize = true;
        request.families = families;
        request.rules = rules;
        request.snapshot = reference.save();
        ++request.worldEpoch;
        ++request.ruleEpoch;
        request.generation = 0;
        request.revision = published.grid().getRevision();
        for (int step = 0; step < 8; ++step) {
          request.operation = ++operation;
          GuestWireWriter input;
          request.write(input);
          std::uint64_t hostId = 0;
          if (!worker.submit(input.data(), hostId) || !reference.advance() ||
              !waitFor(worker, WasmWorkerStatus::Completed)) {
            WasmJobResult failed;
            worker.poll(failed);
            std::fprintf(stderr, "Job failed: %s\n", failed.error.c_str());
            return 1;
          }
          WasmJobResult result;
          SimulationReply reply;
          if (!worker.poll(result) || result.requestId != hostId ||
              !result.error.empty() ||
              !SimulationReply::read(result.bytes, request, reply)) {
            std::puts("Completion mismatch");
            return 1;
          }
          if (reply.changed) {
            std::unique_ptr<SparseCellGrid> restored =
              reply.restore(published.grid(), published.rule());
            if (!restored || restored->getRevision() != reply.revision) {
              return 1;
            }
            published.publish(std::move(restored));
          }
          if (published.hash() != reference.hash()) {
            std::fprintf(stderr,
                         "Worker state mismatch: rule=%d topology=%d "
                         "workload=%d step=%d\n",
                         rule,
                         topology,
                         workload,
                         step);
            return 1;
          }
          SimulationRequest stale = request;
          ++stale.worldEpoch;
          SimulationReply rejected;
          if (SimulationReply::read(result.bytes, stale, rejected)) {
            return 1;
          }
          request.synchronize = false;
          request.families.clear();
          request.rules.clear();
          request.snapshot.clear();
          request.generation = reply.generation;
          request.revision = reply.revision;
        }
      }
    }
  }
  // Duplicate operation cannot execute twice on the persistent mutable store.
  GuestWireWriter replay;
  request.write(replay);
  std::uint64_t hostId = 0;
  if (!worker.submit(replay.data(), hostId)) {
    return 1;
  }
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (worker.status() != WasmWorkerStatus::Failed &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  WasmJobResult failure;
  if (!worker.poll(failure) || failure.error.empty()) {
    return 1;
  }
  std::printf("%d rules, two topologies, three workloads, eight persistent "
              "WASM worker steps: PASS\n",
              reference.ruleCount());
  return 0;
}
