#include "SparseCellGrid.h"
#include "SparseWorkerPool.h"
#include "Rulesets/RuleSet.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <tracy/Tracy.hpp>

static std::size_t
mixAddress(std::uint64_t value)
{
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return static_cast<std::size_t>(value ^ (value >> 31));
}

static constexpr std::uint64_t kChunkLeftEdgeMask = 0x0001000100010001ULL;
static constexpr std::uint64_t kChunkRightEdgeMask = 0x8000800080008000ULL;
static constexpr std::uint64_t kChunkTopRowMask = 0x000000000000FFFFULL;
static constexpr std::uint64_t kChunkBottomRowMask = 0xFFFF000000000000ULL;

static double
millisecondsSince(const std::chrono::steady_clock::time_point& start)
{
  return std::chrono::duration<double, std::milli>(
           std::chrono::steady_clock::now() - start)
    .count();
}

struct SparseCellGrid::ChunkMemoState
{
  static constexpr std::size_t kShardCount = kMaxParallelWorkers + 1u;
  static constexpr std::size_t kEntriesPerShard = 512u;
  static constexpr std::size_t kWays = 4u;
  static constexpr std::size_t kSetCount = kEntriesPerShard / kWays;
  static constexpr std::size_t kProbeBudgetPerShard = 16u;

  enum class AdaptiveMode
  {
    Probe,
    Active,
    Cooldown
  };

  using Key =
    std::array<unsigned char, static_cast<std::size_t>(kHaloDim* kHaloDim)>;

  struct Entry
  {
    Key key{};
    TargetResult result;
    std::uint64_t hash = 0u;
    std::uint64_t stamp = 0u;
    bool valid = false;
  };

  struct Shard
  {
    std::vector<Entry> entries;
    std::uint64_t clock = 0u;
    std::size_t entryCount = 0u;
    std::size_t probes = 0u;
    std::size_t hits = 0u;
    std::size_t misses = 0u;

    void beginGeneration()
    {
      probes = 0u;
      hits = 0u;
      misses = 0u;
    }

    void clearEntries()
    {
      for (Entry& entry : entries) {
        entry.valid = false;
      }
      clock = 0u;
      entryCount = 0u;
    }

    bool canProbe(bool forced, AdaptiveMode mode) const
    {
      return forced || mode == AdaptiveMode::Active ||
             (mode == AdaptiveMode::Probe && probes < kProbeBudgetPerShard);
    }

    bool lookup(const Key& key, std::uint64_t hash, TargetResult* result)
    {
      probes += 1u;
      if (entries.empty()) {
        entries.resize(kEntriesPerShard);
      }
      const std::size_t set = static_cast<std::size_t>(hash) & (kSetCount - 1u);
      const std::size_t begin = set * kWays;
      for (std::size_t way = 0u; way < kWays; ++way) {
        Entry& entry = entries[begin + way];
        if (entry.valid && entry.hash == hash && entry.key == key) {
          entry.stamp = ++clock;
          hits += 1u;
          if (result != nullptr) {
            *result = entry.result;
          }
          return true;
        }
      }
      misses += 1u;
      return false;
    }

    void insert(const Key& key, std::uint64_t hash, const TargetResult& result)
    {
      const std::size_t set = static_cast<std::size_t>(hash) & (kSetCount - 1u);
      const std::size_t begin = set * kWays;
      std::size_t replacement = begin;
      for (std::size_t way = 0u; way < kWays; ++way) {
        Entry& entry = entries[begin + way];
        if (!entry.valid) {
          replacement = begin + way;
          break;
        }
        if (entry.stamp < entries[replacement].stamp) {
          replacement = begin + way;
        }
      }
      Entry& entry = entries[replacement];
      if (!entry.valid) {
        entryCount += 1u;
      }
      entry.key = key;
      entry.result = result;
      entry.result.address = ChunkAddress{};
      entry.hash = hash;
      entry.stamp = ++clock;
      entry.valid = true;
    }
  };

  std::array<Shard, kShardCount> shards;
  AdaptiveMode mode = AdaptiveMode::Probe;
  const std::type_info* ruleType = nullptr;
  std::uint64_t ruleRevision = 0u;
  unsigned int cooldownGenerations = 0u;
  unsigned int lowHitGenerations = 0u;
  bool generationEnabled = false;
  bool generationForced = false;

  static std::uint64_t hashKey(const Key& key)
  {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char state : key) {
      hash ^= state;
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  void clearEntries()
  {
    for (Shard& shard : shards) {
      shard.clearEntries();
    }
  }
};

std::size_t
SparseCellGrid::getCompleteTargetCapacityForTesting() const
{
  return m_completeTargets.capacity();
}

std::size_t
SparseCellGrid::getCompleteTargetIndexCapacityForTesting() const
{
  return m_completeTargetIndex.size();
}

std::size_t
SparseCellGrid::getCompleteResultCapacityForTesting() const
{
  return m_completeResults.capacity();
}

SparseCellGrid::SparseCellGrid() = default;

SparseCellGrid::SparseCellGrid(std::int64_t worldChunkWidth,
                               std::int64_t worldChunkHeight)
{
  if (isValidTopology(worldChunkWidth, worldChunkHeight)) {
    m_worldChunkWidth = worldChunkWidth;
    m_worldChunkHeight = worldChunkHeight;
  }
}

SparseCellGrid::~SparseCellGrid() = default;

int SparseCellGrid::workerOverride = 0;
int SparseCellGrid::cellCandidateOverride = 0;
bool SparseCellGrid::chunkNodeReuseOverride = true;
int SparseCellGrid::chunkMemoOverride = 0;

std::size_t
ChunkAddressHash::operator()(const ChunkAddress& address) const noexcept
{
  const std::size_t hx = mixAddress(static_cast<std::uint64_t>(address.x));
  const std::size_t hy = mixAddress(static_cast<std::uint64_t>(address.y));
  return hx ^
         (hy + static_cast<std::size_t>(0x9e3779b9) + (hx << 6) + (hx >> 2));
}

const SparseCellGrid&
SparseCellGrid::generationSource() const
{
  return m_generationSourceGrid == nullptr ? *this : *m_generationSourceGrid;
}

const SparseCellGrid::ChunkMap&
SparseCellGrid::generationChunks() const
{
  return generationSource().chunks;
}

void
SparseCellGrid::beginAddressSet(std::vector<ChunkAddress>* addresses,
                                std::vector<AddressIndexSlot>* index,
                                std::uint64_t* generation)
{
  if (addresses == nullptr || index == nullptr || generation == nullptr) {
    return;
  }
  addresses->clear();
  *generation += 1u;
  if (*generation == 0u) {
    for (AddressIndexSlot& slot : *index) {
      slot.generation = 0u;
    }
    *generation = 1u;
  }
  if (index->empty()) {
    index->resize(16u);
  }
}

void
SparseCellGrid::ensureAddressSetCapacity(
  std::size_t requiredEntries,
  const std::vector<ChunkAddress>& addresses,
  std::vector<AddressIndexSlot>* index,
  std::uint64_t generation)
{
  if (index == nullptr) {
    return;
  }
  if (index->empty()) {
    index->resize(16u);
  }
  std::size_t newCapacity = index->size();
  while (requiredEntries > newCapacity - newCapacity / 4u) {
    if (newCapacity > std::numeric_limits<std::size_t>::max() / 2u) {
      throw std::bad_alloc();
    }
    newCapacity *= 2u;
  }
  if (newCapacity == index->size()) {
    return;
  }

  std::vector<AddressIndexSlot> replacement;
  replacement.resize(newCapacity);
  const std::size_t mask = newCapacity - 1u;
  for (std::size_t addressIndex = 0u; addressIndex < addresses.size();
       ++addressIndex) {
    const ChunkAddress& address = addresses[addressIndex];
    std::size_t slotIndex = ChunkAddressHash{}(address)&mask;
    while (replacement[slotIndex].generation == generation) {
      slotIndex = (slotIndex + 1u) & mask;
    }
    replacement[slotIndex].address = address;
    replacement[slotIndex].addressIndex = addressIndex;
    replacement[slotIndex].generation = generation;
  }
  index->swap(replacement);
}

bool
SparseCellGrid::insertAddressSet(const ChunkAddress& address,
                                 std::vector<ChunkAddress>* addresses,
                                 std::vector<AddressIndexSlot>* index,
                                 std::uint64_t generation)
{
  if (addresses == nullptr || index == nullptr || generation == 0u) {
    return false;
  }
  ensureAddressSetCapacity(
    addresses->size() + 1u, *addresses, index, generation);
  const std::size_t mask = index->size() - 1u;
  std::size_t slotIndex = ChunkAddressHash{}(address)&mask;
  for (;;) {
    AddressIndexSlot& slot = (*index)[slotIndex];
    if (slot.generation != generation) {
      slot.address = address;
      slot.addressIndex = addresses->size();
      slot.generation = generation;
      addresses->push_back(address);
      return true;
    }
    if (slot.address == address) {
      return false;
    }
    slotIndex = (slotIndex + 1u) & mask;
  }
}

std::size_t
SparseCellGrid::findAddressSetIndex(const ChunkAddress& address,
                                    const std::vector<AddressIndexSlot>& index,
                                    std::uint64_t generation)
{
  if (index.empty() || generation == 0u) {
    return std::numeric_limits<std::size_t>::max();
  }
  const std::size_t mask = index.size() - 1u;
  std::size_t slotIndex = ChunkAddressHash{}(address)&mask;
  for (;;) {
    const AddressIndexSlot& slot = index[slotIndex];
    if (slot.generation != generation) {
      return std::numeric_limits<std::size_t>::max();
    }
    if (slot.address == address) {
      return slot.addressIndex;
    }
    slotIndex = (slotIndex + 1u) & mask;
  }
}

bool
SparseCellGrid::markChangedChunk(const ChunkAddress& address,
                                 const OccupancyMask& stateChanged,
                                 const OccupancyMask& countedChanged)
{
  if (!hasMaskBits(stateChanged) && !hasMaskBits(countedChanged)) {
    return false;
  }
  if (m_frontierInvalid) {
    return false;
  }
  try {
    if (m_changedChunkGeneration == 0u) {
      beginAddressSet(
        &m_changedChunks, &m_changedChunkIndex, &m_changedChunkGeneration);
      m_changedCellMasks.clear();
      m_changedCountedMasks.clear();
    }
    const bool inserted = insertAddressSet(address,
                                           &m_changedChunks,
                                           &m_changedChunkIndex,
                                           m_changedChunkGeneration);
    const std::size_t addressIndex = findAddressSetIndex(
      address, m_changedChunkIndex, m_changedChunkGeneration);
    if (addressIndex == std::numeric_limits<std::size_t>::max()) {
      m_frontierInvalid = true;
      return false;
    }
    if (inserted) {
      m_changedCellMasks.push_back(stateChanged);
      m_changedCountedMasks.push_back(countedChanged);
    } else {
      mergeMask(&m_changedCellMasks[addressIndex], stateChanged);
      mergeMask(&m_changedCountedMasks[addressIndex], countedChanged);
    }
    if (m_changedChunks.size() > kFrontierTrackingLimit) {
      invalidateChangedChunkTracking();
      return false;
    }
    return inserted;
  } catch (const std::bad_alloc&) {
    invalidateChangedChunkTracking();
    return false;
  }
}

void
SparseCellGrid::beginNextChangedChunks()
{
  beginAddressSet(&m_nextChangedChunks,
                  &m_nextChangedChunkIndex,
                  &m_nextChangedChunkGeneration);
  m_nextChangedCellMasks.clear();
  m_nextChangedCountedMasks.clear();
}

void
SparseCellGrid::invalidateChangedChunkTracking()
{
  m_frontierInvalid = true;
  m_changedChunks.clear();
  m_changedCellMasks.clear();
  m_changedCountedMasks.clear();
}

void
SparseCellGrid::invalidateNextChangedChunkTracking()
{
  m_frontierInvalid = true;
  m_nextChangedChunks.clear();
  m_nextChangedCellMasks.clear();
  m_nextChangedCountedMasks.clear();
}

bool
SparseCellGrid::markNextChangedChunk(const ChunkAddress& address,
                                     const OccupancyMask& stateChanged,
                                     const OccupancyMask& countedChanged)
{
  if (m_frontierInvalid ||
      (!hasMaskBits(stateChanged) && !hasMaskBits(countedChanged))) {
    return false;
  }
  try {
    const bool inserted = insertAddressSet(address,
                                           &m_nextChangedChunks,
                                           &m_nextChangedChunkIndex,
                                           m_nextChangedChunkGeneration);
    const std::size_t addressIndex = findAddressSetIndex(
      address, m_nextChangedChunkIndex, m_nextChangedChunkGeneration);
    if (addressIndex == std::numeric_limits<std::size_t>::max()) {
      invalidateNextChangedChunkTracking();
      return false;
    }
    if (inserted) {
      m_nextChangedCellMasks.push_back(stateChanged);
      m_nextChangedCountedMasks.push_back(countedChanged);
    } else {
      mergeMask(&m_nextChangedCellMasks[addressIndex], stateChanged);
      mergeMask(&m_nextChangedCountedMasks[addressIndex], countedChanged);
    }
    if (m_nextChangedChunks.size() > kFrontierTrackingLimit) {
      invalidateNextChangedChunkTracking();
      return false;
    }
    return inserted;
  } catch (const std::bad_alloc&) {
    invalidateNextChangedChunkTracking();
    return false;
  }
}

void
SparseCellGrid::commitNextChangedChunks()
{
  m_changedChunks.swap(m_nextChangedChunks);
  m_changedCellMasks.swap(m_nextChangedCellMasks);
  m_changedCountedMasks.swap(m_nextChangedCountedMasks);
  m_changedChunkIndex.swap(m_nextChangedChunkIndex);
  std::swap(m_changedChunkGeneration, m_nextChangedChunkGeneration);
}

std::size_t
SparseCellGrid::countMaskBits(const OccupancyMask& mask)
{
  std::size_t count = 0u;
  for (std::uint64_t word : mask) {
    count += static_cast<std::size_t>(std::popcount(word));
  }
  return count;
}

bool
SparseCellGrid::hasMaskBits(const OccupancyMask& mask)
{
  for (std::uint64_t word : mask) {
    if (word != 0u) {
      return true;
    }
  }
  return false;
}

void
SparseCellGrid::mergeMask(OccupancyMask* destination,
                          const OccupancyMask& source)
{
  if (destination == nullptr) {
    return;
  }
  for (std::size_t index = 0u; index < destination->size(); ++index) {
    (*destination)[index] |= source[index];
  }
}

void
SparseCellGrid::buildChangeMasks(const ChunkData* previous,
                                 const ChunkData* next,
                                 OccupancyMask* stateChanged,
                                 OccupancyMask* countedChanged)
{
  if (stateChanged == nullptr || countedChanged == nullptr) {
    return;
  }
  stateChanged->fill(0u);
  countedChanged->fill(0u);
  for (std::size_t index = 0u; index < kChunkCellCount; ++index) {
    const unsigned char previousState =
      previous == nullptr ? BackgroundState : previous->cells[index];
    const unsigned char nextState =
      next == nullptr ? BackgroundState : next->cells[index];
    const std::size_t wordIndex = index / 64u;
    const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                              << static_cast<unsigned int>(index % 64u);
    if (previousState != nextState) {
      (*stateChanged)[wordIndex] |= bit;
    }
    if ((previousState == CountedNeighborState) !=
        (nextState == CountedNeighborState)) {
      (*countedChanged)[wordIndex] |= bit;
    }
  }
}

void
SparseCellGrid::enrollAffectedTargets(const ChunkAddress& source,
                                      const OccupancyMask& selfChanged,
                                      const OccupancyMask& countedChanged,
                                      std::vector<ChunkAddress>* targets,
                                      std::vector<AddressIndexSlot>* index,
                                      std::uint64_t generation)
{
  if (targets == nullptr || index == nullptr ||
      (!hasMaskBits(selfChanged) && !hasMaskBits(countedChanged))) {
    return;
  }
  insertAddressSet(source, targets, index, generation);

  const bool left = (countedChanged[0] & kChunkLeftEdgeMask) != 0u ||
                    (countedChanged[1] & kChunkLeftEdgeMask) != 0u ||
                    (countedChanged[2] & kChunkLeftEdgeMask) != 0u ||
                    (countedChanged[3] & kChunkLeftEdgeMask) != 0u;
  const bool right = (countedChanged[0] & kChunkRightEdgeMask) != 0u ||
                     (countedChanged[1] & kChunkRightEdgeMask) != 0u ||
                     (countedChanged[2] & kChunkRightEdgeMask) != 0u ||
                     (countedChanged[3] & kChunkRightEdgeMask) != 0u;
  const bool top = (countedChanged[0] & kChunkTopRowMask) != 0u;
  const bool bottom = (countedChanged[3] & kChunkBottomRowMask) != 0u;
  if (left) {
    insertAddressSet(
      ChunkAddress{ source.x - 1, source.y }, targets, index, generation);
  }
  if (right) {
    insertAddressSet(
      ChunkAddress{ source.x + 1, source.y }, targets, index, generation);
  }
  if (top) {
    insertAddressSet(
      ChunkAddress{ source.x, source.y - 1 }, targets, index, generation);
  }
  if (bottom) {
    insertAddressSet(
      ChunkAddress{ source.x, source.y + 1 }, targets, index, generation);
  }
  if ((countedChanged[0] & 1u) != 0u) {
    insertAddressSet(
      ChunkAddress{ source.x - 1, source.y - 1 }, targets, index, generation);
  }
  if ((countedChanged[0] & (static_cast<std::uint64_t>(1u) << 15u)) != 0u) {
    insertAddressSet(
      ChunkAddress{ source.x + 1, source.y - 1 }, targets, index, generation);
  }
  if ((countedChanged[3] & (static_cast<std::uint64_t>(1u) << 48u)) != 0u) {
    insertAddressSet(
      ChunkAddress{ source.x - 1, source.y + 1 }, targets, index, generation);
  }
  if ((countedChanged[3] & (static_cast<std::uint64_t>(1u) << 63u)) != 0u) {
    insertAddressSet(
      ChunkAddress{ source.x + 1, source.y + 1 }, targets, index, generation);
  }
}

void
SparseCellGrid::publishChangedChunksRevision(bool valid)
{
  m_changedChunksRevision = revision;
  m_changedChunksRevisionValid = false;
  if (!valid) {
    return;
  }
  try {
    beginAddressSet(&m_presentationChangedChunks,
                    &m_presentationChangedChunkIndex,
                    &m_presentationChangedChunkGeneration);
    for (const ChunkAddress& address : m_changedChunks) {
      insertAddressSet(address,
                       &m_presentationChangedChunks,
                       &m_presentationChangedChunkIndex,
                       m_presentationChangedChunkGeneration);
    }
    m_changedChunksRevisionValid = true;
  } catch (const std::bad_alloc&) {
    m_presentationChangedChunks.clear();
  }
}

void
SparseCellGrid::publishChangedChunkRevision(const ChunkAddress& address,
                                            bool valid)
{
  m_changedChunksRevision = revision;
  m_changedChunksRevisionValid = false;
  if (!valid) {
    return;
  }
  try {
    beginAddressSet(&m_presentationChangedChunks,
                    &m_presentationChangedChunkIndex,
                    &m_presentationChangedChunkGeneration);
    insertAddressSet(address,
                     &m_presentationChangedChunks,
                     &m_presentationChangedChunkIndex,
                     m_presentationChangedChunkGeneration);
    m_changedChunksRevisionValid = true;
  } catch (const std::bad_alloc&) {
    m_presentationChangedChunks.clear();
  }
}

void
SparseCellGrid::collectChangedChunksBetweenMaps()
{
  beginNextChangedChunks();
  for (ChunkMap::const_reference entry : chunks) {
    const ChunkMap::const_iterator next = m_nextChunks.find(entry.first);
    OccupancyMask stateChanged;
    OccupancyMask countedChanged;
    buildChangeMasks(&entry.second,
                     next == m_nextChunks.end() ? nullptr : &next->second,
                     &stateChanged,
                     &countedChanged);
    if (hasMaskBits(stateChanged)) {
      markNextChangedChunk(entry.first, stateChanged, countedChanged);
      if (m_frontierInvalid) {
        return;
      }
    }
  }
  for (ChunkMap::const_reference entry : m_nextChunks) {
    if (chunks.find(entry.first) == chunks.end()) {
      OccupancyMask stateChanged;
      OccupancyMask countedChanged;
      buildChangeMasks(nullptr, &entry.second, &stateChanged, &countedChanged);
      markNextChangedChunk(entry.first, stateChanged, countedChanged);
      if (m_frontierInvalid) {
        return;
      }
    }
  }
}

void
SparseCellGrid::buildFrontierTargets()
{
  ZoneScopedN("SparseCellGrid.buildFrontierTargets");
  beginAddressSet(
    &m_frontierTargets, &m_frontierTargetIndex, &m_frontierTargetGeneration);
  for (std::size_t index = 0u; index < m_changedChunks.size(); ++index) {
    enrollAffectedTargets(m_changedChunks[index],
                          m_changedCellMasks[index],
                          m_changedCountedMasks[index],
                          &m_frontierTargets,
                          &m_frontierTargetIndex,
                          m_frontierTargetGeneration);
  }
}

void
SparseCellGrid::buildCompleteTargets()
{
  ZoneScopedN("SparseCellGrid.buildCompleteTargets");
  const ChunkMap& sourceChunks = generationChunks();
  beginAddressSet(
    &m_completeTargets, &m_completeTargetIndex, &m_completeTargetGeneration);
  for (ChunkMap::const_reference entry : sourceChunks) {
    enrollAffectedTargets(entry.first,
                          entry.second.occupied,
                          entry.second.counted,
                          &m_completeTargets,
                          &m_completeTargetIndex,
                          m_completeTargetGeneration);
  }
}

std::size_t
SparseCellGrid::saturatingAdd(std::size_t left, std::size_t right)
{
  if (left > std::numeric_limits<std::size_t>::max() - right) {
    return std::numeric_limits<std::size_t>::max();
  }
  return left + right;
}

std::size_t
SparseCellGrid::saturatingMultiply(std::size_t left, std::size_t right)
{
  if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
    return std::numeric_limits<std::size_t>::max();
  }
  return left * right;
}

