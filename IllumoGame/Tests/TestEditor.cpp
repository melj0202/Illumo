#include "Game/BuiltinPatterns.h"
#include "Game/CellClipboard.h"
#include "Game/CellGameModule.h"
#include "Game/CellPattern.h"
#include "Game/IllumoCodec.h"
#include "Game/PatternCodec.h"
#include "Game/SparseCellGrid.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <filesystem>
#include <limits>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Platform/Clipboard.h>
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <string>
#include <vector>

static TestCounters g;
static std::string gClipboardText;

std::string
Clipboard::GetText()
{
  return gClipboardText;
}

bool
Clipboard::SetText(const std::string& text)
{
  gClipboardText = text;
  return true;
}

struct EditorFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  CommandRegistry registry;
  CommandLine console;
  InputManager input;
  Scene scene;
  IllumoContext context;
  CellGameModule module;
  bool started;

  EditorFixture()
    : window(640, 480)
    , env()
    , camera(glm::vec2(1.0f, 1.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , registry()
    , console(&env, &registry, &window, &renderer)
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               nullptr, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    env.setVar("WinX", 640);
    env.setVar("WinY", 480);
    env.setVar("CanvasX", 8);
    env.setVar("CanvasY", 6);
    env.setVar("ModeString", "GAME_OF_LIFE");
    env.setVar("tps", 30);
    env.setVar("speedFactor", 1.0);
    env.setVar("cellFadeSpeed", 8.0);
    env.setVar("WorldChunksX", 0);
    env.setVar("WorldChunksY", 0);
    env.setVar("vsync", true);
    env.setVar("fullscreen", false);
    mock.Initialize();
    started = module.Start(&context);
  }

  ~EditorFixture()
  {
    if (started) {
      module.Exit();
    }
  }
};

static bool
queueAndRun(EditorFixture& fixture, const std::string& command)
{
  std::vector<std::string> args;
  const std::size_t space = command.find(' ');
  std::string name = command;
  if (space != std::string::npos) {
    name = command.substr(0, space);
    std::string rest = command.substr(space + 1);
    std::size_t start = 0;
    while (start < rest.size()) {
      const std::size_t next = rest.find(' ', start);
      if (next == std::string::npos) {
        args.push_back(rest.substr(start));
        break;
      }
      if (next > start) {
        args.push_back(rest.substr(start, next - start));
      }
      start = next + 1;
    }
  }
  if (!fixture.registry.QueueCommand(name, args)) {
    return false;
  }
  fixture.registry.ExecuteQueue();
  return true;
}

static void
testCopyPasteIdentity()
{
  testSection("Editor: copy/paste identity");
  EditorFixture fixture;
  testTrue(g, fixture.started, "editor fixture starts");
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  SparseCellGrid* grid = cellContext->getGrid();
  grid->clear();
  grid->setCell(CellAddress{ 0, 0 }, 0);
  grid->setCell(CellAddress{ 1, 0 }, 0);
  grid->setCell(CellAddress{ 0, 1 }, 0);
  testTrue(g, queueAndRun(fixture, "select 0 0 1 1"), "select queues");
  testTrue(g, queueAndRun(fixture, "copy"), "copy queues");
  grid->clear();
  testTrue(g, queueAndRun(fixture, "paste 4 5"), "paste queues");
  testEqUChar(g, grid->getCell(CellAddress{ 4, 5 }), 0, "paste origin");
  testEqUChar(g, grid->getCell(CellAddress{ 5, 5 }), 0, "paste +x");
  testEqUChar(g, grid->getCell(CellAddress{ 4, 6 }), 0, "paste +y");
  testEqUChar(
    g, grid->getCell(CellAddress{ 5, 6 }), 1, "empty corner stays empty");
}

static void
testTorusSkip()
{
  testSection("Editor: finite torus skips out-of-bounds paste");
  EditorFixture fixture;
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  SimulatorConfiguration configuration =
    CellGameModuleTestAccess::currentConfiguration(fixture.module);
  configuration.worldChunkWidth = 1;
  configuration.worldChunkHeight = 1;
  testTrue(
    g,
    CellGameModuleTestAccess::applyConfiguration(fixture.module, configuration),
    "1x1 chunk torus applies");
  SparseCellGrid* grid = cellContext->getGrid();
  grid->clear();
  testTrue(g, queueAndRun(fixture, "select 0 0 0 0"), "select origin");
  grid->setCell(CellAddress{ 0, 0 }, 0);
  testTrue(g, queueAndRun(fixture, "copy"), "copy origin");
  grid->clear();
  testTrue(g,
           queueAndRun(fixture, "paste 16 0"),
           "paste outside does not fail command");
  testEqUChar(g,
              grid->getCell(CellAddress{ 0, 0 }),
              1,
              "out-of-bounds paste does not wrap onto the opposite edge");
}

