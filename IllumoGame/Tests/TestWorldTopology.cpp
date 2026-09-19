#include "Game/CanvasView.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/LifeLikeRuleSet.h"
#include "Rulesets/WireworldRuleSet.h"
#include "TestHarness.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdint>
#include <vector>

static TestCounters g;

static bool
sameRecords(const std::vector<SparseChunkRecord>& left,
            const std::vector<SparseChunkRecord>& right)
{
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < left.size(); ++index) {
    if (left[index].chunkX != right[index].chunkX ||
        left[index].chunkY != right[index].chunkY ||
        left[index].cells != right[index].cells) {
      return false;
    }
  }
  return true;
}

static void
testTopologyValidationAndAliases()
{
  testSection("SparseCellGrid: topology validation and canonical aliases");
  testTrue(g,
           SparseCellGrid::isValidTopology(0, 0),
           "zero by zero selects infinite topology");
  testTrue(g,
           SparseCellGrid::isValidTopology(2, 3),
           "positive chunk dimensions select a finite torus");
  testTrue(g,
           !SparseCellGrid::isValidTopology(0, 3),
           "mixed infinite and finite dimensions are rejected");
  testTrue(g,
           !SparseCellGrid::isValidTopology(-1, -1),
           "negative dimensions are rejected");
  testTrue(g,
           !SparseCellGrid::isValidTopology(
             SparseCellGrid::kMaximumWorldChunksPerAxis + 1, 1),
           "oversized dimensions are rejected");

  SparseCellGrid torus(2, 2);
  testTrue(g, torus.isToroidal(), "positive topology reports toroidal mode");
  torus.setCell(CellAddress{ 16, 0 }, 0);
  testEqUChar(g,
              torus.getCell(CellAddress{ -16, 0 }),
              0,
              "positive edge write wraps to the negative edge");
  testEqUChar(g,
              torus.getCell(CellAddress{ 48, 0 }),
              0,
              "multiple periods read the same canonical cell");
  torus.setCell(CellAddress{ -17, 0 }, 2);
  testEqUChar(g,
              torus.getCell(CellAddress{ 15, 0 }),
              2,
              "negative edge write wraps to the positive edge");
  testEqSize(g,
             torus.getAllocatedChunkCount(),
             2u,
             "aliases allocate only canonical chunks");
  testTrue(g,
           torus.isCellInWorldBounds(CellAddress{ -16, -16 }) &&
             torus.isCellInWorldBounds(CellAddress{ 15, 15 }),
           "centered finite bounds include both canonical edges");
  testTrue(g,
           !torus.isCellInWorldBounds(CellAddress{ -17, 0 }) &&
             !torus.isCellInWorldBounds(CellAddress{ 16, 0 }),
           "finite bounds exclude wrapped aliases used by simulation");

  SparseCellGrid infinite;
  infinite.setCell(CellAddress{ 16, 0 }, 0);
  testEqUChar(g,
              infinite.getCell(CellAddress{ -16, 0 }),
              SparseCellGrid::BackgroundState,
              "infinite topology does not wrap");

  SparseCellGrid oneChunkTorus(1, 1);
  oneChunkTorus.setCell(CellAddress{ 0, 0 }, 0);
  int aliasVisits = 0;
  oneChunkTorus.visitChunksInBounds(
    ChunkAddress{ 1, 0 },
    ChunkAddress{ 1, 0 },
    [&aliasVisits](const ChunkAddress& address,
                   const SparseCellGrid::ChunkCells& cells) {
      if (address.x == 1 && address.y == 0 && cells[0] == 0) {
        aliasVisits += 1;
      }
    });
  testEqInt(g, aliasVisits, 0, "bounded visits leave torus aliases blank");
  int canonicalVisits = 0;
  oneChunkTorus.visitChunksInBounds(
    ChunkAddress{ 0, 0 },
    ChunkAddress{ 0, 0 },
    [&canonicalVisits](const ChunkAddress& address,
                       const SparseCellGrid::ChunkCells& cells) {
      if (address.x == 0 && address.y == 0 && cells[0] == 0) {
        canonicalVisits += 1;
      }
    });
  testEqInt(g,
            canonicalVisits,
            1,
            "bounded visits retain the canonical finite-world chunk");
}