std::size_t
SparseCellGrid::estimateCompleteAdvanceWork() const
{
  const SparseCellGrid& source = generationSource();
  const std::size_t chunkBookkeeping =
    saturatingMultiply(source.chunks.size(), 2u);
  if (source.m_chunkStatistics.candidatePreferredChunkCount == 0u) {
    return saturatingAdd(
      chunkBookkeeping,
      saturatingMultiply(source.chunks.size(), kChunkCellCount));
  }

  const std::size_t neighborContributions =
    saturatingMultiply(source.m_chunkStatistics.countedCellCount, 8u);
  const std::size_t affectedCells = saturatingAdd(
    source.m_chunkStatistics.activeCellCount, neighborContributions);
  return saturatingAdd(chunkBookkeeping, saturatingMultiply(affectedCells, 2u));
}

bool
SparseCellGrid::buildFrontierCandidateScratch(std::size_t* estimatedWork,
                                              std::size_t* evaluationWork)
{
  ZoneScopedN("SparseCellGrid.buildFrontierCandidateScratch");
  if (estimatedWork == nullptr || evaluationWork == nullptr) {
    return false;
  }
  *estimatedWork = 0u;
  *evaluationWork = 0u;

  beginCandidateIndexGeneration();
  for (const ChunkAddress& target : m_frontierTargets) {
    CandidateScratchChunk* scratch = findOrCreateCandidateScratch(target);
    populateCandidateSources(scratch);
  }
  classifyHaloScratchTargets();

  beginAddressSet(&m_frontierSourceChunks,
                  &m_frontierSourceChunkIndex,
                  &m_frontierSourceChunkGeneration);
  for (const ChunkAddress& target : m_frontierTargets) {
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
      for (int offsetX = -1; offsetX <= 1; ++offsetX) {
        const ChunkAddress sourceAddress{ target.x + offsetX,
                                          target.y + offsetY };
        if (chunks.find(sourceAddress) != chunks.end()) {
          insertAddressSet(sourceAddress,
                           &m_frontierSourceChunks,
                           &m_frontierSourceChunkIndex,
                           m_frontierSourceChunkGeneration);
        }
      }
    }
  }

  std::size_t preparationWork = 0u;
  for (const ChunkAddress& sourceAddress : m_frontierSourceChunks) {
    if (!sourceNeedsCandidatePreparation(sourceAddress)) {
      continue;
    }
    const ChunkMap::const_iterator source = chunks.find(sourceAddress);
    if (source == chunks.end()) {
      continue;
    }
    preparationWork = saturatingAdd(
      preparationWork,
      saturatingAdd(source->second.occupiedCellCount,
                    saturatingMultiply(source->second.countedCellCount, 8u)));
  }

  lastAdvanceStats.candidatePreparationWorkerCount =
    resolveCandidatePreparationWorkerCount(m_candidateScratch.size(),
                                           preparationWork);
  if (lastAdvanceStats.candidatePreparationWorkerCount > 1u) {
    if (workerPool == nullptr) {
      workerPool = std::make_unique<SparseWorkerPool>();
    }
    buildCandidatePreparationRanges(
      lastAdvanceStats.candidatePreparationWorkerCount);
    workerPool->prepareCandidates(
      this,
      &m_candidateScratch,
      &m_candidatePreparationRanges,
      lastAdvanceStats.candidatePreparationWorkerCount);
  } else {
    for (const ChunkAddress& sourceAddress : m_frontierSourceChunks) {
      if (!sourceNeedsCandidatePreparation(sourceAddress)) {
        continue;
      }
      const ChunkMap::const_iterator source = chunks.find(sourceAddress);
      if (source != chunks.end()) {
        prepareCandidateScratchFromSource(sourceAddress, source->second);
      }
    }
  }

  std::size_t candidateCellCount = 0u;
  std::size_t neighborContributionCount = 0u;
  for (CandidateScratchChunk& scratch : m_candidateScratch) {
    candidateCellCount =
      saturatingAdd(candidateCellCount, scratch.candidateCellCount);
    neighborContributionCount = saturatingAdd(
      neighborContributionCount, scratch.neighborContributionCount);
    scratch.useCellCandidates =
      !scratch.skipNeighborPrep && scratch.neighborContributionCount <
                                     kCandidateNeighborContributionThreshold;
    *evaluationWork = saturatingAdd(
      *evaluationWork,
      scratch.useCellCandidates ? scratch.candidateCellCount : kChunkCellCount);
  }

  *estimatedWork =
    saturatingAdd(saturatingMultiply(m_frontierTargets.size(), 9u),
                  m_frontierSourceChunks.size());
  *estimatedWork = saturatingAdd(*estimatedWork, candidateCellCount);
  *estimatedWork = saturatingAdd(*estimatedWork, neighborContributionCount);
  *estimatedWork = saturatingAdd(*estimatedWork, *evaluationWork);
  return true;
}

bool
SparseCellGrid::frontierPrefersCandidateScratch() const
{
  if (m_frontierTargets.empty()) {
    return false;
  }
  if (m_frontierTargets.size() >= kFrontierScratchTargetLimit) {
    return false;
  }
  const ChunkMap& sourceChunks = generationChunks();
  std::size_t preferredTargets = 0u;
  for (const ChunkAddress& target : m_frontierTargets) {
    const ChunkMap::const_iterator found = sourceChunks.find(target);
    if (found == sourceChunks.end() ||
        isCandidatePreferredChunk(found->second)) {
      preferredTargets += 1u;
    }
  }
  return preferredTargets * kFrontierScratchPreferredDivisor >=
         m_frontierTargets.size();
}

std::int64_t
SparseCellGrid::floorDivide(std::int64_t value, std::int64_t divisor)
{
  if (divisor <= 0) {
    return 0;
  }
  const std::int64_t quotient = value / divisor;
  const std::int64_t remainder = value % divisor;
  if (remainder < 0) {
    return quotient - 1;
  }
  return quotient;
}

std::int64_t
SparseCellGrid::floorModulo(std::int64_t value, std::int64_t divisor)
{
  if (divisor <= 0) {
    return 0;
  }
  const std::int64_t remainder = value % divisor;
  return remainder < 0 ? remainder + divisor : remainder;
}

ChunkAddress
SparseCellGrid::chunkAddressForCell(const CellAddress& address)
{
  ChunkAddress result;
  result.x = floorDivide(address.x, kChunkDim);
  result.y = floorDivide(address.y, kChunkDim);
  return result;
}

int
SparseCellGrid::localIndexForCell(const CellAddress& address)
{
  const int localX = static_cast<int>(floorModulo(address.x, kChunkDim));
  const int localY = static_cast<int>(floorModulo(address.y, kChunkDim));
  return localY * kChunkDim + localX;
}

bool
SparseCellGrid::isValidTopology(std::int64_t worldChunkWidth,
                                std::int64_t worldChunkHeight)
{
  if (worldChunkWidth == 0 && worldChunkHeight == 0) {
    return true;
  }
  return worldChunkWidth > 0 && worldChunkHeight > 0 &&
         worldChunkWidth <= kMaximumWorldChunksPerAxis &&
         worldChunkHeight <= kMaximumWorldChunksPerAxis;
}

bool
SparseCellGrid::isCellInWorldBounds(const CellAddress& address) const
{
  if (!isToroidal()) {
    return true;
  }

  const std::int64_t minimumCellX = -(m_worldChunkWidth / 2) * kChunkDim;
  const std::int64_t minimumCellY = -(m_worldChunkHeight / 2) * kChunkDim;
  const std::int64_t maximumCellX =
    minimumCellX + m_worldChunkWidth * kChunkDim;
  const std::int64_t maximumCellY =
    minimumCellY + m_worldChunkHeight * kChunkDim;
  return address.x >= minimumCellX && address.x < maximumCellX &&
         address.y >= minimumCellY && address.y < maximumCellY;
}

CellAddress
SparseCellGrid::canonicalizeCell(const CellAddress& address) const
{
  if (!isToroidal()) {
    return address;
  }

  const std::int64_t minimumChunkX = -(m_worldChunkWidth / 2);
  const std::int64_t minimumChunkY = -(m_worldChunkHeight / 2);
  const std::int64_t minimumCellX = minimumChunkX * kChunkDim;
  const std::int64_t minimumCellY = minimumChunkY * kChunkDim;
  const std::int64_t cellWidth = m_worldChunkWidth * kChunkDim;
  const std::int64_t cellHeight = m_worldChunkHeight * kChunkDim;

  const std::int64_t xOffset = floorModulo(
    floorModulo(address.x, cellWidth) - floorModulo(minimumCellX, cellWidth),
    cellWidth);
  const std::int64_t yOffset = floorModulo(
    floorModulo(address.y, cellHeight) - floorModulo(minimumCellY, cellHeight),
    cellHeight);
  return CellAddress{ minimumCellX + xOffset, minimumCellY + yOffset };
}

ChunkAddress
SparseCellGrid::canonicalizeChunk(const ChunkAddress& address) const
{
  if (!isToroidal()) {
    return address;
  }

  const std::int64_t minimumX = -(m_worldChunkWidth / 2);
  const std::int64_t minimumY = -(m_worldChunkHeight / 2);
  const std::int64_t xOffset =
    floorModulo(floorModulo(address.x, m_worldChunkWidth) -
                  floorModulo(minimumX, m_worldChunkWidth),
                m_worldChunkWidth);
  const std::int64_t yOffset =
    floorModulo(floorModulo(address.y, m_worldChunkHeight) -
                  floorModulo(minimumY, m_worldChunkHeight),
                m_worldChunkHeight);
  return ChunkAddress{ minimumX + xOffset, minimumY + yOffset };
}

void
SparseCellGrid::setWorkerOverrideForTesting(int workers)
{
  workerOverride = workers < 0 ? 0 : workers;
}

int
SparseCellGrid::getWorkerOverrideForTesting()
{
  return workerOverride;
}

void
SparseCellGrid::setCellCandidateOverrideForTesting(int mode)
{
  if (mode < -1) {
    mode = -1;
  }
  if (mode > 1) {
    mode = 1;
  }
  cellCandidateOverride = mode;
}

int
SparseCellGrid::getCellCandidateOverrideForTesting()
{
  return cellCandidateOverride;
}

void
SparseCellGrid::setChunkNodeReuseOverrideForTesting(bool enabled)
{
  chunkNodeReuseOverride = enabled;
}

bool
SparseCellGrid::getChunkNodeReuseOverrideForTesting()
{
  return chunkNodeReuseOverride;
}

void
SparseCellGrid::setChunkMemoOverrideForTesting(int mode)
{
  chunkMemoOverride = mode;
}

int
SparseCellGrid::getChunkMemoOverrideForTesting()
{
  return chunkMemoOverride;
}

SparseCellGrid::CandidateScratchChunk*
SparseCellGrid::findCandidateScratch(const ChunkAddress& address)
{
  if (m_candidateIndex.empty()) {
    return nullptr;
  }
  const std::size_t mask = m_candidateIndex.size() - 1u;
  std::size_t slotIndex = ChunkAddressHash{}(address)&mask;
  for (;;) {
    CandidateIndexSlot& slot = m_candidateIndex[slotIndex];
    if (slot.generation != m_candidateIndexGeneration) {
      return nullptr;
    }
    if (slot.address == address) {
      return &m_candidateScratch[slot.scratchIndex];
    }
    slotIndex = (slotIndex + 1u) & mask;
  }
}

void
SparseCellGrid::beginCandidateIndexGeneration()
{
  m_candidateScratch.clear();
  m_candidateIndexGeneration += 1u;
  if (m_candidateIndexGeneration == 0u) {
    for (CandidateIndexSlot& slot : m_candidateIndex) {
      slot.generation = 0u;
    }
    m_candidateIndexGeneration = 1u;
  }
  if (m_candidateIndex.empty()) {
    m_candidateIndex.resize(16u);
  }
}

void
SparseCellGrid::ensureCandidateIndexCapacity(std::size_t requiredEntries)
{
  if (m_candidateIndex.empty()) {
    m_candidateIndex.resize(16u);
  }
  std::size_t newCapacity = m_candidateIndex.size();
  while (requiredEntries > newCapacity - newCapacity / 4u) {
    if (newCapacity > std::numeric_limits<std::size_t>::max() / 2u) {
      throw std::bad_alloc();
    }
    newCapacity *= 2u;
  }
  if (newCapacity == m_candidateIndex.size()) {
    return;
  }

  std::vector<CandidateIndexSlot> replacement;
  replacement.resize(newCapacity);
  const std::size_t mask = newCapacity - 1u;
  for (std::size_t scratchIndex = 0u; scratchIndex < m_candidateScratch.size();
       ++scratchIndex) {
    const ChunkAddress& address = m_candidateScratch[scratchIndex].address;
    std::size_t slotIndex = ChunkAddressHash{}(address)&mask;
    while (replacement[slotIndex].generation == m_candidateIndexGeneration) {
      slotIndex = (slotIndex + 1u) & mask;
    }
    CandidateIndexSlot& slot = replacement[slotIndex];
    slot.address = address;
    slot.scratchIndex = scratchIndex;
    slot.generation = m_candidateIndexGeneration;
  }
  m_candidateIndex.swap(replacement);
  lastAdvanceStats.candidateIndexGrowthCount += 1u;
}

SparseCellGrid::CandidateScratchChunk*
SparseCellGrid::findOrCreateCandidateScratch(const ChunkAddress& address)
{
  for (;;) {
    if (m_candidateIndex.empty()) {
      ensureCandidateIndexCapacity(1u);
    }
    const std::size_t mask = m_candidateIndex.size() - 1u;
    std::size_t slotIndex = ChunkAddressHash{}(address)&mask;
    for (;;) {
      CandidateIndexSlot& slot = m_candidateIndex[slotIndex];
      if (slot.generation != m_candidateIndexGeneration) {
        const std::size_t requiredEntries = m_candidateScratch.size() + 1u;
        const std::size_t entryCapacity =
          m_candidateIndex.size() - m_candidateIndex.size() / 4u;
        if (requiredEntries > entryCapacity) {
          ensureCandidateIndexCapacity(requiredEntries);
          break;
        }
        const std::size_t scratchIndex = m_candidateScratch.size();
        m_candidateScratch.emplace_back();
        CandidateScratchChunk& scratch = m_candidateScratch.back();
        scratch.address = address;
        slot.address = address;
        slot.scratchIndex = scratchIndex;
        slot.generation = m_candidateIndexGeneration;
        return &scratch;
      }
      if (slot.address == address) {
        return &m_candidateScratch[slot.scratchIndex];
      }
      slotIndex = (slotIndex + 1u) & mask;
    }
  }
}

void
SparseCellGrid::enrollCandidateTarget(const ChunkAddress& targetAddress,
                                      int sourceOffsetX,
                                      int sourceOffsetY,
                                      const ChunkData* source,
                                      bool linkSources)
{
  lastAdvanceStats.candidateEnrollmentAttemptCount += 1u;
  if (linkSources) {
    linkCandidateSource(targetAddress, sourceOffsetX, sourceOffsetY, source);
  } else {
    findOrCreateCandidateScratch(targetAddress);
  }
}

void
SparseCellGrid::linkCandidateSource(const ChunkAddress& targetAddress,
                                    int sourceOffsetX,
                                    int sourceOffsetY,
                                    const ChunkData* source)
{
  if (source == nullptr || sourceOffsetX < -1 || sourceOffsetX > 1 ||
      sourceOffsetY < -1 || sourceOffsetY > 1) {
    return;
  }
  CandidateScratchChunk* scratch = findOrCreateCandidateScratch(targetAddress);
  if (!scratch->sourcesLinked) {
    scratch->sources.fill(nullptr);
  }
  const std::size_t sourceIndex =
    static_cast<std::size_t>((sourceOffsetY + 1) * 3 + sourceOffsetX + 1);
  scratch->sources[sourceIndex] = source;
  scratch->sourcesLinked = true;
  if (sourceOffsetX == 0 && sourceOffsetY == 0) {
    scratch->centerSource = source;
    scratch->centerSourceKnown = true;
  }
}

