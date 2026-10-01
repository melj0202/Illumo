#include "SimulationLanes.h"
#include "Rulesets/RuleSet.h"
#include <Illumo/Foundation/Profile.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

// Cells in one chunk line of halo.
static constexpr unsigned int kLineCells = 16u;
// Sync patches per message: about 2.2 MiB, so several lanes' parts fit one
// service exchange.
static constexpr std::size_t kSyncPatchesPerMessage = 8192u;

// Generations a halo of `haloLines` keeps exact for `radius`; 0 when not one.
static std::uint32_t
blockStepsFor(unsigned int radius, std::uint32_t haloLines)
{
  const std::uint32_t reach = kLineCells * haloLines;
  const unsigned int effective = radius == 0u ? 1u : radius;
  return std::min<std::uint32_t>(SimulationLaneRequest::MaximumSteps,
                                 reach / effective);
}

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

bool
SimulationLanePartition::owns(std::int64_t row) const
{
  const std::int64_t canonical = canonicalRow(row);
  return (beginUnbounded || canonical >= ownedBegin) &&
         (endUnbounded || canonical < ownedEnd);
}

bool
SimulationLanePartition::isHalo(std::int64_t row) const
{
  const std::int64_t canonical = canonicalRow(row);
  if (owns(canonical)) {
    return false;
  }
  for (std::uint32_t distance = 1u; distance <= haloLines; ++distance) {
    const std::int64_t offset = static_cast<std::int64_t>(distance);
    if (owns(canonicalRow(canonical - offset)) ||
        owns(canonicalRow(canonical + offset))) {
      return true;
    }
  }
  return false;
}

bool
SimulationLanePartition::valid() const
{
  constexpr std::int64_t kLimit = std::numeric_limits<std::int64_t>::max() / 16;
  return laneCount >= 1u && laneCount <= 64u && lane < laneCount &&
         haloLines >= 1u && haloLines <= MaximumHaloLines &&
         (axis == Axis::Rows || axis == Axis::Columns) &&
         (beginUnbounded || (ownedBegin >= -kLimit && ownedBegin <= kLimit)) &&
         (endUnbounded || (ownedEnd >= -kLimit && ownedEnd <= kLimit)) &&
         (beginUnbounded || endUnbounded || ownedBegin <= ownedEnd) &&
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
  ILLUMO_PROFILE_ZONE("Lanes.encodeRequest");
  output.u32(Magic);
  output.u32(Version);
  output.u32(static_cast<std::uint32_t>(kind));
  output.u64(session);
  output.u64(epoch);
  output.u64(generation);
  if (kind == SimulationLaneMessage::SyncBegin) {
    output.u32(partition.lane);
    output.u32(partition.laneCount);
    output.u32(partition.haloLines);
    output.u32((partition.beginUnbounded ? 1u : 0u) |
               (partition.endUnbounded ? 2u : 0u));
    output.u64(std::bit_cast<std::uint64_t>(partition.ownedBegin));
    output.u64(std::bit_cast<std::uint64_t>(partition.ownedEnd));
    output.u64(std::bit_cast<std::uint64_t>(partition.worldChunkWidth));
    output.u64(std::bit_cast<std::uint64_t>(partition.worldChunkHeight));
    output.u32(static_cast<std::uint32_t>(partition.axis));
    output.text(ruleId);
    output.text(rulePackage);
    return;
  }
  writePatches(output, patches);
  if (kind == SimulationLaneMessage::Advance) {
    output.u32(steps);
    writeElementaryRow(output, hasElementaryRow, elementaryRow);
  }
}

bool
SimulationLaneRequest::read(std::span<const std::byte> bytes,
                            SimulationLaneRequest& output)
{
  SimulationLaneRequest request;
  if (!decode(bytes, request)) {
    return false;
  }
  output = std::move(request);
  return true;
}

