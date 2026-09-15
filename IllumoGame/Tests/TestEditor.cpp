#include "Game/BuiltinPatterns.h"
#include "Game/CellClipboard.h"
#include "Game/CellGameModule.h"
#include "Game/CellPattern.h"
#include "Game/IllumoCodec.h"
#include "Game/PatternCodec.h"
#include "Game/SparseCellGrid.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Platform/Clipboard.h>
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <filesystem>
#include <limits>
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

  explicit EditorFixture(bool showInspector = false)
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
    // Preferences are explicit so repeated runs cannot inherit a saved draft.
    env.setVar("fps", 60);
    env.setVar("showInspector", showInspector);
    env.setVar("reducedUiMotion", false);
    env.setVar("uiScale", 1);
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
    env.setVar("editHints", true);
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
testRleByteStates()
{
  testSection("Editor: RLE byte-state round trips");
  std::string error;
  for (int state = 0; state <= 255; ++state) {
    CellPattern original;
    original.setExtent(3, 2);
    if (state != 1) {
      for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
          original.addCell(x, y, static_cast<unsigned char>(state));
        }
      }
    }
    CellPattern parsed;
    bool exact = PatternCodec::parseRle(
                   PatternCodec::encodeRle(original), &parsed, &error) &&
                 parsed.getWidth() == 3 && parsed.getHeight() == 2 &&
                 parsed.getCells().size() == original.getCells().size();
    for (std::size_t i = 0; exact && i < parsed.getCells().size(); ++i) {
      const CellPatternCell& actual = parsed.getCells()[i];
      const CellPatternCell& expected = original.getCells()[i];
      exact = actual.dx == expected.dx && actual.dy == expected.dy &&
              actual.state == expected.state;
    }
    testTrue(
      g, exact, ("RLE exact byte state " + std::to_string(state)).c_str());
  }
  CellPattern mixed;
  mixed.addCell(0, 0, 2);
  mixed.addCell(1, 0, 0);
  mixed.addCell(2, 0, 0);
  mixed.addCell(3, 0, 0);
  mixed.addCell(4, 0, 10);
  mixed.addCell(5, 0, 255);
  mixed.addCell(6, 0, 255);
  const std::string encoded = PatternCodec::encodeRle(mixed);
  testTrue(g,
           encoded.find("p{2}3op{10}2p{255}!") != std::string::npos,
           "state delimiters separate subsequent run counts");
  CellPattern parsed;
  testTrue(g,
           PatternCodec::parse(encoded, &parsed, &error) &&
             parsed.getCells().size() == 7 &&
             parsed.getCells()[4].state == 10 &&
             parsed.getCells()[6].state == 255,
           "mixed adjacent state/run tokens autodetect and round trip");
  testTrue(g,
           PatternCodec::parseRle("p23o!", &parsed, &error) &&
             parsed.getCells().size() == 4 && parsed.getCells()[0].state == 2 &&
             parsed.getCells()[3].state == 0,
           "legacy one-digit state followed by run retains its meaning");
  for (const std::string& invalid :
       { "p{}!", "p{256}!", "p{-1}!", "p{10!", "p{999999999999999999}!" }) {
    testTrue(g,
             !PatternCodec::parseRle(invalid, &parsed, &error),
             "malformed or out-of-range state rejected");
  }
}

