#pragma once

#include "Game/SimulationLaneTransport.h"
#include "Game/SimulationRunner.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Foundation/RollingMetric.h>
#include <IllumoGuest/Wire.h>
#include <chrono>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

class RuleSet;

// Band partition of the chunk plane across simulation lanes. A lane owns every
// chunk line (a row, or a column for elementary 1D rules, whose single active
// row spans columns) whose band maps to it and keeps a one-line halo around
// its bands. Its owned lines depend only on owned and halo lines, so it
// advances them exactly; everything else it computes is discarded.
struct SimulationLanePartition
{
  enum class Axis : std::uint32_t
  {
    Rows = 0,
    Columns = 1
  };
  std::uint32_t lane = 0;
  std::uint32_t laneCount = 1;
  // Band height in chunk lines (rows or columns).
  std::uint32_t bandRows = 8;
  std::int64_t worldChunkWidth = 0;
  std::int64_t worldChunkHeight = 0;
  Axis axis = Axis::Rows;

  // The chunk line a chunk address lies on along the partition axis.
  std::int64_t lineOf(const ChunkAddress& address) const
  {
    return axis == Axis::Columns ? address.x : address.y;
  }
  std::int64_t canonicalRow(std::int64_t row) const;
  std::uint32_t ownerOf(std::int64_t row) const;
  bool owns(std::int64_t row) const { return ownerOf(row) == lane; }
  bool isHalo(std::int64_t row) const;
  bool isRelevant(std::int64_t row) const { return owns(row) || isHalo(row); }
  bool ownsChunk(const ChunkAddress& address) const
  {
    return owns(lineOf(address));
  }
  bool isHaloChunk(const ChunkAddress& address) const
  {
    return isHalo(lineOf(address));
  }
  bool isRelevantChunk(const ChunkAddress& address) const
  {
    return isRelevant(lineOf(address));
  }
  bool valid() const;
};

// Lane protocol "CSL1". Every message carries the coordinator session and
// epoch; a reply whose epoch no longer matches is discarded unread.
enum class SimulationLaneMessage : std::uint32_t
{
  SyncBegin = 1,
  SyncChunks = 2,
  Advance = 3,
  Ack = 4,
  Advanced = 5,
  Overflow = 6
};

struct SimulationLaneRequest
{
  static constexpr std::uint32_t Magic = 0x314C5343u; // CSL1
  // 2: SyncBegin carries the partition axis; Advance and Advanced carry the
  // elementary 1D source row.
  static constexpr std::uint32_t Version = 2u;
  static constexpr std::uint32_t MaximumPatches = 60000u;
  SimulationLaneMessage kind = SimulationLaneMessage::Advance;
  std::uint64_t session = 0;
  std::uint64_t epoch = 0;
  std::uint64_t generation = 0;
  // SyncBegin only.
  SimulationLanePartition partition;
  std::string ruleId;
  std::string rulePackage;
  // SyncChunks and Advance: owned or halo chunks to replace.
  std::vector<SparseChunkPatch> patches;
  // Advance of an elementary 1D rule: the global source row. A lane sees only
  // its columns, so the coordinator supplies it.
  bool hasElementaryRow = false;
  SparseElementaryRow elementaryRow;

  void write(GuestWireWriter& output) const;
  static bool read(std::span<const std::byte> bytes,
                   SimulationLaneRequest& output);
};

struct SimulationLaneReply
{
  SimulationLaneMessage kind = SimulationLaneMessage::Ack;
  std::uint64_t session = 0;
  std::uint64_t epoch = 0;
  std::uint64_t generation = 0;
  double advanceMilliseconds = 0.0;
  // Diagnostics: applying patches and collecting owned changes.
  double patchMilliseconds = 0.0;
  double collectMilliseconds = 0.0;
  // Advanced: every owned chunk the generation changed.
  std::vector<SparseChunkPatch> changes;
  // Advanced, elementary 1D: the source row found in this lane's owned
  // columns after the generation; the coordinator merges every lane's.
  bool hasElementaryRow = false;
  SparseElementaryRow elementaryRow;