void
SparseCellGrid::populateCandidateSources(CandidateScratchChunk* scratch)
{
  if (scratch == nullptr) {
    return;
  }
  const ChunkMap& sourceChunks = generationChunks();
  scratch->sources.fill(nullptr);
  for (int sourceOffsetY = -1; sourceOffsetY <= 1; ++sourceOffsetY) {
    for (int sourceOffsetX = -1; sourceOffsetX <= 1; ++sourceOffsetX) {
      const ChunkAddress sourceAddress{ scratch->address.x + sourceOffsetX,
                                        scratch->address.y + sourceOffsetY };
      const ChunkMap::const_iterator source = sourceChunks.find(sourceAddress);
      const std::size_t sourceIndex =
        static_cast<std::size_t>((sourceOffsetY + 1) * 3 + sourceOffsetX + 1);
      const ChunkData* sourcePointer =
        source == sourceChunks.end() ? nullptr : &source->second;
      scratch->sources[sourceIndex] = sourcePointer;
      if (sourceOffsetX == 0 && sourceOffsetY == 0) {
        scratch->centerSource = sourcePointer;
        scratch->centerSourceKnown = true;
      }
    }
  }
  scratch->sourcesLinked = true;
}

void
SparseCellGrid::classifyHaloScratchTargets()
{
  const ChunkMap& sourceChunks = generationChunks();
  for (CandidateScratchChunk& scratch : m_candidateScratch) {
    const ChunkData* center = nullptr;
    if (scratch.centerSourceKnown) {
      center = scratch.centerSource;
    } else {
      const ChunkMap::const_iterator found = sourceChunks.find(scratch.address);
      if (found != sourceChunks.end()) {
        center = &found->second;
      }
    }
    if (center != nullptr && !isCandidatePreferredChunk(*center)) {
      scratch.useCellCandidates = false;
      scratch.skipNeighborPrep = true;
    }
  }
}

bool
SparseCellGrid::sourceNeedsCandidatePreparation(
  const ChunkAddress& sourceAddress)
{
  for (int offsetY = -1; offsetY <= 1; ++offsetY) {
    for (int offsetX = -1; offsetX <= 1; ++offsetX) {
      const ChunkAddress targetAddress{ sourceAddress.x + offsetX,
                                        sourceAddress.y + offsetY };
      CandidateScratchChunk* target = findCandidateScratch(targetAddress);
      if (target != nullptr && !target->skipNeighborPrep) {
        return true;
      }
    }
  }
  return false;
}

bool
SparseCellGrid::markCandidate(CandidateScratchChunk* scratch, std::size_t index)
{
  if (scratch == nullptr || index >= kChunkCellCount) {
    return false;
  }
  const std::size_t wordIndex = index / 64u;
  const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                            << static_cast<unsigned int>(index % 64u);
  if ((scratch->candidates[wordIndex] & bit) != 0u) {
    return false;
  }
  scratch->candidates[wordIndex] |= bit;
  scratch->neighborCounts[index] = 0u;
  return true;
}

void
SparseCellGrid::prepareCandidateScratchFromSource(
  const ChunkAddress& sourceAddress,
  const ChunkData& source)
{
  CandidateScratchChunk* center = findCandidateScratch(sourceAddress);
  if (center != nullptr && !center->skipNeighborPrep) {
    for (std::size_t wordIndex = 0u; wordIndex < source.occupied.size();
         ++wordIndex) {
      std::uint64_t occupied = source.occupied[wordIndex];
      while (occupied != 0u) {
        const unsigned int offset = std::countr_zero(occupied);
        const std::size_t index = wordIndex * 64u + offset;
        occupied &= occupied - 1u;
        if (markCandidate(center, index)) {
          center->candidateCellCount += 1u;
        }
      }
    }
  }

  for (std::size_t wordIndex = 0u; wordIndex < source.counted.size();
       ++wordIndex) {
    std::uint64_t counted = source.counted[wordIndex];
    while (counted != 0u) {
      const unsigned int offset = std::countr_zero(counted);
      const std::size_t index = wordIndex * 64u + offset;
      counted &= counted - 1u;
      const int sourceLocalX = static_cast<int>(index % kChunkDim);
      const int sourceLocalY = static_cast<int>(index / kChunkDim);
      for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
          if (offsetX == 0 && offsetY == 0) {
            continue;
          }
          int targetLocalX = sourceLocalX + offsetX;
          int targetLocalY = sourceLocalY + offsetY;
          ChunkAddress targetAddress = sourceAddress;
          if (targetLocalX < 0) {
            targetAddress.x -= 1;
            targetLocalX += kChunkDim;
          } else if (targetLocalX >= kChunkDim) {
            targetAddress.x += 1;
            targetLocalX -= kChunkDim;
          }
          if (targetLocalY < 0) {
            targetAddress.y -= 1;
            targetLocalY += kChunkDim;
          } else if (targetLocalY >= kChunkDim) {
            targetAddress.y += 1;
            targetLocalY -= kChunkDim;
          }

          CandidateScratchChunk* target = findCandidateScratch(targetAddress);
          if (target == nullptr || target->skipNeighborPrep) {
            continue;
          }
          const std::size_t targetIndex =
            static_cast<std::size_t>(targetLocalY * kChunkDim + targetLocalX);
          if (markCandidate(target, targetIndex)) {
            target->candidateCellCount += 1u;
          }
          target->neighborCounts[targetIndex] = static_cast<unsigned char>(
            target->neighborCounts[targetIndex] + 1u);
          target->neighborContributionCount += 1u;
        }
      }
    }
  }
}

void
SparseCellGrid::prepareCandidateScratchChunk(
  CandidateScratchChunk* scratch) const
{
  if (scratch == nullptr) {
    return;
  }
  if (scratch->skipNeighborPrep) {
    scratch->sourcesLinked = false;
    return;
  }

  if (!scratch->sourcesLinked) {
    return;
  }
  scratch->sourcesLinked = false;
  const ChunkData* center = scratch->sources[4u];
  if (center != nullptr) {
    for (std::size_t wordIndex = 0u; wordIndex < center->occupied.size();
         ++wordIndex) {
      std::uint64_t occupied = center->occupied[wordIndex];
      while (occupied != 0u) {
        const unsigned int offset = std::countr_zero(occupied);
        const std::size_t index = wordIndex * 64u + offset;
        occupied &= occupied - 1u;
        if (markCandidate(scratch, index)) {
          scratch->candidateCellCount += 1u;
        }
      }
    }
  }

  for (int sourceOffsetY = -1; sourceOffsetY <= 1; ++sourceOffsetY) {
    for (int sourceOffsetX = -1; sourceOffsetX <= 1; ++sourceOffsetX) {
      const std::size_t sourceIndex =
        static_cast<std::size_t>((sourceOffsetY + 1) * 3 + sourceOffsetX + 1);
      const ChunkData* source = scratch->sources[sourceIndex];
      if (source == nullptr) {
        continue;
      }

      for (std::size_t wordIndex = 0u; wordIndex < source->counted.size();
           ++wordIndex) {
        std::uint64_t counted = source->counted[wordIndex];
        if (sourceOffsetX < 0) {
          counted &= kChunkRightEdgeMask;
        } else if (sourceOffsetX > 0) {
          counted &= kChunkLeftEdgeMask;
        }
        if (sourceOffsetY < 0) {
          counted = wordIndex == source->counted.size() - 1u
                      ? counted & kChunkBottomRowMask
                      : 0u;
        } else if (sourceOffsetY > 0) {
          counted = wordIndex == 0u ? counted & kChunkTopRowMask : 0u;
        }

        while (counted != 0u) {
          const unsigned int offset = std::countr_zero(counted);
          const std::size_t index = wordIndex * 64u + offset;
          counted &= counted - 1u;
          const int sourceLocalX = static_cast<int>(index % kChunkDim);
          const int sourceLocalY = static_cast<int>(index / kChunkDim);
          for (int offsetY = -1; offsetY <= 1; ++offsetY) {
            for (int offsetX = -1; offsetX <= 1; ++offsetX) {
              if (offsetX == 0 && offsetY == 0) {
                continue;
              }
              const int targetLocalX =
                sourceOffsetX * kChunkDim + sourceLocalX + offsetX;
              const int targetLocalY =
                sourceOffsetY * kChunkDim + sourceLocalY + offsetY;
              if (targetLocalX < 0 || targetLocalX >= kChunkDim ||
                  targetLocalY < 0 || targetLocalY >= kChunkDim) {
                continue;
              }

              const std::size_t targetIndex = static_cast<std::size_t>(
                targetLocalY * kChunkDim + targetLocalX);
              if (markCandidate(scratch, targetIndex)) {
                scratch->candidateCellCount += 1u;
              }
              scratch->neighborCounts[targetIndex] = static_cast<unsigned char>(
                scratch->neighborCounts[targetIndex] + 1u);
              scratch->neighborContributionCount += 1u;
            }
          }
        }
      }
    }
  }
}

void
SparseCellGrid::buildCandidatePreparationRanges(unsigned int workerCount)
{
  m_candidatePreparationRanges.clear();
  if (m_candidateScratch.empty()) {
    return;
  }
  const std::size_t desiredRangeCount =
    std::max<std::size_t>(1u, static_cast<std::size_t>(workerCount) * 8u);
  const std::size_t unboundedTargetsPerRange =
    (m_candidateScratch.size() + desiredRangeCount - 1u) / desiredRangeCount;
  const std::size_t targetsPerRange =
    std::clamp<std::size_t>(unboundedTargetsPerRange, 1u, 256u);
  for (std::size_t begin = 0u; begin < m_candidateScratch.size();
       begin += targetsPerRange) {
    m_candidatePreparationRanges.push_back(CandidateWorkRange{
      begin, std::min(begin + targetsPerRange, m_candidateScratch.size()) });
  }
  lastAdvanceStats.candidatePreparationRangeCount =
    m_candidatePreparationRanges.size();
}

unsigned int
SparseCellGrid::resolveWorkerCount(std::size_t targetCount) const
{
  if (targetCount == 0u) {
    return 1u;
  }

  if (workerOverride > 0) {
    const unsigned int requested = static_cast<unsigned int>(workerOverride);
    return std::min(std::min(requested, kMaxParallelWorkers),
                    static_cast<unsigned int>(targetCount));
  }

  if (targetCount < kParallelTargetThreshold) {
    return 1u;
  }

  unsigned int workerCount = std::thread::hardware_concurrency();
  if (workerCount < 2u) {
    return 1u;
  }
  workerCount = std::min(workerCount, kMaxParallelWorkers);
  return std::min(workerCount, static_cast<unsigned int>(targetCount));
}

unsigned int
SparseCellGrid::resolveCandidatePreparationWorkerCount(
  std::size_t targetCount,
  std::size_t estimatedWork) const
{
  if (targetCount == 0u) {
    return 1u;
  }
  if (workerOverride > 0) {
    const unsigned int requested = static_cast<unsigned int>(workerOverride);
    return std::min(std::min(requested, kMaxParallelWorkers),
                    static_cast<unsigned int>(targetCount));
  }
  if (estimatedWork < kParallelCandidateCellThreshold) {
    return 1u;
  }

  unsigned int workerCount = std::thread::hardware_concurrency();
  if (workerCount < 2u) {
    return 1u;
  }
  workerCount = std::min(workerCount, kMaxCandidateWorkers);
  return std::min(workerCount, static_cast<unsigned int>(targetCount));
}

unsigned int
SparseCellGrid::resolveCandidateWorkerCount(
  std::size_t candidateCellCount) const
{
  if (m_candidateWorkRanges.empty()) {
    return 1u;
  }
  if (workerOverride > 0) {
    const unsigned int requested = static_cast<unsigned int>(workerOverride);
    return std::min(std::min(requested, kMaxParallelWorkers),
                    static_cast<unsigned int>(m_candidateWorkRanges.size()));
  }
  if (candidateCellCount < kParallelCandidateCellThreshold) {
    return 1u;
  }

  unsigned int workerCount = std::thread::hardware_concurrency();
  if (workerCount < 2u) {
    return 1u;
  }
  workerCount = std::min(workerCount, kMaxCandidateWorkers);
  return std::min(workerCount,
                  static_cast<unsigned int>(m_candidateWorkRanges.size()));
}

void
SparseCellGrid::buildCandidateWorkRanges()
{
  ZoneScopedN("SparseCellGrid.buildCandidateWorkRanges");
  m_candidateWorkRanges.clear();
  std::size_t rangeBegin = 0u;
  std::size_t rangeCandidateCount = 0u;
  for (std::size_t scratchIndex = 0u; scratchIndex < m_candidateScratch.size();
       ++scratchIndex) {
    const CandidateScratchChunk& scratch = m_candidateScratch[scratchIndex];
    rangeCandidateCount +=
      scratch.useCellCandidates ? scratch.candidateCellCount : kChunkCellCount;
    if (rangeCandidateCount >= kCandidateCellsPerWorkRange) {
      m_candidateWorkRanges.push_back(
        CandidateWorkRange{ rangeBegin, scratchIndex + 1u });
      rangeBegin = scratchIndex + 1u;
      rangeCandidateCount = 0u;
    }
  }
  if (rangeBegin < m_candidateScratch.size()) {
    m_candidateWorkRanges.push_back(
      CandidateWorkRange{ rangeBegin, m_candidateScratch.size() });
  }
}

void
SparseCellGrid::evaluateCandidateChunk(const CandidateScratchChunk& scratch,
                                       const unsigned char* transitions,
                                       TargetResult* result,
                                       unsigned int memoShardIndex) const
{
  if (result == nullptr || transitions == nullptr) {
    return;
  }
  result->address = scratch.address;
  result->occupied.fill(0u);
  result->counted.fill(0u);
  result->stateChanged.fill(0u);
  result->countedChanged.fill(0u);
  result->hasNonBackground = false;
  result->completeCells = false;
  if (!scratch.useCellCandidates) {
    evaluateTargetChunk(scratch.address, transitions, result, memoShardIndex);
    return;
  }
  const ChunkData* source =
    scratch.centerSourceKnown ? scratch.centerSource : nullptr;
  if (!scratch.centerSourceKnown) {
    const ChunkMap& sourceChunks = generationChunks();
    const ChunkMap::const_iterator sourceIterator =
      sourceChunks.find(scratch.address);
    if (sourceIterator != sourceChunks.end()) {
      source = &sourceIterator->second;
    }
  }
  for (std::size_t wordIndex = 0u; wordIndex < scratch.candidates.size();
       ++wordIndex) {
    std::uint64_t candidates = scratch.candidates[wordIndex];
    while (candidates != 0u) {
      const unsigned int offset = std::countr_zero(candidates);
      const std::size_t index = wordIndex * 64u + offset;
      candidates &= candidates - 1u;

      const unsigned char current =
        source == nullptr ? BackgroundState : source->cells[index];
      const unsigned char next = transitions[RuleSet::transitionIndex(
        current, scratch.neighborCounts[index])];
      const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                                << static_cast<unsigned int>(index % 64u);
      if (!m_countedChangeCoversStateChange && next != current) {
        result->stateChanged[wordIndex] |= bit;
      }
      if (next == BackgroundState) {
        continue;
      }
      result->cells[index] = next;
      result->occupied[wordIndex] |= bit;
      if (next == CountedNeighborState) {
        result->counted[wordIndex] |= bit;
      }
      result->hasNonBackground = true;
    }
  }
  for (std::size_t wordIndex = 0u; wordIndex < result->counted.size();
       ++wordIndex) {
    const std::uint64_t currentCounted =
      source == nullptr ? 0u : source->counted[wordIndex];
    result->countedChanged[wordIndex] =
      currentCounted ^ result->counted[wordIndex];
    if (m_countedChangeCoversStateChange) {
      result->stateChanged[wordIndex] = result->countedChanged[wordIndex];
    }
  }
}

SparseCellGrid::CellArray*
SparseCellGrid::findChunk(const ChunkAddress& address)
{
  ChunkMap::iterator found = chunks.find(address);
  return found == chunks.end() ? nullptr : &found->second.cells;
}

const SparseCellGrid::CellArray*
SparseCellGrid::findChunk(const ChunkAddress& address) const
{
  ChunkMap::const_iterator found = chunks.find(address);
  return found == chunks.end() ? nullptr : &found->second.cells;
}

SparseCellGrid::ChunkData
SparseCellGrid::makeChunkData(const CellArray& cells)
{
  ChunkData result;
  result.cells = cells;
  result.occupied.fill(0u);
  result.counted.fill(0u);
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (cells[index] != BackgroundState) {
      setOccupied(&result, index, true);
    }
    if (cells[index] == CountedNeighborState) {
      setCounted(&result, index, true);
    }
  }
  return result;
}

bool
SparseCellGrid::hasNonBackgroundState(const CellArray& cells)
{
  for (unsigned char state : cells) {
    if (state != BackgroundState) {
      return true;
    }
  }
  return false;
}

std::size_t
SparseCellGrid::countOccupiedCells(const ChunkData& chunk)
{
  std::size_t count = 0u;
  for (std::uint64_t word : chunk.occupied) {
    count += static_cast<std::size_t>(std::popcount(word));
  }
  return count;
}

std::size_t
SparseCellGrid::countCountedCells(const ChunkData& chunk)
{
  std::size_t count = 0u;
  for (std::uint64_t word : chunk.counted) {
    count += static_cast<std::size_t>(std::popcount(word));
  }
  return count;
}

void
SparseCellGrid::refreshChunkCounts(ChunkData* chunk)
{
  if (chunk == nullptr) {
    return;
  }
  chunk->occupiedCellCount =
    static_cast<std::uint16_t>(countOccupiedCells(*chunk));
  chunk->countedCellCount =
    static_cast<std::uint16_t>(countCountedCells(*chunk));
}

void
SparseCellGrid::writeChunkFromResult(ChunkData* chunk,
                                     const TargetResult& result) const
{
  if (chunk == nullptr) {
    return;
  }
  chunk->occupied = result.occupied;
  chunk->counted = result.counted;
  if (result.completeCells) {
    chunk->cells = result.cells;
  } else {
    chunk->cells.fill(BackgroundState);
    for (std::size_t wordIndex = 0u; wordIndex < result.occupied.size();
         ++wordIndex) {
      std::uint64_t occupied = result.occupied[wordIndex];
      while (occupied != 0u) {
        const unsigned int offset = std::countr_zero(occupied);
        const std::size_t index = wordIndex * 64u + offset;
        occupied &= occupied - 1u;
        chunk->cells[index] = result.cells[index];
      }
    }
  }
  refreshChunkCounts(chunk);
}

bool
SparseCellGrid::isCandidatePreferredChunk(const ChunkData& chunk)
{
  return static_cast<std::size_t>(chunk.countedCellCount) <
         kCandidateCellsPerChunkThreshold;
}

void
SparseCellGrid::addChunkStatistics(const ChunkData& chunk,
                                   ChunkStatistics* statistics)
{
  if (statistics == nullptr) {
    return;
  }
  statistics->activeCellCount += chunk.occupiedCellCount;
  statistics->countedCellCount += chunk.countedCellCount;
  if (isCandidatePreferredChunk(chunk)) {
    statistics->candidatePreferredChunkCount += 1u;
  }
}

