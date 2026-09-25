#include "SimulationLanes.h"
#include "Rulesets/RuleSet.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// Largest neighborhood radius a one-chunk-line halo can serve.
static constexpr unsigned int kMaximumLaneRadius = 16u;
// Sync patches per message: about 2.2 MiB, so several lanes' parts fit one
// service exchange.
static constexpr std::size_t kSyncPatchesPerMessage = 8192u;

static double
millisecondsBetween(std::chrono::steady_clock::time_point start,
                    std::chrono::steady_clock::time_point end)
{
  return std::chrono::duration<double, std::milli>(end - start).count();
}

std::int64_t
SimulationLanePartition::canonicalRow(std::int64_t row) const
{
  if (worldChunkWidth <= 0 || worldChunkHeight <= 0) {
    return row;
  }
  // Matches SparseCellGrid::canonicalizeChunk for the partition axis.
  const std::int64_t extent =
    axis == Axis::Columns ? worldChunkWidth : worldChunkHeight;
  const std::int64_t minimum = -(extent / 2);
  const std::int64_t offset =
    SparseCellGrid::floorModulo(SparseCellGrid::floorModulo(row, extent) -
                                  SparseCellGrid::floorModulo(minimum, extent),
                                extent);
  return minimum + offset;
}

std::uint32_t
SimulationLanePartition::ownerOf(std::int64_t row) const
{
  const std::int64_t band =
    SparseCellGrid::floorDivide(canonicalRow(row), bandRows);
  return static_cast<std::uint32_t>(
    SparseCellGrid::floorModulo(band, laneCount));
}

bool
SimulationLanePartition::isHalo(std::int64_t row) const
{
  const std::int64_t canonical = canonicalRow(row);
  return !owns(canonical) && (owns(canonicalRow(canonical - 1)) ||
                              owns(canonicalRow(canonical + 1)));
}

bool
SimulationLanePartition::valid() const
{
  return laneCount >= 1u && laneCount <= 64u && lane < laneCount &&
         bandRows >= 1u && bandRows <= 1024u &&
         (axis == Axis::Rows || axis == Axis::Columns) &&
         SparseCellGrid::isValidTopology(worldChunkWidth, worldChunkHeight);
}

static void
writeElementaryRow(GuestWireWriter& output,
                   bool present,
                   const SparseElementaryRow& row)
{
  output.u32(!present ? 0u : (row.found ? 2u : 1u));
  if (present && row.found) {
    output.u64(std::bit_cast<std::uint64_t>(row.sourceY));
    output.u64(std::bit_cast<std::uint64_t>(row.minX));
    output.u64(std::bit_cast<std::uint64_t>(row.maxX));
  }
}

static bool
readElementaryRow(GuestWireReader& reader,
                  bool& present,
                  SparseElementaryRow& row)
{
  const std::uint32_t flag = reader.u32();
  if (!reader.valid() || flag > 2u) {
    return false;
  }
  present = flag != 0u;
  row = SparseElementaryRow{};
  row.found = flag == 2u;
  if (row.found) {
    row.sourceY = std::bit_cast<std::int64_t>(reader.u64());
    row.minX = std::bit_cast<std::int64_t>(reader.u64());
    row.maxX = std::bit_cast<std::int64_t>(reader.u64());
    if (!reader.valid() || row.minX > row.maxX) {
      return false;
    }
  }
  return true;
}

static void
writePatches(GuestWireWriter& output,
             const std::vector<SparseChunkPatch>& patches)
{
  output.u32(static_cast<std::uint32_t>(patches.size()));
  for (const SparseChunkPatch& patch : patches) {
    output.u64(std::bit_cast<std::uint64_t>(patch.address.x));
    output.u64(std::bit_cast<std::uint64_t>(patch.address.y));
    output.u32(patch.present ? 1u : 0u);
    if (patch.present) {
      output.bytes(std::as_bytes(std::span(patch.cells)));
    }
  }
}