static void
testPatternFormatRouting()
{
  testSection("Editor: comment-aware detection and explicit formats");
  const std::string plaintext =
    "! Glider comment contains $ p0!\n.O.\n..O\nOOO\n";
  CellPattern parsed;
  std::string error;
  testTrue(g,
           PatternCodec::parse(plaintext, &parsed, &error),
           "commented plaintext autodetects");
  testTrue(g,
           parsed.getWidth() == 3 && parsed.getHeight() == 3 &&
             parsed.getCells().size() == 5,
           "comment text does not turn plaintext into empty RLE");
  testTrue(g,
           PatternCodec::parse(
             "# RLE comment ! $\nx=3,y=3\nbo$2bo$3o!", &parsed, &error),
           "RLE comments and compact header autodetect");
  testTrue(g, parsed.getCells().size() == 5, "RLE glider retains five cells");
  testTrue(g,
           PatternCodec::parse("oo\no", &parsed, &error),
           "ambiguous multiline cells use plaintext rows");
  testTrue(g,
           parsed.getWidth() == 2 && parsed.getHeight() == 2,
           "plaintext line breaks retain rows");

  EditorFixture fixture;
  SparseCellGrid* grid =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getGrid();
  grid->clear();
  testTrue(g,
           fixture.registry.QueueCommand("plaintext", { plaintext }),
           "explicit plaintext command queues");
  fixture.registry.ExecuteQueue();
  testTrue(g,
           grid->getCell(CellAddress{ 1, 0 }) == 0 &&
             grid->getCell(CellAddress{ 2, 1 }) == 0 &&
             grid->getCell(CellAddress{ 0, 2 }) == 0,
           "explicit plaintext imports commented glider at origin");
  grid->clear();
  fixture.registry.QueueCommand("plaintext", { "p0!" });
  fixture.registry.ExecuteQueue();
  testEqUChar(g,
              grid->getCell(CellAddress{ 0, 0 }),
              1,
              "explicit plaintext does not accept RLE p-token");
  fixture.registry.QueueCommand("rle", { "o\no" });
  fixture.registry.ExecuteQueue();
  testTrue(g,
           grid->getCell(CellAddress{ 1, 0 }) == 0 &&
             grid->getCell(CellAddress{ 0, 1 }) == 1,
           "explicit RLE ignores physical line break");
}