void
SparseCellGrid::removeChunkStatistics(const ChunkData& chunk,
                                      ChunkStatistics* statistics)
{
  if (statistics == nullptr) {
    return;
  }
  statistics->activeCellCount -= chunk.occupiedCellCount;
  statistics->countedCellCount -= chunk.countedCellCount;
  if (isCandidatePreferredChunk(chunk)) {
    statistics->candidatePreferredChunkCount -= 1u;
  }
}

bool
SparseCellGrid::hasOccupiedCells(const ChunkData& chunk)
{
  return chunk.occupiedCellCount != 0u;
}

void
SparseCellGrid::setOccupied(ChunkData* chunk, std::size_t index, bool occupied)
{
  if (chunk == nullptr || index >= kChunkCellCount) {
    return;
  }
  const std::size_t word = index / 64u;
  const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                            << static_cast<unsigned int>(index % 64u);
  const bool wasOccupied = (chunk->occupied[word] & bit) != 0u;
  if (wasOccupied == occupied) {
    return;
  }
  if (occupied) {
    chunk->occupied[word] |= bit;
    chunk->occupiedCellCount += 1u;
  } else {
    chunk->occupied[word] &= ~bit;
    chunk->occupiedCellCount -= 1u;
  }
}

void
SparseCellGrid::setCounted(ChunkData* chunk, std::size_t index, bool counted)
{
  if (chunk == nullptr || index >= kChunkCellCount) {
    return;
  }
  const std::size_t word = index / 64u;
  const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                            << static_cast<unsigned int>(index % 64u);
  const bool wasCounted = (chunk->counted[word] & bit) != 0u;
  if (wasCounted == counted) {
    return;
  }
  if (counted) {
    chunk->counted[word] |= bit;
    chunk->countedCellCount += 1u;
  } else {
    chunk->counted[word] &= ~bit;
    chunk->countedCellCount -= 1u;
  }
}

bool
SparseCellGrid::sameChunkMaps(const ChunkMap& left, const ChunkMap& right)
{
  if (left.size() != right.size()) {
    return false;
  }
  for (ChunkMap::const_reference entry : left) {
    const ChunkMap::const_iterator found = right.find(entry.first);
    if (found == right.end() || found->second.cells != entry.second.cells) {
      return false;
    }
  }
  return true;
}

unsigned char
SparseCellGrid::candidateTopologyBits(const OccupancyMask& counted)
{
  unsigned char bits = 0u;
  for (std::size_t wordIndex = 0u; wordIndex < counted.size(); ++wordIndex) {
    if ((counted[wordIndex] & kChunkLeftEdgeMask) != 0u) {
      bits |= 1u << 0u;
    }
    if ((counted[wordIndex] & kChunkRightEdgeMask) != 0u) {
      bits |= 1u << 1u;
    }
  }
  if ((counted[0] & kChunkTopRowMask) != 0u) {
    bits |= 1u << 2u;
  }
  if ((counted[3] & kChunkBottomRowMask) != 0u) {
    bits |= 1u << 3u;
  }
  if ((counted[0] & 1u) != 0u) {
    bits |= 1u << 4u;
  }
  if ((counted[0] & (static_cast<std::uint64_t>(1u) << 15u)) != 0u) {
    bits |= 1u << 5u;
  }
  if ((counted[3] & (static_cast<std::uint64_t>(1u) << 48u)) != 0u) {
    bits |= 1u << 6u;
  }
  if ((counted[3] & (static_cast<std::uint64_t>(1u) << 63u)) != 0u) {
    bits |= 1u << 7u;
  }
  return bits;
}

void
SparseCellGrid::recordNextCandidateTopology(const TargetResult& result)
{
  if (m_nextCandidateTopologyChanged) {
    return;
  }
  const ChunkMap& previousChunks =
    m_generationSourceGrid == nullptr ? generationChunks() : chunks;
  const ChunkMap::const_iterator previous = previousChunks.find(result.address);
  const bool previousPresent = previous != previousChunks.end();
  if (previousPresent != result.hasNonBackground) {
    m_nextCandidateTopologyChanged = true;
    return;
  }
  if (previousPresent && candidateTopologyBits(previous->second.counted) !=
                           candidateTopologyBits(result.counted)) {
    m_nextCandidateTopologyChanged = true;
  }
}

std::uint64_t
SparseCellGrid::nextCandidateTopologyRevision() const
{
  const std::uint64_t sourceRevision =
    m_generationSourceGrid == nullptr
      ? generationSource().m_candidateTopologyRevision
      : m_candidateTopologyRevision;
  if (!m_nextCandidateTopologyChanged) {
    return sourceRevision;
  }
  const std::uint64_t nextRevision = sourceRevision + 1u;
  return nextRevision == 0u ? 1u : nextRevision;
}

bool
SparseCellGrid::prepareNextChunks(std::size_t expectedChunkCount)
{
  ZoneScopedN("SparseCellGrid.prepareNextChunks");
  try {
    if (!chunkNodeReuseOverride) {
      m_nextChunks.clear();
      m_recycledChunkNodes.clear();
      m_nextChunkStatistics = ChunkStatistics{};
    }
    if (m_nextChunks.size() >
        std::numeric_limits<std::size_t>::max() - m_recycledChunkNodes.size()) {
      throw std::bad_alloc();
    }
    const std::size_t retainedChunkCount =
      m_recycledChunkNodes.size() + m_nextChunks.size();
    const std::size_t requiredNodeCapacity =
      std::max(retainedChunkCount, expectedChunkCount);
    if (m_recycledChunkNodes.capacity() < requiredNodeCapacity) {
      m_recycledChunkNodes.reserve(requiredNodeCapacity);
    }

    if (chunkNodeReuseOverride) {
      m_nextChunkStatistics = ChunkStatistics{};
      recycleNextChunks();
    }

    const double retainedBucketCapacity =
      static_cast<double>(m_nextChunks.bucket_count()) *
      static_cast<double>(m_nextChunks.max_load_factor());
    if (static_cast<double>(expectedChunkCount) > retainedBucketCapacity) {
      m_nextChunks.reserve(expectedChunkCount);
    }
  } catch (const std::bad_alloc&) {
    return false;
  }
  return true;
}

bool
SparseCellGrid::prepareDirectChunks(std::size_t expectedChunkCount)
{
  ZoneScopedN("SparseCellGrid.prepareDirectChunks");
  try {
    if (m_recycledChunkNodes.size() >
        std::numeric_limits<std::size_t>::max() - m_nextChunks.size()) {
      throw std::bad_alloc();
    }
    const std::size_t retainedNodeCount =
      m_recycledChunkNodes.size() + m_nextChunks.size();
    if (retainedNodeCount >
        std::numeric_limits<std::size_t>::max() - chunks.size()) {
      throw std::bad_alloc();
    }
    const std::size_t maximumRecycledNodeCount =
      retainedNodeCount + chunks.size();
    const std::size_t requiredNodeCapacity =
      std::max(maximumRecycledNodeCount, expectedChunkCount);
    if (m_recycledChunkNodes.capacity() < requiredNodeCapacity) {
      m_recycledChunkNodes.reserve(requiredNodeCapacity);
    }
    recycleNextChunks();
    const double retainedBucketCapacity =
      static_cast<double>(chunks.bucket_count()) *
      static_cast<double>(chunks.max_load_factor());
    if (static_cast<double>(expectedChunkCount) > retainedBucketCapacity) {
      chunks.reserve(expectedChunkCount);
    }
  } catch (const std::bad_alloc&) {
    return false;
  }

  m_directOutputGeneration += 1u;
  if (m_directOutputGeneration == 0u) {
    for (ChunkMap::reference entry : chunks) {
      entry.second.directOutputGeneration = 0u;
    }
    m_directOutputGeneration = 1u;
  }
  m_chunkStatistics = ChunkStatistics{};
  return true;
}

void
SparseCellGrid::recycleNextChunks()
{
  ZoneScopedN("SparseCellGrid.recycleNextChunks");
  while (!m_nextChunks.empty()) {
    const ChunkMap::iterator entry = m_nextChunks.begin();
    ChunkNode node = m_nextChunks.extract(entry);
    m_recycledChunkNodes.push_back(std::move(node));
  }
}

SparseCellGrid::ChunkMap::iterator
SparseCellGrid::acquireNextChunk(const ChunkAddress& address)
{
  if (m_recycledChunkNodes.empty()) {
    const std::pair<ChunkMap::iterator, bool> inserted =
      m_nextChunks.try_emplace(address);
    if (!inserted.second) {
      return m_nextChunks.end();
    }
    lastAdvanceStats.allocatedChunkNodeCount += 1u;
    return inserted.first;
  }

  ChunkNode node = std::move(m_recycledChunkNodes.back());
  m_recycledChunkNodes.pop_back();
  node.key() = address;
  ChunkMap::insert_return_type inserted = m_nextChunks.insert(std::move(node));
  if (!inserted.inserted) {
    m_recycledChunkNodes.push_back(std::move(inserted.node));
    return m_nextChunks.end();
  }
  lastAdvanceStats.reusedChunkNodeCount += 1u;
  return inserted.position;
}

bool
SparseCellGrid::insertNextResultChunk(const TargetResult& result)
{
  ChunkMap::iterator destination = acquireNextChunk(result.address);
  if (destination == m_nextChunks.end()) {
    return false;
  }

  ChunkData& next = destination->second;
  writeChunkFromResult(&next, result);
  addChunkStatistics(next, &m_nextChunkStatistics);
  return true;
}

bool
SparseCellGrid::insertDirectResultChunk(const TargetResult& result,
                                        ChunkData* knownDestination,
                                        ChunkData** destinationOut)
{
  ChunkMap::iterator destination = chunks.end();
  ChunkData* destinationChunk = knownDestination;
  if (destinationChunk == nullptr) {
    destination = chunks.find(result.address);
  }
  if (destinationChunk == nullptr && destination == chunks.end()) {
    if (m_recycledChunkNodes.empty()) {
      const std::pair<ChunkMap::iterator, bool> inserted =
        chunks.try_emplace(result.address);
      if (!inserted.second) {
        return false;
      }
      destination = inserted.first;
      lastAdvanceStats.allocatedChunkNodeCount += 1u;
    } else {
      ChunkNode node = std::move(m_recycledChunkNodes.back());
      m_recycledChunkNodes.pop_back();
      node.key() = result.address;
      ChunkMap::insert_return_type inserted = chunks.insert(std::move(node));
      if (!inserted.inserted) {
        m_recycledChunkNodes.push_back(std::move(inserted.node));
        return false;
      }
      destination = inserted.position;
      lastAdvanceStats.reusedChunkNodeCount += 1u;
    }
  } else if (destinationChunk == nullptr) {
    lastAdvanceStats.reusedChunkNodeCount += 1u;
  }

  if (destinationChunk == nullptr) {
    destinationChunk = &destination->second;
  }
  ChunkData& next = *destinationChunk;
  writeChunkFromResult(&next, result);
  next.directOutputGeneration = m_directOutputGeneration;
  addChunkStatistics(next, &m_chunkStatistics);
  if (destinationOut != nullptr) {
    *destinationOut = destinationChunk;
  }
  return true;
}

SparseCellGrid::ChunkMap::iterator
SparseCellGrid::insertNextChunk(const ChunkAddress& address,
                                const ChunkData& chunk)
{
  if (m_recycledChunkNodes.empty()) {
    const std::pair<ChunkMap::iterator, bool> inserted =
      m_nextChunks.emplace(address, chunk);
    if (inserted.second) {
      addChunkStatistics(inserted.first->second, &m_nextChunkStatistics);
      lastAdvanceStats.allocatedChunkNodeCount += 1u;
    }
    return inserted.first;
  }

  ChunkNode node = std::move(m_recycledChunkNodes.back());
  m_recycledChunkNodes.pop_back();
  node.key() = address;
  node.mapped() = chunk;
  ChunkMap::insert_return_type inserted = m_nextChunks.insert(std::move(node));
  if (!inserted.inserted) {
    m_recycledChunkNodes.push_back(std::move(inserted.node));
    return inserted.position;
  }
  addChunkStatistics(inserted.position->second, &m_nextChunkStatistics);
  lastAdvanceStats.reusedChunkNodeCount += 1u;
  return inserted.position;
}

void
SparseCellGrid::finishNextChunks(bool changesPrepared)
{
  ZoneScopedN("SparseCellGrid.finishNextChunks");
  const bool directSourceGeneration = m_generationSourceGrid != nullptr;
  const std::uint64_t sourceRevision =
    directSourceGeneration ? m_generationSourceGrid->revision : revision;
  bool changed = m_frontierInvalid || !m_nextChangedChunks.empty();
  if (!changesPrepared) {
    try {
      m_frontierInvalid = false;
      collectChangedChunksBetweenMaps();
      changed = m_frontierInvalid || !m_nextChangedChunks.empty();
    } catch (const std::bad_alloc&) {
      changed = !sameChunkMaps(chunks, m_nextChunks);
      m_frontierInvalid = true;
    }
  }
  if (changed || directSourceGeneration) {
    chunks.swap(m_nextChunks);
    std::swap(m_chunkStatistics, m_nextChunkStatistics);
    revision = changed ? sourceRevision + 1u : sourceRevision;
    m_candidateTopologyRevision = nextCandidateTopologyRevision();
    m_nextChunksMirrorCurrent = false;
    if (directSourceGeneration) {
      m_inactiveCatchupDeltaValid = false;
      m_inactiveCatchupFullReplacement = false;
    }
  }
  if (!m_frontierInvalid) {
    commitNextChangedChunks();
  }
  if (changed) {
    publishChangedChunksRevision(!m_frontierInvalid);
  }
  lastAdvanceStats.retainedChunkNodeCount =
    chunkNodeReuseOverride ? m_recycledChunkNodes.size() + m_nextChunks.size()
                           : 0u;
}

void
SparseCellGrid::finishDirectChunks()
{
  ZoneScopedN("SparseCellGrid.finishDirectChunks");
  if (m_nextCandidateTopologyChanged) {
    for (ChunkMap::iterator entry = chunks.begin(); entry != chunks.end();) {
      if (entry->second.directOutputGeneration == m_directOutputGeneration) {
        ++entry;
        continue;
      }
      const ChunkMap::iterator stale = entry;
      ++entry;
      m_recycledChunkNodes.push_back(chunks.extract(stale));
    }
  }

  const bool changed = m_frontierInvalid || !m_nextChangedChunks.empty();
  const std::uint64_t sourceRevision = m_generationSourceGrid->revision;
  revision = changed ? sourceRevision + 1u : sourceRevision;
  m_candidateTopologyRevision = nextCandidateTopologyRevision();
  m_nextChunkStatistics = ChunkStatistics{};
  m_nextChunksMirrorCurrent = false;
  m_inactiveCatchupDeltaValid = false;
  m_inactiveCatchupFullReplacement = false;
  if (!m_frontierInvalid) {
    commitNextChangedChunks();
  }
  if (changed) {
    publishChangedChunksRevision(!m_frontierInvalid);
  }
  lastAdvanceStats.retainedChunkNodeCount = m_recycledChunkNodes.size();
}

unsigned char
SparseCellGrid::getCell(const CellAddress& address) const
{
  const CellAddress canonicalAddress = canonicalizeCell(address);
  const ChunkAddress chunkAddress = chunkAddressForCell(canonicalAddress);
  const CellArray* chunk = findChunk(chunkAddress);
  if (chunk == nullptr) {
    return BackgroundState;
  }
  return (
    *chunk)[static_cast<std::size_t>(localIndexForCell(canonicalAddress))];
}

bool
SparseCellGrid::setCell(const CellAddress& address, unsigned char state)
{
  const CellAddress canonicalAddress = canonicalizeCell(address);
  const ChunkAddress chunkAddress = chunkAddressForCell(canonicalAddress);
  const std::size_t index =
    static_cast<std::size_t>(localIndexForCell(canonicalAddress));
  ChunkMap::iterator found = chunks.find(chunkAddress);

  if (state == BackgroundState) {
    if (found == chunks.end()) {
      return true;
    }
    if (found->second.cells[index] == BackgroundState) {
      return true;
    }
    OccupancyMask stateChanged;
    OccupancyMask countedChanged;
    stateChanged.fill(0u);
    countedChanged.fill(0u);
    const std::size_t wordIndex = index / 64u;
    const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                              << static_cast<unsigned int>(index % 64u);
    stateChanged[wordIndex] |= bit;
    if (found->second.cells[index] == CountedNeighborState) {
      countedChanged[wordIndex] |= bit;
    }
    markChangedChunk(chunkAddress, stateChanged, countedChanged);
    removeChunkStatistics(found->second, &m_chunkStatistics);
    found->second.cells[index] = BackgroundState;
    setOccupied(&found->second, index, false);
    setCounted(&found->second, index, false);
    if (!hasOccupiedCells(found->second)) {
      chunks.erase(found);
    } else {
      addChunkStatistics(found->second, &m_chunkStatistics);
    }
    revision += 1;
    m_candidateTopologyRevision += 1u;
    if (m_candidateTopologyRevision == 0u) {
      m_candidateTopologyRevision = 1u;
    }
    m_inactiveCatchupDeltaValid = false;
    m_inactiveCatchupFullReplacement = false;
    m_nextChunksMirrorCurrent = false;
    publishChangedChunkRevision(chunkAddress, true);
    return true;
  }

  if (found != chunks.end() && found->second.cells[index] == state) {
    return true;
  }

  if (found == chunks.end()) {
    ChunkData next;
    next.cells.fill(BackgroundState);
    next.cells[index] = state;
    setOccupied(&next, index, true);
    setCounted(&next, index, state == CountedNeighborState);
    try {
      const std::pair<ChunkMap::iterator, bool> inserted =
        chunks.emplace(chunkAddress, next);
      if (!inserted.second) {
        return false;
      }
      OccupancyMask stateChanged;
      OccupancyMask countedChanged;
      stateChanged.fill(0u);
      countedChanged.fill(0u);
      const std::size_t wordIndex = index / 64u;
      const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                                << static_cast<unsigned int>(index % 64u);
      stateChanged[wordIndex] |= bit;
      if (state == CountedNeighborState) {
        countedChanged[wordIndex] |= bit;
      }
      markChangedChunk(chunkAddress, stateChanged, countedChanged);
      addChunkStatistics(inserted.first->second, &m_chunkStatistics);
    } catch (const std::bad_alloc&) {
      return false;
    }
  } else {
    OccupancyMask stateChanged;
    OccupancyMask countedChanged;
    stateChanged.fill(0u);
    countedChanged.fill(0u);
    const std::size_t wordIndex = index / 64u;
    const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                              << static_cast<unsigned int>(index % 64u);
    stateChanged[wordIndex] |= bit;
    if ((found->second.cells[index] == CountedNeighborState) !=
        (state == CountedNeighborState)) {
      countedChanged[wordIndex] |= bit;
    }
    markChangedChunk(chunkAddress, stateChanged, countedChanged);
    removeChunkStatistics(found->second, &m_chunkStatistics);
    found->second.cells[index] = state;
    setOccupied(&found->second, index, true);
    setCounted(&found->second, index, state == CountedNeighborState);
    addChunkStatistics(found->second, &m_chunkStatistics);
  }
  revision += 1;
  m_candidateTopologyRevision += 1u;
  if (m_candidateTopologyRevision == 0u) {
    m_candidateTopologyRevision = 1u;
  }
  m_inactiveCatchupDeltaValid = false;
  m_inactiveCatchupFullReplacement = false;
  m_nextChunksMirrorCurrent = false;
  publishChangedChunkRevision(chunkAddress, true);
  return true;
}