static bool
readPatches(GuestWireReader& reader, std::vector<SparseChunkPatch>& patches)
{
  const std::uint32_t count = reader.u32();
  // Minimum encoded patch: two coordinates and the presence flag.
  if (!reader.valid() || count > SimulationLaneRequest::MaximumPatches ||
      count > reader.remaining() / 20u) {
    return false;
  }
  patches.resize(count);
  for (SparseChunkPatch& patch : patches) {
    patch.address.x = std::bit_cast<std::int64_t>(reader.u64());
    patch.address.y = std::bit_cast<std::int64_t>(reader.u64());
    const std::uint32_t present = reader.u32();
    if (!reader.valid() || present > 1u ||
        patch.address.x < std::numeric_limits<std::int64_t>::min() / 16 ||
        patch.address.x > std::numeric_limits<std::int64_t>::max() / 16 ||
        patch.address.y < std::numeric_limits<std::int64_t>::min() / 16 ||
        patch.address.y > std::numeric_limits<std::int64_t>::max() / 16) {
      return false;
    }
    patch.present = present == 1u;
    patch.cells.fill(SparseCellGrid::BackgroundState);
    if (patch.present) {
      const std::span<const std::byte> cells = reader.bytes(patch.cells.size());
      if (!reader.valid()) {
        return false;
      }
      std::memcpy(patch.cells.data(), cells.data(), cells.size());
    }
  }
  return true;
}

void
SimulationLaneRequest::write(GuestWireWriter& output) const
{
  output.u32(Magic);
  output.u32(Version);
  output.u32(static_cast<std::uint32_t>(kind));
  output.u64(session);
  output.u64(epoch);
  output.u64(generation);
  if (kind == SimulationLaneMessage::SyncBegin) {
    output.u32(partition.lane);
    output.u32(partition.laneCount);
    output.u32(partition.bandRows);
    output.u64(std::bit_cast<std::uint64_t>(partition.worldChunkWidth));
    output.u64(std::bit_cast<std::uint64_t>(partition.worldChunkHeight));
    output.u32(static_cast<std::uint32_t>(partition.axis));
    output.text(ruleId);
    output.text(rulePackage);
    return;
  }
  writePatches(output, patches);
  if (kind == SimulationLaneMessage::Advance) {
    writeElementaryRow(output, hasElementaryRow, elementaryRow);
  }
}