static void
testClipboardRejectsStaleFallback()
{
  testSection("Editor: invalid clipboard cannot paste previous pattern");
  CellClipboard clipboard;
  CellPattern previous;
  BuiltinPatterns::find("glider", &previous);
  clipboard.setClipboardPattern(previous);
  SparseCellGrid grid;
  CanvasView view(4, 4, &grid, nullptr, nullptr, nullptr);
  std::string error;
  for (const std::string& invalid : { std::string(),
                                      std::string("! comment only"),
                                      std::string("3b!"),
                                      std::string("o?!") }) {
    gClipboardText = invalid;
    const std::uint64_t revision = grid.getRevision();
    testTrue(g,
             !clipboard.pasteAtCursor(&grid, &view, 0, 0, &error),
             "empty or invalid clipboard paste fails");
    testTrue(g,
             grid.getRevision() == revision && !error.empty(),
             "failed paste reports error and leaves world unchanged");
    testEqSize(g,
               clipboard.getClipboardPattern().getCells().size(),
               5,
               "failed paste preserves internal pattern without reusing it");
  }
  gClipboardText = "! glider\n.O.\n..O\nOOO\n";
  testTrue(g,
           clipboard.pasteAtCursor(&grid, &view, 0, 0, &error),
           "valid commented plaintext clipboard pastes after failures");
  testEqUChar(g,
              grid.getCell(CellAddress{ 2, 1 }),
              0,
              "clipboard paste uses new plaintext cells");
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
testInspectorPreference()
{
  EditorFixture fixture(true);
  fixture.module.Update(0.016);
  GameVisual* inspector =
    CellGameModuleTestAccess::getInspectorVisual(fixture.module);
  testTrue(g,
           inspector != nullptr && inspector->isVisible(),
           "persisted inspector preference is honored at startup");
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
  CanvasView view(4, 4, &grid, nullptr, nullptr, nullptr);
  for (const std::int64_t endpoint :
       { std::numeric_limits<std::int64_t>::min(),
         std::numeric_limits<std::int64_t>::max() }) {
    cb.setSelection(endpoint, endpoint, endpoint, endpoint);
    grid.setCell(CellAddress{ endpoint, endpoint }, 0);
    testTrue(g,
             cb.captureSelection(&grid, &captured, &error),
             "one-cell endpoint selection captured");
    testTrue(g,
             captured.getWidth() == 1 && captured.getHeight() == 1 &&
               captured.getCells().size() == 1,
             "endpoint capture has exact local extent and occupancy");
    testTrue(g, cb.fillSelection(&grid, &view, 3), "endpoint selection fills");
    testEqUChar(g,
                grid.getCell(CellAddress{ endpoint, endpoint }),
                3,
                "endpoint fill changes selected cell");
    testTrue(
      g, cb.cutSelection(&grid, &view, &error), "endpoint selection cuts");
    testEqUChar(g,
                grid.getCell(CellAddress{ endpoint, endpoint }),
                1,
                "endpoint cut clears selected cell");
  }
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
  testTrue(g,
           IllumoCodec::writeFile(testFile, doc, &error),
           "direct writeFile succeeds");

  IllumoDocument loaded;
  testTrue(g,
           IllumoCodec::readFile(testFile, &loaded, &error),
           "direct readFile succeeds");
  testEqInt(g, loaded.version, 3, "loaded version is 3");
  testEqStr(g, loaded.ruleString, "GAME_OF_LIFE", "rule tag preserved");
  testTrue(g, loaded.cameraX == 15.25, "cameraX preserved");
  testTrue(g, loaded.cameraY == -27.5, "cameraY preserved");
  testTrue(g, loaded.cameraZoom == 3.5, "cameraZoom preserved");
  testTrue(g, loaded.grid != nullptr, "grid allocated");
  testEqUChar(
    g, loaded.grid->getCell(CellAddress{ 3, 4 }), 0, "saved cell preserved");
  grid.setCell(CellAddress{ 8, 9 }, 0);
  testTrue(g,
           IllumoCodec::writeFile(testFile, doc, &error),
           "replaces existing sparse save after finalization");
  testTrue(g,
           IllumoCodec::readFile(testFile, &loaded, &error) &&
             loaded.grid->getCell(CellAddress{ 8, 9 }) == 0,
           "replacement remains sparse version 3 compatible");
  std::filesystem::remove(testFile);
}

static void
testSelectionLifecycle()
{
  testSection("Editor: selection ends on painting and mode changes");
  EditorFixture fixture;
  SparseCellGrid* grid =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getGrid();
  CellClipboard& clipboard =
    CellGameModuleTestAccess::getClipboard(fixture.module);
  grid->clear();
  fixture.window.mouseX = 200.0;
  fixture.window.mouseY = 200.0;
  InputManagerTestAccess::setModifierFlags(fixture.input, 1); // Shift
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.0);
  fixture.window.mouseX = 248.0;
  fixture.module.Update(0.0);
  testTrue(g, clipboard.isSelecting(), "Shift-drag creates a selection");
  InputManagerTestAccess::setModifierFlags(fixture.input, 0);
  fixture.module.Update(0.0);
  testTrue(g, clipboard.isSelecting(), "releasing Shift keeps the drag");
  testEqSize(
    g, grid->getAllocatedChunkCount(), 0, "selection never paints cells");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.0);
  testTrue(g,
           clipboard.hasSelection() && !clipboard.isSelecting(),
           "mouse release preserves the completed selection for copying");

  CellPattern pattern;
  pattern.setExtent(1, 1);
  pattern.addCell(0, 0, 0);
  clipboard.setClipboardPattern(pattern);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.0);
  testTrue(g, !clipboard.hasSelection(), "plain left click clears selection");
  testTrue(
    g,
    !CellGameModuleTestAccess::getSelectionVisual(fixture.module).isVisible(),
    "outline disappears on the same frame");
  testEqSize(g,
             clipboard.getClipboardPattern().getCells().size(),
             1,
             "clearing selection preserves the copied buffer");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  clipboard.setSelection(0, 0, 1, 1);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.0);
  testTrue(g,
           CellGameModuleTestAccess::getState(fixture.module) ==
             CellState::NORMAL,
           "E enters Normal mode");
  testTrue(g, !clipboard.hasSelection(), "E clears the selection state");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Release);
  queueAndRun(fixture, "pause");
  clipboard.setSelection(0, 0, 1, 1);
  queueAndRun(fixture, "run");
  testTrue(g, !clipboard.hasSelection(), "console run also clears selection");
  testEqSize(g,
             clipboard.getClipboardPattern().getCells().size(),
             1,
             "mode changes preserve the copied buffer");

  grid->clear();
  InputManagerTestAccess::setModifierFlags(fixture.input, 2); // Control
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::V, InputAction::Press);
  fixture.module.Update(0.0);
  testEqSize(
    g, grid->getAllocatedChunkCount(), 0, "paste hotkey is inactive in Normal");
}