void
SparseCellGrid::clear()
{
  if (!chunks.empty()) {
    for (ChunkMap::const_reference entry : chunks) {
      markChangedChunk(
        entry.first, entry.second.occupied, entry.second.counted);
      if (m_frontierInvalid) {
        break;
      }
    }
    chunks.clear();
    m_chunkStatistics = ChunkStatistics{};
    revision += 1;
    m_candidateTopologyRevision += 1u;
    if (m_candidateTopologyRevision == 0u) {
      m_candidateTopologyRevision = 1u;
    }
    m_inactiveCatchupDeltaValid = false;
    m_inactiveCatchupFullReplacement = false;
    m_nextChunksMirrorCurrent = false;
    publishChangedChunksRevision(!m_frontierInvalid);
  }
}

bool
SparseCellGrid::assignChunk(const SparseChunkRecord& record)
{
  const ChunkAddress address{ record.chunkX, record.chunkY };
  if (isToroidal() && !(canonicalizeChunk(address) == address)) {
    return false;
  }
  if (!hasNonBackgroundState(record.cells)) {
    return true;
  }
  try {
    ChunkMap::iterator found = chunks.find(address);
    if (found != chunks.end() && found->second.cells == record.cells) {
      return true;
    }
    ChunkData next = makeChunkData(record.cells);
    OccupancyMask stateChanged;
    OccupancyMask countedChanged;
    buildChangeMasks(found == chunks.end() ? nullptr : &found->second,
                     &next,
                     &stateChanged,
                     &countedChanged);
    markChangedChunk(address, stateChanged, countedChanged);
    if (found == chunks.end()) {
      const std::pair<ChunkMap::iterator, bool> inserted =
        chunks.emplace(address, next);
      if (!inserted.second) {
        return false;
      }
      addChunkStatistics(inserted.first->second, &m_chunkStatistics);
    } else {
      removeChunkStatistics(found->second, &m_chunkStatistics);
      found->second = next;
      addChunkStatistics(found->second, &m_chunkStatistics);
    }
  } catch (const std::bad_alloc&) {
    return false;
  }
  revision += 1;
  m_candidateTopologyRevision += 1u;
  if (m_candidateTopologyRevision == 0u) {
    m_candidateTopologyRevision = 1u;
  }
  m_inactiveCatchupDeltaValid = false;
  m_inactiveCatchupFullReplacement = false;
  m_nextChunksMirrorCurrent = false;
  publishChangedChunkRevision(ChunkAddress{ record.chunkX, record.chunkY },
                              true);
  return true;
}

std::vector<SparseChunkRecord>
SparseCellGrid::collectChunkRecords() const
{
  std::vector<SparseChunkRecord> records;
  records.reserve(chunks.size());
  for (ChunkMap::const_reference entry : chunks) {
    SparseChunkRecord record;
    record.chunkX = entry.first.x;
    record.chunkY = entry.first.y;
    record.cells = entry.second.cells;
    records.push_back(record);
  }
  std::sort(records.begin(),
            records.end(),
            [](const SparseChunkRecord& left, const SparseChunkRecord& right) {
              if (left.chunkY != right.chunkY) {
                return left.chunkY < right.chunkY;
              }
              return left.chunkX < right.chunkX;
            });
  return records;
}

void
SparseCellGrid::collectChunkRecords(
  std::vector<SparseChunkRecord>* records) const
{
  if (records == nullptr) {
    return;
  }
  *records = collectChunkRecords();
}

void
SparseCellGrid::visitChunksInBounds(const ChunkAddress& minimum,
                                    const ChunkAddress& maximum,
                                    const ChunkVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  visitOccupiedChunksInBounds(
    minimum,
    maximum,
    [&visitor](const ChunkAddress& address,
               const ChunkCells& cells,
               const SparseChunkMask&) { visitor(address, cells); });
}

void
SparseCellGrid::visitOccupiedChunksInBounds(
  const ChunkAddress& minimum,
  const ChunkAddress& maximum,
  const OccupiedChunkVisitor& visitor) const
{
  if (!visitor || minimum.x > maximum.x || minimum.y > maximum.y) {
    return;
  }
  ChunkAddress first = minimum;
  ChunkAddress last = maximum;
  if (isToroidal()) {
    const std::int64_t worldMinimumX = -(m_worldChunkWidth / 2);
    const std::int64_t worldMinimumY = -(m_worldChunkHeight / 2);
    const std::int64_t worldMaximumX = worldMinimumX + m_worldChunkWidth - 1;
    const std::int64_t worldMaximumY = worldMinimumY + m_worldChunkHeight - 1;
    first.x = std::max(first.x, worldMinimumX);
    first.y = std::max(first.y, worldMinimumY);
    last.x = std::min(last.x, worldMaximumX);
    last.y = std::min(last.y, worldMaximumY);
    if (first.x > last.x || first.y > last.y) {
      return;
    }
  }
  for (std::int64_t chunkY = first.y;; ++chunkY) {
    for (std::int64_t chunkX = first.x;; ++chunkX) {
      const ChunkAddress address{ chunkX, chunkY };
      const ChunkMap::const_iterator found = chunks.find(address);
      if (found != chunks.end()) {
        visitor(address, found->second.cells, found->second.occupied);
      }
      if (chunkX == last.x) {
        break;
      }
    }
    if (chunkY == last.y) {
      break;
    }
  }
}

bool
SparseCellGrid::visitChangedChunksSince(
  std::uint64_t previousRevision,
  const ChangedChunkVisitor& visitor) const
{
  if (!visitor ||
      previousRevision == std::numeric_limits<std::uint64_t>::max() ||
      revision != previousRevision + 1u ||
      m_changedChunksRevision != revision || !m_changedChunksRevisionValid) {
    return false;
  }
  for (const ChunkAddress& address : m_presentationChangedChunks) {
    visitor(address, findChunk(address));
  }
  return true;
}

void
SparseCellGrid::copyStateFrom(const SparseCellGrid& source)
{
  if (this == &source) {
    return;
  }
  chunks = source.chunks;
  m_worldChunkWidth = source.m_worldChunkWidth;
  m_worldChunkHeight = source.m_worldChunkHeight;
  m_nextChunks = chunks;
  m_chunkStatistics = source.m_chunkStatistics;
  m_nextChunkStatistics = m_chunkStatistics;
  revision = source.revision;
  m_candidateTopologyRevision = source.m_candidateTopologyRevision;
  m_candidateScratchSourceGrid = nullptr;
  m_candidateScratchSourceTopologyRevision = 0u;
  lastRuleType = source.lastRuleType;
  m_backgroundTransitionsStayBinary = source.m_backgroundTransitionsStayBinary;
  m_countedChangeCoversStateChange = source.m_countedChangeCoversStateChange;
  lastAdvanceStats = source.lastAdvanceStats;
  m_frontierInvalid = source.m_frontierInvalid;
  m_inactiveCatchupDeltaValid = false;
  m_inactiveCatchupFullReplacement = false;
  m_nextChunksMirrorCurrent = true;
  beginAddressSet(
    &m_changedChunks, &m_changedChunkIndex, &m_changedChunkGeneration);
  m_changedCellMasks.clear();
  m_changedCountedMasks.clear();
  if (!m_frontierInvalid) {
    for (std::size_t index = 0u; index < source.m_changedChunks.size();
         ++index) {
      const ChunkAddress& address = source.m_changedChunks[index];
      insertAddressSet(address,
                       &m_changedChunks,
                       &m_changedChunkIndex,
                       m_changedChunkGeneration);
      m_changedCellMasks.push_back(source.m_changedCellMasks[index]);
      m_changedCountedMasks.push_back(source.m_changedCountedMasks[index]);
    }
  }
  publishChangedChunksRevision(false);
}

bool
SparseCellGrid::captureGenerationDelta(std::uint64_t previousRevision,
                                       SparseGenerationDelta* delta,
                                       bool includeFullReplacementChunks) const
{
  if (delta == nullptr || previousRevision > revision) {
    return false;
  }
  delta->clear();
  delta->fromRevision = previousRevision;
  delta->toRevision = revision;
  if (previousRevision == revision) {
    return true;
  }
  if (revision == previousRevision + 1u &&
      m_changedChunksRevision == revision && m_changedChunksRevisionValid) {
    if (m_presentationChangedChunks.size() >=
        kLightweightReplacementChunkLimit) {
      delta->fullReplacement = true;
      return true;
    }
    delta->changedChunks.reserve(m_presentationChangedChunks.size());
    for (const ChunkAddress& address : m_presentationChangedChunks) {
      SparseChangedChunkRecord record;
      record.address = address;
      const std::size_t changedIndex = findAddressSetIndex(
        address, m_changedChunkIndex, m_changedChunkGeneration);
      if (changedIndex == std::numeric_limits<std::size_t>::max()) {
        return false;
      }
      record.stateChanged = m_changedCellMasks[changedIndex];
      record.countedChanged = m_changedCountedMasks[changedIndex];
      const CellArray* cells = findChunk(address);
      record.present = cells != nullptr;
      if (cells != nullptr) {
        const ChunkMap::const_iterator chunk = chunks.find(address);
        record.cells = chunk->second.cells;
        record.occupied = chunk->second.occupied;
        record.counted = chunk->second.counted;
        record.occupiedCellCount = chunk->second.occupiedCellCount;
        record.countedCellCount = chunk->second.countedCellCount;
      }
      delta->changedChunks.push_back(record);
    }
    return true;
  }

  delta->fullReplacement = true;
  if (!includeFullReplacementChunks) {
    return true;
  }
  delta->fullChunks.reserve(chunks.size());
  for (ChunkMap::const_reference entry : chunks) {
    SparseChangedChunkRecord record;
    record.address = entry.first;
    record.present = true;
    record.cells = entry.second.cells;
    record.occupied = entry.second.occupied;
    record.counted = entry.second.counted;
    record.occupiedCellCount = entry.second.occupiedCellCount;
    record.countedCellCount = entry.second.countedCellCount;
    delta->fullChunks.push_back(record);
  }
  return true;
}

bool
SparseCellGrid::applyDeltaToMap(const SparseGenerationDelta& delta,
                                ChunkMap* target,
                                ChunkStatistics* statistics)
{
  if (target == nullptr || statistics == nullptr) {
    return false;
  }
  try {
    if (delta.fullReplacement) {
      while (!target->empty()) {
        ChunkMap::iterator current = target->begin();
        m_recycledChunkNodes.push_back(target->extract(current));
      }
      *statistics = ChunkStatistics{};
      target->reserve(delta.fullChunks.size());
      for (const SparseChangedChunkRecord& record : delta.fullChunks) {
        ChunkData replacement;
        replacement.cells = record.cells;
        replacement.occupied = record.occupied;
        replacement.counted = record.counted;
        replacement.occupiedCellCount = record.occupiedCellCount;
        replacement.countedCellCount = record.countedCellCount;
        if (m_recycledChunkNodes.empty()) {
          const std::pair<ChunkMap::iterator, bool> inserted =
            target->emplace(record.address, replacement);
          if (!inserted.second) {
            return false;
          }
          addChunkStatistics(inserted.first->second, statistics);
        } else {
          ChunkNode node = std::move(m_recycledChunkNodes.back());
          m_recycledChunkNodes.pop_back();
          node.key() = record.address;
          node.mapped() = replacement;
          ChunkMap::insert_return_type inserted =
            target->insert(std::move(node));
          if (!inserted.inserted) {
            m_recycledChunkNodes.push_back(std::move(inserted.node));
            return false;
          }
          addChunkStatistics(inserted.position->second, statistics);
        }
      }
    } else {
      for (const SparseChangedChunkRecord& record : delta.changedChunks) {
        ChunkMap::iterator current = target->find(record.address);
        if (current != target->end()) {
          removeChunkStatistics(current->second, statistics);
        }
        if (!record.present) {
          if (current != target->end()) {
            target->erase(current);
          }
        } else {
          ChunkData replacement;
          replacement.cells = record.cells;
          replacement.occupied = record.occupied;
          replacement.counted = record.counted;
          replacement.occupiedCellCount = record.occupiedCellCount;
          replacement.countedCellCount = record.countedCellCount;
          if (current == target->end()) {
            target->emplace(record.address, replacement);
          } else {
            current->second = replacement;
          }
          addChunkStatistics(replacement, statistics);
        }
      }
    }
  } catch (const std::bad_alloc&) {
    return false;
  }
  return true;
}

