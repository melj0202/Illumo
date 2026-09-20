#include "SimulationProtocol.h"
#include "Game/IllumoCodec.h"
#include "Rulesets/RuleSetRegistry.h"
#include <bit>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_set>

// Bound serialized bytes while writing, before allocating an oversized string.
class SimulationSnapshotBuffer final : public std::streambuf
{
public:
  std::string bytes;

protected:
  std::streamsize xsputn(const char* data, std::streamsize count) override
  {
    if (count < 0 ||
        static_cast<std::uint64_t>(count) > Maximum - bytes.size()) {
      return 0;
    }
    bytes.append(data, static_cast<std::size_t>(count));
    return count;
  }
  int_type overflow(int_type value) override
  {
    if (traits_type::eq_int_type(value, traits_type::eof())) {
      return traits_type::not_eof(value);
    }
    if (bytes.size() == Maximum) {
      return traits_type::eof();
    }
    bytes.push_back(traits_type::to_char_type(value));
    return value;
  }

private:
  static constexpr std::size_t Maximum = 64u * 1024u * 1024u - 512u;
};

void
SimulationRequest::write(GuestWireWriter& output) const
{
  output.u32(Magic);
  output.u32(2);
  output.u32(synchronize ? 1 : 0);
  output.u64(session);
  output.u64(worldEpoch);
  output.u64(ruleEpoch);
  output.u64(operation);
  output.u64(generation);
  output.u64(revision);
  output.text(families);
  output.text(rules);
  output.text(snapshot);
}

bool
SimulationRequest::read(std::span<const std::byte> bytes,
                        SimulationRequest& output)
{
  if (bytes.size() > 64u * 1024u * 1024u) {
    return false;
  }
  GuestWireReader reader(bytes);
  const std::uint32_t magic = reader.u32();
  const std::uint32_t version = reader.u32();
  const std::uint32_t synchronize = reader.u32();
  SimulationRequest request;
  request.synchronize = synchronize != 0;
  request.session = reader.u64();
  request.worldEpoch = reader.u64();
  request.ruleEpoch = reader.u64();
  request.operation = reader.u64();
  request.generation = reader.u64();
  request.revision = reader.u64();
  request.families = reader.text(4u * 1024u * 1024u);
  request.rules = reader.text(4u * 1024u * 1024u);
  request.snapshot = reader.text(64u * 1024u * 1024u);
  if (!reader.finished() || magic != Magic || version != 2 || synchronize > 1 ||
      request.session == 0 || request.worldEpoch == 0 ||
      request.ruleEpoch == 0 || request.operation == 0 ||
      request.generation == UINT64_MAX || request.revision == UINT64_MAX ||
      (request.synchronize &&
       (request.families.empty() || request.rules.empty() ||
        request.snapshot.empty())) ||
      (!request.synchronize &&
       (!request.families.empty() || !request.rules.empty() ||
        !request.snapshot.empty()))) {
    return false;
  }
  output = std::move(request);
  return true;
}

void
SimulationReply::write(GuestWireWriter& output) const
{
  output.u32(SimulationRequest::Magic);
  output.u32(2);
  output.u32(static_cast<std::uint32_t>(content));
  output.u64(session);
  output.u64(worldEpoch);
  output.u64(ruleEpoch);
  output.u64(operation);
  output.u64(baseGeneration);
  output.u64(baseRevision);
  output.u64(generation);
  output.u64(revision);
  output.text(snapshot);
  output.u32(static_cast<std::uint32_t>(chunks.size()));
  for (const SparseChangedChunkRecord& chunk : chunks) {
    output.u64(std::bit_cast<std::uint64_t>(chunk.address.x));
    output.u64(std::bit_cast<std::uint64_t>(chunk.address.y));
    output.u32(chunk.present ? 1 : 0);
    if (chunk.present) {
      output.bytes(std::as_bytes(std::span(chunk.cells)));
    }
  }
}