static void
testEditHints()
{
  testSection("Editor: hints follow settings, mode, and overlays");
  EditorFixture fixture;
  fixture.module.Update(0.0);
  GameVisual& hints =
    CellGameModuleTestAccess::getEditHintsVisual(fixture.module);
  CanvasView* canvas =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getCanvasView();
  const int fullHintInset = canvas->getBottomInsetPixels();
  testTrue(g, hints.isVisible() && hints.textCount() > 0, "hints default on");
  std::string allText;
  for (std::size_t i = 0; i < hints.textCount(); ++i) {
    TextPrimitive* text = hints.getText(i);
    allText += text->content;
    testTrue(g,
             text->y >= 0.0f && text->y + text->sizePt <= 480.0f,
             "hint text stays inside the window");
  }
  testTrue(g,
           allText.find("Shift+Left") != std::string::npos &&
             allText.find("Ctrl+V") != std::string::npos,
           "hints explain selection and clipboard modifiers");
  testTrue(g,
           allText.find("Ctrl+C/X") == std::string::npos,
           "selection actions stay hidden until relevant");
  CellGameModuleTestAccess::getClipboard(fixture.module)
    .setSelection(0, 0, 1, 1);
  fixture.module.Update(0.0);
  allText.clear();
  for (std::size_t i = 0; i < hints.textCount(); ++i) {
    allText += hints.getText(i)->content;
  }
  testTrue(g,
           allText.find("Ctrl+C/X") != std::string::npos &&
             allText.find("Delete") != std::string::npos,
           "selecting cells reveals copy, cut, and erase hints");
  CellGameModuleTestAccess::getClipboard(fixture.module).clearSelection();
  fixture.scene.ClearDrawables();
  fixture.scene.AddDrawable(&hints, RenderLayerId::UI);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) > 0,
           "hints emit render tokens");

  SimulatorConfiguration configuration =
    CellGameModuleTestAccess::currentConfiguration(fixture.module);
  configuration.editHints = false;
  testTrue(
    g,
    CellGameModuleTestAccess::applyConfiguration(fixture.module, configuration),
    "hint setting applies");
  fixture.module.Update(0.0);
  testTrue(g,
           !hints.isVisible() && !fixture.env.getVar("editHints").valueAsBool,
           "hint setting is saved to environment and hides the legend");
  testTrue(
    g,
    !CellGameModuleTestAccess::currentConfiguration(fixture.module).editHints,
    "reopening settings retains the hint preference");
  configuration.editHints = true;
  CellGameModuleTestAccess::applyConfiguration(fixture.module, configuration);
  fixture.console.isOpen = true;
  fixture.module.Update(0.0);
  testTrue(g, !hints.isVisible(), "console hides hints");
  fixture.console.isOpen = false;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Press);
  fixture.module.Update(0.0);
  testTrue(g, !hints.isVisible(), "settings hide hints");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Release);
  CellGameModuleTestAccess::getConfigurationMenu(fixture.module)->close();
  queueAndRun(fixture, "run");
  fixture.module.Update(0.0);
  testTrue(g, !hints.isVisible(), "Normal mode hides hints");
  queueAndRun(fixture, "pause");
  fixture.module.Update(0.05);
  testTrue(g,
           hints.isVisible() && canvas->getBottomInsetPixels() > 0 &&
             canvas->getBottomInsetPixels() < fullHintInset,
           "returning to Edit begins sliding the hints into view");
  for (int frame = 0; frame < 12; ++frame) {
    fixture.module.Update(0.05);
  }
  testTrue(g,
           hints.isVisible() && canvas->getBottomInsetPixels() == fullHintInset,
           "returning to Edit restores the full hint area");
  fixture.env.setVar("uiScale", 2);
  queueAndRun(fixture, "ruleset WIREWORLD");
  fixture.module.Update(0.0);
  const std::shared_ptr<Font> font = Font::getDefaultFont();
  testTrue(g, font != nullptr, "font metrics available for layout checks");
  allText.clear();
  for (std::size_t i = 0; i < hints.textCount(); ++i) {
    TextPrimitive* text = hints.getText(i);
    allText += text->content;
    if (font != nullptr) {
      const TextBounds bounds = font->measureText(text->content, text->sizePt);
      testTrue(g,
               text->x + bounds.width <= 320.1f &&
                 text->y + bounds.height <= 240.1f,
               "Wireworld hints fit at double UI scale");
    }
  }
  testTrue(g,
           allText.find("4: conductor") != std::string::npos,
           "Wireworld hints include the brush keys");
}