bool
SparseCellGrid::synchronizeInactiveMap(
  const SparseGenerationDelta& incomingDelta)
{
  ZoneScopedN("SparseCellGrid.synchronizeInactiveMap");
  try {
    if (m_inactiveCatchupFullReplacement) {
      m_nextChunks = chunks;
      m_nextChunkStatistics = m_chunkStatistics;
      return true;
    }

    beginAddressSet(&m_mirrorIncomingAddresses,
                    &m_mirrorIncomingAddressIndex,
                    &m_mirrorIncomingAddressGeneration);
    for (const SparseChangedChunkRecord& record : incomingDelta.changedChunks) {
      insertAddressSet(record.address,
                       &m_mirrorIncomingAddresses,
                       &m_mirrorIncomingAddressIndex,
                       m_mirrorIncomingAddressGeneration);
    }

    for (const ChunkAddress& address : m_changedChunks) {
      if (findAddressSetIndex(address,
                              m_mirrorIncomingAddressIndex,
                              m_mirrorIncomingAddressGeneration) !=
          std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      ChunkMap::iterator inactive = m_nextChunks.find(address);
      if (inactive != m_nextChunks.end()) {
        removeChunkStatistics(inactive->second, &m_nextChunkStatistics);
      }
      const ChunkMap::const_iterator current = chunks.find(address);
      if (current == chunks.end()) {
        if (inactive != m_nextChunks.end()) {
          m_nextChunks.erase(inactive);
        }
      } else if (inactive == m_nextChunks.end()) {
        const std::pair<ChunkMap::iterator, bool> inserted =
          m_nextChunks.emplace(address, current->second);
        if (!inserted.second) {
          return false;
        }
        addChunkStatistics(inserted.first->second, &m_nextChunkStatistics);
      } else {
        inactive->second = current->second;
        addChunkStatistics(inactive->second, &m_nextChunkStatistics);
      }
    }
  } catch (const std::bad_alloc&) {
    return false;
  }
  return true;
}

bool
SparseCellGrid::applyGenerationDelta(const SparseGenerationDelta& delta)
{
  if (revision != delta.fromRevision) {
    return false;
  }
  if (delta.fullReplacement) {
    if (!applyDeltaToMap(delta, &chunks, &m_chunkStatistics)) {
      return false;
    }
    beginAddressSet(
      &m_changedChunks, &m_changedChunkIndex, &m_changedChunkGeneration);
    m_changedCellMasks.clear();
    m_changedCountedMasks.clear();
    revision = delta.toRevision;
    m_candidateTopologyRevision += 1u;
    if (m_candidateTopologyRevision == 0u) {
      m_candidateTopologyRevision = 1u;
    }
    m_frontierInvalid = true;
    m_inactiveCatchupDeltaValid = false;
    m_inactiveCatchupFullReplacement = false;
    m_nextChunksMirrorCurrent = false;
    publishChangedChunksRevision(false);
    return true;
  }
  if (m_inactiveCatchupDeltaValid) {
    if (!synchronizeInactiveMap(delta)) {
      return false;
    }
    m_inactiveCatchupDeltaValid = false;
    m_inactiveCatchupFullReplacement = false;
  } else if (!m_nextChunksMirrorCurrent) {
    m_nextChunks = chunks;
    m_nextChunkStatistics = m_chunkStatistics;
  }
  if (!applyDeltaToMap(delta, &chunks, &m_chunkStatistics) ||
      !applyDeltaToMap(delta, &m_nextChunks, &m_nextChunkStatistics)) {
    return false;
  }
  beginAddressSet(
    &m_changedChunks, &m_changedChunkIndex, &m_changedChunkGeneration);
  m_changedCellMasks.clear();
  m_changedCountedMasks.clear();
  m_frontierInvalid = delta.changedChunks.size() > kFrontierTrackingLimit;
  if (!m_frontierInvalid) {
    for (const SparseChangedChunkRecord& record : delta.changedChunks) {
      markChangedChunk(
        record.address, record.stateChanged, record.countedChanged);
    }
  }
  revision = delta.toRevision;
  m_candidateTopologyRevision += 1u;
  if (m_candidateTopologyRevision == 0u) {
    m_candidateTopologyRevision = 1u;
  }
  m_nextChunksMirrorCurrent = true;
  publishChangedChunksRevision(false);
  return true;
}

void
SparseCellGrid::rememberInactiveGenerationDelta(
  const SparseGenerationDelta& delta)
{
  m_inactiveCatchupDeltaValid = true;
  m_inactiveCatchupFullReplacement = delta.fullReplacement;
  m_nextChunksMirrorCurrent = false;
}

void
SparseCellGrid::swap(SparseCellGrid& other) noexcept
{
  const bool topologyChanged = m_worldChunkWidth != other.m_worldChunkWidth ||
                               m_worldChunkHeight != other.m_worldChunkHeight;
  const bool changed = topologyChanged || !sameChunkMaps(chunks, other.chunks);
  chunks.swap(other.chunks);
  m_nextChunks.swap(other.m_nextChunks);
  std::swap(m_chunkStatistics, other.m_chunkStatistics);
  std::swap(m_nextChunkStatistics, other.m_nextChunkStatistics);
  m_recycledChunkNodes.swap(other.m_recycledChunkNodes);
  m_changedChunks.swap(other.m_changedChunks);
  m_changedCellMasks.swap(other.m_changedCellMasks);
  m_changedCountedMasks.swap(other.m_changedCountedMasks);
  m_changedChunkIndex.swap(other.m_changedChunkIndex);
  std::swap(m_changedChunkGeneration, other.m_changedChunkGeneration);
  m_nextChangedChunks.swap(other.m_nextChangedChunks);
  m_nextChangedCellMasks.swap(other.m_nextChangedCellMasks);
  m_nextChangedCountedMasks.swap(other.m_nextChangedCountedMasks);
  m_nextChangedChunkIndex.swap(other.m_nextChangedChunkIndex);
  std::swap(m_nextChangedChunkGeneration, other.m_nextChangedChunkGeneration);
  std::swap(lastRuleType, other.lastRuleType);
  std::swap(m_backgroundTransitionsStayBinary,
            other.m_backgroundTransitionsStayBinary);
  std::swap(m_countedChangeCoversStateChange,
            other.m_countedChangeCoversStateChange);
  std::swap(m_worldChunkWidth, other.m_worldChunkWidth);
  std::swap(m_worldChunkHeight, other.m_worldChunkHeight);
  std::swap(m_candidateTopologyRevision, other.m_candidateTopologyRevision);
  m_candidateScratchSourceGrid = nullptr;
  other.m_candidateScratchSourceGrid = nullptr;
  m_candidateScratchSourceTopologyRevision = 0u;
  other.m_candidateScratchSourceTopologyRevision = 0u;
  std::swap(m_frontierInvalid, other.m_frontierInvalid);
  if (changed) {
    revision += 1;
    other.revision += 1;
    m_inactiveCatchupDeltaValid = false;
    other.m_inactiveCatchupDeltaValid = false;
    m_inactiveCatchupFullReplacement = false;
    other.m_inactiveCatchupFullReplacement = false;
    m_nextChunksMirrorCurrent = false;
    other.m_nextChunksMirrorCurrent = false;
    publishChangedChunksRevision(false);
    other.publishChangedChunksRevision(false);
  }
}

void
SparseCellGrid::buildHorizontalNeighborRow(
  const ChunkData* const neighborhood[3][3],
  int haloY,
  std::array<unsigned char, kChunkDim>* horizontalCounts)
{
  if (horizontalCounts == nullptr || haloY < 0 || haloY >= kHaloDim) {
    return;
  }

  int chunkY = 1;
  int localY = haloY - 1;
  if (haloY == 0) {
    chunkY = 0;
    localY = kChunkDim - 1;
  } else if (haloY == kHaloDim - 1) {
    chunkY = 2;
    localY = 0;
  }

  const std::size_t wordIndex = static_cast<std::size_t>(localY / 4);
  const unsigned int rowShift =
    static_cast<unsigned int>((localY % 4) * kChunkDim);
  std::uint16_t leftRow = 0u;
  std::uint16_t centerRow = 0u;
  std::uint16_t rightRow = 0u;
  if (neighborhood[chunkY][0] != nullptr) {
    leftRow = static_cast<std::uint16_t>(
      neighborhood[chunkY][0]->counted[wordIndex] >> rowShift);
  }
  if (neighborhood[chunkY][1] != nullptr) {
    centerRow = static_cast<std::uint16_t>(
      neighborhood[chunkY][1]->counted[wordIndex] >> rowShift);
  }
  if (neighborhood[chunkY][2] != nullptr) {
    rightRow = static_cast<std::uint16_t>(
      neighborhood[chunkY][2]->counted[wordIndex] >> rowShift);
  }

  const std::uint32_t countedHaloRow =
    static_cast<std::uint32_t>((leftRow >> (kChunkDim - 1)) & 1u) |
    (static_cast<std::uint32_t>(centerRow) << 1u) |
    (static_cast<std::uint32_t>(rightRow & 1u) << (kChunkDim + 1));
  for (int localX = 0; localX < kChunkDim; ++localX) {
    const std::uint32_t countedWindow =
      (countedHaloRow >> static_cast<unsigned int>(localX)) & 7u;
    (*horizontalCounts)[static_cast<std::size_t>(localX)] =
      static_cast<unsigned char>(std::popcount(countedWindow));
  }
}

void
SparseCellGrid::evaluateTargetChunk(const ChunkAddress& target,
                                    const unsigned char* transitions,
                                    TargetResult* result,
                                    unsigned int memoShardIndex) const
{
  if (result == nullptr || transitions == nullptr) {
    return;
  }

  const ChunkMap& sourceChunks = generationChunks();
  const ChunkData* neighborhood[3][3];
  for (int neighborY = -1; neighborY <= 1; ++neighborY) {
    for (int neighborX = -1; neighborX <= 1; ++neighborX) {
      const ChunkAddress neighborAddress{ target.x + neighborX,
                                          target.y + neighborY };
      const ChunkMap::const_iterator found = sourceChunks.find(neighborAddress);
      neighborhood[neighborY + 1][neighborX + 1] =
        found == sourceChunks.end() ? nullptr : &found->second;
    }
  }

  bool memoProbed = false;
  std::uint64_t memoHash = 0u;
  ChunkMemoState::Key memoKey;
  ChunkMemoState::Shard* memoShard = nullptr;
  if (m_chunkMemo != nullptr && m_chunkMemo->generationEnabled) {
    const std::size_t shardIndex =
      std::min(static_cast<std::size_t>(memoShardIndex),
               ChunkMemoState::kShardCount - 1u);
    memoShard = &m_chunkMemo->shards[shardIndex];
    if (memoShard->canProbe(m_chunkMemo->generationForced, m_chunkMemo->mode)) {
      std::size_t keyIndex = 0u;
      for (int haloY = 0; haloY < kHaloDim; ++haloY) {
        int chunkY = 1;
        int localY = haloY - 1;
        if (haloY == 0) {
          chunkY = 0;
          localY = kChunkDim - 1;
        } else if (haloY == kHaloDim - 1) {
          chunkY = 2;
          localY = 0;
        }
        for (int haloX = 0; haloX < kHaloDim; ++haloX) {
          int chunkX = 1;
          int localX = haloX - 1;
          if (haloX == 0) {
            chunkX = 0;
            localX = kChunkDim - 1;
          } else if (haloX == kHaloDim - 1) {
            chunkX = 2;
            localX = 0;
          }
          const ChunkData* chunk = neighborhood[chunkY][chunkX];
          memoKey[keyIndex++] = chunk == nullptr
                                  ? BackgroundState
                                  : chunk->cells[static_cast<std::size_t>(
                                      localY * kChunkDim + localX)];
        }
      }
      memoHash = ChunkMemoState::hashKey(memoKey);
      memoProbed = true;
      if (memoShard->lookup(memoKey, memoHash, result)) {
        result->address = target;
        return;
      }
    }
  }

  result->address = target;
  result->occupied.fill(0u);
  result->counted.fill(0u);
  result->stateChanged.fill(0u);
  result->countedChanged.fill(0u);
  result->hasNonBackground = false;
  result->completeCells = true;
  std::array<unsigned char, kChunkDim> previousRow;
  std::array<unsigned char, kChunkDim> currentRow;
  std::array<unsigned char, kChunkDim> nextRow;
  buildHorizontalNeighborRow(neighborhood, 0, &previousRow);
  buildHorizontalNeighborRow(neighborhood, 1, &currentRow);
  buildHorizontalNeighborRow(neighborhood, 2, &nextRow);
  const ChunkData* center = neighborhood[1][1];
  for (int localY = 0; localY < kChunkDim; ++localY) {
    for (int localX = 0; localX < kChunkDim; ++localX) {
      const std::size_t resultIndex =
        static_cast<std::size_t>(localY * kChunkDim + localX);
      const unsigned char current =
        center == nullptr ? BackgroundState : center->cells[resultIndex];
      unsigned char aliveNeighbors = static_cast<unsigned char>(
        previousRow[static_cast<std::size_t>(localX)] +
        currentRow[static_cast<std::size_t>(localX)] +
        nextRow[static_cast<std::size_t>(localX)]);
      if (current == CountedNeighborState) {
        aliveNeighbors = static_cast<unsigned char>(aliveNeighbors - 1u);
      }
      const unsigned char next =
        transitions[RuleSet::transitionIndex(current, aliveNeighbors)];
      result->cells[resultIndex] = next;
      const std::size_t word = resultIndex / 64u;
      const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                                << static_cast<unsigned int>(resultIndex % 64u);
      if (!m_countedChangeCoversStateChange && next != current) {
        result->stateChanged[word] |= bit;
      }
      if (next != BackgroundState) {
        result->occupied[word] |= bit;
        if (next == CountedNeighborState) {
          result->counted[word] |= bit;
        }
        result->hasNonBackground = true;
      }
    }
    if (localY + 1 < kChunkDim) {
      previousRow = currentRow;
      currentRow = nextRow;
      buildHorizontalNeighborRow(neighborhood, localY + 3, &nextRow);
    }
  }
  for (std::size_t wordIndex = 0u; wordIndex < result->counted.size();
       ++wordIndex) {
    const std::uint64_t currentCounted =
      center == nullptr ? 0u : center->counted[wordIndex];
    result->countedChanged[wordIndex] =
      currentCounted ^ result->counted[wordIndex];
    if (m_countedChangeCoversStateChange) {
      result->stateChanged[wordIndex] = result->countedChanged[wordIndex];
    }
  }
  if (memoProbed && memoShard != nullptr) {
    memoShard->insert(memoKey, memoHash, *result);
  }
}

void
SparseCellGrid::beginChunkMemoGeneration(std::size_t haloTargetCount,
                                         const RuleSet& ruleSet)
{
  lastAdvanceStats.memoProbeCount = 0u;
  lastAdvanceStats.memoHitCount = 0u;
  lastAdvanceStats.memoMissCount = 0u;
  lastAdvanceStats.memoBypassTargetCount = haloTargetCount;
  lastAdvanceStats.memoEntryCount = 0u;
  lastAdvanceStats.memoMemoryBytes = 0u;
  lastAdvanceStats.usedChunkMemo = false;
  lastAdvanceStats.chunkMemoActive = false;

  if (chunkMemoOverride < 0 || haloTargetCount < kParallelTargetThreshold) {
    if (m_chunkMemo != nullptr) {
      m_chunkMemo->generationEnabled = false;
      m_chunkMemo->generationForced = false;
    }
    return;
  }
  if (m_chunkMemo == nullptr) {
    m_chunkMemo = std::make_unique<ChunkMemoState>();
  }

  ChunkMemoState& memo = *m_chunkMemo;
  const std::type_info* ruleType = &typeid(ruleSet);
  const std::uint64_t ruleRevision = ruleSet.getTransitionRevision();
  if (memo.ruleType != ruleType || memo.ruleRevision != ruleRevision) {
    memo.clearEntries();
    memo.ruleType = ruleType;
    memo.ruleRevision = ruleRevision;
    memo.mode = ChunkMemoState::AdaptiveMode::Probe;
    memo.cooldownGenerations = 0u;
    memo.lowHitGenerations = 0u;
  }
  for (ChunkMemoState::Shard& shard : memo.shards) {
    shard.beginGeneration();
  }

  memo.generationForced = chunkMemoOverride > 0;
  if (!memo.generationForced &&
      memo.mode == ChunkMemoState::AdaptiveMode::Cooldown) {
    if (memo.cooldownGenerations > 0u) {
      memo.cooldownGenerations -= 1u;
    }
    if (memo.cooldownGenerations == 0u) {
      memo.mode = ChunkMemoState::AdaptiveMode::Probe;
    }
  }
  memo.generationEnabled = memo.generationForced ||
                           memo.mode != ChunkMemoState::AdaptiveMode::Cooldown;
  lastAdvanceStats.chunkMemoActive =
    memo.generationForced || memo.mode == ChunkMemoState::AdaptiveMode::Active;
}

void
SparseCellGrid::finishChunkMemoGeneration(std::size_t haloTargetCount)
{
  if (m_chunkMemo == nullptr) {
    lastAdvanceStats.memoBypassTargetCount = haloTargetCount;
    return;
  }

  ChunkMemoState& memo = *m_chunkMemo;
  std::size_t probes = 0u;
  std::size_t hits = 0u;
  std::size_t misses = 0u;
  std::size_t entries = 0u;
  std::size_t memoryBytes = 0u;
  for (const ChunkMemoState::Shard& shard : memo.shards) {
    probes = saturatingAdd(probes, shard.probes);
    hits = saturatingAdd(hits, shard.hits);
    misses = saturatingAdd(misses, shard.misses);
    entries = saturatingAdd(entries, shard.entryCount);
    memoryBytes =
      saturatingAdd(memoryBytes,
                    saturatingMultiply(shard.entries.capacity(),
                                       sizeof(ChunkMemoState::Entry)));
  }
  lastAdvanceStats.memoProbeCount = probes;
  lastAdvanceStats.memoHitCount = hits;
  lastAdvanceStats.memoMissCount = misses;
  lastAdvanceStats.memoBypassTargetCount =
    probes >= haloTargetCount ? 0u : haloTargetCount - probes;
  lastAdvanceStats.memoEntryCount = entries;
  lastAdvanceStats.memoMemoryBytes = memoryBytes;
  lastAdvanceStats.usedChunkMemo = probes != 0u;

  if (chunkMemoOverride != 0 || !memo.generationEnabled) {
    return;
  }
  const double hitRate =
    probes == 0u ? 0.0
                 : static_cast<double>(hits) / static_cast<double>(probes);
  if (memo.mode == ChunkMemoState::AdaptiveMode::Probe && probes >= 16u) {
    if (hitRate >= 0.25) {
      memo.mode = ChunkMemoState::AdaptiveMode::Active;
      memo.lowHitGenerations = 0u;
    } else {
      memo.mode = ChunkMemoState::AdaptiveMode::Cooldown;
      memo.cooldownGenerations = 32u;
    }
  } else if (memo.mode == ChunkMemoState::AdaptiveMode::Active) {
    if (probes >= 32u && hitRate < 0.10) {
      memo.lowHitGenerations += 1u;
      if (memo.lowHitGenerations >= 3u) {
        memo.mode = ChunkMemoState::AdaptiveMode::Cooldown;
        memo.cooldownGenerations = 32u;
        memo.lowHitGenerations = 0u;
      }
    } else {
      memo.lowHitGenerations = 0u;
    }
  }
}

bool
SparseCellGrid::advanceChangedFrontier(const RuleSet& ruleSet,
                                       bool useCandidateScratch)
{
  ZoneScopedN("SparseCellGrid.advanceChangedFrontier");
  try {
    const unsigned char* transitions = ruleSet.getTransitionTable().data();
    m_frontierResults.resize(m_frontierTargets.size());
    lastAdvanceStats.targetChunkCount = m_frontierTargets.size();
    lastAdvanceStats.frontierTargetCount = m_frontierTargets.size();
    lastAdvanceStats.usedChangedFrontier = true;

    if (useCandidateScratch) {
      if (m_candidateScratch.size() != m_frontierTargets.size()) {
        return false;
      }
      std::size_t evaluationWork = 0u;
      for (const CandidateScratchChunk& scratch : m_candidateScratch) {
        lastAdvanceStats.candidateCellCount = saturatingAdd(
          lastAdvanceStats.candidateCellCount, scratch.candidateCellCount);
        evaluationWork =
          saturatingAdd(evaluationWork,
                        scratch.useCellCandidates ? scratch.candidateCellCount
                                                  : kChunkCellCount);
        if (scratch.useCellCandidates) {
          lastAdvanceStats.candidateTargetCount += 1u;
        } else {
          lastAdvanceStats.haloTargetCount += 1u;
        }
      }
      lastAdvanceStats.usedCellCandidates =
        lastAdvanceStats.candidateTargetCount != 0u;
      lastAdvanceStats.usedMixedTargets =
        lastAdvanceStats.candidateTargetCount != 0u &&
        lastAdvanceStats.haloTargetCount != 0u;
      const bool parallelCandidatesRequested =
        workerOverride > 1 ||
        (workerOverride == 0 &&
         evaluationWork >= kParallelCandidateCellThreshold);
      if (parallelCandidatesRequested) {
        buildCandidateWorkRanges();
        lastAdvanceStats.candidateWorkRangeCount = m_candidateWorkRanges.size();
        lastAdvanceStats.workerCount =
          resolveCandidateWorkerCount(evaluationWork);
      }

      beginChunkMemoGeneration(lastAdvanceStats.haloTargetCount, ruleSet);
      const std::chrono::steady_clock::time_point evaluationStart =
        std::chrono::steady_clock::now();
      if (lastAdvanceStats.workerCount > 1u) {
        ZoneScopedN("SparseCellGrid.evaluateFrontierCandidatesParallel");
        if (workerPool == nullptr) {
          workerPool = std::make_unique<SparseWorkerPool>();
        }
        workerPool->evaluateCandidates(this,
                                       transitions,
                                       &m_candidateScratch,
                                       &m_candidateWorkRanges,
                                       &m_frontierResults,
                                       lastAdvanceStats.workerCount);
      } else {
        ZoneScopedN("SparseCellGrid.evaluateFrontierCandidatesSerial");
        for (std::size_t index = 0u; index < m_candidateScratch.size();
             ++index) {
          evaluateCandidateChunk(
            m_candidateScratch[index], transitions, &m_frontierResults[index]);
        }
      }
      lastAdvanceStats.candidateEvaluationMilliseconds =
        millisecondsSince(evaluationStart);
      finishChunkMemoGeneration(lastAdvanceStats.haloTargetCount);
    } else {
      lastAdvanceStats.haloTargetCount = m_frontierTargets.size();
      lastAdvanceStats.workerCount =
        resolveWorkerCount(m_frontierTargets.size());
      beginChunkMemoGeneration(lastAdvanceStats.haloTargetCount, ruleSet);
      const std::chrono::steady_clock::time_point evaluationStart =
        std::chrono::steady_clock::now();
      if (lastAdvanceStats.workerCount > 1u) {
        ZoneScopedN("SparseCellGrid.evaluateFrontierParallel");
        if (workerPool == nullptr) {
          workerPool = std::make_unique<SparseWorkerPool>();
        }
        workerPool->evaluate(this,
                             transitions,
                             &m_frontierTargets,
                             &m_frontierResults,
                             lastAdvanceStats.workerCount);
      } else {
        ZoneScopedN("SparseCellGrid.evaluateFrontierSerial");
        for (std::size_t index = 0u; index < m_frontierTargets.size();
             ++index) {
          evaluateTargetChunk(
            m_frontierTargets[index], transitions, &m_frontierResults[index]);
        }
      }
      lastAdvanceStats.candidateEvaluationMilliseconds =
        millisecondsSince(evaluationStart);
      finishChunkMemoGeneration(lastAdvanceStats.haloTargetCount);
    }

    const std::chrono::steady_clock::time_point changeStart =
      std::chrono::steady_clock::now();
    beginNextChangedChunks();
    for (const TargetResult& result : m_frontierResults) {
      if (hasMaskBits(result.stateChanged)) {
        markNextChangedChunk(
          result.address, result.stateChanged, result.countedChanged);
      }
    }
    lastAdvanceStats.candidateChangeTrackingMilliseconds =
      millisecondsSince(changeStart);

    if (m_recycledChunkNodes.size() >
        std::numeric_limits<std::size_t>::max() - m_frontierTargets.size()) {
      throw std::bad_alloc();
    }
    m_recycledChunkNodes.reserve(m_recycledChunkNodes.size() +
                                 m_frontierTargets.size());
    if (m_nextChunks.size() >
        std::numeric_limits<std::size_t>::max() - m_frontierTargets.size()) {
      throw std::bad_alloc();
    }
    const std::size_t maximumMapSize =
      m_nextChunks.size() + m_frontierTargets.size();
    const double retainedBucketCapacity =
      static_cast<double>(m_nextChunks.bucket_count()) *
      static_cast<double>(m_nextChunks.max_load_factor());
    if (static_cast<double>(maximumMapSize) > retainedBucketCapacity) {
      m_nextChunks.reserve(maximumMapSize);
    }

    {
      ZoneScopedN("SparseCellGrid.applyFrontierResults");
      const std::chrono::steady_clock::time_point outputStart =
        std::chrono::steady_clock::now();
      for (const TargetResult& result : m_frontierResults) {
        ChunkMap::iterator destination = m_nextChunks.find(result.address);
        if (!result.hasNonBackground) {
          if (destination != m_nextChunks.end()) {
            removeChunkStatistics(destination->second, &m_nextChunkStatistics);
            m_recycledChunkNodes.push_back(m_nextChunks.extract(destination));
          }
          continue;
        }

        ChunkData next;
        writeChunkFromResult(&next, result);
        if (destination == m_nextChunks.end()) {
          insertNextChunk(result.address, next);
        } else {
          removeChunkStatistics(destination->second, &m_nextChunkStatistics);
          destination->second = next;
          addChunkStatistics(destination->second, &m_nextChunkStatistics);
          lastAdvanceStats.reusedChunkNodeCount += 1u;
        }
      }
      lastAdvanceStats.candidateOutputMilliseconds =
        millisecondsSince(outputStart);
    }
  } catch (const std::bad_alloc&) {
    return false;
  } catch (const std::exception&) {
    return false;
  }

  const std::chrono::steady_clock::time_point mergeStart =
    std::chrono::steady_clock::now();
  const bool changed = m_frontierInvalid || !m_nextChangedChunks.empty();
  if (changed) {
    chunks.swap(m_nextChunks);
    std::swap(m_chunkStatistics, m_nextChunkStatistics);
    revision += 1u;
    m_candidateTopologyRevision += 1u;
    if (m_candidateTopologyRevision == 0u) {
      m_candidateTopologyRevision = 1u;
    }
    m_nextChunksMirrorCurrent = false;
  }
  if (!m_frontierInvalid) {
    commitNextChangedChunks();
  }
  if (changed) {
    publishChangedChunksRevision(!m_frontierInvalid);
  }
  lastAdvanceStats.retainedChunkNodeCount =
    m_recycledChunkNodes.size() + m_nextChunks.size();
  lastAdvanceStats.candidateMergeMilliseconds = millisecondsSince(mergeStart);
  return true;
}

bool
SparseCellGrid::advanceCellCandidates(const RuleSet& ruleSet,
                                      bool adaptiveTargets)
{
  ZoneScopedN("SparseCellGrid.advanceCellCandidates");

  bool usedDirectResultMerge = false;
  try {
    const ChunkMap& sourceChunks = generationChunks();
    const ChunkStatistics& sourceStatistics =
      generationSource().m_chunkStatistics;
    const unsigned char* transitions = ruleSet.getTransitionTable().data();
    std::size_t candidateCellCount = 0u;
    const std::size_t preparationWork =
      saturatingAdd(sourceStatistics.activeCellCount,
                    saturatingMultiply(sourceStatistics.countedCellCount, 8u));
    const std::size_t maximumTargetCount =
      saturatingMultiply(sourceChunks.size(), 9u);
    const bool linkCandidateSources =
      resolveCandidatePreparationWorkerCount(maximumTargetCount,
                                             preparationWork) > 1u;
    const bool reuseCandidateTopology =
      linkCandidateSources && m_generationSourceGrid != nullptr &&
      m_candidateScratchSourceGrid == &generationSource() &&
      m_candidateScratchSourceTopologyRevision ==
        generationSource().m_candidateTopologyRevision &&
      !m_candidateScratch.empty();
    const std::chrono::steady_clock::time_point discoveryStart =
      std::chrono::steady_clock::now();
    {
      ZoneScopedN("SparseCellGrid.collectCandidateChunks");
      if (reuseCandidateTopology) {
        for (CandidateScratchChunk& scratch : m_candidateScratch) {
          scratch.candidates.fill(0u);
          scratch.centerSource = scratch.sources[4u];
          scratch.candidateCellCount = 0u;
          scratch.neighborContributionCount = 0u;
          scratch.sourcesLinked = true;
          scratch.centerSourceKnown = true;
          scratch.useCellCandidates = true;
          scratch.skipNeighborPrep = false;
        }
        lastAdvanceStats.reusedCandidateTopology = true;
      } else {
        beginCandidateIndexGeneration();
        for (ChunkMap::const_reference entry : sourceChunks) {
          enrollCandidateTarget(
            entry.first, 0, 0, &entry.second, linkCandidateSources);
          const OccupancyMask& counted = entry.second.counted;
          const bool negativeX = (counted[0] & kChunkLeftEdgeMask) != 0u ||
                                 (counted[1] & kChunkLeftEdgeMask) != 0u ||
                                 (counted[2] & kChunkLeftEdgeMask) != 0u ||
                                 (counted[3] & kChunkLeftEdgeMask) != 0u;
          const bool positiveX = (counted[0] & kChunkRightEdgeMask) != 0u ||
                                 (counted[1] & kChunkRightEdgeMask) != 0u ||
                                 (counted[2] & kChunkRightEdgeMask) != 0u ||
                                 (counted[3] & kChunkRightEdgeMask) != 0u;
          const bool negativeY = (counted[0] & kChunkTopRowMask) != 0u;
          const bool positiveY = (counted[3] & kChunkBottomRowMask) != 0u;
          const bool negativeXNegativeY = (counted[0] & 1u) != 0u;
          const bool positiveXNegativeY =
            (counted[0] & (static_cast<std::uint64_t>(1u) << 15u)) != 0u;
          const bool negativeXPositiveY =
            (counted[3] & (static_cast<std::uint64_t>(1u) << 48u)) != 0u;
          const bool positiveXPositiveY =
            (counted[3] & (static_cast<std::uint64_t>(1u) << 63u)) != 0u;

          if (negativeX) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x - 1, entry.first.y },
              1,
              0,
              &entry.second,
              linkCandidateSources);
          }
          if (positiveX) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x + 1, entry.first.y },
              -1,
              0,
              &entry.second,
              linkCandidateSources);
          }
          if (negativeY) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x, entry.first.y - 1 },
              0,
              1,
              &entry.second,
              linkCandidateSources);
          }
          if (positiveY) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x, entry.first.y + 1 },
              0,
              -1,
              &entry.second,
              linkCandidateSources);
          }
          if (negativeXNegativeY) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x - 1, entry.first.y - 1 },
              1,
              1,
              &entry.second,
              linkCandidateSources);
          }
          if (positiveXNegativeY) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x + 1, entry.first.y - 1 },
              -1,
              1,
              &entry.second,
              linkCandidateSources);
          }
          if (negativeXPositiveY) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x - 1, entry.first.y + 1 },
              1,
              -1,
              &entry.second,
              linkCandidateSources);
          }
          if (positiveXPositiveY) {
            enrollCandidateTarget(
              ChunkAddress{ entry.first.x + 1, entry.first.y + 1 },
              -1,
              -1,
              &entry.second,
              linkCandidateSources);
          }
        }
        if (linkCandidateSources) {
          m_candidateScratchSourceGrid = &generationSource();
          m_candidateScratchSourceTopologyRevision =
            generationSource().m_candidateTopologyRevision;
        } else {
          m_candidateScratchSourceGrid = nullptr;
          m_candidateScratchSourceTopologyRevision = 0u;
        }
      }
      classifyHaloScratchTargets();
    }
    lastAdvanceStats.candidateDiscoveryMilliseconds =
      millisecondsSince(discoveryStart);

    const std::chrono::steady_clock::time_point preparationStart =
      std::chrono::steady_clock::now();
    {
      ZoneScopedN("SparseCellGrid.buildCandidateScratch");
      lastAdvanceStats.candidatePreparationWorkerCount =
        resolveCandidatePreparationWorkerCount(m_candidateScratch.size(),
                                               preparationWork);
      if (lastAdvanceStats.candidatePreparationWorkerCount > 1u) {
        if (workerPool == nullptr) {
          workerPool = std::make_unique<SparseWorkerPool>();
        }
        buildCandidatePreparationRanges(
          lastAdvanceStats.candidatePreparationWorkerCount);
        workerPool->prepareCandidates(
          this,
          &m_candidateScratch,
          &m_candidatePreparationRanges,
          lastAdvanceStats.candidatePreparationWorkerCount);
      } else {
        for (ChunkMap::const_reference entry : sourceChunks) {
          if (!sourceNeedsCandidatePreparation(entry.first)) {
            continue;
          }
          prepareCandidateScratchFromSource(entry.first, entry.second);
        }
      }
      for (const CandidateScratchChunk& scratch : m_candidateScratch) {
        candidateCellCount =
          saturatingAdd(candidateCellCount, scratch.candidateCellCount);
      }
    }
    lastAdvanceStats.candidatePreparationMilliseconds =
      millisecondsSince(preparationStart);

    lastAdvanceStats.targetChunkCount = m_candidateScratch.size();
    lastAdvanceStats.candidateCellCount = candidateCellCount;
    std::size_t evaluationCellCount = 0u;
    for (CandidateScratchChunk& scratch : m_candidateScratch) {
      if (scratch.skipNeighborPrep) {
        scratch.useCellCandidates = false;
      } else {
        scratch.useCellCandidates =
          !adaptiveTargets || scratch.neighborContributionCount <
                                kCandidateNeighborContributionThreshold;
      }
      const std::size_t targetWork = scratch.useCellCandidates
                                       ? scratch.candidateCellCount
                                       : kChunkCellCount;
      if (evaluationCellCount >
          std::numeric_limits<std::size_t>::max() - targetWork) {
        evaluationCellCount = std::numeric_limits<std::size_t>::max();
      } else {
        evaluationCellCount += targetWork;
      }
      if (scratch.useCellCandidates) {
        lastAdvanceStats.candidateTargetCount += 1u;
      } else {
        lastAdvanceStats.haloTargetCount += 1u;
      }
    }
    lastAdvanceStats.usedCellCandidates =
      lastAdvanceStats.candidateTargetCount != 0u;
    lastAdvanceStats.usedMixedTargets =
      lastAdvanceStats.candidateTargetCount != 0u &&
      lastAdvanceStats.haloTargetCount != 0u;
    const bool parallelCandidatesRequested =
      workerOverride > 1 ||
      (workerOverride == 0 &&
       evaluationCellCount >= kParallelCandidateCellThreshold);
    if (parallelCandidatesRequested) {
      buildCandidateWorkRanges();
      lastAdvanceStats.candidateWorkRangeCount = m_candidateWorkRanges.size();
      lastAdvanceStats.workerCount =
        resolveCandidateWorkerCount(evaluationCellCount);
    }
    if (parallelCandidatesRequested || lastAdvanceStats.haloTargetCount != 0u) {
      m_candidateResults.resize(m_candidateScratch.size());
    }
    beginChunkMemoGeneration(lastAdvanceStats.haloTargetCount, ruleSet);

    std::size_t outputChunkCount = 0u;
    const std::chrono::steady_clock::time_point evaluationStart =
      std::chrono::steady_clock::now();
    if (lastAdvanceStats.workerCount > 1u ||
        lastAdvanceStats.haloTargetCount != 0u) {
      if (lastAdvanceStats.workerCount > 1u) {
        ZoneScopedN("SparseCellGrid.evaluateCellCandidates");
        if (workerPool == nullptr) {
          workerPool = std::make_unique<SparseWorkerPool>();
        }
        workerPool->evaluateCandidates(this,
                                       transitions,
                                       &m_candidateScratch,
                                       &m_candidateWorkRanges,
                                       &m_candidateResults,
                                       lastAdvanceStats.workerCount);
        for (const CandidateWorkRange& range : m_candidateWorkRanges) {
          outputChunkCount =
            saturatingAdd(outputChunkCount, range.outputChunkCount);
        }
      } else {
        ZoneScopedN("SparseCellGrid.evaluateMixedTargetsSerial");
        for (std::size_t index = 0u; index < m_candidateScratch.size();
             ++index) {
          evaluateCandidateChunk(
            m_candidateScratch[index], transitions, &m_candidateResults[index]);
          if (m_candidateResults[index].hasNonBackground) {
            outputChunkCount += 1u;
          }
        }
      }
      lastAdvanceStats.producedChunkCount = outputChunkCount;
      lastAdvanceStats.candidateEvaluationMilliseconds =
        millisecondsSince(evaluationStart);

      const std::chrono::steady_clock::time_point mergeStart =
        std::chrono::steady_clock::now();

      m_frontierInvalid = false;
      beginNextChangedChunks();
      for (const TargetResult& result : m_candidateResults) {
        recordNextCandidateTopology(result);
        if (hasMaskBits(result.stateChanged)) {
          markNextChangedChunk(
            result.address, result.stateChanged, result.countedChanged);
        }
      }
      lastAdvanceStats.candidateChangeTrackingMilliseconds =
        millisecondsSince(mergeStart);

      {
        ZoneScopedN("SparseCellGrid.mergeCandidateResults");
        const std::chrono::steady_clock::time_point recycleStart =
          std::chrono::steady_clock::now();
        const bool directSourceGeneration = m_generationSourceGrid != nullptr;
        const bool prepared = directSourceGeneration
                                ? prepareDirectChunks(outputChunkCount)
                                : prepareNextChunks(m_candidateScratch.size());
        if (!prepared) {
          return false;
        }
        usedDirectResultMerge = directSourceGeneration;
        lastAdvanceStats.candidateRecycleMilliseconds =
          millisecondsSince(recycleStart);
        const std::chrono::steady_clock::time_point outputStart =
          std::chrono::steady_clock::now();
        for (std::size_t resultIndex = 0u;
             resultIndex < m_candidateResults.size();
             ++resultIndex) {
          const TargetResult& result = m_candidateResults[resultIndex];
          if (!result.hasNonBackground) {
            if (directSourceGeneration) {
              m_candidateScratch[resultIndex].directDestination = nullptr;
            }
            continue;
          }
          bool inserted = false;
          if (directSourceGeneration) {
            ChunkData* knownDestination =
              reuseCandidateTopology && !m_nextCandidateTopologyChanged
                ? m_candidateScratch[resultIndex].directDestination
                : nullptr;
            inserted = insertDirectResultChunk(
              result,
              knownDestination,
              &m_candidateScratch[resultIndex].directDestination);
          } else {
            inserted = insertNextResultChunk(result);
          }
          if (!inserted) {
            return false;
          }
        }
        lastAdvanceStats.candidateOutputMilliseconds =
          millisecondsSince(outputStart);
      }
      lastAdvanceStats.candidateMergeMilliseconds =
        millisecondsSince(mergeStart);
    } else {
      ZoneScopedN("SparseCellGrid.evaluateCellCandidatesSerial");
      if (!prepareNextChunks(m_candidateScratch.size())) {
        return false;
      }
      m_frontierInvalid = false;
      beginNextChangedChunks();
      for (const CandidateScratchChunk& scratch : m_candidateScratch) {
        const ChunkMap::const_iterator source =
          sourceChunks.find(scratch.address);
        ChunkData nextChunk;
        nextChunk.cells.fill(BackgroundState);
        OccupancyMask stateChanged;
        OccupancyMask countedChanged;
        stateChanged.fill(0u);
        countedChanged.fill(0u);
        for (std::size_t wordIndex = 0u; wordIndex < scratch.candidates.size();
             ++wordIndex) {
          std::uint64_t candidates = scratch.candidates[wordIndex];
          while (candidates != 0u) {
            const unsigned int offset = std::countr_zero(candidates);
            const std::size_t index = wordIndex * 64u + offset;
            candidates &= candidates - 1u;

            const unsigned char current = source == sourceChunks.end()
                                            ? BackgroundState
                                            : source->second.cells[index];
            const unsigned char next = transitions[RuleSet::transitionIndex(
              current, scratch.neighborCounts[index])];
            const std::uint64_t bit = static_cast<std::uint64_t>(1u)
                                      << static_cast<unsigned int>(index % 64u);
            if (!m_countedChangeCoversStateChange && next != current) {
              stateChanged[wordIndex] |= bit;
            }
            if (next == BackgroundState) {
              continue;
            }
            nextChunk.cells[index] = next;
            setOccupied(&nextChunk, index, true);
            setCounted(&nextChunk, index, next == CountedNeighborState);
          }
        }
        if (hasOccupiedCells(nextChunk)) {
          insertNextChunk(scratch.address, nextChunk);
          outputChunkCount += 1u;
        }
        TargetResult topologyResult;
        topologyResult.address = scratch.address;
        topologyResult.counted = nextChunk.counted;
        topologyResult.hasNonBackground = hasOccupiedCells(nextChunk);
        recordNextCandidateTopology(topologyResult);
        for (std::size_t wordIndex = 0u; wordIndex < countedChanged.size();
             ++wordIndex) {
          const std::uint64_t currentCounted =
            source == sourceChunks.end() ? 0u
                                         : source->second.counted[wordIndex];
          countedChanged[wordIndex] =
            currentCounted ^ nextChunk.counted[wordIndex];
          if (m_countedChangeCoversStateChange) {
            stateChanged[wordIndex] = countedChanged[wordIndex];
          }
        }
        if (!m_frontierInvalid && hasMaskBits(stateChanged)) {
          markNextChangedChunk(scratch.address, stateChanged, countedChanged);
        }
      }
      lastAdvanceStats.candidateEvaluationMilliseconds =
        millisecondsSince(evaluationStart);
      lastAdvanceStats.producedChunkCount = outputChunkCount;
    }
    finishChunkMemoGeneration(lastAdvanceStats.haloTargetCount);
  } catch (const std::bad_alloc&) {
    return false;
  } catch (const std::exception&) {
    return false;
  }

  if (usedDirectResultMerge) {
    finishDirectChunks();
  } else {
    finishNextChunks(true);
  }
  return true;
}