bool
SimulationReply::read(std::span<const std::byte> bytes,
                      const SimulationRequest& expected,
                      SimulationReply& output)
{
  if (bytes.size() > 64u * 1024u * 1024u) {
    return false;
  }
  GuestWireReader reader(bytes);
  const std::uint32_t magic = reader.u32();
  const std::uint32_t version = reader.u32();
  const std::uint32_t kind = reader.u32();
  SimulationReply reply;
  reply.changed = kind != 0;
  reply.content = static_cast<Content>(kind);
  reply.session = reader.u64();
  reply.worldEpoch = reader.u64();
  reply.ruleEpoch = reader.u64();
  reply.operation = reader.u64();
  reply.baseGeneration = reader.u64();
  reply.baseRevision = reader.u64();
  reply.generation = reader.u64();
  reply.revision = reader.u64();
  reply.snapshot = reader.text(64u * 1024u * 1024u);
  const std::uint32_t count = reader.u32();
  if (!reader.valid() ||
      count >= SparseCellGrid::kLightweightReplacementChunkLimit ||
      count > reader.remaining() / 20) {
    return false;
  }
  std::unordered_set<ChunkAddress, ChunkAddressHash> addresses;
  for (std::uint32_t index = 0; index < count; ++index) {
    SparseChangedChunkRecord chunk;
    chunk.address.x = std::bit_cast<std::int64_t>(reader.u64());
    chunk.address.y = std::bit_cast<std::int64_t>(reader.u64());
    const std::uint32_t present = reader.u32();
    if (present > 1 || !addresses.insert(chunk.address).second) {
      return false;
    }
    chunk.present = present != 0;
    chunk.cells.fill(SparseCellGrid::BackgroundState);
    if (chunk.present) {
      const std::span<const std::byte> cells = reader.bytes(chunk.cells.size());
      if (!reader.valid()) {
        return false;
      }
      std::memcpy(chunk.cells.data(), cells.data(), cells.size());
    }
    reply.chunks.push_back(chunk);
  }
  if (!reader.finished() || magic != SimulationRequest::Magic || version != 2 ||
      kind > 2 || expected.generation == UINT64_MAX ||
      expected.revision == UINT64_MAX || reply.session != expected.session ||
      reply.worldEpoch != expected.worldEpoch ||
      reply.ruleEpoch != expected.ruleEpoch ||
      reply.operation != expected.operation ||
      reply.baseGeneration != expected.generation ||
      reply.baseRevision != expected.revision ||
      reply.generation != expected.generation + 1 ||
      reply.revision != expected.revision + (reply.changed ? 1 : 0) ||
      (reply.content == Content::Unchanged &&
       (!reply.snapshot.empty() || count != 0)) ||
      (reply.content == Content::Chunks &&
       (!reply.snapshot.empty() || count == 0)) ||
      (reply.content == Content::Snapshot &&
       (reply.snapshot.empty() || count != 0))) {
    return false;
  }
  output = std::move(reply);
  return true;
}

static bool
setSnapshotRevision(SparseCellGrid& grid, std::uint64_t revision)
{
  // The grid is private and freshly decoded. No incoming cache metadata is
  // trusted: the codec/assignChunk computed all masks and aggregate counts.
  SparseGenerationDelta revisionOnly;
  revisionOnly.fromRevision = grid.getRevision();
  revisionOnly.toRevision = revision;
  return grid.applyGenerationDelta(revisionOnly);
}

std::unique_ptr<SparseCellGrid>
SimulationReply::restore(const SparseCellGrid& base,
                         const RuleSet& rule,
                         std::string* error) const