bool
SimulationLaneRequest::read(std::span<const std::byte> bytes,
                            SimulationLaneRequest& output)
{
  GuestWireReader reader(bytes);
  SimulationLaneRequest request;
  const std::uint32_t magic = reader.u32();
  const std::uint32_t version = reader.u32();
  const std::uint32_t kind = reader.u32();
  request.session = reader.u64();
  request.epoch = reader.u64();
  request.generation = reader.u64();
  if (!reader.valid() || magic != Magic || version != Version || kind < 1 ||
      kind > 3 || request.session == 0 || request.epoch == 0 ||
      request.generation == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  request.kind = static_cast<SimulationLaneMessage>(kind);
  if (request.kind == SimulationLaneMessage::SyncBegin) {
    request.partition.lane = reader.u32();
    request.partition.laneCount = reader.u32();
    request.partition.bandRows = reader.u32();
    request.partition.worldChunkWidth =
      std::bit_cast<std::int64_t>(reader.u64());
    request.partition.worldChunkHeight =
      std::bit_cast<std::int64_t>(reader.u64());
    const std::uint32_t axis = reader.u32();
    if (axis > 1u) {
      return false;
    }
    request.partition.axis = static_cast<SimulationLanePartition::Axis>(axis);
    request.ruleId = reader.text(256);
    request.rulePackage = reader.text(1024u * 1024u);
    if (!reader.finished() || !request.partition.valid() ||
        request.ruleId.empty() || request.rulePackage.empty()) {
      return false;
    }
  } else if (!readPatches(reader, request.patches) ||
             (request.kind == SimulationLaneMessage::Advance &&
              !readElementaryRow(
                reader, request.hasElementaryRow, request.elementaryRow)) ||
             !reader.finished()) {
    return false;
  }
  output = std::move(request);
  return true;
}

void
SimulationLaneReply::write(GuestWireWriter& output) const
{
  output.u32(SimulationLaneRequest::Magic);
  output.u32(SimulationLaneRequest::Version);
  output.u32(static_cast<std::uint32_t>(kind));
  output.u64(session);
  output.u64(epoch);
  output.u64(generation);
  output.f64(advanceMilliseconds);
  output.f64(patchMilliseconds);
  output.f64(collectMilliseconds);
  writePatches(output, changes);
  if (kind == SimulationLaneMessage::Advanced) {
    writeElementaryRow(output, hasElementaryRow, elementaryRow);
  }
}

bool
SimulationLaneReply::read(std::span<const std::byte> bytes,
                          SimulationLaneReply& output)
{
  GuestWireReader reader(bytes);
  SimulationLaneReply reply;
  const std::uint32_t magic = reader.u32();
  const std::uint32_t version = reader.u32();
  const std::uint32_t kind = reader.u32();
  reply.session = reader.u64();
  reply.epoch = reader.u64();
  reply.generation = reader.u64();
  reply.advanceMilliseconds = reader.f64();
  reply.patchMilliseconds = reader.f64();
  reply.collectMilliseconds = reader.f64();
  if (!reader.valid() || magic != SimulationLaneRequest::Magic ||
      version != SimulationLaneRequest::Version || kind < 4 || kind > 6 ||
      !readPatches(reader, reply.changes) ||
      (kind == 5 && !readElementaryRow(
                      reader, reply.hasElementaryRow, reply.elementaryRow)) ||
      !reader.finished() || !(reply.advanceMilliseconds >= 0.0) ||
      !(reply.patchMilliseconds >= 0.0) ||
      !(reply.collectMilliseconds >= 0.0) ||
      (kind != 5 && !reply.changes.empty())) {
    return false;
  }
  reply.kind = static_cast<SimulationLaneMessage>(kind);
  output = std::move(reply);
  return true;
}

static bool
isElementary(const RuleSet& rule)
{
  return rule.getNeighborhoodKind() == RuleSet::NeighborhoodKind::Elementary1D;
}

// Every rule within the halo radius partitions: elementary 1D rules by chunk
// column (their one active row spans columns), all others by chunk row.
static bool
supportsLanes(const RuleSet& rule)
{
  return rule.getNeighborhoodRadius() <= kMaximumLaneRadius;
}

bool
SimulationLaneWorker::execute(std::span<const std::byte> bytes,
                              std::vector<std::byte>& output,
                              std::string& error)
try {
  output.clear();
  error.clear();
  SimulationLaneRequest request;
  if (!SimulationLaneRequest::read(bytes, request)) {
    error = "Malformed simulation lane request";
    return false;
  }
  SimulationLaneReply reply;
  reply.kind = SimulationLaneMessage::Ack;
  reply.session = request.session;
  reply.epoch = request.epoch;
  reply.generation = request.generation;
  if (request.kind == SimulationLaneMessage::SyncBegin) {
    RuleSetRegistry registry;
    if (!registry.loadRulePackage(request.rulePackage)) {
      error = "Invalid lane rule package";
      return false;
    }
    std::unique_ptr<RuleSet> rule = registry.createRuleSet(request.ruleId);
    if (!rule || !supportsLanes(*rule) ||
        isElementary(*rule) !=
          (request.partition.axis == SimulationLanePartition::Axis::Columns)) {
      error = "Lane rule is missing or not partitionable";
      return false;
    }
    m_grid = std::make_unique<SparseCellGrid>(
      request.partition.worldChunkWidth, request.partition.worldChunkHeight);
    m_registry = std::move(registry);
    m_rule = std::move(rule);
    m_partition = request.partition;
    m_session = request.session;
    m_epoch = request.epoch;
    m_generation = request.generation;
    m_restore.clear();
    m_synced = true;
  } else {
    if (!m_synced || request.session != m_session || request.epoch != m_epoch ||
        request.generation != m_generation) {
      error = "Simulation lane requires resynchronization";
      return false;
    }
    for (const SparseChunkPatch& patch : request.patches) {
      if (!(m_grid->canonicalizeChunk(patch.address) == patch.address) ||
          !m_partition.isRelevantChunk(patch.address)) {
        error = "Lane patch outside its owned and halo rows";
        return false;
      }
      if (patch.present) {
        for (unsigned char state : patch.cells) {
          if (!m_rule->isValidState(state)) {
            error = "Lane patch state outside the active rule";
            return false;
          }
        }
      }
    }
    if (request.kind == SimulationLaneMessage::Advance &&
        request.hasElementaryRow != isElementary(*m_rule)) {
      error = "Lane advance does not match the rule's partition";
      return false;
    }
    if (request.kind == SimulationLaneMessage::SyncChunks) {
      if (!m_grid->applyChunkPatches(request.patches)) {
        error = "Lane synchronization failed";
        return false;
      }
    } else if (!advance(request, reply, error)) {
      return false;
    }
  }
  GuestWireWriter writer;
  reply.write(writer);
  output = writer.take();
  return true;
} catch (const std::exception& exception) {
  output.clear();
  error = exception.what();
  return false;
}

bool
SimulationLaneWorker::advance(const SimulationLaneRequest& request,
                              SimulationLaneReply& reply,
                              std::string& error)
{
  // Rows this lane computed but does not own return to their previous
  // contents, unless the coordinator sent their new truth.
  std::vector<SparseChunkPatch> patches = request.patches;
  std::unordered_set<ChunkAddress, ChunkAddressHash> sent;
  sent.reserve(patches.size());
  for (const SparseChunkPatch& patch : patches) {
    if (!sent.insert(patch.address).second) {
      error = "Duplicate lane patch";
      return false;
    }
  }
  for (const SparseChunkPatch& restore : m_restore) {
    if (!sent.contains(restore.address)) {
      patches.push_back(restore);
    }
  }
  m_restore.clear();
  const std::chrono::steady_clock::time_point patchStart =
    std::chrono::steady_clock::now();
  if (!m_grid->applyChunkPatches(patches)) {
    error = "Lane patch failed";
    return false;
  }
  std::unordered_map<ChunkAddress, SparseCellGrid::ChunkCells, ChunkAddressHash>
    halo;
  m_grid->visitChunks(
    [&](const ChunkAddress& address, const SparseCellGrid::ChunkCells& cells) {
      if (m_partition.isHaloChunk(address)) {
        halo.emplace(address, cells);
      }
    });
  const std::uint64_t before = m_grid->getRevision();
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  reply.patchMilliseconds = millisecondsBetween(patchStart, started);
  // Elementary lanes receive the global source row and write only the row
  // cells in their own columns.
  const bool elementary = isElementary(*m_rule);
  const std::function<bool(std::int64_t)> ownsColumn =
    [this](std::int64_t column) { return m_partition.owns(column); };
  const bool advancedOk =
    elementary
      ? m_grid->advanceElementaryRow(*m_rule, request.elementaryRow, ownsColumn)
      : m_grid->advance(*m_rule);
  if (!advancedOk) {
    error = "Lane generation failed";
    return false;
  }
  const std::chrono::steady_clock::time_point advanced =
    std::chrono::steady_clock::now();
  reply.advanceMilliseconds = millisecondsBetween(started, advanced);
  if (m_grid->getRevision() != before) {
    const bool journaled = m_grid->visitChangedChunksSince(
      before,
      [&](const ChunkAddress& address,
          const SparseCellGrid::ChunkCells* cells) {
        SparseChunkPatch patch;
        patch.address = address;
        patch.present = cells != nullptr;
        patch.cells.fill(SparseCellGrid::BackgroundState);
        if (m_partition.ownsChunk(address)) {
          if (cells != nullptr) {
            patch.cells = *cells;
          }
          reply.changes.push_back(patch);
          return;
        }
        const std::unordered_map<ChunkAddress,
                                 SparseCellGrid::ChunkCells,
                                 ChunkAddressHash>::const_iterator previous =
          halo.find(address);
        patch.present = previous != halo.end();
        if (patch.present) {
          patch.cells = previous->second;
        }
        m_restore.push_back(patch);
      });
    if (!journaled) {
      // Too many changes to journal: the coordinator stops using lanes.
      reply.kind = SimulationLaneMessage::Overflow;
      reply.changes.clear();
      m_synced = false;
      return true;
    }
  }
  m_generation += 1;
  reply.kind = SimulationLaneMessage::Advanced;
  reply.generation = m_generation;
  if (elementary) {
    reply.hasElementaryRow = true;
    reply.elementaryRow = m_grid->findElementarySourceRow(ownsColumn);
  }
  reply.collectMilliseconds =
    millisecondsBetween(advanced, std::chrono::steady_clock::now());
  return true;
}

// Sessions are unique per store, so replies to a previous coordinator (a
// retired game module) are recognized and dropped by its successor.
static std::uint64_t s_nextLaneSession = 1;

SimulationLaneCoordinator::SimulationLaneCoordinator(
  SimulationLaneTransport& transport)
  : m_transport(transport)
  , m_session(s_nextLaneSession++)
{
}

void
SimulationLaneCoordinator::fail(std::string reason)
{
  if (!m_failed) {
    m_failed = true;
    m_failure = std::move(reason);
  }
  m_inFlight = false;
  m_ahead = false;
  m_mergeReady = false;
  m_merged.clear();
  m_synced = false;
  for (Lane& lane : m_lanes) {
    lane.queue.clear();
    lane.awaitingAdvance = false;
    lane.replied = false;
    lane.haloUpdates.clear();
  }
}

SimulationLaneCoordinator::Availability
SimulationLaneCoordinator::availability(const RuleSet& rule)
{
  if (m_failed || !supportsLanes(rule)) {
    return Availability::Unavailable;
  }
  const std::uint32_t count = m_transport.laneCount();
  if (!m_transport.laneCountKnown()) {
    return Availability::Pending;
  }
  if (count == 0u) {
    return Availability::Unavailable;
  }
  if (m_laneCount != count) {
    if (busy()) {
      return Availability::Pending;
    }
    m_laneCount = count;
    m_lanes.clear();
    m_lanes.resize(count);
    m_synced = false;
  }
  return Availability::Available;
}

SimulationLanePartition
SimulationLaneCoordinator::partitionFor(std::uint32_t lane) const
{
  SimulationLanePartition partition;
  partition.lane = lane;
  partition.laneCount = m_laneCount;
  partition.bandRows = m_bandRows;
  partition.worldChunkWidth = m_syncedWidth;
  partition.worldChunkHeight = m_syncedHeight;
  partition.axis = m_axis;
  return partition;
}

bool
SimulationLaneCoordinator::queueSync(const SparseCellGrid& published,
                                     const RuleSet& rule)
{
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(rule.getRuleTag());
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(rule.getFamilyTag());
  if (definition == nullptr || family == nullptr) {
    return false;
  }
  const std::string package =
    RuleSetRegistry::serializeRulePackage(*family, *definition);
  ++m_epoch;
  ++m_resyncs;
  m_syncedWidth = published.getWorldChunkWidth();
  m_syncedHeight = published.getWorldChunkHeight();
  m_axis = isElementary(rule) ? SimulationLanePartition::Axis::Columns
                              : SimulationLanePartition::Axis::Rows;
  std::vector<std::vector<SparseChunkPatch>> relevant(m_laneCount);
  std::vector<SimulationLanePartition> partitions;
  for (std::uint32_t lane = 0; lane < m_laneCount; ++lane) {
    partitions.push_back(partitionFor(lane));
  }
  published.visitChunks(
    [&](const ChunkAddress& address, const SparseCellGrid::ChunkCells& cells) {
      for (std::uint32_t lane = 0; lane < m_laneCount; ++lane) {
        if (partitions[lane].isRelevantChunk(address)) {
          relevant[lane].push_back({ address, true, cells });
        }
      }
    });
  for (std::uint32_t lane = 0; lane < m_laneCount; ++lane) {
    Lane& state = m_lanes[lane];
    state.queue.clear();
    state.haloUpdates.clear();
    state.replied = false;
    state.awaitingAdvance = false;
    SimulationLaneRequest begin;
    begin.kind = SimulationLaneMessage::SyncBegin;
    begin.session = m_session;
    begin.epoch = m_epoch;
    begin.generation = m_generation;
    begin.partition = partitions[lane];
    begin.ruleId = rule.getRuleTag();
    begin.rulePackage = package;
    GuestWireWriter writer;
    begin.write(writer);
    state.queue.push_back(writer.take());
    std::vector<SparseChunkPatch>& chunks = relevant[lane];
    for (std::size_t first = 0; first < chunks.size();
         first += kSyncPatchesPerMessage) {
      SimulationLaneRequest part;
      part.kind = SimulationLaneMessage::SyncChunks;
      part.session = m_session;
      part.epoch = m_epoch;
      part.generation = m_generation;
      const std::size_t last =
        std::min(chunks.size(), first + kSyncPatchesPerMessage);
      part.patches.assign(chunks.begin() + static_cast<std::ptrdiff_t>(first),
                          chunks.begin() + static_cast<std::ptrdiff_t>(last));
      writer.clear();
      part.write(writer);
      state.queue.push_back(writer.take());
    }
  }
  m_synced = true;
  m_syncedRevision = published.getRevision();
  m_syncedRule = rule.getRuleTag();
  return true;
}

bool
SimulationLaneCoordinator::start(SparseCellGrid* working,
                                 const SparseCellGrid* published,
                                 const RuleSet* rule,
                                 SparseGenerationDelta&& mirrorDelta,
                                 bool useMirrorDelta)
{
  if (working == nullptr || published == nullptr || rule == nullptr ||
      m_failed || m_inFlight || m_laneCount == 0u || !supportsLanes(*rule)) {
    return false;
  }
  const bool consistent = m_synced &&
                          m_syncedRevision == published->getRevision() &&
                          m_syncedRule == rule->getRuleTag() &&
                          m_syncedWidth == published->getWorldChunkWidth() &&
                          m_syncedHeight == published->getWorldChunkHeight();
  if (m_ahead && !consistent) {
    retire(); // the world changed under the speculative generation
  }
  if (!m_ahead && lanesOutstanding()) {
    return false; // stale replies still draining; try on a later frame
  }
  if (!m_ahead && isElementary(*rule)) {
    // Lanes see only their columns; the source row is found globally here.
    m_nextRow = published->findElementarySourceRow({});
    if (m_nextRow.found &&
        m_nextRow.sourceY == std::numeric_limits<std::int64_t>::max()) {
      fail("The elementary row reached the coordinate limit");
      return false;
    }
  }
  if (!consistent && !queueSync(*published, *rule)) {
    fail("The active rule has no catalog definition for lanes");
    return false;
  }
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  // The spare grid follows the published one, as the serial runner does.
  m_usedMirror = false;
  m_usedCopy = false;
  if (useMirrorDelta &&
      !(mirrorDelta.fullReplacement && mirrorDelta.fullChunks.empty())) {
    m_usedMirror = working->applyGenerationDelta(mirrorDelta);
  }
  if (!m_usedMirror) {
    working->copyStateFrom(*published);
    m_usedCopy = true;
  }
  m_mirrorMilliseconds =
    millisecondsBetween(started, std::chrono::steady_clock::now());
  if (!m_ahead) {
    queueAdvance();
  }
  m_ahead = false;
  m_working = working;
  m_published = published;
  m_startMicroseconds = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(
      started.time_since_epoch())
      .count());
  m_inFlight = true;
  pump();
  // An adopted speculative generation may already be complete: collect it
  // now so its successor leaves with this store's current call.
  collectReplies();
  return true;
}