static void
testFooterReservation()
{
  EditorFixture fixture;
  CellContext* context =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  CanvasView* canvas = context->getCanvasView();
  SparseCellGrid* grid = context->getGrid();
  grid->clear();
  fixture.module.Update(0.0);
  const int inset = canvas->getBottomInsetPixels();
  testTrue(
    g, inset > 0 && inset < 100, "footer reserves a compact bottom band");
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
  fixture.renderer.EndFrame();
  bool clipped = false;
  bool restored = false;
  bool canvasDrawClipped = false;
  for (std::size_t i = 0; i < fixture.mock.getLastNonEmptySubmittedCount();
       ++i) {
    const RenderCommand& command = fixture.mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetScissorState) {
      if (command.scissor.enabled) {
        clipped =
          command.scissor.y == inset && command.scissor.height == 480 - inset;
      } else if (clipped) {
        restored = true;
      }
    }
    if (command.commandType == CommandType::DrawIndexed && clipped &&
        !restored) {
      canvasDrawClipped = true;
    }
  }
  testTrue(g,
           canvasDrawClipped && restored,
           "canvas clips above footer and restores scissor before UI drawing");

  // Stay outside the centered palette tab when the footer is disabled.
  fixture.window.mouseX = 100.0;
  fixture.window.mouseY = 479.0;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.0);
  testEqSize(
    g, grid->getAllocatedChunkCount(), 0, "clicking footer does not paint");
  InputManagerTestAccess::setModifierFlags(fixture.input, 1);
  fixture.module.Update(0.0);
  CellClipboard& clipboard =
    CellGameModuleTestAccess::getClipboard(fixture.module);
  testTrue(
    g, !clipboard.hasSelection(), "Shift-clicking footer does not select");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  CellPattern pattern;
  pattern.setExtent(1, 1);
  pattern.addCell(0, 0, 0);
  clipboard.setClipboardPattern(pattern);
  InputManagerTestAccess::setModifierFlags(fixture.input, 2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::V, InputAction::Press);
  const float zoom = fixture.camera.GetTargetZoom();
  *fixture.input.getMouseScrollOffset() = 1.0;
  fixture.module.Update(0.0);
  testEqSize(g,
             grid->getAllocatedChunkCount(),
             0,
             "paste over footer leaves world alone");
  testTrue(g,
           fixture.camera.GetTargetZoom() == zoom &&
             *fixture.input.getMouseScrollOffset() == 0.0,
           "footer consumes scrolling without zooming the canvas");

  InputManagerTestAccess::setModifierFlags(fixture.input, 0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::V, InputAction::Release);
  fixture.env.setVar("editHints", false);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.0);
  testTrue(g,
           canvas->getBottomInsetPixels() == 0 &&
             grid->getAllocatedChunkCount() > 0,
           "disabling hints restores the bottom canvas area immediately");
}