try {
  if (!changed || base.getRevision() != baseRevision ||
      baseRevision == UINT64_MAX || revision != baseRevision + 1) {
    if (error) {
      *error = "Simulation result does not match the published base";
    }
    return {};
  }
  if (content == Content::Snapshot) {
    std::istringstream stream(snapshot, std::ios::binary);
    IllumoDocument document;
    if (!IllumoCodec::readStream(stream, &document, error) || !document.grid ||
        document.version != 4 ||
        stream.peek() != std::char_traits<char>::eof() ||
        document.familyString != rule.getFamilyTag() ||
        document.ruleString != rule.getRuleTag() ||
        document.worldChunkWidth != base.getWorldChunkWidth() ||
        document.worldChunkHeight != base.getWorldChunkHeight()) {
      return {};
    }
    const std::vector<SparseChunkRecord> previous = base.collectChunkRecords();
    const std::vector<SparseChunkRecord> next =
      document.grid->collectChunkRecords();
    for (const SparseChunkRecord& chunk : next) {
      if (chunk.chunkX < INT64_MIN / 16 || chunk.chunkX > INT64_MAX / 16 ||
          chunk.chunkY < INT64_MIN / 16 || chunk.chunkY > INT64_MAX / 16) {
        return {};
      }
      for (unsigned char state : chunk.cells) {
        if (!rule.isValidState(state)) {
          return {};
        }
      }
    }
    bool different = previous.size() != next.size();
    for (std::size_t index = 0; !different && index < previous.size();
         ++index) {
      different = previous[index].chunkX != next[index].chunkX ||
                  previous[index].chunkY != next[index].chunkY ||
                  previous[index].cells != next[index].cells;
    }
    if (!different || !setSnapshotRevision(*document.grid, revision)) {
      return {};
    }
    return std::move(document.grid);
  }
  if (content != Content::Chunks || chunks.empty() || !snapshot.empty()) {
    return {};
  }
  SparseGenerationDelta delta;
  delta.fromRevision = baseRevision;
  delta.toRevision = revision;
  std::unordered_set<ChunkAddress, ChunkAddressHash> addresses;
  for (const SparseChangedChunkRecord& incoming : chunks) {
    if (!addresses.insert(incoming.address).second ||
        !(base.canonicalizeChunk(incoming.address) == incoming.address) ||
        incoming.address.x < std::numeric_limits<std::int64_t>::min() / 16 ||
        incoming.address.x > std::numeric_limits<std::int64_t>::max() / 16 ||
        incoming.address.y < std::numeric_limits<std::int64_t>::min() / 16 ||
        incoming.address.y > std::numeric_limits<std::int64_t>::max() / 16) {
      return {};
    }
    SparseChangedChunkRecord record;
    record.address = incoming.address;
    record.present = incoming.present;
    record.cells.fill(SparseCellGrid::BackgroundState);
    if (record.present) {
      record.cells = incoming.cells;
    }
    bool different = false;
    for (std::size_t cell = 0; cell < record.cells.size(); ++cell) {
      const unsigned char value = record.cells[cell];
      if (!rule.isValidState(value)) {
        return {};
      }
      const unsigned char old = base.getCell(
        { record.address.x * 16 + static_cast<std::int64_t>(cell % 16),
          record.address.y * 16 + static_cast<std::int64_t>(cell / 16) });
      const std::size_t word = cell / 64;
      const std::uint64_t bit = UINT64_C(1) << (cell % 64);
      if (value != SparseCellGrid::BackgroundState) {
        record.occupied[word] |= bit;
        ++record.occupiedCellCount;
      }
      if (value == SparseCellGrid::CountedNeighborState) {
        record.counted[word] |= bit;
        ++record.countedCellCount;
      }
      if (value != old) {
        record.stateChanged[word] |= bit;
        different = true;
      }
      if ((value == 0) != (old == 0)) {
        record.countedChanged[word] |= bit;
      }
    }
    if (!different || (record.present && record.occupiedCellCount == 0)) {
      return {};
    }
    delta.changedChunks.push_back(record);
  }
  std::unique_ptr<SparseCellGrid> replacement =
    std::make_unique<SparseCellGrid>();
  replacement->copyStateFrom(base);
  if (!replacement->applyGenerationDelta(delta)) {
    return {};
  }
  return replacement;
} catch (const std::exception& exception) {
  if (error) {
    *error = exception.what();
  }
  return {};
}

SimulationGuestWorker::SimulationGuestWorker() = default;
SimulationGuestWorker::~SimulationGuestWorker() = default;

bool
SimulationGuestWorker::execute(std::span<const std::byte> bytes,
                               std::vector<std::byte>& output,
                               std::string& error)