bool
SimulationLaneRequest::decode(std::span<const std::byte> bytes,
                              SimulationLaneRequest& request)
{
  ILLUMO_PROFILE_ZONE("Lane.decodeRequest");
  // Fields a message kind does not carry return to their defaults; reused
  // strings and patches keep their capacity.
  request.partition = SimulationLanePartition{};
  request.ruleId.clear();
  request.rulePackage.clear();
  request.patches.clear();
  request.steps = 1u;
  request.hasElementaryRow = false;
  request.elementaryRow = SparseElementaryRow{};
  GuestWireReader reader(bytes);
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
    request.partition.haloLines = reader.u32();
    const std::uint32_t unbounded = reader.u32();
    request.partition.ownedBegin = std::bit_cast<std::int64_t>(reader.u64());
    request.partition.ownedEnd = std::bit_cast<std::int64_t>(reader.u64());
    request.partition.worldChunkWidth =
      std::bit_cast<std::int64_t>(reader.u64());
    request.partition.worldChunkHeight =
      std::bit_cast<std::int64_t>(reader.u64());
    const std::uint32_t axis = reader.u32();
    if (axis > 1u || unbounded > 3u) {
      return false;
    }
    request.partition.beginUnbounded = (unbounded & 1u) != 0u;
    request.partition.endUnbounded = (unbounded & 2u) != 0u;
    request.partition.axis = static_cast<SimulationLanePartition::Axis>(axis);
    request.ruleId = reader.text(256);
    request.rulePackage = reader.text(1024u * 1024u);
    if (!reader.finished() || !request.partition.valid() ||
        request.ruleId.empty() || request.rulePackage.empty()) {
      return false;
    }
  } else {
    if (!readPatches(reader, request.patches)) {
      return false;
    }
    if (request.kind == SimulationLaneMessage::Advance) {
      request.steps = reader.u32();
      if (!reader.valid() || request.steps == 0u ||
          request.steps > MaximumSteps ||
          request.generation >
            std::numeric_limits<std::uint64_t>::max() - request.steps ||
          !readElementaryRow(
            reader, request.hasElementaryRow, request.elementaryRow)) {
        return false;
      }
    }
    if (!reader.finished()) {
      return false;
    }
  }
  return true;
}

void
SimulationLaneReply::write(GuestWireWriter& output) const
{
  ILLUMO_PROFILE_ZONE("Lane.encodeReply");
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
  SimulationLaneReply reply;
  if (!decode(bytes, reply)) {
    return false;
  }
  output = std::move(reply);
  return true;
}

bool
SimulationLaneReply::decode(std::span<const std::byte> bytes,
                            SimulationLaneReply& reply)
{
  ILLUMO_PROFILE_ZONE("Lanes.decodeReply");
  reply.changes.clear();
  reply.hasElementaryRow = false;
  reply.elementaryRow = SparseElementaryRow{};
  GuestWireReader reader(bytes);
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
  return true;
}

static bool
isElementary(const RuleSet& rule)
{
  return rule.getNeighborhoodKind() == RuleSet::NeighborhoodKind::Elementary1D;
}

// Every rule whose radius the halo covers partitions: elementary 1D rules by
// chunk column (their one active row spans columns), all others by chunk row.
static bool
supportsLanes(const RuleSet& rule, std::uint32_t haloLines)
{
  return blockStepsFor(rule.getNeighborhoodRadius(), haloLines) != 0u;
}

// Generations one block may run: elementary rows need the global source row
// every generation, so they run one at a time.
static std::uint32_t
blockStepsFor(const RuleSet& rule, std::uint32_t haloLines)
{
  return isElementary(rule)
           ? std::min<std::uint32_t>(
               1u, blockStepsFor(rule.getNeighborhoodRadius(), haloLines))
           : blockStepsFor(rule.getNeighborhoodRadius(), haloLines);
}