  void write(GuestWireWriter& output) const;
  static bool read(std::span<const std::byte> bytes,
                   SimulationLaneReply& output);
};

// One lane's state, compiled into the worker store. The grid holds exactly
// the owned and halo rows at the generation the coordinator last sent.
class SimulationLaneWorker
{
public:
  SimulationLaneWorker() = default;
  ~SimulationLaneWorker() = default;
  SimulationLaneWorker(const SimulationLaneWorker&) = delete;
  SimulationLaneWorker& operator=(const SimulationLaneWorker&) = delete;
  SimulationLaneWorker(SimulationLaneWorker&&) = delete;
  SimulationLaneWorker& operator=(SimulationLaneWorker&&) = delete;

  bool execute(std::span<const std::byte> request,
               std::vector<std::byte>& reply,
               std::string& error);

private:
  bool advance(const SimulationLaneRequest& request,
               SimulationLaneReply& reply,
               std::string& error);

  RuleSetRegistry m_registry;
  std::unique_ptr<RuleSet> m_rule;
  std::unique_ptr<SparseCellGrid> m_grid;
  SimulationLanePartition m_partition;
  std::uint64_t m_session = 0;
  std::uint64_t m_epoch = 0;
  std::uint64_t m_generation = 0;
  bool m_synced = false;
  // Halo and outside chunks the last generation changed, with the contents
  // they must return to (their pre-generation values, or removal).
  std::vector<SparseChunkPatch> m_restore;
};

// Control-side scheduler: keeps lanes synchronized with the published grid,
// fans one generation out, and merges the owned-row replies into the spare
// grid as an exact one-revision delta. Never blocks; unavailable, failed or
// oversized work reports so the runner can fall back to serial generations.
class SimulationLaneCoordinator
{
public:
  explicit SimulationLaneCoordinator(SimulationLaneTransport& transport);
  ~SimulationLaneCoordinator() = default;
  SimulationLaneCoordinator(const SimulationLaneCoordinator&) = delete;
  SimulationLaneCoordinator& operator=(const SimulationLaneCoordinator&) =
    delete;
  SimulationLaneCoordinator(SimulationLaneCoordinator&&) = delete;
  SimulationLaneCoordinator& operator=(SimulationLaneCoordinator&&) = delete;