bool
SparseCellGrid::advance(const RuleSet& ruleSet)
{
  m_generationSourceGrid = nullptr;
  return advanceImpl(ruleSet, true);
}

void
SparseCellGrid::enrollToroidalCandidate(const CellAddress& address,
                                        bool addNeighborContribution)
{
  CellAddress canonicalAddress = address;
  const std::int64_t minimumX = -(m_worldChunkWidth / 2) * kChunkDim;
  const std::int64_t minimumY = -(m_worldChunkHeight / 2) * kChunkDim;
  const std::int64_t maximumX = minimumX + m_worldChunkWidth * kChunkDim;
  const std::int64_t maximumY = minimumY + m_worldChunkHeight * kChunkDim;
  if (canonicalAddress.x < minimumX) {
    canonicalAddress.x += m_worldChunkWidth * kChunkDim;
  } else if (canonicalAddress.x >= maximumX) {
    canonicalAddress.x -= m_worldChunkWidth * kChunkDim;
  }
  if (canonicalAddress.y < minimumY) {
    canonicalAddress.y += m_worldChunkHeight * kChunkDim;
  } else if (canonicalAddress.y >= maximumY) {
    canonicalAddress.y -= m_worldChunkHeight * kChunkDim;
  }
  const ChunkAddress targetAddress = chunkAddressForCell(canonicalAddress);
  const std::size_t targetIndex =
    static_cast<std::size_t>(localIndexForCell(canonicalAddress));
  CandidateScratchChunk* scratch = findOrCreateCandidateScratch(targetAddress);
  lastAdvanceStats.candidateEnrollmentAttemptCount += 1u;
  if (markCandidate(scratch, targetIndex)) {
    scratch->candidateCellCount += 1u;
  }
  if (addNeighborContribution) {
    scratch->neighborCounts[targetIndex] =
      static_cast<unsigned char>(scratch->neighborCounts[targetIndex] + 1u);
    scratch->neighborContributionCount += 1u;
  }
}

