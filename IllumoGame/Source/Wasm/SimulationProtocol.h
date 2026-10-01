#pragma once

#include "Game/SparseCellGrid.h"
#include <IllumoGuest/Wire.h>
#include <memory>

class SparseCellGrid;
class RuleSet;

struct SimulationRequest
{
  static constexpr std::uint32_t Magic = 0x31575343u; // CSW1
  bool synchronize = false;
  std::uint64_t session = 0;
  std::uint64_t worldEpoch = 0;
  std::uint64_t ruleEpoch = 0;
  std::uint64_t operation = 0;
  std::uint64_t generation = 0;
  std::uint64_t revision = 0;
  std::string families;
  std::string rules;
  std::string snapshot;

  void write(GuestWireWriter& output) const;
  static bool read(std::span<const std::byte> bytes, SimulationRequest& output);
};

struct SimulationReply
{
  enum class Content : std::uint32_t
  {
    Unchanged,
    Chunks,
    Snapshot
  };
  std::uint64_t session = 0;
  std::uint64_t worldEpoch = 0;
  std::uint64_t ruleEpoch = 0;
  std::uint64_t operation = 0;
  std::uint64_t baseGeneration = 0;
  std::uint64_t baseRevision = 0;
  std::uint64_t generation = 0;
  std::uint64_t revision = 0;
  bool changed = false;
  Content content = Content::Unchanged;
  std::vector<SparseChangedChunkRecord> chunks;
  std::string snapshot;

  void write(GuestWireWriter& output) const;
  static bool read(std::span<const std::byte> bytes,
                   const SimulationRequest& expected,
                   SimulationReply& output);
  // Constructs a private replacement. Publication remains an owning-pointer
  // swap in the control guest after host request ID and epoch checks.
  std::unique_ptr<SparseCellGrid> restore(const SparseCellGrid& base,
                                          const RuleSet& rule,
                                          std::string* error = nullptr) const;
};

// Compiled into worker.wasm. Native code only schedules opaque bytes.
// Full snapshots establish correctness; incremental/streamed transfers are a
// separate performance gate before the production simulator can cut over.
class SimulationGuestWorker
{
public:
  SimulationGuestWorker();
  ~SimulationGuestWorker();
  SimulationGuestWorker(const SimulationGuestWorker&) = delete;
  SimulationGuestWorker& operator=(const SimulationGuestWorker&) = delete;
  SimulationGuestWorker(SimulationGuestWorker&&) = delete;
  SimulationGuestWorker& operator=(SimulationGuestWorker&&) = delete;
  bool execute(std::span<const std::byte> request,
               std::vector<std::byte>& reply,
               std::string& error);

private:
  std::unique_ptr<SparseCellGrid> m_grid;
  std::unique_ptr<RuleSet> m_rule;
  std::uint64_t m_session = 0;
  std::uint64_t m_worldEpoch = 0;
  std::uint64_t m_ruleEpoch = 0;
  std::uint64_t m_operation = 0;
  std::uint64_t m_generation = 0;
};
