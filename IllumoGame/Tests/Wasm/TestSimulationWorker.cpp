#include "DomainFixture.h"
#include "Rulesets/RuleSet.h"
#include "Wasm/SimulationLanes.h"
#include "Wasm/SimulationProtocol.h"
#include <Illumo/Wasm/WasmWorker.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <thread>
#ifdef _WIN32
#include <malloc.h>
#endif

// Heap allocations made by the counting thread while a bounded check runs.
// Loopback lanes execute on that thread, so coordinator, lane workers and
// grid delta application are all counted; grid worker-pool threads are not.
static thread_local bool g_countAllocations = false;
static thread_local std::size_t g_allocations = 0;

void*
operator new(std::size_t size)
{
  if (g_countAllocations) {
    ++g_allocations;
  }
  void* pointer = std::malloc(size == 0 ? 1 : size);
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}
void*
operator new[](std::size_t size)
{
  return ::operator new(size);
}
void
operator delete(void* pointer) noexcept
{
  std::free(pointer);
}
void
operator delete[](void* pointer) noexcept
{
  std::free(pointer);
}
void
operator delete(void* pointer, std::size_t) noexcept
{
  std::free(pointer);
}
void
operator delete[](void* pointer, std::size_t) noexcept
{
  std::free(pointer);
}
void*
operator new(std::size_t size, const std::nothrow_t&) noexcept
{
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void*
operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
  return ::operator new(size, std::nothrow);
}
void
operator delete(void* pointer, const std::nothrow_t&) noexcept
{
  ::operator delete(pointer);
}
void
operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
  ::operator delete[](pointer);
}
void*
operator new(std::size_t size, std::align_val_t alignment)
{
  if (g_countAllocations) {
    ++g_allocations;
  }
  const std::size_t align = static_cast<std::size_t>(alignment);
#ifdef _WIN32
  void* pointer = _aligned_malloc(size == 0 ? 1 : size, align);
#else
  const std::size_t rounded =
    ((size == 0 ? 1 : size) + align - 1) / align * align;
  void* pointer = std::aligned_alloc(align, rounded);
#endif
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}
void*
operator new[](std::size_t size, std::align_val_t alignment)
{
  return ::operator new(size, alignment);
}
void
operator delete(void* pointer, std::align_val_t) noexcept
{
#ifdef _WIN32
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
void
operator delete[](void* pointer, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}
void
operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}
void
operator delete[](void* pointer,
                  std::size_t,
                  std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}

// In-process lanes: each owns one SimulationLaneWorker and answers on the
// poll after its submission, as the asynchronous host transport would.
class LoopbackLanes final : public SimulationLaneTransport
{
public:
  explicit LoopbackLanes(std::uint32_t lanes)
    : m_workers(lanes)
    , m_replies(lanes)
    , m_ready(lanes, false)
    , m_failed(lanes, false)
  {
  }
  std::uint32_t laneCount() override
  {
    return static_cast<std::uint32_t>(m_workers.size());
  }
  bool laneCountKnown() const override { return true; }
  bool submit(std::uint32_t lane, std::vector<std::byte>&& request) override
  {
    if (lane >= m_workers.size() || m_ready[lane] || m_failed[lane]) {
      return false;
    }
    std::string error;
    if (!m_workers[lane].execute(request, m_replies[lane], error)) {
      std::fprintf(stderr, "Lane %u failed: %s\n", lane, error.c_str());
      m_failed[lane] = true;
    }
    m_ready[lane] = true;
    request.clear();
    ++submissions;
    return true;
  }
  int poll(std::uint32_t lane, std::vector<std::byte>& reply) override
  {
    if (lane >= m_workers.size() || !m_ready[lane]) {
      return 0;
    }
    m_ready[lane] = false;
    if (m_failed[lane]) {
      return -1;
    }
    // Swapped, as a retaining transport would: both buffers keep capacity.
    reply.swap(m_replies[lane]);
    return 1;
  }
  bool busy(std::uint32_t lane) const override
  {
    return lane < m_workers.size() && m_ready[lane];
  }
  std::size_t submissions = 0;

private:
  std::vector<SimulationLaneWorker> m_workers;
  std::vector<std::vector<std::byte>> m_replies;
  std::vector<bool> m_ready;
  std::vector<bool> m_failed;
};