static unsigned char
displayRedAt(const CanvasView& view, const CellAddress& address)
{
  const CellAddress first = view.getCacheFirstCell();
  const int cellsPerTexel = view.getCellsPerTexel();
  const int x = static_cast<int>((address.x - first.x) /
                                 static_cast<std::int64_t>(cellsPerTexel));
  const int y = static_cast<int>((first.y - address.y) /
                                 static_cast<std::int64_t>(cellsPerTexel));
  if (x < 0 || x >= view.getTextureWidth() || y < 0 ||
      y >= view.getTextureHeight()) {
    return 127;
  }
  const std::size_t index =
    static_cast<std::size_t>(y * view.getTextureWidth() + x) * 3u;
  return view.getDisplayTexBuffer()[index];
}

static void
testFinitePresentationLeavesAliasesBlank()
{
  testSection("CanvasView: finite world bounds remain visually blank");
  NullRenderWindow window(64, 64);
  EnvVars env;
  env.setVar("WinX", 64);
  env.setVar("WinY", 64);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  camera.SetPositionPrecise(16.0 * SparseCellGrid::kChunkDim, 0.0);

  SparseCellGrid grid(1, 1);
  grid.setCell(CellAddress{ 0, 0 }, SparseCellGrid::CountedNeighborState);
  CanvasView view(4, 4, &grid, &window, &camera, nullptr);
  view.setFadeSpeed(0.0f);
  view.rebuildTargetsFromGrid();

  testEqUChar(g,
              view.getCanvasPixel(16, 0),
              SparseCellGrid::BackgroundState,
              "view-facing reads treat out-of-bounds aliases as empty");
  testTrue(g,
           !view.setCanvasPixel(16, 0, SparseCellGrid::BackgroundState),
           "view-facing edits reject out-of-bounds aliases");
  testEqUChar(g,
              displayRedAt(view, CellAddress{ 0, 0 }),
              0,
              "canonical finite-world cell remains visible");
  testEqUChar(g,
              displayRedAt(view, CellAddress{ 16, 0 }),
              255,
              "out-of-bounds wrapped alias remains background colored");
}

static void
testGameOfLifeAcrossToroidalSeams()
{
  testSection("SparseCellGrid: Game of Life across toroidal seams");
  LifeLikeRuleSet rules("GAME_OF_LIFE", 1u << 3, (1u << 2) | (1u << 3));

  SparseCellGrid horizontal(2, 2);
  horizontal.setCell(CellAddress{ 15, 0 }, 0);
  horizontal.setCell(CellAddress{ -16, 0 }, 0);
  horizontal.setCell(CellAddress{ -15, 0 }, 0);
  testTrue(g, horizontal.advance(rules), "horizontal seam advances");
  testEqUChar(g,
              horizontal.getCell(CellAddress{ -16, -1 }),
              0,
              "birth appears above the wrapped center");
  testEqUChar(g,
              horizontal.getCell(CellAddress{ 16, 0 }),
              0,
              "wrapped center survives through its positive alias");
  testEqUChar(g,
              horizontal.getCell(CellAddress{ -16, 1 }),
              0,
              "birth appears below the wrapped center");

  SparseCellGrid vertical(2, 2);
  vertical.setCell(CellAddress{ 0, 15 }, 0);
  vertical.setCell(CellAddress{ 0, -16 }, 0);
  vertical.setCell(CellAddress{ 0, -15 }, 0);
  testTrue(g, vertical.advance(rules), "vertical seam advances");
  testEqUChar(g,
              vertical.getCell(CellAddress{ -1, -16 }),
              0,
              "birth appears left of the wrapped center");
  testEqUChar(g,
              vertical.getCell(CellAddress{ 1, -16 }),
              0,
              "birth appears right of the wrapped center");

  SparseCellGrid corners(2, 2);
  corners.setCell(CellAddress{ 15, 15 }, 0);
  corners.setCell(CellAddress{ -16, 15 }, 0);
  corners.setCell(CellAddress{ 15, -16 }, 0);
  corners.setCell(CellAddress{ -16, -16 }, 0);
  const std::uint64_t before = corners.getRevision();
  testTrue(g, corners.advance(rules), "corner-spanning block advances");
  testTrue(g,
           corners.getRevision() == before,
           "corner-spanning block is a toroidal still life");
}

