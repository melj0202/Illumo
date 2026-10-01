#pragma once

#include "Game/IllumoCodec.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/RuleSetRegistry.h"
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

// Compiled twice: native reference and the actual wasm32 guest use exactly the
// same inputs. Production evaluators and containers come from their own build.
class DomainFixture
{
public:
  DomainFixture() = default;
  ~DomainFixture() = default;
  DomainFixture(const DomainFixture&) = delete;
  DomainFixture& operator=(const DomainFixture&) = delete;
  DomainFixture(DomainFixture&&) = delete;
  DomainFixture& operator=(DomainFixture&&) = delete;
  bool initialize(const std::string& families, const std::string& rules)
  {
    if (!m_registry.loadFromCatalogTexts(families, rules)) {
      return false;
    }
    RuleSetRegistry::instance() = m_registry;
    return true;
  }
  int ruleCount() const
  {
    return static_cast<int>(m_registry.getKnownRules().size());
  }
  bool select(int index, int topology, int workload)
  {
    const std::vector<std::string> ids = m_registry.getKnownRules();
    if (index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
      return false;
    }
    m_rule = m_registry.createRuleSet(ids[static_cast<std::size_t>(index)]);
    m_topology = topology;
    m_grid = std::make_unique<SparseCellGrid>(topology == 0 ? 0 : 4,
                                              topology == 0 ? 0 : 4);
    if (!m_rule || workload == 0) {
      return m_rule != nullptr;
    }
    for (int y = -24; y < 24; ++y) {
      for (int x = -24; x < 24; ++x) {
        const std::uint32_t seed = static_cast<std::uint32_t>(x) * 1664525u ^
                                   static_cast<std::uint32_t>(y) * 1013904223u;
        if (workload == 1 && seed % 7u != 0u) {
          continue;
        }
        const unsigned char state =
          static_cast<unsigned char>(seed % m_rule->getStateCount());
        m_grid->setCell({ x, y }, state);
      }
    }
    return true;
  }
  bool advance() { return m_grid && m_rule && m_grid->advance(*m_rule); }
  const SparseCellGrid& grid() const { return *m_grid; }
  const RuleSet& rule() const { return *m_rule; }
  void publish(std::unique_ptr<SparseCellGrid> grid)
  {
    m_grid = std::move(grid);
  }
  std::string save() const
  {
    IllumoDocument document;
    document.sourceGrid = m_grid.get();
    document.ruleString = m_rule->getRuleTag();
    document.familyString = m_rule->getFamilyTag();
    document.worldChunkWidth = m_topology == 0 ? 0 : 4;
    document.worldChunkHeight = document.worldChunkWidth;
    document.cameraX = -123.25;
    document.cameraY = 456.5;
    document.cameraZoom = 2.0;
    std::ostringstream stream(std::ios::binary);
    return IllumoCodec::writeStream(stream, document) ? stream.str()
                                                      : std::string();
  }
  bool restore(const std::string& bytes)
  {
    std::istringstream stream(bytes, std::ios::binary);
    IllumoDocument document;
    if (!IllumoCodec::readStream(stream, &document)) {
      return false;
    }
    m_grid = std::move(document.grid);
    return true;
  }
  std::uint64_t hash() const
  {
    std::uint64_t result = 14695981039346656037ULL;
    const std::vector<SparseChunkRecord> chunks = m_grid->collectChunkRecords();
    for (const SparseChunkRecord& chunk : chunks) {
      for (const std::int64_t coordinate : { chunk.chunkX, chunk.chunkY }) {
        const std::uint64_t bits = static_cast<std::uint64_t>(coordinate);
        for (unsigned int index = 0; index < 8u; ++index) {
          result =
            (result ^ ((bits >> (index * 8u)) & 255u)) * 1099511628211ULL;
        }
      }
      for (unsigned char cell : chunk.cells) {
        result = (result ^ cell) * 1099511628211ULL;
      }
    }
    return result;
  }

private:
  RuleSetRegistry m_registry;
  int m_topology = 0;
  std::unique_ptr<RuleSet> m_rule;
  std::unique_ptr<SparseCellGrid> m_grid;
};