try {
  output.clear();
  error.clear();
  SimulationRequest request;
  if (!SimulationRequest::read(bytes, request) ||
      (m_session != 0 &&
       (request.session != m_session || request.operation <= m_operation ||
        request.worldEpoch < m_worldEpoch || request.ruleEpoch < m_ruleEpoch ||
        (request.synchronize && request.worldEpoch == m_worldEpoch &&
         request.ruleEpoch == m_ruleEpoch)))) {
    error = "Invalid or replayed simulation request";
    return false;
  }
  if (request.synchronize) {
    RuleSetRegistry registry;
    if (!registry.loadFromCatalogTexts(request.families, request.rules)) {
      error = "Invalid worker rule catalogs";
      return false;
    }
    RuleSetRegistry::instance() = std::move(registry);
    std::istringstream stream(request.snapshot, std::ios::binary);
    IllumoDocument document;
    if (!IllumoCodec::readStream(stream, &document, &error) || !document.grid ||
        document.version != 4 ||
        stream.peek() != std::char_traits<char>::eof() ||
        !setSnapshotRevision(*document.grid, request.revision)) {
      return false;
    }
    std::unique_ptr<RuleSet> rule =
      RuleSetRegistry::instance().createRuleSet(document.ruleString);
    if (!rule) {
      error = "Missing worker ruleset";
      return false;
    }
    for (const SparseChunkRecord& chunk :
         document.grid->collectChunkRecords()) {
      if (chunk.chunkX < INT64_MIN / 16 || chunk.chunkX > INT64_MAX / 16 ||
          chunk.chunkY < INT64_MIN / 16 || chunk.chunkY > INT64_MAX / 16) {
        error = "Snapshot chunk outside cell-coordinate range";
        return false;
      }
      for (unsigned char state : chunk.cells) {
        if (!rule->isValidState(state)) {
          error = "Snapshot state outside active rule";
          return false;
        }
      }
    }
    m_rule = std::move(rule);
    m_grid = std::move(document.grid);
    m_session = request.session;
    m_worldEpoch = request.worldEpoch;
    m_ruleEpoch = request.ruleEpoch;
    m_generation = request.generation;
  } else if (!m_grid || request.session != m_session ||
             request.worldEpoch != m_worldEpoch ||
             request.ruleEpoch != m_ruleEpoch ||
             request.generation != m_generation ||
             request.revision != m_grid->getRevision()) {
    error = "Worker requires a full state resynchronization";
    return false;
  }
  m_operation = request.operation;
  if (!m_grid->advance(*m_rule)) {
    error = "Simulation generation failed";
    return false;
  }
  ++m_generation;
  SimulationReply reply;
  reply.session = m_session;
  reply.worldEpoch = m_worldEpoch;
  reply.ruleEpoch = m_ruleEpoch;
  reply.operation = m_operation;
  reply.baseGeneration = request.generation;
  reply.baseRevision = request.revision;
  reply.generation = m_generation;
  reply.revision = m_grid->getRevision();
  reply.changed = reply.revision != request.revision;
  if (reply.changed) {
    SparseGenerationDelta delta;
    if (!m_grid->captureGenerationDelta(request.revision, &delta, false)) {
      error = "Worker failed to capture generation changes";
      return false;
    }
    if (!delta.fullReplacement) {
      reply.content = SimulationReply::Content::Chunks;
      reply.chunks = std::move(delta.changedChunks);
    } else {
      reply.content = SimulationReply::Content::Snapshot;
      IllumoDocument document;
      document.sourceGrid = m_grid.get();
      document.familyString = m_rule->getFamilyTag();
      document.ruleString = m_rule->getRuleTag();
      document.worldChunkWidth = m_grid->getWorldChunkWidth();
      document.worldChunkHeight = m_grid->getWorldChunkHeight();
      SimulationSnapshotBuffer buffer;
      std::ostream stream(&buffer);
      if (!IllumoCodec::writeStream(stream, document, &error)) {
        return false;
      }
      reply.snapshot = std::move(buffer.bytes);
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