static void
testWireworldAndDirectPublicationAcrossSeam()
{
  testSection("SparseCellGrid: multi-state and dual-grid toroidal stepping");
  WireworldRuleSet wireworld{};
  SparseCellGrid wire(2, 2);
  wire.setCell(CellAddress{ 15, 0 }, WireworldRuleSet::CELL_HEAD);
  wire.setCell(CellAddress{ -16, 0 }, WireworldRuleSet::CELL_CONDUCTOR);
  testTrue(g, wire.advance(wireworld), "Wireworld seam advances");
  testEqUChar(g,
              wire.getCell(CellAddress{ -16, 0 }),
              WireworldRuleSet::CELL_HEAD,
              "wrapped conductor sees the head across the seam");
  testEqUChar(g,
              wire.getCell(CellAddress{ 15, 0 }),
              WireworldRuleSet::CELL_TAIL,
              "wrapped head advances to tail");

  LifeLikeRuleSet life("GAME_OF_LIFE", 1u << 3, (1u << 2) | (1u << 3));
  SparseCellGrid source(2, 2);
  source.setCell(CellAddress{ 15, 0 }, 0);
  source.setCell(CellAddress{ -16, 0 }, 0);
  source.setCell(CellAddress{ -15, 0 }, 0);
  SparseCellGrid expected(2, 2);
  expected.copyStateFrom(source);
  SparseCellGrid spare(2, 2);
  testTrue(g, expected.advance(life), "in-place toroidal reference advances");
  testTrue(g,
           spare.advanceFrom(source, life),
           "dual-grid toroidal generation advances");
  testTrue(
    g,
    sameRecords(expected.collectChunkRecords(), spare.collectChunkRecords()),
    "dual-grid toroidal output matches in-place output");

  SparseCellGrid wrongTopology(3, 2);
  testTrue(g,
           !wrongTopology.advanceFrom(source, life),
           "dual-grid stepping rejects mismatched topology");
}

static void
testFiniteSparseMicroBench()
{
  testSection("SparseCellGrid: finite versus infinite sparse micro-benchmark");
  LifeLikeRuleSet rules("GAME_OF_LIFE", 1u << 3, (1u << 2) | (1u << 3));
  SparseCellGrid finite(64, 64);
  SparseCellGrid infinite;
  for (int blockY = 0; blockY < 8; ++blockY) {
    for (int blockX = 0; blockX < 32; ++blockX) {
      const std::int64_t x = static_cast<std::int64_t>(blockX * 4);
      const std::int64_t y = static_cast<std::int64_t>(blockY * 4);
      for (int offsetY = 0; offsetY < 2; ++offsetY) {
        for (int offsetX = 0; offsetX < 2; ++offsetX) {
          const CellAddress cell{ x + offsetX, y + offsetY };
          finite.setCell(cell, 0);
          infinite.setCell(cell, 0);
        }
      }
    }
  }

  const int generations = 100;
  const std::chrono::steady_clock::time_point infiniteStart =
    std::chrono::steady_clock::now();
  bool infiniteAdvanced = true;
  for (int generation = 0; generation < generations; ++generation) {
    infiniteAdvanced = infiniteAdvanced && infinite.advance(rules);
  }
  const double infiniteMilliseconds =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                              infiniteStart)
      .count();

  const std::chrono::steady_clock::time_point finiteStart =
    std::chrono::steady_clock::now();
  bool finiteAdvanced = true;
  for (int generation = 0; generation < generations; ++generation) {
    finiteAdvanced = finiteAdvanced && finite.advance(rules);
  }
  const double finiteMilliseconds =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                              finiteStart)
      .count();

  std::printf(
    "finite sparse: %.3f ms; infinite sparse: %.3f ms (%d generations)\n",
    finiteMilliseconds,
    infiniteMilliseconds,
    generations);
  testTrue(g,
           finiteAdvanced && infiniteAdvanced,
           "both sparse topology paths complete the benchmark");
  testTrue(
    g,
    sameRecords(finite.collectChunkRecords(), infinite.collectChunkRecords()),
    "finite and infinite paths stay identical away from seams");
}

static int
runWorldTopologyCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerWorldTopologyTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.WorldTopology.ValidationAndAliases", []() {
    return runWorldTopologyCase(testTopologyValidationAndAliases);
  });
  registry.add("IllumoGame.WorldTopology.GameOfLifeSeams", []() {
    return runWorldTopologyCase(testGameOfLifeAcrossToroidalSeams);
  });
  registry.add("IllumoGame.WorldTopology.WireworldAndDualGrid", []() {
    return runWorldTopologyCase(testWireworldAndDirectPublicationAcrossSeam);
  });
  registry.add("IllumoGame.WorldTopology.BoundedPresentation", []() {
    return runWorldTopologyCase(testFinitePresentationLeavesAliasesBlank);
  });
  registry.add("IllumoGame.WorldTopology.MicroBench", []() {
    return runWorldTopologyCase(testFiniteSparseMicroBench);
  });
}