static void
testOversizeReject()
{
  testSection("Editor: oversize selection is rejected");
  EditorFixture fixture;
  testTrue(g, queueAndRun(fixture, "select 0 0 300 300"), "select oversize");
  testTrue(g, queueAndRun(fixture, "copy"), "copy oversize");
  bool foundError = false;
  const std::vector<CommandLine::historyBuffer>& history =
    fixture.console.getHistory();
  for (const CommandLine::historyBuffer& entry : history) {
    if (entry.content.find("Copy failed") != std::string::npos ||
        entry.content.find("exceeds") != std::string::npos) {
      foundError = true;
    }
  }
  testTrue(g, foundError, "oversize copy logs a failure");
}

static void
testRleGliderRoundTrip()
{
  testSection("Editor: RLE glider round trip");
  CellPattern original;
  testTrue(g, BuiltinPatterns::find("glider", &original), "builtin glider");
  const std::string encoded = PatternCodec::encodeRle(original);
  CellPattern parsed;
  std::string error;
  testTrue(
    g, PatternCodec::parseRle(encoded, &parsed, &error), "parse encoded");
  testEqInt(g, parsed.getWidth(), original.getWidth(), "width preserved");
  testEqInt(g, parsed.getHeight(), original.getHeight(), "height preserved");
  testEqSize(g, parsed.getCells().size(), original.getCells().size(), "cells");
}

static void
testStampGlider()
{
  testSection("Editor: stamp glider occupancy");
  EditorFixture fixture;
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  SparseCellGrid* grid = cellContext->getGrid();
  grid->clear();
  testTrue(g, queueAndRun(fixture, "stamp glider"), "stamp glider");
  std::size_t alive = 0;
  for (std::int64_t y = 0; y < 4; ++y) {
    for (std::int64_t x = 0; x < 4; ++x) {
      if (grid->getCell(CellAddress{ x, y }) == 0) {
        alive += 1;
      }
    }
  }
  testEqSize(g, alive, 5u, "glider has five live cells");
}

static void
testPasteDrainsSimulation()
{
  testSection("Editor: paste drains outstanding simulation");
  EditorFixture fixture;
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  cellContext->getGrid()->clear();
  cellContext->getGrid()->setCell(CellAddress{ 0, 0 }, 0);
  testTrue(g, queueAndRun(fixture, "select 0 0 0 0"), "select");
  testTrue(g, queueAndRun(fixture, "copy"), "copy");
  testTrue(g, queueAndRun(fixture, "run"), "start simulation");
  testTrue(g, queueAndRun(fixture, "paste 2 2"), "paste while running");
  testTrue(g,
           !CellGameModuleTestAccess::isSimulationBusy(fixture.module),
           "paste drained the runner");
  testEqUChar(
    g, cellContext->getGrid()->getCell(CellAddress{ 2, 2 }), 0, "pasted");
}

static void
testCDoesNotClearWorld()
{
  testSection("Editor: C no longer clears the world");
  EditorFixture fixture;
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  SparseCellGrid* grid = cellContext->getGrid();
  const unsigned char before = grid->getCell(CellAddress{ 0, 0 });
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::C, InputAction::Press);
  fixture.module.Update(0.016);
  testEqUChar(g,
              grid->getCell(CellAddress{ 0, 0 }),
              before,
              "C does not clear the canvas");
}

static void
testInspectorTokens()
{
  testSection("Editor: inspector HUD emits UI tokens");
  EditorFixture fixture;
  testTrue(g, queueAndRun(fixture, "inspect"), "inspect toggle");
  fixture.module.Update(0.016);
  GameVisual* inspector =
    CellGameModuleTestAccess::getInspectorVisual(fixture.module);
  testTrue(
    g, inspector != nullptr && inspector->isVisible(), "inspector visible");
  bool foundGeneration = false;
  for (std::size_t i = 0; i < inspector->textCount(); ++i) {
    TextPrimitive* text = inspector->getText(i);
    if (text != nullptr && text->content.find("gen ") != std::string::npos) {
      foundGeneration = true;
    }
  }
  testTrue(g, foundGeneration, "inspector reports generation");
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testTrue(
    g, fixture.scene.drawableCount() >= 2u, "inspector adds a UI drawable");
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "inspector frame draws");
}