static std::uint64_t
gridHash(const SparseCellGrid& grid)
{
  std::uint64_t result = 14695981039346656037ULL;
  for (const SparseChunkRecord& chunk : grid.collectChunkRecords()) {
    for (const std::int64_t coordinate : { chunk.chunkX, chunk.chunkY }) {
      const std::uint64_t bits = static_cast<std::uint64_t>(coordinate);
      for (unsigned int index = 0; index < 8u; ++index) {
        result = (result ^ ((bits >> (index * 8u)) & 255u)) * 1099511628211ULL;
      }
    }
    for (unsigned char cell : chunk.cells) {
      result = (result ^ cell) * 1099511628211ULL;
    }
  }
  return result;
}

// Starts a block of up to `steps` generations, retrying while lanes drain
// retired work, as later frames would. `accepted` receives the block length.
static bool
startLanes(SimulationLaneCoordinator& lanes,
           SparseCellGrid* published,
           SparseCellGrid* spare,
           const RuleSet& rule,
           SparseGenerationDelta& mirror,
           bool mirrorValid,
           std::uint32_t steps = 1u,
           std::uint32_t* accepted = nullptr)
{
  for (int attempt = 0; attempt < 10000; ++attempt) {
    if (lanes.availability(rule) ==
          SimulationLaneCoordinator::Availability::Available &&
        lanes.start(spare,
                    published,
                    &rule,
                    std::move(mirror),
                    mirrorValid,
                    steps,
                    accepted)) {
      return true;
    }
    if (lanes.failed()) {
      return false;
    }
    lanes.poll(nullptr, nullptr, nullptr, nullptr, nullptr);
  }
  return false;
}

// Drives one coordinator block to completion (publication swap included),
// pumping the loopback lanes; `published` then holds `generations` more.
// False on a stall or failure.
static bool
laneGeneration(SimulationLaneCoordinator& lanes,
               SparseCellGrid*& published,
               SparseCellGrid*& spare,
               const RuleSet& rule,
               SparseGenerationDelta& mirror,
               bool& mirrorValid,
               std::uint32_t steps = 1u,
               std::uint32_t* generations = nullptr)
{
  std::uint32_t accepted = 0u;
  if (!startLanes(
        lanes, published, spare, rule, mirror, mirrorValid, steps, &accepted)) {
    return false;
  }
  for (int attempt = 0; attempt < 10000; ++attempt) {
    SparseCellGrid* completed = nullptr;
    SparseGenerationDelta delta;
    bool succeeded = false;
    SimulationRunnerTimings timings;
    if (lanes.poll(&completed, &delta, nullptr, &succeeded, &timings)) {
      if (!succeeded || completed != spare || timings.generations != accepted) {
        return false;
      }
      std::swap(published, spare);
      mirror = std::move(delta);
      mirrorValid = true;
      if (generations != nullptr) {
        *generations = timings.generations;
      }
      return true;
    }
    if (lanes.failed()) {
      std::fprintf(stderr, "Lanes failed: %s\n", lanes.failure().c_str());
      return false;
    }
  }
  return false;
}

// Advances `serial` by `generations`.
static bool
advanceSerial(SparseCellGrid& serial,
              const RuleSet& rule,
              std::uint32_t generations)
{
  for (std::uint32_t step = 0u; step < generations; ++step) {
    if (!serial.advance(rule)) {
      return false;
    }
  }
  return true;
}