void
SimulationLaneCoordinator::queueAdvance()
{
  for (Lane& lane : m_lanes) {
    SimulationLaneRequest advance;
    advance.kind = SimulationLaneMessage::Advance;
    advance.session = m_session;
    advance.epoch = m_epoch;
    advance.generation = m_generation;
    advance.patches = std::move(lane.haloUpdates);
    lane.haloUpdates.clear();
    advance.hasElementaryRow = elementary();
    advance.elementaryRow = m_nextRow;
    GuestWireWriter writer;
    advance.write(writer);
    lane.queue.push_back(writer.take());
    lane.awaitingAdvance = true;
    lane.replied = false;
  }
}

bool
SimulationLaneCoordinator::lanesOutstanding() const
{
  for (const Lane& lane : m_lanes) {
    if (lane.outstanding) {
      return true;
    }
  }
  return false;
}

void
SimulationLaneCoordinator::pump()
{
  for (std::uint32_t index = 0; index < m_lanes.size() && !m_failed; ++index) {
    Lane& lane = m_lanes[index];
    // A busy lane this coordinator did not submit to still carries an
    // earlier session's request; its reply is drained and dropped.
    if (lane.outstanding || m_transport.busy(index)) {
      std::vector<std::byte> bytes;
      const int status = m_transport.poll(index, bytes);
      if (status < 0) {
        fail("A simulation lane failed");
        return;
      }
      if (status > 0) {
        lane.outstanding = false;
        SimulationLaneReply reply;
        if (!SimulationLaneReply::read(bytes, reply)) {
          fail("Invalid simulation lane reply");
          return;
        }
        if (reply.session == m_session && reply.epoch == m_epoch) {
          if (reply.kind == SimulationLaneMessage::Overflow) {
            fail("A generation changed too many chunks for lane tracking");
            return;
          }
          if (reply.kind == SimulationLaneMessage::Advanced) {
            if (!lane.awaitingAdvance || reply.generation != m_generation + 1 ||
                reply.hasElementaryRow != elementary()) {
              fail("Unexpected simulation lane generation");
              return;
            }
            lane.reply = std::move(reply);
            lane.replied = true;
            lane.awaitingAdvance = false;
          }
        }
      }
    }
    if (!lane.outstanding && !lane.queue.empty() && !m_transport.busy(index) &&
        m_transport.submit(index, std::move(lane.queue.front()))) {
      lane.queue.pop_front();
      lane.outstanding = true;
    }
  }
}