static void
testCellClipboardOperations()
{
  testSection("Editor: CellClipboard standalone operations");
  CellClipboard cb;
  testTrue(g, !cb.hasSelection(), "initially no selection");
  testTrue(g, !cb.isSelecting(), "initially not selecting");

  cb.setSelection(10, 20, 5, 8);
  testTrue(g, cb.hasSelection(), "selection set");
  std::int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  cb.getNormalizedSelection(&x0, &y0, &x1, &y1);
  testEqInt(g, static_cast<int>(x0), 5, "normalized x0");
  testEqInt(g, static_cast<int>(y0), 8, "normalized y0");
  testEqInt(g, static_cast<int>(x1), 10, "normalized x1");
  testEqInt(g, static_cast<int>(y1), 20, "normalized y1");

  cb.startSelection(2, 3);
  testTrue(g, cb.isSelecting(), "isSelecting true during drag");
  cb.updateSelectionDrag(7, 9);
  cb.stopSelectionDrag();
  testTrue(g, !cb.isSelecting(), "isSelecting false after stop");
  cb.getNormalizedSelection(&x0, &y0, &x1, &y1);
  testEqInt(g, static_cast<int>(x0), 2, "drag normalized x0");
  testEqInt(g, static_cast<int>(y0), 3, "drag normalized y0");
  testEqInt(g, static_cast<int>(x1), 7, "drag normalized x1");
  testEqInt(g, static_cast<int>(y1), 9, "drag normalized y1");

  cb.clearSelection();
  testTrue(g, !cb.hasSelection(), "cleared selection");

  CellPattern pattern;
  pattern.setExtent(2, 2);
  pattern.addCell(0, 0, 0);
  cb.setClipboardPattern(pattern);
  testTrue(g, cb.rotateCw(), "rotateCw succeeds");
  testTrue(g, cb.flipHorizontal(), "flipHorizontal succeeds");
  testTrue(g, cb.flipVertical(), "flipVertical succeeds");

  // Extreme coordinate boundary protection
  cb.setSelection(-5000000000000000000LL,
                  -5000000000000000000LL,
                  5000000000000000000LL,
                  5000000000000000000LL);
  SparseCellGrid grid;
  CellPattern captured;
  std::string error;
  testTrue(g,
           !cb.captureSelection(&grid, &captured, &error),
           "extreme 64-bit span safely rejected without overflow");
  testTrue(g,
           error.find("exceeds") != std::string::npos,
           "reports exceeds error for extreme span");

  cb.setSelection(0,
                  0,
                  std::numeric_limits<std::int64_t>::max(),
                  std::numeric_limits<std::int64_t>::max());
  testTrue(g,
           !cb.captureSelection(&grid, &captured, &error),
           "INT64_MAX selection rejected");
  testTrue(
    g, !cb.fillSelection(&grid, nullptr, 0), "fillSelection on null rejected");
}

static void
testIllumoCodecDirect()
{
  testSection("Persistence: IllumoCodec direct file serialization");
  testTrue(g,
           IllumoCodec::withIllumoExtension("world") == "world.illumo",
           "adds .illumo extension");
  testTrue(g,
           IllumoCodec::withIllumoExtension("world.illumo") == "world.illumo",
           "preserves existing .illumo extension");

  IllumoDocument doc;
  doc.version = IllumoCodec::kVersion;
  doc.ruleString = "GAME_OF_LIFE";
  doc.cameraX = 15.25;
  doc.cameraY = -27.5;
  doc.cameraZoom = 3.5;
  doc.worldChunkWidth = 0;
  doc.worldChunkHeight = 0;
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 3, 4 }, 0);
  doc.sourceGrid = &grid;

  const std::string testFile = "direct-codec-test.illumo";
  std::string error;
  testTrue(
    g, IllumoCodec::writeFile(testFile, doc, &error), "direct writeFile succeeds");

  IllumoDocument loaded;
  testTrue(
    g, IllumoCodec::readFile(testFile, &loaded, &error), "direct readFile succeeds");
  testEqInt(g, loaded.version, 3, "loaded version is 3");
  testEqStr(g, loaded.ruleString, "GAME_OF_LIFE", "rule tag preserved");
  testTrue(g, loaded.cameraX == 15.25, "cameraX preserved");
  testTrue(g, loaded.cameraY == -27.5, "cameraY preserved");
  testTrue(g, loaded.cameraZoom == 3.5, "cameraZoom preserved");
  testTrue(g, loaded.grid != nullptr, "grid allocated");
  testEqUChar(
    g, loaded.grid->getCell(CellAddress{ 3, 4 }), 0, "saved cell preserved");
  std::filesystem::remove(testFile);
}

static int
runEditorCase(void (*testFunction)())
{
  g.failures = 0;
  gClipboardText.clear();
  testFunction();
  return g.failures;
}

void
registerEditorTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Editor.CopyPasteIdentity",
               []() { return runEditorCase(testCopyPasteIdentity); });
  registry.add("IllumoGame.Editor.TorusSkip",
               []() { return runEditorCase(testTorusSkip); });
  registry.add("IllumoGame.Editor.OversizeReject",
               []() { return runEditorCase(testOversizeReject); });
  registry.add("IllumoGame.Editor.RleGliderRoundTrip",
               []() { return runEditorCase(testRleGliderRoundTrip); });
  registry.add("IllumoGame.Editor.StampGlider",
               []() { return runEditorCase(testStampGlider); });
  registry.add("IllumoGame.Editor.PasteDrainsSimulation",
               []() { return runEditorCase(testPasteDrainsSimulation); });
  registry.add("IllumoGame.Editor.CDoesNotClearWorld",
               []() { return runEditorCase(testCDoesNotClearWorld); });
  registry.add("IllumoGame.Editor.InspectorTokens",
               []() { return runEditorCase(testInspectorTokens); });
  registry.add("IllumoGame.Editor.CellClipboardOperations",
               []() { return runEditorCase(testCellClipboardOperations); });
  registry.add("IllumoGame.Editor.IllumoCodecDirect",
               []() { return runEditorCase(testIllumoCodecDirect); });
}
