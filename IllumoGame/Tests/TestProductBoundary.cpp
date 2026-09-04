#include "Game/Canvas.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/LifeLikeRuleSet.h"
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static void
testRenderSceneCanvasTokens()
{
  testSection("Renderer: IllumoGame canvas token stream");
  HeadlessRenderFixture fixture(1280, 720);
  fixture.env.setVar("CanvasX", 16);
  fixture.env.setVar("CanvasY", 12);
  Scene scene(&fixture.window, &fixture.camera);
  Canvas canvas(16, 12, &fixture.window, &fixture.camera, &fixture.renderer);
  scene.AddDrawable(&canvas);

  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::UpdateTexture) >= 1u,
           "initial canvas enroll uploads");

  canvas.setCanvasPixel(1, 1, 0);
  canvas.rebuildTargetsFromLife();
  canvas.setFadeSpeed(0.0f);
  canvas.setTargetColor(1 * canvas.canvasWidth + 1, 0, 0, 0);
  canvas.snapVisualToTargets();

  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();

  testEqSize(g,
             fixture.mock.countNonEmptyOfType(CommandType::UpdateTexture),
             1u,
             "dirty canvas emits one texture update");
  testEqSize(g,
             fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed),
             1u,
             "canvas emits one indexed draw");
  testEqSize(g,
             fixture.mock.countNonEmptyOfType(CommandType::SetUniformMat4),
             1u,
             "canvas emits one MVP uniform");
  testEqSize(g,
             fixture.mock.countNonEmptyOfType(CommandType::ClearScreen),
             1u,
             "frame emits one clear");

  bool foundRgbUpdate = false;
  for (size_t i = 0; i < fixture.mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = fixture.mock.getLastNonEmptySubmitted(i);
    if (command.commandType != CommandType::UpdateTexture) {
      continue;
    }
    testEqInt(g, command.updateTexture.x, 1, "dirty update x");
    testEqInt(g, command.updateTexture.y, 1, "dirty update y");
    testEqInt(g, command.updateTexture.width, 1, "dirty update width");
    testEqInt(g, command.updateTexture.height, 1, "dirty update height");
    testEqInt(g, command.updateTexture.channels, 3, "dirty update channels");
    testEqInt(
      g, command.updateTexture.srcRowStride, 16, "dirty update row stride");
    foundRgbUpdate = true;
  }
  testTrue(g, foundRgbUpdate, "canvas emits the expected RGB update");
}

static void
testSparseCellGridPool()
{
  testSection("SparseCellGrid: pool-backed chunks and GoL step");
  SparseCellGrid grid;
  testEqSize(g, grid.getAllocatedChunkCount(), 0, "starts empty");
  testEqUChar(g,
              grid.getCell(CellAddress{ 0, 0 }),
              SparseCellGrid::BackgroundState,
              "default background");
  testTrue(g, grid.setCell(CellAddress{ 0, 0 }, 0), "set 0,0");
  testTrue(g, grid.setCell(CellAddress{ 0, 1 }, 0), "set 0,1");
  testTrue(g, grid.setCell(CellAddress{ 0, 2 }, 0), "set 0,2");
  testTrue(g, grid.getAllocatedChunkCount() >= 1, "chunk allocated");
  testTrue(g, grid.getPoolChunks() >= 1, "pool has chunks");

  LifeLikeRuleSet rules(
    nullptr, "GAME_OF_LIFE", 1u << 3, (1u << 2) | (1u << 3));
  testTrue(g, grid.advance(rules), "advance succeeds");
  testEqUChar(g, grid.getCell(CellAddress{ -1, 1 }), 0, "blinker left");
  testEqUChar(g, grid.getCell(CellAddress{ 0, 1 }), 0, "blinker middle");
  testEqUChar(g, grid.getCell(CellAddress{ 1, 1 }), 0, "blinker right");
  testEqUChar(g,
              grid.getCell(CellAddress{ 0, 0 }),
              SparseCellGrid::BackgroundState,
              "old top becomes background");
  testTrue(g, grid.setCell(CellAddress{ 100, 100 }, 0), "far cell accepted");
  testTrue(g, grid.getAllocatedChunkCount() >= 2, "far cell adds a chunk");

  grid.clear();
  testEqSize(g, grid.getAllocatedChunkCount(), 0, "clear drops chunks");
}

static int
runProductBoundaryCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerProductBoundaryTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Renderer.CanvasTokens", []() {
    return runProductBoundaryCase(testRenderSceneCanvasTokens);
  });
  registry.add("IllumoGame.Alloc.SparseCellGridPool",
               []() { return runProductBoundaryCase(testSparseCellGridPool); });
}