// Serial multi-generation starts (the guest runner's frame budget) publish
// their net change as one delta: the result equals plain advances, the delta
// brings the other grid of the pair to it exactly, and the pair keeps
// alternating as the canvas publishes it.
static bool
serialBlocks()
{
  std::unique_ptr<RuleSet> life =
    RuleSetRegistry::instance().createRuleSet("GAME_OF_LIFE");
  if (!life) {
    return false;
  }
  for (std::int64_t size : { std::int64_t{ 0 }, std::int64_t{ 12 } }) {
    SparseCellGrid serial(size, size);
    std::uint32_t state = 4242u;
    for (std::int64_t y = -80; y < 80; ++y) {
      for (std::int64_t x = -80; x < 80; ++x) {
        state = state * 1664525u + 1013904223u;
        if ((state >> 24) < 80u) {
          serial.setCell({ x, y }, 0);
        }
      }
    }
    SparseCellGrid first(size, size);
    SparseCellGrid second(size, size);
    first.copyStateFrom(serial);
    SparseCellGrid* published = &first;
    SparseCellGrid* spare = &second;
    SparseGenerationDelta mirror;
    bool mirrorValid = false;
    for (std::uint32_t generations : { 1u, 5u, 2u, 16u, 1u, 7u, 3u }) {
      SparseGenerationDelta delta;
      SimulationRunnerTimings timings;
      if (!SimulationRunner::runGenerationsForTesting(spare,
                                                      published,
                                                      life.get(),
                                                      std::move(mirror),
                                                      mirrorValid,
                                                      generations,
                                                      &delta,
                                                      &timings) ||
          timings.generations != generations ||
          delta.fromRevision != published->getRevision() ||
          delta.toRevision != spare->getRevision() ||
          !advanceSerial(serial, *life, generations) ||
          gridHash(serial) != gridHash(*spare)) {
        std::fprintf(stderr,
                     "Serial block mismatch: size=%lld generations=%u\n",
                     static_cast<long long>(size),
                     generations);
        return false;
      }
      std::swap(published, spare);
      mirror = std::move(delta);
      mirrorValid = true;
    }
  }
  return true;
}