bool
SparseCellGrid::advanceElementarySpaceTime(const RuleSet& ruleSet)
{
  ZoneScopedN("SparseCellGrid.advanceElementary");
  const SparseCellGrid& source = generationSource();
  lastAdvanceStats = SparseAdvanceStats{};
  lastAdvanceStats.activeChunkCount = source.chunks.size();
  lastAdvanceStats.activeCellCount = source.m_chunkStatistics.activeCellCount;
  lastAdvanceStats.countedCellCount = source.m_chunkStatistics.countedCellCount;
  lastAdvanceStats.workerCount = 1u;

  bool found = false;
  std::int64_t sourceY = 0;
  std::int64_t minX = 0;
  std::int64_t maxX = 0;
  for (ChunkMap::const_reference entry : source.chunks) {
    for (std::size_t wordIndex = 0u; wordIndex < entry.second.counted.size();
         ++wordIndex) {
      std::uint64_t counted = entry.second.counted[wordIndex];
      while (counted != 0u) {
        const unsigned int offset = std::countr_zero(counted);
        const std::size_t index = wordIndex * 64u + offset;
        counted &= counted - 1u;
        const int localX = static_cast<int>(index % kChunkDim);
        const int localY = static_cast<int>(index / kChunkDim);
        const std::int64_t cellX = entry.first.x * kChunkDim + localX;
        const std::int64_t cellY = entry.first.y * kChunkDim + localY;
        if (!found) {
          sourceY = cellY;
          minX = cellX;
          maxX = cellX;
          found = true;
        } else if (cellY > sourceY) {
          sourceY = cellY;
          minX = cellX;
          maxX = cellX;
        } else if (cellY == sourceY) {
          if (cellX < minX) {
            minX = cellX;
          }
          if (cellX > maxX) {
            maxX = cellX;
          }
        }
      }
    }
  }
  if (!found) {
    return true;
  }

  CellAddress destination{ 0, sourceY + 1 };
  destination = canonicalizeCell(destination);
  const std::int64_t destY = destination.y;

  struct PendingWrite
  {
    std::int64_t x = 0;
    unsigned char state = 1;
  };
  std::vector<PendingWrite> writes;
  for (std::int64_t x = minX - 1; x <= maxX + 1; ++x) {
    const unsigned char left = source.getCell(CellAddress{ x - 1, sourceY });
    const unsigned char center = source.getCell(CellAddress{ x, sourceY });
    const unsigned char right = source.getCell(CellAddress{ x + 1, sourceY });
    PendingWrite write;
    write.x = x;
    write.state = ruleSet.nextElementary(left, center, right);
    writes.push_back(write);
  }

  if (&source != this) {
    copyStateFrom(source);
  }
  for (std::size_t i = 0; i < writes.size(); ++i) {
    CellAddress dest{ writes[i].x, destY };
    dest = canonicalizeCell(dest);
    if (!isCellInWorldBounds(dest)) {
      continue;
    }
    setCell(dest, writes[i].state);
  }
  lastAdvanceStats.targetChunkCount = 1u;
  lastAdvanceStats.producedChunkCount = 1u;
  return true;
}

bool
SparseCellGrid::advanceToroidal(const RuleSet& ruleSet)
{
  ZoneScopedN("SparseCellGrid.advanceToroidal");
  const SparseCellGrid& source = generationSource();
  const unsigned char* transitions = ruleSet.getTransitionTable().data();
  try {
    const std::chrono::steady_clock::time_point discoveryStart =
      std::chrono::steady_clock::now();
    const std::size_t maximumTargetCount =
      saturatingMultiply(source.chunks.size(), 9u);
    if (m_candidateScratch.capacity() < maximumTargetCount) {
      m_candidateScratch.reserve(maximumTargetCount);
    }
    beginCandidateIndexGeneration();
    for (ChunkMap::const_reference entry : source.chunks) {
      for (std::size_t wordIndex = 0u; wordIndex < entry.second.occupied.size();
           ++wordIndex) {
        std::uint64_t occupied = entry.second.occupied[wordIndex];
        while (occupied != 0u) {
          const unsigned int offset = std::countr_zero(occupied);
          const std::size_t index = wordIndex * 64u + offset;
          occupied &= occupied - 1u;
          const int localX = static_cast<int>(index % kChunkDim);
          const int localY = static_cast<int>(index / kChunkDim);
          enrollToroidalCandidate(
            CellAddress{ entry.first.x * kChunkDim + localX,
                         entry.first.y * kChunkDim + localY },
            false);
        }
      }

      for (std::size_t wordIndex = 0u; wordIndex < entry.second.counted.size();
           ++wordIndex) {
        std::uint64_t counted = entry.second.counted[wordIndex];
        while (counted != 0u) {
          const unsigned int offset = std::countr_zero(counted);
          const std::size_t index = wordIndex * 64u + offset;
          counted &= counted - 1u;
          const std::int64_t sourceX =
            entry.first.x * kChunkDim +
            static_cast<std::int64_t>(index % kChunkDim);
          const std::int64_t sourceY =
            entry.first.y * kChunkDim +
            static_cast<std::int64_t>(index / kChunkDim);
          for (int offsetY = -1; offsetY <= 1; ++offsetY) {
            for (int offsetX = -1; offsetX <= 1; ++offsetX) {
              if (offsetX == 0 && offsetY == 0) {
                continue;
              }
              enrollToroidalCandidate(
                CellAddress{ sourceX + offsetX, sourceY + offsetY }, true);
            }
          }
        }
      }
    }
    lastAdvanceStats.candidateDiscoveryMilliseconds =
      millisecondsSince(discoveryStart);

    std::size_t candidateCellCount = 0u;
    for (CandidateScratchChunk& scratch : m_candidateScratch) {
      const ChunkMap::const_iterator center =
        source.chunks.find(scratch.address);
      scratch.centerSource =
        center == source.chunks.end() ? nullptr : &center->second;
      scratch.centerSourceKnown = true;
      scratch.useCellCandidates = true;
      candidateCellCount =
        saturatingAdd(candidateCellCount, scratch.candidateCellCount);
    }
    lastAdvanceStats.targetChunkCount = m_candidateScratch.size();
    lastAdvanceStats.candidateTargetCount = m_candidateScratch.size();
    lastAdvanceStats.candidateCellCount = candidateCellCount;
    lastAdvanceStats.usedCellCandidates = !m_candidateScratch.empty();
    lastAdvanceStats.workerCount =
      resolveCandidateWorkerCount(candidateCellCount);
    m_candidateResults.resize(m_candidateScratch.size());

    const std::chrono::steady_clock::time_point evaluationStart =
      std::chrono::steady_clock::now();
    if (lastAdvanceStats.workerCount > 1u) {
      buildCandidateWorkRanges();
      lastAdvanceStats.candidateWorkRangeCount = m_candidateWorkRanges.size();
      if (workerPool == nullptr) {
        workerPool = std::make_unique<SparseWorkerPool>();
      }
      workerPool->evaluateCandidates(this,
                                     transitions,
                                     &m_candidateScratch,
                                     &m_candidateWorkRanges,
                                     &m_candidateResults,
                                     lastAdvanceStats.workerCount);
    } else {
      for (std::size_t index = 0u; index < m_candidateScratch.size(); ++index) {
        evaluateCandidateChunk(
          m_candidateScratch[index], transitions, &m_candidateResults[index]);
      }
    }
    lastAdvanceStats.candidateEvaluationMilliseconds =
      millisecondsSince(evaluationStart);

    if (!prepareNextChunks(m_candidateResults.size())) {
      return false;
    }
    m_frontierInvalid = false;
    beginNextChangedChunks();
    std::size_t outputChunkCount = 0u;
    for (const TargetResult& result : m_candidateResults) {
      recordNextCandidateTopology(result);
      if (hasMaskBits(result.stateChanged)) {
        markNextChangedChunk(
          result.address, result.stateChanged, result.countedChanged);
      }
      if (result.hasNonBackground) {
        if (!insertNextResultChunk(result)) {
          return false;
        }
        outputChunkCount += 1u;
      }
    }
    lastAdvanceStats.producedChunkCount = outputChunkCount;
    finishNextChunks(true);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  } catch (const std::exception&) {
    return false;
  }
}

bool
SparseCellGrid::advanceFrom(const SparseCellGrid& source,
                            const RuleSet& ruleSet)
{
  if (&source == this) {
    return advance(ruleSet);
  }
  if (m_worldChunkWidth != source.m_worldChunkWidth ||
      m_worldChunkHeight != source.m_worldChunkHeight) {
    return false;
  }

  m_generationSourceGrid = &source;
  revision = source.revision;
  const bool advanced = advanceImpl(ruleSet, false);
  m_generationSourceGrid = nullptr;
  return advanced;
}

bool
SparseCellGrid::advanceImpl(const RuleSet& ruleSet, bool allowFrontier)
{
  ZoneScopedN("SparseCellGrid.advance");
  if (ruleSet.getNeighborhoodKind() ==
      RuleSet::NeighborhoodKind::Elementary1D) {
    return advanceElementarySpaceTime(ruleSet);
  }
  const SparseCellGrid& source = generationSource();
  const ChunkMap& sourceChunks = source.chunks;
  const ChunkStatistics& sourceStatistics = source.m_chunkStatistics;
  lastAdvanceStats.activeChunkCount = sourceChunks.size();
  lastAdvanceStats.activeCellCount = sourceStatistics.activeCellCount;
  lastAdvanceStats.countedCellCount = sourceStatistics.countedCellCount;
  lastAdvanceStats.candidatePreferredChunkCount =
    sourceStatistics.candidatePreferredChunkCount;
  lastAdvanceStats.targetChunkCount = 0u;
  lastAdvanceStats.candidateCellCount = 0u;
  lastAdvanceStats.candidateTargetCount = 0u;
  lastAdvanceStats.haloTargetCount = 0u;
  lastAdvanceStats.allocatedChunkNodeCount = 0u;
  lastAdvanceStats.reusedChunkNodeCount = 0u;
  lastAdvanceStats.retainedChunkNodeCount =
    m_recycledChunkNodes.size() + m_nextChunks.size();
  lastAdvanceStats.candidateEnrollmentAttemptCount = 0u;
  lastAdvanceStats.candidateIndexGrowthCount = 0u;
  lastAdvanceStats.producedChunkCount = 0u;
  lastAdvanceStats.candidatePreparationRangeCount = 0u;
  lastAdvanceStats.candidateWorkRangeCount = 0u;
  lastAdvanceStats.changedChunkCount = source.m_changedChunks.size();
  lastAdvanceStats.changedCellCount = 0u;
  lastAdvanceStats.countedChangedCellCount = 0u;
  lastAdvanceStats.memoProbeCount = 0u;
  lastAdvanceStats.memoHitCount = 0u;
  lastAdvanceStats.memoMissCount = 0u;
  lastAdvanceStats.memoBypassTargetCount = 0u;
  lastAdvanceStats.memoEntryCount = 0u;
  lastAdvanceStats.memoMemoryBytes = 0u;
  for (const OccupancyMask& mask : source.m_changedCellMasks) {
    lastAdvanceStats.changedCellCount =
      saturatingAdd(lastAdvanceStats.changedCellCount, countMaskBits(mask));
  }
  for (const OccupancyMask& mask : source.m_changedCountedMasks) {
    lastAdvanceStats.countedChangedCellCount = saturatingAdd(
      lastAdvanceStats.countedChangedCellCount, countMaskBits(mask));
  }
  lastAdvanceStats.frontierTargetCount = 0u;
  lastAdvanceStats.frontierSourceChunkCount = 0u;
  lastAdvanceStats.frontierEstimatedWork = 0u;
  lastAdvanceStats.completeEstimatedWork = estimateCompleteAdvanceWork();
  lastAdvanceStats.candidateDiscoveryMilliseconds = 0.0;
  lastAdvanceStats.candidatePreparationMilliseconds = 0.0;
  lastAdvanceStats.candidateEvaluationMilliseconds = 0.0;
  lastAdvanceStats.candidateChangeTrackingMilliseconds = 0.0;
  lastAdvanceStats.candidateRecycleMilliseconds = 0.0;
  lastAdvanceStats.candidateOutputMilliseconds = 0.0;
  lastAdvanceStats.candidateMergeMilliseconds = 0.0;
  lastAdvanceStats.candidatePreparationWorkerCount = 1u;
  lastAdvanceStats.workerCount = 1u;
  lastAdvanceStats.usedCellCandidates = false;
  lastAdvanceStats.usedMixedTargets = false;
  lastAdvanceStats.usedChangedFrontier = false;
  lastAdvanceStats.reusedCandidateTopology = false;
  lastAdvanceStats.usedChunkMemo = false;
  lastAdvanceStats.chunkMemoActive = false;
  m_nextCandidateTopologyChanged = false;

  if (lastRuleType != &typeid(ruleSet)) {
    lastRuleType = &typeid(ruleSet);
    m_backgroundTransitionsStayBinary = true;
    const RuleSet::TransitionTable& transitions = ruleSet.getTransitionTable();
    for (unsigned char neighbors = 0u; neighbors < RuleSet::kNeighborCountCount;
         ++neighbors) {
      const unsigned char next =
        transitions[RuleSet::transitionIndex(BackgroundState, neighbors)];
      if (next != BackgroundState && next != CountedNeighborState) {
        m_backgroundTransitionsStayBinary = false;
        break;
      }
    }
    if (allowFrontier) {
      for (ChunkMap::const_reference entry : sourceChunks) {
        markChangedChunk(
          entry.first, entry.second.occupied, entry.second.counted);
        if (m_frontierInvalid) {
          break;
        }
      }
    }
    lastAdvanceStats.changedChunkCount = source.m_changedChunks.size();
    lastAdvanceStats.changedCellCount = 0u;
    lastAdvanceStats.countedChangedCellCount = 0u;
    for (const OccupancyMask& mask : source.m_changedCellMasks) {
      lastAdvanceStats.changedCellCount =
        saturatingAdd(lastAdvanceStats.changedCellCount, countMaskBits(mask));
    }
    for (const OccupancyMask& mask : source.m_changedCountedMasks) {
      lastAdvanceStats.countedChangedCellCount = saturatingAdd(
        lastAdvanceStats.countedChangedCellCount, countMaskBits(mask));
    }
  }
  m_countedChangeCoversStateChange =
    m_backgroundTransitionsStayBinary &&
    sourceStatistics.activeCellCount == sourceStatistics.countedCellCount;

  if (source.isToroidal()) {
    return advanceToroidal(ruleSet);
  }

  if (allowFrontier && cellCandidateOverride == 0 && !m_frontierInvalid) {
    if (source.m_changedChunks.empty()) {
      lastAdvanceStats.usedChangedFrontier = true;
      return true;
    }
    try {
      const std::chrono::steady_clock::time_point discoveryStart =
        std::chrono::steady_clock::now();
      buildFrontierTargets();
      lastAdvanceStats.candidateDiscoveryMilliseconds =
        millisecondsSince(discoveryStart);
      std::size_t frontierEvaluationWork = 0u;
      bool useCandidateScratch =
        sourceStatistics.candidatePreferredChunkCount != 0u;
      if (useCandidateScratch && !frontierPrefersCandidateScratch()) {
        useCandidateScratch = false;
      }
      if (useCandidateScratch) {
        const std::chrono::steady_clock::time_point preparationStart =
          std::chrono::steady_clock::now();
        if (!buildFrontierCandidateScratch(
              &lastAdvanceStats.frontierEstimatedWork,
              &frontierEvaluationWork)) {
          m_frontierInvalid = true;
        }
        lastAdvanceStats.candidatePreparationMilliseconds =
          millisecondsSince(preparationStart);
        lastAdvanceStats.frontierSourceChunkCount =
          m_frontierSourceChunks.size();
      } else {
        lastAdvanceStats.frontierEstimatedWork =
          saturatingMultiply(m_frontierTargets.size(), kChunkCellCount + 1u);
      }
      if (!m_frontierInvalid && lastAdvanceStats.frontierEstimatedWork <=
                                  lastAdvanceStats.completeEstimatedWork) {
        return advanceChangedFrontier(ruleSet, useCandidateScratch);
      }
    } catch (const std::bad_alloc&) {
      m_frontierInvalid = true;
    }
  }

  if (cellCandidateOverride > 0) {
    return advanceCellCandidates(ruleSet, false);
  }
  if (cellCandidateOverride == 0 &&
      sourceStatistics.candidatePreferredChunkCount != 0u) {
    return advanceCellCandidates(ruleSet, true);
  }

  try {
    const unsigned char* transitions = ruleSet.getTransitionTable().data();
    buildCompleteTargets();
    lastAdvanceStats.targetChunkCount = m_completeTargets.size();
    lastAdvanceStats.haloTargetCount = m_completeTargets.size();
    lastAdvanceStats.workerCount = resolveWorkerCount(m_completeTargets.size());
    m_completeResults.resize(m_completeTargets.size());
    beginChunkMemoGeneration(lastAdvanceStats.haloTargetCount, ruleSet);

    if (lastAdvanceStats.workerCount > 1u) {
      ZoneScopedN("SparseCellGrid.evaluateParallel");
      if (workerPool == nullptr) {
        workerPool = std::make_unique<SparseWorkerPool>();
      }
      workerPool->evaluate(this,
                           transitions,
                           &m_completeTargets,
                           &m_completeResults,
                           lastAdvanceStats.workerCount);
    } else {
      ZoneScopedN("SparseCellGrid.evaluateSerial");
      for (std::size_t index = 0; index < m_completeTargets.size(); ++index) {
        evaluateTargetChunk(
          m_completeTargets[index], transitions, &m_completeResults[index]);
      }
    }
    finishChunkMemoGeneration(lastAdvanceStats.haloTargetCount);

    m_frontierInvalid = false;
    beginNextChangedChunks();
    for (const TargetResult& result : m_completeResults) {
      recordNextCandidateTopology(result);
      if (hasMaskBits(result.stateChanged)) {
        markNextChangedChunk(
          result.address, result.stateChanged, result.countedChanged);
      }
    }

    {
      ZoneScopedN("SparseCellGrid.mergeResults");
      const bool directSourceGeneration = m_generationSourceGrid != nullptr;
      const bool prepared = directSourceGeneration
                              ? prepareDirectChunks(m_completeResults.size())
                              : prepareNextChunks(m_completeResults.size());
      if (!prepared) {
        return false;
      }
      for (const TargetResult& result : m_completeResults) {
        if (result.hasNonBackground) {
          if (directSourceGeneration) {
            if (!insertDirectResultChunk(result)) {
              return false;
            }
          } else {
            ChunkData next;
            next.cells = result.cells;
            next.occupied = result.occupied;
            next.counted = result.counted;
            refreshChunkCounts(&next);
            insertNextChunk(result.address, next);
          }
          lastAdvanceStats.producedChunkCount += 1u;
        }
      }
    }
  } catch (const std::bad_alloc&) {
    return false;
  } catch (const std::exception&) {
    return false;
  }

  if (m_generationSourceGrid != nullptr) {
    finishDirectChunks();
  } else {
    finishNextChunks(true);
  }
  return true;
}