void
SimulationLaneCoordinator::collectReplies()
{
  if (!m_inFlight || m_mergeReady || m_failed) {
    return;
  }
  for (const Lane& lane : m_lanes) {
    if (!lane.replied) {
      return;
    }
  }
  {
    m_merged.clear();
    // The next elementary source row: every lane's owned-column row combined.
    m_nextRow = SparseElementaryRow{};
    m_slowestAdvance = 0.0;
    m_slowestPatch = 0.0;
    m_slowestCollect = 0.0;
    m_totalLaneWork = 0.0;
    for (Lane& lane : m_lanes) {
      m_totalLaneWork +=
        lane.reply.advanceMilliseconds + lane.reply.patchMilliseconds;
      m_slowestAdvance =
        std::max(m_slowestAdvance, lane.reply.advanceMilliseconds);
      m_slowestPatch = std::max(m_slowestPatch, lane.reply.patchMilliseconds);
      m_slowestCollect =
        std::max(m_slowestCollect, lane.reply.collectMilliseconds);
      m_merged.insert(
        m_merged.end(), lane.reply.changes.begin(), lane.reply.changes.end());
      SparseCellGrid::mergeElementaryRow(&m_nextRow, lane.reply.elementaryRow);
      lane.reply.changes.clear();
      lane.reply.elementaryRow = SparseElementaryRow{};
      lane.replied = false;
    }
    // Each lane's next halo: the lines it borders that other lanes changed.
    for (std::uint32_t index = 0; index < m_lanes.size(); ++index) {
      const SimulationLanePartition partition = partitionFor(index);
      std::vector<SparseChunkPatch>& updates = m_lanes[index].haloUpdates;
      updates.clear();
      for (const SparseChunkPatch& patch : m_merged) {
        if (partition.isHaloChunk(patch.address)) {
          updates.push_back(patch);
        }
      }
    }
    m_generation += 1;
    // The lanes need only their halos to continue: the next generation
    // starts as soon as this store returns, overlapping the merge (next
    // poll) and the caller's publication. An elementary row at the coordinate
    // limit is left to start(), which turns lanes off before advancing it.
    const bool rowAtLimit =
      elementary() && m_nextRow.found &&
      m_nextRow.sourceY == std::numeric_limits<std::int64_t>::max();
    if (!rowAtLimit) {
      queueAdvance();
    }
    m_ahead = !rowAtLimit;
    m_mergeReady = true;
    pump();
  }
}