// Lane blocks match serial generations for every catalog rule, both
// topologies, several lane counts and halo depths, blocks from one generation
// to the halo's limit, across edits, a retired in-flight block, forced
// recuts and wide elementary rows.
static bool
laneParity(const std::string& families, const std::string& rules)
{
  DomainFixture reference;
  if (!reference.initialize(families, rules)) {
    return false;
  }
  int partitioned = 0;
  for (int ruleIndex = 0; ruleIndex < reference.ruleCount(); ++ruleIndex) {
    for (int topology = 0; topology < 2; ++topology) {
      for (std::uint32_t laneCount : { 1u, 2u, 3u }) {
        if (!reference.select(ruleIndex, topology, 2)) {
          return false;
        }
        const RuleSet& rule = reference.rule();
        LoopbackLanes transport(laneCount);
        SimulationLaneCoordinator lanes(transport);
        // One-line halos give the tightest block limit; three lanes also
        // recut after every imbalanced block.
        const std::uint32_t haloLines = laneCount == 2u ? 1u : 2u;
        lanes.setHaloLinesForTesting(haloLines);
        if (laneCount == 3u) {
          lanes.setRebalanceForTesting(1u, 0.0, 0.0);
        }
        // Every rule partitions: elementary 1D by chunk column.
        if (lanes.availability(rule) !=
            SimulationLaneCoordinator::Availability::Available) {
          std::fprintf(stderr, "Rule %d refused lanes\n", ruleIndex);
          return false;
        }
        const std::uint32_t cap = lanes.maximumBlockSteps(rule);
        if (cap == 0u) {
          return false;
        }
        const std::int64_t size = topology == 0 ? 0 : 4;
        SparseCellGrid serial(size, size);
        SparseCellGrid first(size, size);
        SparseCellGrid second(size, size);
        serial.copyStateFrom(reference.grid());
        first.copyStateFrom(reference.grid());
        SparseCellGrid* published = &first;
        SparseCellGrid* spare = &second;
        SparseGenerationDelta mirror;
        bool mirrorValid = false;
        for (int step = 0; step < 10; ++step) {
          if (step == 4) {
            // An edit: lanes resynchronize from the edited world.
            published->setCell({ 3, -7 }, 0);
            serial.setCell({ 3, -7 }, 0);
            mirrorValid = false;
          }
          if (step == 6) {
            // A retired block leaves the published world unchanged.
            if (!startLanes(
                  lanes, published, spare, rule, mirror, mirrorValid, cap)) {
              return false;
            }
            lanes.retire();
            for (int drain = 0; drain < 100 && lanes.busy(); ++drain) {
              lanes.poll(nullptr, nullptr, nullptr, nullptr, nullptr);
            }
            mirrorValid = false;
            if (lanes.busy()) {
              return false;
            }
          }
          // Block lengths cycle through one, a few and the halo's limit.
          const std::uint32_t lengths[] = { 1u, 3u, cap, 2u, cap };
          const std::uint32_t steps = std::min(cap, lengths[step % 5]);
          std::uint32_t generations = 0u;
          if (!laneGeneration(lanes,
                              published,
                              spare,
                              rule,
                              mirror,
                              mirrorValid,
                              steps,
                              &generations)) {
            std::fprintf(stderr,
                         "Lane block failed: rule=%d topology=%d "
                         "lanes=%u step=%d\n",
                         ruleIndex,
                         topology,
                         laneCount,
                         step);
            return false;
          }
          if (!advanceSerial(serial, rule, generations) ||
              gridHash(serial) != gridHash(*published)) {
            std::fprintf(stderr,
                         "Lane state mismatch: rule=%s topology=%d lanes=%u "
                         "halo=%u step=%d generations=%u\n",
                         rule.getRuleTag().c_str(),
                         topology,
                         laneCount,
                         haloLines,
                         step,
                         generations);
            return false;
          }
        }
        ++partitioned;
      }
    }
  }
  // A soup large enough to span many lines, grow and collide across lanes.
  std::unique_ptr<RuleSet> life =
    RuleSetRegistry::instance().createRuleSet("GAME_OF_LIFE");
  if (!life) {
    return false;
  }
  for (std::int64_t size : { std::int64_t{ 0 }, std::int64_t{ 12 } }) {
    for (std::uint32_t haloLines : { 1u, 2u }) {
      for (bool recut : { false, true }) {
        SparseCellGrid serial(size, size);
        std::uint32_t state = 12345u;
        for (std::int64_t y = -96; y < 96; ++y) {
          for (std::int64_t x = -96; x < 96; ++x) {
            state = state * 1664525u + 1013904223u;
            if ((state >> 24) < 90u) {
              serial.setCell({ x, y }, 0);
            }
          }
        }
        LoopbackLanes transport(4);
        SimulationLaneCoordinator lanes(transport);
        lanes.setHaloLinesForTesting(haloLines);
        lanes.setRebalanceForTesting(
          recut ? 1u : std::numeric_limits<std::uint32_t>::max(), 0.0, 0.0);
        SparseCellGrid first(size, size);
        SparseCellGrid second(size, size);
        first.copyStateFrom(serial);
        SparseCellGrid* published = &first;
        SparseCellGrid* spare = &second;
        SparseGenerationDelta mirror;
        bool mirrorValid = false;
        const std::uint32_t cap = lanes.maximumBlockSteps(*life);
        for (int step = 0; step < 24; ++step) {
          const std::uint32_t steps = step % 3 == 0 ? 1u : cap;
          std::uint32_t generations = 0u;
          if (!laneGeneration(lanes,
                              published,
                              spare,
                              *life,
                              mirror,
                              mirrorValid,
                              steps,
                              &generations) ||
              !advanceSerial(serial, *life, generations) ||
              gridHash(serial) != gridHash(*published)) {
            std::fprintf(stderr,
                         "Large lane world mismatch: size=%lld halo=%u "
                         "recut=%d step=%d\n",
                         static_cast<long long>(size),
                         haloLines,
                         recut ? 1 : 0,
                         step);
            return false;
          }
        }
        if (!recut && lanes.resynchronizations() != 1u) {
          std::puts("Steady lane blocks must not resynchronize");
          return false;
        }
        if (recut && lanes.rebalances() == 0u &&
            lanes.resynchronizations() != 1u) {
          std::puts("Rebalance accounting is inconsistent");
          return false;
        }
      }
    }
  }
  // Elementary rows wide enough to cross many column runs, including a torus
  // narrower than the row so it wraps, and a user edit below the active row.
  for (const char* id : { "RULE_90", "RULE_184" }) {
    std::unique_ptr<RuleSet> elementary =
      RuleSetRegistry::instance().createRuleSet(id);
    if (!elementary) {
      return false;
    }
    for (std::int64_t size : { std::int64_t{ 0 }, std::int64_t{ 12 } }) {
      for (std::uint32_t haloLines : { 1u, 2u }) {
        SparseCellGrid serial(size, size);
        std::uint32_t state = 777u;
        for (std::int64_t x = -1500; x < 1500; ++x) {
          state = state * 1664525u + 1013904223u;
          if ((state >> 24) < 100u) {
            serial.setCell({ x, 0 }, 0);
          }
        }
        LoopbackLanes transport(4);
        SimulationLaneCoordinator lanes(transport);
        lanes.setHaloLinesForTesting(haloLines);
        if (lanes.maximumBlockSteps(*elementary) != 1u) {
          std::puts("Elementary rows must run one generation per block");
          return false;
        }
        SparseCellGrid first(size, size);
        SparseCellGrid second(size, size);
        first.copyStateFrom(serial);
        SparseCellGrid* published = &first;
        SparseCellGrid* spare = &second;
        SparseGenerationDelta mirror;
        bool mirrorValid = false;
        for (int step = 0; step < 30; ++step) {
          if (step == 12) {
            published->setCell({ 40, -3 }, 0);
            serial.setCell({ 40, -3 }, 0);
            mirrorValid = false;
          }
          std::uint32_t generations = 0u;
          if (!laneGeneration(lanes,
                              published,
                              spare,
                              *elementary,
                              mirror,
                              mirrorValid,
                              4u,
                              &generations) ||
              generations != 1u ||
              !advanceSerial(serial, *elementary, generations) ||
              gridHash(serial) != gridHash(*published)) {
            std::fprintf(stderr,
                         "Elementary lane mismatch: rule=%s size=%lld "
                         "halo=%u step=%d\n",
                         id,
                         static_cast<long long>(size),
                         haloLines,
                         step);
            return false;
          }
        }
      }
    }
  }
  if (!serialBlocks()) {
    return false;
  }
  std::printf("%d rule/topology/lane-count combinations, large worlds, "
              "wide elementary rows and serial blocks match serial "
              "generations\n",
              partitioned);
  return partitioned > 0;
}