bool
SimulationLaneWorker::execute(std::span<const std::byte> bytes,
                              std::vector<std::byte>& output,
                              std::string& error)
{
  ILLUMO_PROFILE_ZONE("Lane.execute");
  output.clear();
  error.clear();
  SimulationLaneRequest& request = m_request;
  if (!SimulationLaneRequest::decode(bytes, request)) {
    error = "Malformed simulation lane request";
    return false;
  }
  SimulationLaneReply& reply = m_reply;
  reply.kind = SimulationLaneMessage::Ack;
  reply.session = request.session;
  reply.epoch = request.epoch;
  reply.generation = request.generation;
  reply.advanceMilliseconds = 0.0;
  reply.patchMilliseconds = 0.0;
  reply.collectMilliseconds = 0.0;
  reply.changes.clear();
  reply.hasElementaryRow = false;
  reply.elementaryRow = SparseElementaryRow{};
  if (request.kind == SimulationLaneMessage::SyncBegin) {
    ILLUMO_PROFILE_ZONE("Lane.syncBegin");
    RuleSetRegistry registry;
    if (!registry.loadRulePackage(request.rulePackage)) {
      error = "Invalid lane rule package";
      return false;
    }
    std::unique_ptr<RuleSet> rule = registry.createRuleSet(request.ruleId);
    if (!rule || !supportsLanes(*rule, request.partition.haloLines) ||
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
    {
      ILLUMO_PROFILE_ZONE("Lane.validatePatches");
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
    }
    if (request.kind == SimulationLaneMessage::Advance &&
        request.hasElementaryRow != isElementary(*m_rule)) {
      error = "Lane advance does not match the rule's partition";
      return false;
    }
    if (request.kind == SimulationLaneMessage::Advance &&
        request.steps > blockStepsFor(*m_rule, m_partition.haloLines)) {
      error = "Lane block is longer than its halo keeps exact";
      return false;
    }
    if (request.kind == SimulationLaneMessage::SyncChunks) {
      ILLUMO_PROFILE_ZONE("Lane.syncChunks");
      if (!m_grid->applyChunkPatches(request.patches)) {
        error = "Lane synchronization failed";
        return false;
      }
    } else if (!advance(request, reply, error)) {
      return false;
    }
  }
  m_writer.clear();
  reply.write(m_writer);
  if (m_writer.failed()) {
    error = m_writer.failure();
    return false;
  }
  output.assign(m_writer.data().begin(), m_writer.data().end());
  return true;
}

static bool
chunkAddressLess(const ChunkAddress& left, const ChunkAddress& right)
{
  return left.y < right.y || (left.y == right.y && left.x < right.x);
}

bool
SimulationLaneWorker::advance(const SimulationLaneRequest& request,
                              SimulationLaneReply& reply,
                              std::string& error)
{
  ILLUMO_PROFILE_ZONE("Lane.advance");
  // Rows this lane computed but does not own return to their previous
  // contents, unless the coordinator sent their new truth.
  m_patches.assign(request.patches.begin(), request.patches.end());
  m_sent.clear();
  for (const SparseChunkPatch& patch : m_patches) {
    m_sent.push_back(patch.address);
  }
  std::sort(m_sent.begin(), m_sent.end(), chunkAddressLess);
  if (std::adjacent_find(m_sent.begin(), m_sent.end()) != m_sent.end()) {
    error = "Duplicate lane patch";
    return false;
  }
  for (const SparseChunkPatch& restore : m_restore) {
    if (!std::binary_search(
          m_sent.begin(), m_sent.end(), restore.address, chunkAddressLess)) {
      m_patches.push_back(restore);
    }
  }
  m_restore.clear();
  const std::chrono::steady_clock::time_point patchStart =
    std::chrono::steady_clock::now();
  {
    ILLUMO_PROFILE_ZONE("Lane.applyPatches");
    if (!m_grid->applyChunkPatches(m_patches)) {
      error = "Lane patch failed";
      return false;
    }
  }
  {
    ILLUMO_PROFILE_ZONE("Lane.collectHalo");
    m_halo.clear();
    m_grid->visitChunks([this](const ChunkAddress& address,
                               const SparseCellGrid::ChunkCells& cells) {
      if (m_partition.isHaloChunk(address)) {
        m_halo.push_back({ address, cells });
      }
    });
    std::sort(m_halo.begin(),
              m_halo.end(),
              [](const HaloChunk& left, const HaloChunk& right) {
                return chunkAddressLess(left.address, right.address);
              });
  }
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  reply.patchMilliseconds = millisecondsBetween(patchStart, started);
  // Elementary lanes receive the global source row and write only the row
  // cells in their own columns (one generation per block).
  const bool elementary = isElementary(*m_rule);
  const std::function<bool(std::int64_t)> ownsColumn =
    [this](std::int64_t column) { return m_partition.owns(column); };
  // Every chunk any generation of the block changes: owned ones are
  // replied with their final contents, the rest restored afterwards.
  m_changed.clear();
  const std::function<void(const ChunkAddress&,
                           const SparseCellGrid::ChunkCells*)>
    noteChanged =
      [this](const ChunkAddress& address, const SparseCellGrid::ChunkCells*) {
        m_changed.push_back(address);
      };
  std::uint64_t lastBefore = m_grid->getRevision();
  {
    ILLUMO_PROFILE_ZONE("Lane.advanceBand");
    for (std::uint32_t step = 0u; step < request.steps; ++step) {
      lastBefore = m_grid->getRevision();
      const bool advancedOk = elementary
                                ? m_grid->advanceElementaryRow(
                                    *m_rule, request.elementaryRow, ownsColumn)
                                : m_grid->advance(*m_rule);
      if (!advancedOk) {
        error = "Lane generation failed";
        return false;
      }
      if (m_grid->getRevision() != lastBefore &&
          !m_grid->visitChangedChunksSince(lastBefore, noteChanged)) {
        // Too many changes to journal: the coordinator stops using lanes.
        reply.kind = SimulationLaneMessage::Overflow;
        reply.changes.clear();
        m_synced = false;
        return true;
      }
    }
  }
  const std::chrono::steady_clock::time_point advanced =
    std::chrono::steady_clock::now();
  reply.advanceMilliseconds = millisecondsBetween(started, advanced);
  ILLUMO_PROFILE_ZONE("Lane.collectChanges");
  // As the native runner does after a self-advance: the inactive map catches
  // up from the last generation's journal when the next patches apply,
  // instead of copying every chunk. Each advance resets that state, so only
  // the block's last generation matters. Elementary rows write the inactive
  // map directly, so those lanes keep the full copy.
  if (!elementary &&
      m_grid->captureGenerationDelta(lastBefore, &m_generationDelta, false) &&
      !m_generationDelta.fullReplacement) {
    m_grid->rememberInactiveGenerationDelta(m_generationDelta);
  }
  std::sort(m_changed.begin(), m_changed.end(), chunkAddressLess);
  m_changed.erase(std::unique(m_changed.begin(), m_changed.end()),
                  m_changed.end());
  for (const ChunkAddress& address : m_changed) {
    SparseChunkPatch& patch = m_partition.ownsChunk(address)
                                ? reply.changes.emplace_back()
                                : m_restore.emplace_back();
    patch.address = address;
    patch.cells.fill(SparseCellGrid::BackgroundState);
    if (m_partition.ownsChunk(address)) {
      const SparseCellGrid::ChunkCells* cells = m_grid->findChunkCells(address);
      patch.present = cells != nullptr;
      if (cells != nullptr) {
        patch.cells = *cells;
      }
      continue;
    }
    // A halo chunk returns to its contents before the block; any other
    // (born outside the owned and halo lines) is removed.
    const std::vector<HaloChunk>::const_iterator previous =
      std::lower_bound(m_halo.begin(),
                       m_halo.end(),
                       address,
                       [](const HaloChunk& chunk, const ChunkAddress& key) {
                         return chunkAddressLess(chunk.address, key);
                       });
    patch.present = previous != m_halo.end() && previous->address == address;
    if (patch.present) {
      patch.cells = previous->cells;
    }
  }
  m_generation += request.steps;
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
  if (m_failed || !supportsLanes(rule, m_haloLines)) {
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

std::uint32_t
SimulationLaneCoordinator::maximumBlockSteps(const RuleSet& rule) const
{
  return blockStepsFor(rule, m_haloLines);
}

SimulationLanePartition
SimulationLaneCoordinator::partitionFor(std::uint32_t lane) const
{
  SimulationLanePartition partition;
  partition.lane = lane;
  partition.laneCount = m_laneCount;
  partition.haloLines = m_haloLines;
  partition.worldChunkWidth = m_syncedWidth;
  partition.worldChunkHeight = m_syncedHeight;
  partition.axis = m_axis;
  const std::int64_t extent = m_axis == SimulationLanePartition::Axis::Columns
                                ? m_syncedWidth
                                : m_syncedHeight;
  const bool toroidal = m_syncedWidth > 0 && m_syncedHeight > 0;
  const std::int64_t minimum = -(extent / 2);
  // A torus's first and last runs end at its edges; an infinite world's
  // extend without limit.
  partition.beginUnbounded = lane == 0u && !toroidal;
  partition.ownedBegin =
    lane == 0u ? (toroidal ? minimum : 0) : m_cuts[lane - 1u];
  partition.endUnbounded = lane + 1u == m_laneCount && !toroidal;
  partition.ownedEnd =
    lane + 1u == m_laneCount ? (toroidal ? minimum + extent : 0) : m_cuts[lane];
  return partition;
}

void
SimulationLaneCoordinator::computeCuts(const SparseCellGrid& published)
{
  ILLUMO_PROFILE_ZONE("Lanes.computeCuts");
  // Equal stored-chunk counts per lane: sorted lines of every stored chunk,
  // cut at each lane's share. A single line never splits, so a lane may end
  // up empty when one line holds several shares.
  std::vector<std::int64_t>& lines = m_lineScratch;
  lines.clear();
  lines.reserve(published.getAllocatedChunkCount());
  const bool columns = m_axis == SimulationLanePartition::Axis::Columns;
  published.visitChunks([&lines, columns](const ChunkAddress& address,
                                          const SparseCellGrid::ChunkCells&) {
    lines.push_back(columns ? address.x : address.y);
  });
  std::sort(lines.begin(), lines.end());
  m_cuts.assign(m_laneCount > 0u ? m_laneCount - 1u : 0u, 0);
  const std::int64_t extent = columns ? m_syncedWidth : m_syncedHeight;
  const bool toroidal = m_syncedWidth > 0 && m_syncedHeight > 0;
  const std::int64_t minimum = toroidal ? -(extent / 2) : 0;
  for (std::uint32_t cut = 0u; cut + 1u < m_laneCount; ++cut) {
    if (lines.empty()) {
      // Nothing stored: split the torus evenly, or give the infinite plane's
      // origin to the first and last lanes until a rebalance.
      m_cuts[cut] = toroidal
                      ? minimum + extent * static_cast<std::int64_t>(cut + 1u) /
                                    static_cast<std::int64_t>(m_laneCount)
                      : 0;
      continue;
    }
    const std::size_t share =
      lines.size() * (cut + 1u) / static_cast<std::size_t>(m_laneCount);
    const std::size_t index = std::min(share, lines.size() - 1u);
    m_cuts[cut] = lines[index];
    if (cut > 0u) {
      m_cuts[cut] = std::max(m_cuts[cut], m_cuts[cut - 1u]);
    }
  }
}

bool
SimulationLaneCoordinator::queueSync(const SparseCellGrid& published,
                                     const RuleSet& rule)
{
  ILLUMO_PROFILE_ZONE("Lanes.queueSync");
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
  computeCuts(published);
  m_recut = false;
  m_imbalancedBlocks = 0u;
  m_lastSync = std::chrono::steady_clock::now();
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
    state.queue.push_back(writer.data());
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
      state.queue.push_back(writer.data());
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
                                 bool useMirrorDelta,
                                 std::uint32_t requestedSteps,
                                 std::uint32_t* acceptedSteps)
{
  ILLUMO_PROFILE_ZONE("Lanes.start");
  if (working == nullptr || published == nullptr || rule == nullptr ||
      m_failed || m_inFlight || m_laneCount == 0u ||
      !supportsLanes(*rule, m_haloLines)) {
    return false;
  }
  m_stepCap = maximumBlockSteps(*rule);
  const std::uint32_t steps =
    std::clamp<std::uint32_t>(requestedSteps, 1u, std::max(1u, m_stepCap));
  m_lastSteps = steps;
  const bool consistent = m_synced &&
                          m_syncedRevision == published->getRevision() &&
                          m_syncedRule == rule->getRuleTag() &&
                          m_syncedWidth == published->getWorldChunkWidth() &&
                          m_syncedHeight == published->getWorldChunkHeight();
  if (m_ahead && !consistent) {
    retire(); // the world changed under the speculative block
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
  {
    ILLUMO_PROFILE_ZONE("Lanes.mirrorWorking");
    if (useMirrorDelta &&
        !(mirrorDelta.fullReplacement && mirrorDelta.fullChunks.empty())) {
      m_usedMirror = working->applyGenerationDelta(mirrorDelta);
    }
    if (!m_usedMirror) {
      working->copyStateFrom(*published);
      m_usedCopy = true;
    }
  }
  m_mirrorMilliseconds =
    millisecondsBetween(started, std::chrono::steady_clock::now());
  // A block already running ahead keeps its own length; the caller counts
  // what it will publish.
  if (!m_ahead) {
    queueAdvance(steps);
  }
  if (acceptedSteps != nullptr) {
    *acceptedSteps = m_blockSteps;
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
  // An adopted speculative block may already be complete: collect it now so
  // its successor leaves with this store's current call.
  collectReplies();
  return true;
}

void
SimulationLaneCoordinator::queueAdvance(std::uint32_t steps)
{
  ILLUMO_PROFILE_ZONE("Lanes.queueAdvance");
  m_blockSteps = steps;
  SimulationLaneRequest& advance = m_advance;
  advance.kind = SimulationLaneMessage::Advance;
  advance.session = m_session;
  advance.epoch = m_epoch;
  advance.generation = m_generation;
  advance.steps = steps;
  advance.hasElementaryRow = elementary();
  advance.elementaryRow = m_nextRow;
  for (Lane& lane : m_lanes) {
    // Borrow the lane's halo updates for the write, then hand them back
    // empty so both vectors keep their capacity.
    advance.patches.swap(lane.haloUpdates);
    m_writer.clear();
    advance.write(m_writer);
    advance.patches.swap(lane.haloUpdates);
    lane.haloUpdates.clear();
    lane.queue.push_back(m_writer.data());
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
  ILLUMO_PROFILE_ZONE("Lanes.pump");
  for (std::uint32_t index = 0; index < m_lanes.size() && !m_failed; ++index) {
    Lane& lane = m_lanes[index];
    // A busy lane this coordinator did not submit to still carries an
    // earlier session's request; its reply is drained and dropped.
    if (lane.outstanding || m_transport.busy(index)) {
      const int status = m_transport.poll(index, m_pollBytes);
      if (status < 0) {
        fail("A simulation lane failed");
        return;
      }
      if (status > 0) {
        lane.outstanding = false;
        SimulationLaneReply& reply = m_replyScratch;
        if (!SimulationLaneReply::decode(m_pollBytes, reply)) {
          fail("Invalid simulation lane reply");
          return;
        }
        if (reply.session == m_session && reply.epoch == m_epoch) {
          if (reply.kind == SimulationLaneMessage::Overflow) {
            fail("A generation changed too many chunks for lane tracking");
            return;
          }
          if (reply.kind == SimulationLaneMessage::Advanced) {
            if (!lane.awaitingAdvance ||
                reply.generation != m_generation + m_blockSteps ||
                reply.hasElementaryRow != elementary()) {
              fail("Unexpected simulation lane generation");
              return;
            }
            // Swapped: the lane's previous reply becomes the next scratch.
            std::swap(lane.reply, reply);
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

bool
SimulationLaneCoordinator::noteImbalance()
{
  if (m_laneCount < 2u || !(m_meanAdvance > 0.0)) {
    m_imbalancedBlocks = 0u;
    return false;
  }
  const double ratio = m_slowestAdvance / m_meanAdvance;
  m_imbalance.add(ratio);
  ILLUMO_PROFILE_PLOT("Lanes.imbalance", ratio);
  if (ratio <= kImbalanceRatio ||
      m_slowestAdvance < m_rebalanceMinimumMilliseconds) {
    m_imbalancedBlocks = 0u;
    return false;
  }
  m_imbalancedBlocks += 1u;
  const double sinceSync =
    millisecondsBetween(m_lastSync, std::chrono::steady_clock::now()) / 1000.0;
  return m_imbalancedBlocks >= m_rebalanceBlocks &&
         sinceSync >= m_rebalanceIntervalSeconds;
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
    ILLUMO_PROFILE_ZONE("Lanes.collectReplies");
    m_merged.clear();
    // The next elementary source row: every lane's owned-column row combined.
    m_nextRow = SparseElementaryRow{};
    m_slowestAdvance = 0.0;
    m_slowestPatch = 0.0;
    m_slowestCollect = 0.0;
    m_totalLaneWork = 0.0;
    double totalAdvance = 0.0;
    for (Lane& lane : m_lanes) {
      ILLUMO_PROFILE_ZONE("Lanes.concatenateReply");
      m_totalLaneWork +=
        lane.reply.advanceMilliseconds + lane.reply.patchMilliseconds;
      totalAdvance += lane.reply.advanceMilliseconds;
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
    m_meanAdvance = m_lanes.empty()
                      ? 0.0
                      : totalAdvance / static_cast<double>(m_lanes.size());
    ILLUMO_PROFILE_PLOT("Lanes.mergedPatches", m_merged.size());
    ILLUMO_PROFILE_PLOT("Lanes.mergedPatchBytes",
                        m_merged.size() * sizeof(SparseChunkPatch));
    ILLUMO_PROFILE_PLOT("Lanes.slowestAdvanceMs", m_slowestAdvance);
    ILLUMO_PROFILE_PLOT("Lanes.totalLaneWorkMs", m_totalLaneWork);
    ILLUMO_PROFILE_PLOT("Lanes.blockSteps", m_blockSteps);
    // Each lane's next halo: the lines it borders that other lanes changed.
    {
      ILLUMO_PROFILE_ZONE("Lanes.buildHaloUpdates");
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
    }
    m_generation += m_blockSteps;
    m_mergeSteps = m_blockSteps;
    // The lanes need only their halos to continue: the next block starts as
    // soon as this store returns, overlapping the merge (next poll) and the
    // caller's publication, sized like the caller's last request. An
    // elementary row at the coordinate limit is left to start(), which turns
    // lanes off before advancing it; persistent imbalance recuts the lanes at
    // the next start() instead.
    const bool rowAtLimit =
      elementary() && m_nextRow.found &&
      m_nextRow.sourceY == std::numeric_limits<std::int64_t>::max();
    if (noteImbalance()) {
      m_recut = true;
      m_synced = false;
      ++m_rebalances;
    }
    const bool speculate = !rowAtLimit && !m_recut;
    if (speculate) {
      queueAdvance(
        std::clamp<std::uint32_t>(m_lastSteps, 1u, std::max(1u, m_stepCap)));
    }
    m_ahead = speculate;
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
  ILLUMO_PROFILE_ZONE("Lanes.poll");
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
    // The next block's jobs leave only when this call returns. With frames
    // shorter than a merge, merging on the next poll starts them sooner; with
    // longer frames, merging now saves a whole frame.
    if (!m_mergeReady ||
        (m_merge.size() != 0u && interval < m_merge.median())) {
      return false;
    }
  }
  m_mergeReady = false;
  ILLUMO_PROFILE_ZONE("Lanes.merge");
  const std::chrono::steady_clock::time_point mergeStart =
    std::chrono::steady_clock::now();
  // Swapped with a retained drain so both keep their capacity.
  m_mergedDrain.swap(m_merged);
  m_merged.clear();
  const std::vector<SparseChunkPatch>& merged = m_mergedDrain;
  const double slowestLane = m_slowestAdvance;
  const double slowestPatch = m_slowestPatch;
  const double slowestCollect = m_slowestCollect;
  SparseGenerationDelta& result = m_resultDelta;
  bool built = false;
  {
    ILLUMO_PROFILE_ZONE("Lanes.merge.buildPatchDelta");
    built = m_working->buildPatchDelta(merged, &result);
  }
  if (!built) {
    fail("Simulation lane results could not be merged");
    return false;
  }
  const std::chrono::steady_clock::time_point builtAt =
    std::chrono::steady_clock::now();
  bool applied = true;
  if (!result.changedChunks.empty()) {
    ILLUMO_PROFILE_ZONE("Lanes.merge.applyGenerationDelta");
    applied = m_working->applyGenerationDelta(result);
  }
  if (!applied) {
    fail("Simulation lane results could not be merged");
    return false;
  }
  m_mergeBuild.add(millisecondsBetween(mergeStart, builtAt));
  // A recut resynchronizes at the next start(); otherwise lanes still mirror
  // the world this merge publishes.
  if (!m_recut) {
    m_syncedRevision = m_working->getRevision();
  }
  m_inFlight = false;
  const std::chrono::steady_clock::time_point finished =
    std::chrono::steady_clock::now();
  const double total =
    static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                          finished.time_since_epoch())
                          .count() -
                        static_cast<std::int64_t>(m_startMicroseconds)) /
    1000.0;
  const std::uint32_t generations = std::max(1u, m_mergeSteps);
  m_roundTrip.add(total);
  m_generationCost.add(total / static_cast<double>(generations));
  m_laneAdvance.add(slowestLane);
  m_lanePatch.add(slowestPatch);
  m_laneCollect.add(slowestCollect);
  m_laneWork.add(m_totalLaneWork / static_cast<double>(generations));
  m_merge.add(millisecondsBetween(mergeStart, finished));
  ILLUMO_PROFILE_PLOT("Lanes.deltaChunks", result.changedChunks.size());
  ILLUMO_PROFILE_PLOT("Lanes.mergeMs",
                      millisecondsBetween(mergeStart, finished));
  ILLUMO_PROFILE_PLOT("Lanes.roundTripMs", total);
  ILLUMO_PROFILE_PLOT("Lanes.storedChunks",
                      m_working->getAllocatedChunkCount());
  ILLUMO_PROFILE_FRAME_MARK("Lanes.generation");
  if (completedGrid != nullptr) {
    *completedGrid = m_working;
  }
  if (delta != nullptr) {
    // The caller's previous delta becomes the next result's storage.
    std::swap(*delta, result);
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
    timings->generations = generations;
  }
  m_working = nullptr;
  m_published = nullptr;
  return true;
}

bool
SimulationLaneCoordinator::busy() const
{
  // A speculative block never blocks the caller's next start().
  return m_inFlight || (!m_ahead && lanesOutstanding());
}

void
SimulationLaneCoordinator::retire()
{
  ILLUMO_PROFILE_ZONE("Lanes.retire");
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