static void
testEditChromeModeTransition()
{
  testSection("Editor: hint bar and palette follow mode transitions");
  EditorFixture fixture;
  fixture.module.Update(0.0);
  GameVisual& hints =
    CellGameModuleTestAccess::getEditHintsVisual(fixture.module);
  GameVisual& palette =
    CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module);
  CanvasView* canvas =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getCanvasView();
  const int fullInset = canvas->getBottomInsetPixels();
  const float editTabY = palette.getShape(0)->rect.y;

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.04);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Release);
  testTrue(
    g,
    CellGameModuleTestAccess::getState(fixture.module) == CellState::NORMAL &&
      hints.isVisible() && palette.isVisible() &&
      canvas->getBottomInsetPixels() == fullInset &&
      hints.getTransform().y == 0.0f && palette.getShape(0)->rect.y > editTabY,
    "the palette starts its exit cascade before the hint bar");
  fixture.module.Update(0.10);
  testTrue(g,
           hints.getTransform().y > 0.0f &&
             canvas->getBottomInsetPixels() < fullInset &&
             palette.getShape(0)->rect.y - editTabY >
               static_cast<float>(fullInset),
           "the tab travels the footer and tab heights before the bar follows");
  for (int frame = 0; frame < 12; ++frame) {
    fixture.module.Update(0.05);
  }
  testTrue(g,
           !hints.isVisible() && !palette.isVisible() &&
             canvas->getBottomInsetPixels() == 0,
           "both controls leave the screen and release the canvas inset");

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.04);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Release);
  testTrue(g,
           CellGameModuleTestAccess::getState(fixture.module) ==
               CellState::EDIT &&
             hints.isVisible() && !palette.isVisible() &&
             canvas->getBottomInsetPixels() > 0 &&
             canvas->getBottomInsetPixels() < fullInset &&
             hints.getTransform().y > 0.0f,
           "the hint bar starts its entry cascade before the palette");
  fixture.module.Update(0.10);
  testTrue(g,
           palette.isVisible() && canvas->getBottomInsetPixels() < fullInset,
           "the palette follows the hint bar onto the screen");
  for (int frame = 0; frame < 12; ++frame) {
    fixture.module.Update(0.05);
  }
  testTrue(g,
           canvas->getBottomInsetPixels() == fullInset &&
             std::abs(hints.getTransform().y) < 0.01f,
           "the hint bar restores its full canvas reservation");
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
  registry.add("IllumoGame.Editor.FooterReservation",
               []() { return runEditorCase(testFooterReservation); });
  registry.add("IllumoGame.Editor.SelectionLifecycle",
               []() { return runEditorCase(testSelectionLifecycle); });
  registry.add("IllumoGame.Editor.EditHints",
               []() { return runEditorCase(testEditHints); });
  registry.add("IllumoGame.Editor.ModeChromeTransition",
               []() { return runEditorCase(testEditChromeModeTransition); });
  registry.add("IllumoGame.Editor.CopyPasteIdentity",
               []() { return runEditorCase(testCopyPasteIdentity); });
  registry.add("IllumoGame.Editor.TorusSkip",
               []() { return runEditorCase(testTorusSkip); });
  registry.add("IllumoGame.Editor.OversizeReject",
               []() { return runEditorCase(testOversizeReject); });
  registry.add("IllumoGame.Editor.RleGliderRoundTrip",
               []() { return runEditorCase(testRleGliderRoundTrip); });
  registry.add("IllumoGame.Editor.RleByteStates",
               []() { return runEditorCase(testRleByteStates); });
  registry.add("IllumoGame.Editor.PatternFormatRouting",
               []() { return runEditorCase(testPatternFormatRouting); });
  registry.add("IllumoGame.Editor.ClipboardRejectsStaleFallback", []() {
    return runEditorCase(testClipboardRejectsStaleFallback);
  });
  registry.add("IllumoGame.Editor.StampGlider",
               []() { return runEditorCase(testStampGlider); });
  registry.add("IllumoGame.Editor.PasteDrainsSimulation",
               []() { return runEditorCase(testPasteDrainsSimulation); });
  registry.add("IllumoGame.Editor.CDoesNotClearWorld",
               []() { return runEditorCase(testCDoesNotClearWorld); });
  registry.add("IllumoGame.Editor.InspectorPreference",
               []() { return runEditorCase(testInspectorPreference); });
  registry.add("IllumoGame.Editor.InspectorTokens",
               []() { return runEditorCase(testInspectorTokens); });
  registry.add("IllumoGame.Editor.CellClipboardOperations",
               []() { return runEditorCase(testCellClipboardOperations); });
  registry.add("IllumoGame.Editor.IllumoCodecDirect",
               []() { return runEditorCase(testIllumoCodecDirect); });
}
