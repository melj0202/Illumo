#include "DenseRuleEvaluator.h"
// Domain boundary tests: CellGrid + RuleSet without Renderer/window/camera.

#include "Game/Canvas.h"
#include "Game/CellGrid.h"
#include "Rulesets/LifeLikeRuleSet.h"
#include "Rulesets/WireworldRuleSet.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>

static TestCounters g;

static void
testCellGridDomainWithoutRenderer()
{
  testSection("CellGrid: construct/set/get without render services");
  CellGrid grid(8, 6);
  testEqInt(g, grid.getWidth(), 8, "width");
  testEqInt(g, grid.getHeight(), 6, "height");
  testTrue(g, grid.lifeCanvas != nullptr, "life buffer allocated");
  testEqUChar(g, grid.getCanvasPixel(0, 0), 1, "default cell is dead");

  testTrue(g, grid.setCanvasPixel(2, 3, 0), "set alive");
  testEqUChar(g, grid.getCanvasPixel(2, 3), 0, "alive readback");
  testTrue(g, grid.isCellsDirty(), "write marks dirty");

  grid.clearCells();
  testEqUChar(g, grid.getCanvasPixel(2, 3), 1, "clear returns dead");
  testTrue(g, grid.isCellsDirty(), "clear marks dirty");
}

static void
testGameOfLifeGenerationOnCellGrid()
{
  testSection("CellGrid + GameOfLife: blinker generation without renderer");
  // 5x5 grid with a vertical blinker at column 2, rows 1-3.
  CellGrid grid(5, 5);
  grid.clearCells();
  grid.setCanvasPixel(2, 1, 0);
  grid.setCanvasPixel(2, 2, 0);
  grid.setCanvasPixel(2, 3, 0);

  LifeLikeRuleSet rules("GAME_OF_LIFE", 1u << 3, (1u << 2) | (1u << 3));
  DenseRuleEvaluator rulesDense(&grid, rules);
  rulesDense.calcGeneration(0, 0, 5, 5);

  // After one step the blinker is horizontal through row 2.
  testEqUChar(g, grid.getCanvasPixel(1, 2), 0, "blinker left alive");
  testEqUChar(g, grid.getCanvasPixel(2, 2), 0, "blinker center alive");
  testEqUChar(g, grid.getCanvasPixel(3, 2), 0, "blinker right alive");
  testEqUChar(g, grid.getCanvasPixel(2, 1), 1, "old top dead");
  testEqUChar(g, grid.getCanvasPixel(2, 3), 1, "old bottom dead");

  rulesDense.calcGeneration(0, 0, 5, 5);
  testEqUChar(g, grid.getCanvasPixel(2, 1), 0, "blinker returns vertical top");
  testEqUChar(g, grid.getCanvasPixel(2, 2), 0, "blinker vertical mid");
  testEqUChar(g, grid.getCanvasPixel(2, 3), 0, "blinker vertical bottom");
  testEqUChar(g, grid.getCanvasPixel(1, 2), 1, "horizontal left cleared");
  testEqUChar(g, grid.getCanvasPixel(3, 2), 1, "horizontal right cleared");
}

static void
testWireworldElectronOnCellGrid()
{
  testSection("CellGrid + Wireworld: electron advances without renderer");
  CellGrid grid(8, 3);
  grid.clearCells();
  const int y = 1;
  // Straight wire: H C C C C C — head advances one cell per generation.
  for (int x = 0; x < 6; ++x) {
    grid.setCanvasPixel(x, y, WireworldRuleSet::CELL_CONDUCTOR);
  }
  grid.setCanvasPixel(0, y, WireworldRuleSet::CELL_HEAD);

  WireworldRuleSet rules{};
  DenseRuleEvaluator rulesDense(&grid, rules);
  rulesDense.calcGeneration(0, 0, 8, 3);

  testEqUChar(g,
              grid.getCanvasPixel(0, y),
              WireworldRuleSet::CELL_TAIL,
              "head becomes tail");
  testEqUChar(g,
              grid.getCanvasPixel(1, y),
              WireworldRuleSet::CELL_HEAD,
              "conductor with one head neighbor becomes head");
  testEqUChar(g,
              grid.getCanvasPixel(2, y),
              WireworldRuleSet::CELL_CONDUCTOR,
              "far conductor stays conductor");
}

static void
testCanvasDomainOnlyConstruction()
{
  testSection("Canvas: domain-only construction (null render services)");
  // Shipped Canvas type with no window/camera/renderer — presentation enroll
  // is skipped; domain ops still work.
  Canvas canvas(4, 4, nullptr, nullptr, nullptr);
  testEqInt(g, canvas.canvasWidth, 4, "canvas width");
  testEqInt(g, canvas.canvasHeight, 4, "canvas height");
  testTrue(g, canvas.setCanvasPixel(1, 1, 0), "domain write");
  testEqUChar(g, canvas.getCanvasPixel(1, 1), 0, "domain read");

  LifeLikeRuleSet rules("GAME_OF_LIFE", 1u << 3, (1u << 2) | (1u << 3));
  DenseRuleEvaluator rulesDense(&canvas, rules);
  // Still life block.
  canvas.setCanvasPixel(1, 1, 0);
  canvas.setCanvasPixel(1, 2, 0);
  canvas.setCanvasPixel(2, 1, 0);
  canvas.setCanvasPixel(2, 2, 0);
  rulesDense.calcGeneration(0, 0, 4, 4);
  testEqUChar(g, canvas.getCanvasPixel(1, 1), 0, "block stable");
  testEqUChar(g, canvas.getCanvasPixel(2, 2), 0, "block stable corner");
}

static int
runDomainBoundaryCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerDomainBoundaryTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Domain.CellGridWithoutRenderer", []() {
    return runDomainBoundaryCase(testCellGridDomainWithoutRenderer);
  });
  registry.add("IllumoGame.Domain.GameOfLifeOnCellGrid", []() {
    return runDomainBoundaryCase(testGameOfLifeGenerationOnCellGrid);
  });
  registry.add("IllumoGame.Domain.WireworldOnCellGrid", []() {
    return runDomainBoundaryCase(testWireworldElectronOnCellGrid);
  });
  registry.add("IllumoGame.Domain.CanvasWithoutRenderServices", []() {
    return runDomainBoundaryCase(testCanvasDomainOnlyConstruction);
  });
}