// CSL1 decoders and the lane worker reject malformed, stale, out-of-lane and
// out-of-rule input before touching state.
static bool
laneProtocol(const std::string& families, const std::string& rules)
{
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(families, rules)) {
    return false;
  }
  const RuleSetDefinition* definition =
    registry.getRuleSetDefinition("GAME_OF_LIFE");
  const RuleFamilyDefinition* family =
    definition == nullptr ? nullptr
                          : registry.getFamilyDefinition(definition->familyId);
  if (definition == nullptr || family == nullptr) {
    return false;
  }
  SimulationLaneRequest begin;
  begin.kind = SimulationLaneMessage::SyncBegin;
  begin.session = 3;
  begin.epoch = 1;
  // Lane 1 of 2 owns row 1 with a one-line halo (rows 0 and 2).
  begin.partition.lane = 1;
  begin.partition.laneCount = 2;
  begin.partition.haloLines = 1;
  begin.partition.beginUnbounded = false;
  begin.partition.ownedBegin = 1;
  begin.partition.endUnbounded = false;
  begin.partition.ownedEnd = 2;
  begin.ruleId = "GAME_OF_LIFE";
  begin.rulePackage =
    RuleSetRegistry::serializeRulePackage(*family, *definition);
  GuestWireWriter writer;
  begin.write(writer);
  const std::vector<std::byte> beginBytes = writer.take();
  SimulationLaneRequest decoded;
  if (!SimulationLaneRequest::read(beginBytes, decoded) ||
      decoded.partition.lane != 1 || decoded.ruleId != "GAME_OF_LIFE") {
    std::puts("SyncBegin round trip failed");
    return false;
  }
  const std::function<bool(const SimulationLaneRequest&)> rejected =
    [](const SimulationLaneRequest& request) {
      GuestWireWriter output;
      request.write(output);
      SimulationLaneRequest ignored;
      return !SimulationLaneRequest::read(output.data(), ignored);
    };
  SimulationLaneRequest invalid = begin;
  invalid.epoch = 0;
  bool denied = rejected(invalid);
  invalid = begin;
  invalid.session = 0;
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.partition.lane = 2;
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.partition.laneCount = 0;
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.partition.worldChunkWidth = 4; // mixed topology
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.ruleId.clear();
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.partition.haloLines = 0;
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.partition.haloLines = SimulationLanePartition::MaximumHaloLines + 1u;
  denied = denied && rejected(invalid);
  invalid = begin;
  invalid.partition.ownedBegin = 3; // an inverted run
  denied = denied && rejected(invalid);
  std::vector<std::byte> truncated = beginBytes;
  truncated.pop_back();
  std::vector<std::byte> trailing = beginBytes;
  trailing.push_back(std::byte{ 0 });
  std::vector<std::byte> badMagic = beginBytes;
  badMagic[0] = std::byte{ 0 };
  SimulationLaneRequest ignored;
  denied = denied && !SimulationLaneRequest::read(truncated, ignored) &&
           !SimulationLaneRequest::read(trailing, ignored) &&
           !SimulationLaneRequest::read(badMagic, ignored);
  if (!denied) {
    std::puts("Malformed lane requests were accepted");
    return false;
  }

  SimulationLaneWorker worker;
  std::vector<std::byte> output;
  std::string error;
  SimulationLaneRequest advance;
  advance.kind = SimulationLaneMessage::Advance;
  advance.session = 3;
  advance.epoch = 1;
  GuestWireWriter advanceWriter;
  advance.write(advanceWriter);
  if (worker.execute(advanceWriter.data(), output, error)) {
    std::puts("A lane advanced before synchronization");
    return false;
  }
  if (!worker.execute(beginBytes, output, error)) {
    std::fprintf(stderr, "SyncBegin failed: %s\n", error.c_str());
    return false;
  }
  // The lane owns row 1; rows 0 and 2 are its halo.
  SparseChunkPatch owned;
  owned.address = { 0, 1 };
  owned.present = true;
  owned.cells.fill(SparseCellGrid::BackgroundState);
  owned.cells[17] = 0;
  const std::function<bool(SimulationLaneRequest)> accepted =
    [&](SimulationLaneRequest request) {
      GuestWireWriter output2;
      request.write(output2);
      return worker.execute(output2.data(), output, error);
    };
  SimulationLaneRequest chunks = advance;
  chunks.kind = SimulationLaneMessage::SyncChunks;
  chunks.patches = { owned };
  SimulationLaneRequest stale = chunks;
  stale.epoch = 2;
  SimulationLaneRequest badState = chunks;
  badState.patches[0].cells[0] = 200;
  SimulationLaneRequest wrongGeneration = advance;
  wrongGeneration.generation = 5;
  SimulationLaneRequest duplicate = advance;
  duplicate.patches = { owned, owned };
  // A one-line halo keeps Life exact for 16 generations, not 17.
  SimulationLaneRequest tooLong = advance;
  tooLong.steps = 17u;
  if (!accepted(chunks) || accepted(stale) || accepted(badState) ||
      accepted(wrongGeneration) || accepted(duplicate) || accepted(tooLong)) {
    std::puts("Lane worker accepted stale, invalid or duplicate input");
    return false;
  }
  // Lane 1 of 3 owning rows 2-3 has halo rows 1 and 4; row 5 is outside.
  SimulationLaneWorker narrow;
  begin.partition.laneCount = 3;
  begin.partition.ownedBegin = 2;
  begin.partition.ownedEnd = 4;
  writer.clear();
  begin.write(writer);
  if (!narrow.execute(writer.data(), output, error)) {
    return false;
  }
  SimulationLaneRequest outside = chunks;
  outside.patches[0].address.y = 5;
  GuestWireWriter outsideWriter;
  outside.write(outsideWriter);
  if (narrow.execute(outsideWriter.data(), output, error)) {
    std::puts("A lane accepted a patch outside its rows");
    return false;
  }
  SimulationLaneReply reply;
  reply.kind = SimulationLaneMessage::Ack;
  reply.session = 3;
  reply.epoch = 1;
  reply.changes = { owned };
  GuestWireWriter replyWriter;
  reply.write(replyWriter);
  SimulationLaneReply decodedReply;
  if (SimulationLaneReply::read(replyWriter.data(), decodedReply)) {
    std::puts("An acknowledgement carried changes");
    return false;
  }
  // The partition axis and the elementary source row round-trip,
  // and a worker refuses an axis or a row that does not match its rule.
  SimulationLaneRequest columns = begin;
  columns.partition.axis = SimulationLanePartition::Axis::Columns;
  writer.clear();
  columns.write(writer);
  SimulationLaneRequest decodedColumns;
  SimulationLaneWorker mismatched;
  if (!SimulationLaneRequest::read(writer.data(), decodedColumns) ||
      decodedColumns.partition.axis != SimulationLanePartition::Axis::Columns ||
      mismatched.execute(writer.data(), output, error)) {
    std::puts("A Life rule accepted a column partition");
    return false;
  }
  SimulationLaneRequest rowAdvance = advance;
  rowAdvance.hasElementaryRow = true;
  rowAdvance.elementaryRow = SparseElementaryRow{ true, -9, -4, 12 };
  GuestWireWriter rowWriter;
  rowAdvance.write(rowWriter);
  SimulationLaneRequest decodedRow;
  if (!SimulationLaneRequest::read(rowWriter.data(), decodedRow) ||
      !decodedRow.hasElementaryRow || !decodedRow.elementaryRow.found ||
      decodedRow.elementaryRow.sourceY != -9 ||
      decodedRow.elementaryRow.minX != -4 ||
      decodedRow.elementaryRow.maxX != 12) {
    std::puts("An elementary source row did not round-trip");
    return false;
  }
  SimulationLaneRequest backwardsRow = rowAdvance;
  backwardsRow.elementaryRow.minX = 13;
  rowWriter.clear();
  backwardsRow.write(rowWriter);
  if (SimulationLaneRequest::read(rowWriter.data(), decodedRow)) {
    std::puts("An inverted elementary row was accepted");
    return false;
  }
  SimulationLaneWorker lifeLane;
  writer.clear();
  begin.partition.laneCount = 2;
  begin.partition.ownedBegin = 1;
  begin.partition.ownedEnd = 2;
  begin.write(writer);
  rowWriter.clear();
  rowAdvance.write(rowWriter);
  if (!lifeLane.execute(writer.data(), output, error) ||
      lifeLane.execute(rowWriter.data(), output, error)) {
    std::puts("A Life lane accepted an elementary source row");
    return false;
  }
  SimulationLaneReply rowReply;
  rowReply.kind = SimulationLaneMessage::Advanced;
  rowReply.session = 3;
  rowReply.epoch = 1;
  rowReply.hasElementaryRow = true;
  rowReply.elementaryRow = SparseElementaryRow{ true, 5, 0, 0 };
  GuestWireWriter rowReplyWriter;
  rowReply.write(rowReplyWriter);
  SimulationLaneReply decodedRowReply;
  std::vector<std::byte> oldVersion = rowReplyWriter.data();
  oldVersion[4] = std::byte{ 2 };
  if (!SimulationLaneReply::read(rowReplyWriter.data(), decodedRowReply) ||
      !decodedRowReply.hasElementaryRow ||
      decodedRowReply.elementaryRow.sourceY != 5 ||
      SimulationLaneReply::read(oldVersion, decodedRowReply)) {
    std::puts("Elementary replies did not round-trip or version 2 was read");
    return false;
  }

  // Informational: cost of building a merge delta for 1,024 changed chunks.
  SparseCellGrid timed;
  std::vector<SparseChunkPatch> patches;
  for (std::int64_t chunk = 0; chunk < 1024; ++chunk) {
    SparseChunkPatch patch;
    patch.address = { chunk % 32, chunk / 32 };
    patch.present = true;
    patch.cells.fill(SparseCellGrid::BackgroundState);
    patch.cells[static_cast<std::size_t>(chunk % 256)] = 0;
    patches.push_back(patch);
    timed.setCell({ patch.address.x * 16, patch.address.y * 16 }, 0);
  }
  SparseGenerationDelta built;
  const std::chrono::steady_clock::time_point timing =
    std::chrono::steady_clock::now();
  for (int repeat = 0; repeat < 20; ++repeat) {
    timed.buildPatchDelta(patches, &built);
  }
  std::printf("BENCH: buildPatchDelta 1024 chunks %.3f ms\n",
              std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - timing)
                  .count() /
                20.0);
  std::puts("Lane protocol decoding and worker rejection cases: PASS");
  return true;
}

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