bool
SimulationLaneCoordinator::poll(SparseCellGrid** completedGrid,
                                SparseGenerationDelta* delta,
                                double* elapsedMilliseconds,
                                bool* advanceSucceeded,
                                SimulationRunnerTimings* timings)
{
  pump();
  if (!m_inFlight || m_failed) {
    return false;
  }
  // Poll interval: this store runs once per frame, so it is the frame time.
  const std::chrono::steady_clock::time_point polled =
    std::chrono::steady_clock::now();
  const double interval = m_lastPoll.time_since_epoch().count() == 0
                            ? 0.0
                            : millisecondsBetween(m_lastPoll, polled);
  m_lastPoll = polled;
  if (!m_mergeReady) {
    collectReplies();
    // The next generation's jobs leave only when this call returns. With
    // frames shorter than a merge, merging on the next poll starts them
    // sooner; with longer frames, merging now saves a whole frame.
    if (!m_mergeReady ||
        (m_merge.size() != 0u && interval < m_merge.median())) {
      return false;
    }
  }
  m_mergeReady = false;
  const std::chrono::steady_clock::time_point mergeStart =
    std::chrono::steady_clock::now();
  const std::vector<SparseChunkPatch> merged = std::move(m_merged);
  m_merged.clear();
  const double slowestLane = m_slowestAdvance;
  const double slowestPatch = m_slowestPatch;
  const double slowestCollect = m_slowestCollect;
  SparseGenerationDelta result;
  if (!m_working->buildPatchDelta(merged, &result)) {
    fail("Simulation lane results could not be merged");
    return false;
  }
  const std::chrono::steady_clock::time_point built =
    std::chrono::steady_clock::now();
  if (!result.changedChunks.empty() &&
      !m_working->applyGenerationDelta(result)) {
    fail("Simulation lane results could not be merged");
    return false;
  }
  m_mergeBuild.add(millisecondsBetween(mergeStart, built));
  m_syncedRevision = m_working->getRevision();
  m_inFlight = false;
  const std::chrono::steady_clock::time_point finished =
    std::chrono::steady_clock::now();
  const double total =
    static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                          finished.time_since_epoch())
                          .count() -
                        static_cast<std::int64_t>(m_startMicroseconds)) /
    1000.0;
  m_roundTrip.add(total);
  m_laneAdvance.add(slowestLane);
  m_lanePatch.add(slowestPatch);
  m_laneCollect.add(slowestCollect);
  m_laneWork.add(m_totalLaneWork);
  m_merge.add(millisecondsBetween(mergeStart, finished));
  if (completedGrid != nullptr) {
    *completedGrid = m_working;
  }
  if (delta != nullptr) {
    *delta = std::move(result);
  }
  if (elapsedMilliseconds != nullptr) {
    *elapsedMilliseconds = total;
  }
  if (advanceSucceeded != nullptr) {
    *advanceSucceeded = true;
  }
  if (timings != nullptr) {
    timings->mirrorMilliseconds = m_mirrorMilliseconds;
    timings->advanceMilliseconds = slowestLane;
    timings->captureMilliseconds = millisecondsBetween(mergeStart, finished);
    timings->totalMilliseconds = total;
    timings->usedMirrorDelta = m_usedMirror;
    timings->usedFullCopy = m_usedCopy;
    timings->usedDirectSourceAdvance = false;
  }
  m_working = nullptr;
  m_published = nullptr;
  return true;
}

bool
SimulationLaneCoordinator::busy() const
{
  // A speculative generation never blocks the caller's next start().
  return m_inFlight || (!m_ahead && lanesOutstanding());
}

void
SimulationLaneCoordinator::retire()
{
  if (m_inFlight || m_ahead) {
    ++m_retirements;
  }
  m_inFlight = false;
  m_ahead = false;
  m_mergeReady = false;
  m_merged.clear();
  m_working = nullptr;
  m_published = nullptr;
  // A new epoch makes every reply still on its way stale.
  ++m_epoch;
  m_synced = false;
  for (Lane& lane : m_lanes) {
    lane.queue.clear();
    lane.awaitingAdvance = false;
    lane.replied = false;
    lane.haloUpdates.clear();
  }
}