  enum class Availability
  {
    Pending,
    Available,
    Unavailable
  };
  // Lanes can take this generation (rule family, topology and grant).
  Availability availability(const RuleSet& rule);
  // Starts one generation of `published` into `working`. `working` is first
  // brought to the published state (mirror delta or copy). False leaves no
  // work outstanding; the caller then runs the generation itself.
  bool start(SparseCellGrid* working,
             const SparseCellGrid* published,
             const RuleSet* rule,
             SparseGenerationDelta&& mirrorDelta,
             bool useMirrorDelta);
  // True once the outstanding generation completed; the working grid then
  // holds it and `delta` is its exact one-revision change.
  bool poll(SparseCellGrid** completedGrid,
            SparseGenerationDelta* delta,
            double* elapsedMilliseconds,
            bool* advanceSucceeded,
            SimulationRunnerTimings* timings);
  // Lane requests are still outstanding (current or retired work).
  bool busy() const;
  // Discards the outstanding generation; its replies are dropped on arrival
  // and every lane resynchronizes before the next generation.
  void retire();
  // A lane failed or a generation overflowed: lanes stay off.
  bool failed() const { return m_failed; }
  const std::string& failure() const { return m_failure; }
  std::uint32_t lanes() const { return m_laneCount; }
  const RollingMetric& roundTripMetric() const { return m_roundTrip; }
  const RollingMetric& laneAdvanceMetric() const { return m_laneAdvance; }
  const RollingMetric& lanePatchMetric() const { return m_lanePatch; }
  const RollingMetric& laneCollectMetric() const { return m_laneCollect; }
  const RollingMetric& mergeMetric() const { return m_merge; }
  const RollingMetric& mergeBuildMetric() const { return m_mergeBuild; }
  // Sum of every lane's patch and advance time per generation: the work a
  // serial generation would roughly have done in this store.
  const RollingMetric& laneWorkMetric() const { return m_laneWork; }
  std::uint64_t resynchronizations() const { return m_resyncs; }
  std::uint64_t retirements() const { return m_retirements; }
  // Band height in chunk rows (default 8); takes effect at the next resync.
  void setBandRowsForTesting(std::uint32_t rows)
  {
    m_bandRows = rows;
    m_synced = false;
  }

private:
  struct Lane
  {
    std::deque<std::vector<std::byte>> queue;
    bool awaitingAdvance = false;
    bool outstanding = false;
    std::vector<SparseChunkPatch> haloUpdates;
    SimulationLaneReply reply;
    bool replied = false;
  };
  void fail(std::string reason);
  bool queueSync(const SparseCellGrid& published, const RuleSet& rule);
  // Queues the next Advance on every lane with its pending halo updates.
  void queueAdvance();
  bool elementary() const
  {
    return m_axis == SimulationLanePartition::Axis::Columns;
  }
  // Once every lane answered the in-flight generation: keeps the owned
  // changes for the merge, derives halos and launches the next generation.
  void collectReplies();
  bool lanesOutstanding() const;
  void pump();
  SimulationLanePartition partitionFor(std::uint32_t lane) const;

  SimulationLaneTransport& m_transport;
  std::vector<Lane> m_lanes;
  std::uint32_t m_laneCount = 0;
  std::uint32_t m_bandRows = 8;
  SimulationLanePartition::Axis m_axis = SimulationLanePartition::Axis::Rows;
  // Elementary 1D: the source row of the generation queued next.
  SparseElementaryRow m_nextRow;
  std::uint64_t m_session = 0;
  std::uint64_t m_epoch = 0;
  std::uint64_t m_generation = 0;
  // The published state lanes mirror: revision, rule and topology.
  bool m_synced = false;
  std::uint64_t m_syncedRevision = 0;
  std::string m_syncedRule;
  std::int64_t m_syncedWidth = 0;
  std::int64_t m_syncedHeight = 0;
  bool m_inFlight = false;
  // Pipelining: lanes already compute the generation after the one last
  // returned, from the halo updates alone, while the control store merges
  // and publishes. start() adopts it when the published world still matches.
  bool m_ahead = false;
  // All replies of the in-flight generation arrived and the next one was
  // launched; the merge runs on the next poll, after those jobs have left
  // this single-threaded store.
  bool m_mergeReady = false;
  std::vector<SparseChunkPatch> m_merged;
  double m_slowestAdvance = 0.0;
  double m_slowestPatch = 0.0;
  double m_slowestCollect = 0.0;
  std::chrono::steady_clock::time_point m_lastPoll{};
  SparseCellGrid* m_working = nullptr;
  const SparseCellGrid* m_published = nullptr;
  std::uint64_t m_startMicroseconds = 0;
  double m_mirrorMilliseconds = 0.0;
  bool m_usedMirror = false;
  bool m_usedCopy = false;
  bool m_failed = false;
  std::string m_failure;
  RollingMetric m_roundTrip;
  RollingMetric m_laneAdvance;
  RollingMetric m_lanePatch;
  RollingMetric m_laneCollect;
  RollingMetric m_merge;
  RollingMetric m_mergeBuild;
  RollingMetric m_laneWork;
  double m_totalLaneWork = 0.0;
  std::uint64_t m_resyncs = 0;
  std::uint64_t m_retirements = 0;
};
