// Canvas domain / visual buffer tests (CPU-side; GPU is mock-enrolled only).

#include "Rulesets/LifeLikeRuleSet.h"
#include "TestHarness.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>

static TestCounters g;

static void
testClearAndSetGet()
{
  testSection("Canvas: clear + set/get pixel");
  HeadlessCanvasFixture f(8, 6);
  f.clearDead();
  testEqUChar(
    g, f.at(0, 0), HeadlessCanvasFixture::Dead, "cleared cell is dead");
  f.setAlive(2, 3);
  testEqUChar(g, f.at(2, 3), HeadlessCanvasFixture::Alive, "set alive");
  testEqUChar(g, f.at(1, 3), HeadlessCanvasFixture::Dead, "neighbor unchanged");
  f.setDead(2, 3);
  testEqUChar(g, f.at(2, 3), HeadlessCanvasFixture::Dead, "set dead");
}

static void
testDimensions()
{
  testSection("Canvas: dimensions");
  HeadlessCanvasFixture f(16, 10);
  std::array<int, 2> d = f.canvas->getDimensions();
  testEqInt(g, d[0], 16, "width");
  testEqInt(g, d[1], 10, "height");
}

static void
testClearMarksUpload()
{
  testSection("Canvas: clear marks full display upload");
  HeadlessCanvasFixture f(4, 4);
  f.canvas->AppendCommands(&f.renderer);
  testTrue(g, !f.canvas->isTextureUploadPending(), "enroll upload drained");
  f.clearDead();
  testTrue(g, f.canvas->isTextureUploadPending(), "clear needs texture upload");
  const DirtyRect& ur = f.canvas->getUploadDirtyRegion();
  testTrue(g, ur.valid(), "clear upload rect valid");
  testEqInt(g, ur.width(), 4, "clear upload width");
  testEqInt(g, ur.height(), 4, "clear upload height");
}

static void
testDefaultPalette()
{
  testSection("Canvas: default palette alive/dead colors");
  HeadlessCanvasFixture f(2, 2);
  const unsigned char* pal = f.canvas->getPaletteRgb();
  testEqUChar(g, pal[0], 0, "alive R");
  testEqUChar(g, pal[1], 0, "alive G");
  testEqUChar(g, pal[2], 0, "alive B");
  testEqUChar(g, pal[3], 255, "dead R");
  testEqUChar(g, pal[4], 255, "dead G");
  testEqUChar(g, pal[5], 255, "dead B");
}

static void
testFadeTowardTarget()
{
  testSection("Canvas: tickVisual fades display toward target");
  HeadlessCanvasFixture f(4, 4);
  f.canvas->AppendCommands(&f.renderer);
  f.canvas->onTargetsRebuilt();

  // Paint alive (black target via palette) and rebuild targets.
  f.setAlive(1, 1);
  f.canvas->rebuildTargetsFromLife();
  testTrue(g,
           f.canvas->isFadeActive() || f.canvas->getFadeSpeed() <= 0.0f,
           "fade active or instant");

  f.canvas->setFadeSpeed(8.0f);
  // Re-set target so fade is active with known speed.
  f.canvas->setTargetColor(1 * 4 + 1, 0, 0, 0);
  f.canvas->tickVisual(0.5f);
  const unsigned char* tex = f.canvas->getDisplayTexBuffer();
  const int base = (1 * 4 + 1) * 3;
  testTrue(g, tex[base] < 255, "R decreased toward black after fade step");

  f.canvas->setFadeSpeed(0.0f);
  f.canvas->setTargetColor(1 * 4 + 1, 0, 0, 0);
  f.canvas->tickVisual(0.01f);
  testEqUChar(
    g, f.canvas->getDisplayTexBuffer()[base], 0, "fadeSpeed 0 snaps R");
}

static void
testEnrollCreatesGpuHandles()
{
  testSection("Canvas: init enrolls mesh/display + shared styles");
  HeadlessCanvasFixture f(4, 4);
  // built-in styles + GameVisual meshes + RGB display
  testTrue(g, f.mock.getCreateCount() >= 8u, "at least 8 create records");
  testTrue(g, f.renderer.builtinStylesReady(), "builtin styles enrolled");
  testTrue(g,
           f.renderer.getStyle(RenderStyleId::Canvas) != nullptr,
           "Canvas style ready");
}

static void
testDirtyFlagsAndSparseUpload()
{
  testSection("Canvas: dirty life + sparse display upload after snap");
  HeadlessCanvasFixture f(4, 4);
  f.canvas->AppendCommands(&f.renderer);
  f.canvas->onTargetsRebuilt();
  testTrue(g, !f.canvas->isTextureUploadPending(), "upload clears pending");
  testTrue(g, !f.canvas->isCellsDirty(), "logical dirty cleared");

  f.canvas->tickVisual(0.016f);
  testTrue(
    g, !f.canvas->isTextureUploadPending(), "idle tick does not dirty texture");

  f.setAlive(1, 1);
  testTrue(g, f.canvas->isCellsDirty(), "paint sets cellsDirty");
  testTrue(
    g, f.canvas->hasCellsDirtyRegion(), "paint sets sparse dirty region");
  // GPU upload waits until visual targets/fade write texels.
  testTrue(g,
           !f.canvas->isTextureUploadPending(),
           "paint alone does not upload display");

  f.canvas->rebuildTargetsFromLife();
  f.canvas->setFadeSpeed(0.0f);
  // Instant snap path in onTargetsRebuilt when fadeSpeed was already 0 after
  // rebuild — force snap after targets:
  f.canvas->setTargetColor(1 * 4 + 1, 0, 0, 0);
  f.canvas->snapVisualToTargets();
  testTrue(
    g, f.canvas->isTextureUploadPending(), "snap dirties display upload");
  const DirtyRect& ur = f.canvas->getUploadDirtyRegion();
  testEqInt(g, ur.minX, 1, "upload minX");
  testEqInt(g, ur.minY, 1, "upload minY");
}

static int
runCanvasDomainCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerCanvasDomainTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Canvas.ClearAndSetGet",
               []() { return runCanvasDomainCase(testClearAndSetGet); });
  registry.add("IllumoGame.Canvas.Dimensions",
               []() { return runCanvasDomainCase(testDimensions); });
  registry.add("IllumoGame.Canvas.ClearMarksUpload",
               []() { return runCanvasDomainCase(testClearMarksUpload); });
  registry.add("IllumoGame.Canvas.DefaultPalette",
               []() { return runCanvasDomainCase(testDefaultPalette); });
  registry.add("IllumoGame.Canvas.FadeTowardTarget",
               []() { return runCanvasDomainCase(testFadeTowardTarget); });
  registry.add("IllumoGame.Canvas.EnrollCreatesHandles", []() {
    return runCanvasDomainCase(testEnrollCreatesGpuHandles);
  });
  registry.add("IllumoGame.Canvas.DirtySparseUpload", []() {
    return runCanvasDomainCase(testDirtyFlagsAndSparseUpload);
  });
}