// Warmed lane generations allocate (almost) nothing on the control thread:
// the coordinator, the lane workers and grid delta application reuse
// retained scratch and recycled chunk nodes
// (docs/plans/hot-loop-allocations.md).
static bool
laneAllocations(const std::string& families, const std::string& rules)
{
  DomainFixture reference;
  if (!reference.initialize(families, rules)) {
    return false;
  }
  std::unique_ptr<RuleSet> life =
    RuleSetRegistry::instance().createRuleSet("GAME_OF_LIFE");
  if (!life) {
    return false;
  }
  // A finite torus keeps the chunk count bounded, so steady generations
  // need no new chunk nodes.
  const std::int64_t size = 12;
  SparseCellGrid first(size, size);
  SparseCellGrid second(size, size);
  std::uint32_t state = 12345u;
  for (std::int64_t y = -96; y < 96; ++y) {
    for (std::int64_t x = -96; x < 96; ++x) {
      state = state * 1664525u + 1013904223u;
      if ((state >> 24) < 90u) {
        first.setCell({ x, y }, 0);
      }
    }
  }
  LoopbackLanes transport(4);
  SimulationLaneCoordinator lanes(transport);
  SparseCellGrid* published = &first;
  SparseCellGrid* spare = &second;
  SparseGenerationDelta mirror;
  SparseGenerationDelta delta;
  bool mirrorValid = false;
  const std::function<bool()> generation = [&]() {
    if (!startLanes(lanes, published, spare, *life, mirror, mirrorValid)) {
      return false;
    }
    for (int attempt = 0; attempt < 10000; ++attempt) {
      SparseCellGrid* completed = nullptr;
      bool succeeded = false;
      if (lanes.poll(&completed, &delta, nullptr, &succeeded, nullptr)) {
        if (!succeeded || completed != spare) {
          return false;
        }
        std::swap(published, spare);
        std::swap(mirror, delta);
        mirrorValid = true;
        return true;
      }
      if (lanes.failed()) {
        return false;
      }
    }
    return false;
  };
  for (int warmup = 0; warmup < 60; ++warmup) {
    if (!generation()) {
      std::fprintf(stderr, "Lane warm-up generation failed\n");
      return false;
    }
  }
  const int generations = 60;
  bool advanced = true;
  g_allocations = 0;
  g_countAllocations = true;
  for (int step = 0; step < generations && advanced; ++step) {
    advanced = generation();
  }
  g_countAllocations = false;
  std::printf("Heap allocations over %d warmed lane generations: %zu\n",
              generations,
              g_allocations);
  // Before retained lane scratch this was about 667 per generation (40,009
  // over 60). What remains is high-water growth of retained buffers; one
  // allocation per lane per generation would already count 240.
  if (g_allocations >= 30u) {
    std::fprintf(stderr, "Warmed lane generations still allocate\n");
    return false;
  }
  return advanced;
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
    std::puts("IllumoGame.Wasm.WorkerParity\nIllumoGame.Wasm.SimulationProtocol"
              "\nIllumoGame.Wasm.LaneParity\nIllumoGame.Wasm.LaneProtocol"
              "\nIllumoGame.Wasm.LaneAllocations");
    return 0;
  }
  const std::string name = argc == 3 ? argv[2] : "";
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      (name != "IllumoGame.Wasm.WorkerParity" &&
       name != "IllumoGame.Wasm.SimulationProtocol" &&
       name != "IllumoGame.Wasm.LaneParity" &&
       name != "IllumoGame.Wasm.LaneProtocol" &&
       name != "IllumoGame.Wasm.LaneAllocations")) {
    return 2;
  }
  const std::string families = readText(ILLUMO_FAMILIES);
  const std::string rules = readText(ILLUMO_RULES);
  if (name == "IllumoGame.Wasm.LaneAllocations") {
    return laneAllocations(families, rules) ? 0 : 1;
  }
  if (name == "IllumoGame.Wasm.SimulationProtocol") {
    return protocolCases(families, rules) ? 0 : 1;
  }
  if (name == "IllumoGame.Wasm.LaneParity") {
    return laneParity(families, rules) ? 0 : 1;
  }
  if (name == "IllumoGame.Wasm.LaneProtocol") {
    return laneProtocol(families, rules) ? 0 : 1;
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
