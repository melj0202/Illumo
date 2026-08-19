#include "CellGameModule.h"
#include "Rulesets/WireworldRuleSet.h"
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Primitives/DebugDraw3D.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <new>
#include <random>
#include <vector>

static bool
parseIntegerArgument(const std::string& text, int* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    long long parsed = std::stoll(text, &consumed);
    if (consumed != text.size() || parsed < -2147483648LL ||
        parsed > 2147483647LL) {
      return false;
    }
    *value = static_cast<int>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

static bool
parseWorldCoordinate(const std::string& text, std::int64_t* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    const long long parsed = std::stoll(text, &consumed);
    if (consumed != text.size()) {
      return false;
    }
    *value = static_cast<std::int64_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

static bool
parseFloatingArgument(const std::string& text, double* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    double parsed = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(parsed)) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

static bool
cellGameContextComplete(const IllumoContext* context)
{
  return context != nullptr && context->envVars != nullptr &&
         context->window != nullptr && context->camera != nullptr &&
         context->renderer != nullptr && context->inputManager != nullptr &&
         context->commandLine != nullptr &&
         context->commandRegistry != nullptr && context->scene != nullptr;
}

static std::string
withIllumoExtension(const std::string& filename)
{
  const std::string extension = ".illumo";
  if (filename.size() >= extension.size()) {
    std::string ending = filename.substr(filename.size() - extension.size());
    for (std::size_t i = 0; i < ending.size(); ++i) {
      ending[i] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(ending[i])));
    }
    if (ending == extension) {
      return filename;
    }
  }
  return filename + extension;
}

CellGameModule::CellGameModule()
  : cellContext(nullptr)
  , currentState(CellState::EDIT)
  , simAccum(0.0)
  , simStepSeconds(1.0 / 30.0)
  , requestedSimulationTps(30.0)
  , achievedSimulationTps(0.0)
  , lastSimulationStepMilliseconds(0.0)
  , lastSimulationFrameMilliseconds(0.0)
  , lastSimulationSteps(0)
  , simulationDebtDropped(false)
  , simulationBudgetLimited(false)
  , mirrorDeltaValid(false)
  , wireworldBrush(WireworldRuleSet::CELL_CONDUCTOR)
  , modeSplash(nullptr)
  , configurationMenu(nullptr)
  , render3dTestStatic(nullptr)
  , render3dTestAnimated(nullptr)
  , render3dTestTime(0.0)
{
  ic = nullptr;
}

CellGameModule::~CellGameModule() {}

bool
CellGameModule::Start(IllumoContext* context)
{
  if (!cellGameContextComplete(context)) {
    Logger::LogError(
      "CellGameModule::Start: IllumoContext missing required services "
      "(envVars, window, camera, renderer, inputManager, commandLine, "
      "commandRegistry, scene)");
    ic = context;
    return false;
  }
  ic = context;

  // Prefer ModeString from envvars / previous console command.
  std::string startMode = ic->envVars->getVar("ModeString").value;
  if (startMode.empty()) {
    startMode = "GAME_OF_LIFE";
  }
  this->cellContext = new CellContext(
    startMode, ic->envVars, ic->window, ic->camera, ic->renderer);

  // Simulation step rate comes from env (tps * speedFactor). Re-read live in
  // Normal().
  simAccum = 0.0;
  syncSimRateFromEnv();

  InputEvent ac;
  ac.keyCode = KeyCode::MouseMiddle;
  ac.inputAction = InputAction::Press;
  this->inputContext.bindAction("CameraPan", ac);

  ac.keyCode = KeyCode::MouseRight;
  ac.inputAction = InputAction::Press;
  this->inputContext.bindAction("CameraRotate", ac);

  ac.keyCode = KeyCode::C;
  ac.inputAction = InputAction::Press;
  this->inputContext.bindAction("ClearCanvas", ac);

  ac.keyCode = KeyCode::E;
  ac.inputAction = InputAction::Press;
  this->inputContext.bindAction("ToggleState", ac);

  ac.keyCode = KeyCode::F1;
  ac.inputAction = InputAction::Press;
  this->inputContext.bindAction("ToggleSettings", ac);

  ac.keyCode = KeyCode::MouseLeft;
  ac.inputAction = InputAction::Press;
  this->inputContext.bindAction("PaintCanvas", ac);

  long contextId = ic->inputManager->registerInputContext(this->inputContext);
  ic->inputManager->setActiveInputContext(contextId);

  currentState = CellState::EDIT;
  wireworldBrush = WireworldRuleSet::CELL_CONDUCTOR;

  // Ruleset-aware startup seed (GoL glider, Wireworld electron-on-wire, …).
  seedInitialPattern();

  // Initial palette from active ruleset; seed cells already mark upload dirty.
  cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
  updateVisualTargets();
  registerConsoleCommands();

  // Mode splash label (top-left corner). Shown briefly when toggling
  // EDIT/NORMAL with E. Owned by this module (not a translation-unit global).
  if (modeSplash == nullptr && ic->renderer != nullptr) {
    modeSplash = std::make_unique<SplashText>(
      "EDIT", 255, 230, 120, 255, 32, 16, 48, ic->renderer);
    modeSplash->setVisible(false);
  }

  editorCursor.init(ic->renderer, ic->window, ic->camera);
  editorCursor.setCellSize(16.0f);
  // Hidden until Edit() updates cell position (avoids extra Scene entry at
  // Start before the first mouse sample).
  editorCursor.setVisible(false);

  configurationMenu =
    std::make_unique<ConfigurationMenu>(ic->window, ic->renderer);

  return true;
}

void
CellGameModule::showModeSplash(const char* label)
{
  if (modeSplash == nullptr || label == nullptr) {
    return;
  }
  modeSplash->setContent(label);
  modeSplash->Wake();
}

void
CellGameModule::seedInitialPattern()
{
  SparseCellGrid* grid = cellContext->getGrid();

  if (cellContext->getModeString() == "WIREWORLD") {
    // Horizontal conductor with a head+tail pair so one electron travels right.
    const std::int64_t y = 0;
    const std::int64_t startX = -4;
    for (int i = 0; i < 8; ++i) {
      grid->setCell(CellAddress{ startX + i, y },
                    WireworldRuleSet::CELL_CONDUCTOR);
    }
    grid->setCell(CellAddress{ startX, y }, WireworldRuleSet::CELL_HEAD);
    grid->setCell(CellAddress{ startX + 1, y }, WireworldRuleSet::CELL_TAIL);
    return;
  }

  // Classic Game-of-Life glider (pointing down-right). Works for most binary
  // life-like rules as a visible non-empty startup.
  grid->setCell(CellAddress{ 1, 0 }, 0);
  grid->setCell(CellAddress{ 2, 1 }, 0);
  grid->setCell(CellAddress{ 0, 2 }, 0);
  grid->setCell(CellAddress{ 1, 2 }, 0);
  grid->setCell(CellAddress{ 2, 2 }, 0);
}

void
CellGameModule::updateWireworldBrushFromInput()
{
  if (ic == nullptr || ic->inputManager == nullptr ||
      ic->commandLine == nullptr || ic->commandLine->isOpen) {
    return;
  }
  // Sticky brush: last selected key wins until another is pressed.
  // 1/H = head, 2 = empty, 3/T = tail, 4 = conductor (default).
  if (ic->inputManager->isKeyPressed(KeyCode::Num1) ||
      ic->inputManager->isKeyPressed(KeyCode::H)) {
    wireworldBrush = WireworldRuleSet::CELL_HEAD;
  } else if (ic->inputManager->isKeyPressed(KeyCode::Num2)) {
    wireworldBrush = WireworldRuleSet::CELL_EMPTY;
  } else if (ic->inputManager->isKeyPressed(KeyCode::Num3) ||
             ic->inputManager->isKeyPressed(KeyCode::T)) {
    wireworldBrush = WireworldRuleSet::CELL_TAIL;
  } else if (ic->inputManager->isKeyPressed(KeyCode::Num4)) {
    wireworldBrush = WireworldRuleSet::CELL_CONDUCTOR;
  }
}

void
CellGameModule::updateVisualTargets()
{
  ZoneScopedN("Visual.updateTargets");
  // life → palette target colors (sparse); tickVisual eases displayRgb toward
  // them.
  cellContext->getCanvasView()->rebuildTargetsFromGrid();
}

bool
CellGameModule::consumeCompletedSimulation(bool waitForCompletion)
{
  SparseCellGrid* completedGrid = nullptr;
  SparseGenerationDelta completedDelta;
  double elapsedMilliseconds = 0.0;
  bool advanceSucceeded = false;
  SimulationRunnerTimings timings;
  const bool completed =
    waitForCompletion
      ? simulationRunner.waitAndTakeCompleted(&completedGrid,
                                              &completedDelta,
                                              &elapsedMilliseconds,
                                              &advanceSucceeded,
                                              &timings)
      : simulationRunner.tryTakeCompleted(&completedGrid,
                                          &completedDelta,
                                          &elapsedMilliseconds,
                                          &advanceSucceeded,
                                          &timings);
  if (!completed) {
    return false;
  }
  if (!advanceSucceeded || completedGrid != cellContext->getSpareGrid()) {
    mirrorDeltaValid = false;
    return true;
  }
  cellContext->publishSpareGrid(completedDelta);
  mirrorDelta = std::move(completedDelta);
  mirrorDeltaValid = true;
  lastSimulationRunnerTimings = timings;
  lastSimulationStepMilliseconds = elapsedMilliseconds;
  lastSimulationFrameMilliseconds = elapsedMilliseconds;
  lastSimulationSteps += 1;
  simulationStepMetric.add(elapsedMilliseconds);
  simulationMirrorMetric.add(timings.mirrorMilliseconds);
  simulationAdvanceMetric.add(timings.advanceMilliseconds);
  simulationCaptureMetric.add(timings.captureMilliseconds);
  return true;
}

void
CellGameModule::drainSimulation()
{
  if (cellContext == nullptr) {
    return;
  }
  while (simulationRunner.isBusy()) {
    if (!consumeCompletedSimulation(true)) {
      break;
    }
  }
}

void
CellGameModule::prepareGridMutation()
{
  drainSimulation();
  mirrorDelta.clear();
  mirrorDeltaValid = false;
}

SimulatorConfiguration
CellGameModule::currentConfiguration() const
{
  SimulatorConfiguration configuration;
  if (cellContext == nullptr || ic == nullptr || ic->envVars == nullptr) {
    return configuration;
  }
  configuration.ruleSet = cellContext->getModeString();
  configuration.worldChunkWidth = cellContext->getWorldChunkWidth();
  configuration.worldChunkHeight = cellContext->getWorldChunkHeight();
  configuration.tps = ic->envVars->getVar("tps").valueAsLong;
  if (configuration.tps < 1 || configuration.tps > 1000) {
    configuration.tps = 12;
  }
  configuration.speedFactor = ic->envVars->getVar("speedFactor").valueAsDouble;
  if (configuration.speedFactor <= 0.0 || configuration.speedFactor > 100.0) {
    configuration.speedFactor = 1.0;
  }
  configuration.fadeSpeed = ic->envVars->getVar("cellFadeSpeed").valueAsDouble;
  if (configuration.fadeSpeed < 0.0 || configuration.fadeSpeed > 100.0) {
    configuration.fadeSpeed = 6.0;
  }
  configuration.vsync = ic->envVars->getVar("vsync").valueAsBool;
  configuration.fullscreen = ic->envVars->getVar("fullscreen").valueAsBool;
  return configuration;
}

bool
CellGameModule::applyConfiguration(const SimulatorConfiguration& configuration)
{
  if (cellContext == nullptr || ic == nullptr || ic->envVars == nullptr ||
      !CellContext::IsKnownModeString(configuration.ruleSet) ||
      !SparseCellGrid::isValidTopology(configuration.worldChunkWidth,
                                       configuration.worldChunkHeight) ||
      configuration.tps < 1 || configuration.tps > 1000 ||
      !std::isfinite(configuration.speedFactor) ||
      configuration.speedFactor <= 0.0 || configuration.speedFactor > 100.0 ||
      !std::isfinite(configuration.fadeSpeed) ||
      configuration.fadeSpeed < 0.0 || configuration.fadeSpeed > 100.0) {
    return false;
  }

  const bool topologyChanged =
    configuration.worldChunkWidth != cellContext->getWorldChunkWidth() ||
    configuration.worldChunkHeight != cellContext->getWorldChunkHeight();
  const bool rulesetChanged =
    configuration.ruleSet != cellContext->getModeString();
  const bool fullscreenChanged =
    configuration.fullscreen != ic->envVars->getVar("fullscreen").valueAsBool;

  if (topologyChanged || rulesetChanged) {
    prepareGridMutation();
  }
  if (topologyChanged) {
    if (!cellContext->resetWorld(configuration.worldChunkWidth,
                                 configuration.worldChunkHeight)) {
      return false;
    }
  }
  if (rulesetChanged) {
    cellContext->setRuleSet(configuration.ruleSet);
  }

  ic->envVars->setVar("WorldChunksX",
                      static_cast<long>(configuration.worldChunkWidth));
  ic->envVars->setVar("WorldChunksY",
                      static_cast<long>(configuration.worldChunkHeight));
  ic->envVars->setVar("ModeString", configuration.ruleSet);
  ic->envVars->setVar("tps", configuration.tps);
  ic->envVars->setVar("speedFactor", configuration.speedFactor);
  ic->envVars->setVar("cellFadeSpeed", configuration.fadeSpeed);
  ic->envVars->setVar("vsync", configuration.vsync);
  ic->envVars->setVar("fullscreen", configuration.fullscreen);

  if (topologyChanged) {
    seedInitialPattern();
    ic->camera->Reset();
    currentState = CellState::EDIT;
    simAccum = 0.0;
    achievedSimulationTps = 0.0;
    showModeSplash("EDIT");
  }
  if (topologyChanged || rulesetChanged) {
    cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
    updateVisualTargets();
    cellContext->getCanvasView()->snapVisualToTargets();
  }
  if (cellContext->getModeString() == "WIREWORLD") {
    wireworldBrush = WireworldRuleSet::CELL_CONDUCTOR;
  }
  syncSimRateFromEnv();
  if (fullscreenChanged && ic->window != nullptr) {
    ic->window->toggleFullscreen();
  }
  ic->envVars->save();
  return true;
}

void
CellGameModule::registerConsoleCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr ||
      ic->commandLine == nullptr) {
    return;
  }

  const std::vector<std::string> rulesets = CellContext::GetKnownModeStrings();
  CommandFn rulesetCommand = [this](const std::vector<std::string>& args) {
    if (args.empty()) {
      ic->commandLine->logNormal("Current ruleset: " +
                                 cellContext->getModeString());
      ic->commandLine->logNormal("Usage: ruleset <name>");
      return;
    }
    if (args.size() != 1) {
      ic->commandLine->logError("Usage: ruleset <name>");
      return;
    }

    const std::string mode = CellContext::NormalizeModeString(args[0]);
    if (!CellContext::IsKnownModeString(mode)) {
      ic->commandLine->logError("Unknown ruleset '" + args[0] + "'");
      return;
    }

    prepareGridMutation();
    if (cellContext->setRuleSet(mode)) {
      cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
      updateVisualTargets();
    }
    ic->commandLine->logSuccess("Ruleset: " + cellContext->getModeString());
  };
  ic->commandRegistry->RegisterCommand(
    "ruleset",
    rulesetCommand,
    "ruleset [name]",
    "Show or change the cellular-automaton ruleset",
    rulesets);
  ic->commandRegistry->RegisterCommand(
    "mode", rulesetCommand, "mode [name]", "Alias for ruleset", rulesets);

  CommandFn tpsCommand = [this](const std::vector<std::string>& args) {
    if (args.empty()) {
      ic->commandLine->logNormal("tps = " + ic->envVars->getVar("tps").value);
      return;
    }
    int value = 0;
    if (args.size() != 1 || !parseIntegerArgument(args[0], &value) ||
        value < 1 || value > 1000) {
      ic->commandLine->logError("tps must be an integer from 1 to 1000");
      return;
    }
    ic->envVars->setVar("tps", value);
    ic->commandLine->logSuccess("tps = " + std::to_string(value));
  };
  ic->commandRegistry->RegisterCommand(
    "tps",
    tpsCommand,
    "tps [1..1000]",
    "Show or set simulation ticks per second");

  CommandFn speedCommand = [this](const std::vector<std::string>& args) {
    if (args.empty()) {
      ic->commandLine->logNormal("speedFactor = " +
                                 ic->envVars->getVar("speedFactor").value);
      return;
    }
    double value = 0.0;
    if (args.size() != 1 || !parseFloatingArgument(args[0], &value) ||
        value < 0.01 || value > 100.0) {
      ic->commandLine->logError("speed must be a number from 0.01 to 100");
      return;
    }
    ic->envVars->setVar("speedFactor", args[0]);
    ic->commandLine->logSuccess("speedFactor = " + args[0]);
  };
  ic->commandRegistry->RegisterCommand(
    "speed",
    speedCommand,
    "speed [0.01..100]",
    "Show or set the simulation speed multiplier");
  ic->commandRegistry->RegisterCommand(
    "speedfactor", speedCommand, "speedfactor [0.01..100]", "Alias for speed");

  CommandFn fadeCommand = [this](const std::vector<std::string>& args) {
    if (args.empty()) {
      ic->commandLine->logNormal("cellFadeSpeed = " +
                                 ic->envVars->getVar("cellFadeSpeed").value);
      return;
    }
    double value = 0.0;
    if (args.size() != 1 || !parseFloatingArgument(args[0], &value) ||
        value < 0.0 || value > 1000.0) {
      ic->commandLine->logError("fade must be a number from 0 to 1000");
      return;
    }
    ic->envVars->setVar("cellFadeSpeed", args[0]);
    ic->commandLine->logSuccess("cellFadeSpeed = " + args[0]);
  };
  ic->commandRegistry->RegisterCommand(
    "fade", fadeCommand, "fade [0..1000]", "Show or set cell fade speed");
  ic->commandRegistry->RegisterCommand(
    "cellfadespeed", fadeCommand, "cellfadespeed [0..1000]", "Alias for fade");

  ic->commandRegistry->RegisterCommand(
    "save",
    [this](const std::vector<std::string>& args) {
      if (args.size() != 1) {
        ic->commandLine->logError("Usage: save <filename>");
        return;
      }
      const std::string filename = withIllumoExtension(args[0]);
      if (SaveCellGame(filename)) {
        ic->commandLine->logSuccess("Saved canvas to " + filename);
      }
    },
    "save <filename>",
    "Save the current canvas; .illumo is added when omitted");

  ic->commandRegistry->RegisterCommand(
    "load",
    [this](const std::vector<std::string>& args) {
      if (args.size() != 1) {
        ic->commandLine->logError("Usage: load <filename>");
        return;
      }
      std::string filename = args[0];
      std::ifstream exactFile(filename, std::ios::binary);
      if (!exactFile.is_open()) {
        filename = withIllumoExtension(filename);
      }
      exactFile.close();
      if (LoadCellGame(filename)) {
        ic->commandLine->logSuccess("Loaded canvas from " + filename);
      }
    },
    "load <filename>",
    "Load a canvas and activate its saved ruleset");

  ic->commandRegistry->RegisterCommand(
    "save_dialog",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: save_dialog");
        return;
      }
      const SaveLoadDialogSpec dialogSpec{ "IllumoGame File Format",
                                           "MyCanvas.illumo",
                                           "*.ILLUMO" };
      const std::string selectedPath = SaveLoad::GetSaveLocation(dialogSpec);
      if (selectedPath.empty()) {
        ic->commandLine->logWarning("Save cancelled");
        return;
      }
      const std::string filename = withIllumoExtension(selectedPath);
      if (SaveCellGame(filename)) {
        ic->commandLine->logSuccess("Saved canvas to " + filename);
      }
    },
    "save_dialog",
    "Open the native save-file picker");

  ic->commandRegistry->RegisterCommand(
    "load_dialog",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: load_dialog");
        return;
      }
      const SaveLoadDialogSpec dialogSpec{ "IllumoGame File Format",
                                           "myCanvas.illumo",
                                           "*.ILLUMO" };
      const std::string filename = SaveLoad::GetLoadLocation(dialogSpec);
      if (filename.empty()) {
        ic->commandLine->logWarning("Load cancelled");
        return;
      }
      if (LoadCellGame(filename)) {
        ic->commandLine->logSuccess("Loaded canvas from " + filename);
      }
    },
    "load_dialog",
    "Open the native load-file picker");

  ic->commandRegistry->RegisterCommand(
    "pause",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: pause");
        return;
      }
      setRunning(false);
    },
    "pause",
    "Pause simulation and enter edit mode");

  ic->commandRegistry->RegisterCommand(
    "run",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: run");
        return;
      }
      setRunning(true);
    },
    "run",
    "Resume continuous simulation");

  ic->commandRegistry->RegisterCommand(
    "step",
    [this](const std::vector<std::string>& args) {
      int generations = 1;
      if ((!args.empty() && !parseIntegerArgument(args[0], &generations)) ||
          args.size() > 1 || generations < 1 || generations > 1000) {
        ic->commandLine->logError(
          "step count must be an integer from 1 to 1000");
        return;
      }
      stepSimulation(generations);
      ic->commandLine->logSuccess(
        "Advanced " + std::to_string(generations) +
        (generations == 1 ? " generation" : " generations"));
    },
    "step [count]",
    "Advance a paused canvas by one or more generations");

  ic->commandRegistry->RegisterCommand(
    "clear_canvas",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: clear_canvas");
        return;
      }
      prepareGridMutation();
      cellContext->getGrid()->clear();
      updateVisualTargets();
      cellContext->getCanvasView()->snapVisualToTargets();
      ic->commandLine->logSuccess("Canvas cleared");
    },
    "clear_canvas",
    "Set every cell to the empty state");

  ic->commandRegistry->RegisterCommand(
    "randomize",
    [this](const std::vector<std::string>& args) {
      double density = 25.0;
      if ((!args.empty() && !parseFloatingArgument(args[0], &density)) ||
          args.size() > 1 || density < 0.0 || density > 100.0) {
        ic->commandLine->logError(
          "randomize density must be a percentage from 0 to 100");
        return;
      }

      CanvasView* canvas = cellContext->getCanvasView();
      prepareGridMutation();
      canvas->syncVisibleRegion();
      std::mt19937 generator(std::random_device{}());
      std::uniform_real_distribution<double> distribution(0.0, 100.0);
      const bool wireworld = cellContext->getModeString() == "WIREWORLD";
      const CellAddress firstCell = canvas->getVisibleFirstCell();
      for (int y = 0; y < canvas->getVisibleCellHeight(); ++y) {
        for (int x = 0; x < canvas->getVisibleCellWidth(); ++x) {
          const bool selected = distribution(generator) < density;
          const unsigned char state =
            wireworld ? (selected ? WireworldRuleSet::CELL_CONDUCTOR
                                  : WireworldRuleSet::CELL_EMPTY)
                      : (selected ? static_cast<unsigned char>(0)
                                  : static_cast<unsigned char>(1));
          const CellAddress address{ firstCell.x + x, firstCell.y - y };
          canvas->setCanvasPixel(address.x, address.y, state);
        }
      }
      ic->commandLine->logSuccess("Randomized canvas at " +
                                  std::to_string(density) + "% density");
    },
    "randomize [density-percent]",
    "Fill the canvas randomly; Wireworld creates conductors");

  ic->commandRegistry->RegisterCommand(
    "setcell",
    [this](const std::vector<std::string>& args) {
      std::int64_t x = 0;
      std::int64_t y = 0;
      int state = 0;
      if (args.size() != 3 || !parseWorldCoordinate(args[0], &x) ||
          !parseWorldCoordinate(args[1], &y) ||
          !parseIntegerArgument(args[2], &state) || state < 0 || state > 255) {
        ic->commandLine->logError("Usage: setcell <x> <y> <state 0..255>");
        return;
      }
      prepareGridMutation();
      cellContext->getGrid()->setCell(CellAddress{ x, y },
                                      static_cast<unsigned char>(state));
      ic->commandLine->logSuccess("Cell (" + std::to_string(x) + ", " +
                                  std::to_string(y) +
                                  ") = " + std::to_string(state));
    },
    "setcell <x> <y> <state>",
    "Set one cell state directly, including Wireworld head/tail states");

  ic->commandRegistry->RegisterCommand(
    "camera_reset",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: camera_reset");
        return;
      }
      ic->camera->Reset();
      ic->commandLine->logSuccess("Camera reset");
    },
    "camera_reset",
    "Center the canvas and restore 1x zoom");

  ic->commandRegistry->RegisterCommand(
    "camera",
    [this](const std::vector<std::string>& args) {
      if (args.empty()) {
        glm::dvec2 position = ic->camera->GetPositionPrecise();
        ic->commandLine->logNormal(
          "Camera: x=" + std::to_string(position.x) +
          " y=" + std::to_string(position.y) +
          " zoom=" + std::to_string(ic->camera->GetZoom()));
        return;
      }
      double x = 0.0;
      double y = 0.0;
      double zoom = static_cast<double>(ic->camera->GetZoom());
      if ((args.size() != 2 && args.size() != 3) ||
          !parseFloatingArgument(args[0], &x) ||
          !parseFloatingArgument(args[1], &y) ||
          (args.size() == 3 && !parseFloatingArgument(args[2], &zoom)) ||
          zoom < 0.1 || zoom > 100.0) {
        ic->commandLine->logError("Usage: camera <x> <y> [zoom 0.1..100]");
        return;
      }
      ic->camera->SetPositionPrecise(x, y);
      ic->camera->SetZoom(static_cast<float>(zoom));
      ic->commandLine->logSuccess("Camera updated");
    },
    "camera [x y [zoom]]",
    "Show or set camera position and zoom");

  ic->commandRegistry->RegisterCommand(
    "status",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: status");
        return;
      }
      printStatus();
    },
    "status",
    "Show simulation, canvas, ruleset, and camera state");
}

void
CellGameModule::unregisterConsoleCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  const char* commandNames[] = {
    "camera",  "camera_reset", "cellfadespeed", "clear_canvas", "fade",
    "load",    "load_dialog",  "mode",          "pause",        "randomize",
    "ruleset", "run",          "save",          "save_dialog",  "setcell",
    "speed",   "speedfactor",  "status",        "step",         "tps"
  };
  for (const char* commandName : commandNames) {
    ic->commandRegistry->UnregisterCommand(commandName);
  }
}

void
CellGameModule::setRunning(bool running)
{
  prepareGridMutation();
  currentState = running ? CellState::NORMAL : CellState::EDIT;
  simAccum = 0.0;
  achievedSimulationTps = 0.0;
  lastSimulationStepMilliseconds = 0.0;
  lastSimulationFrameMilliseconds = 0.0;
  lastSimulationSteps = 0;
  simulationDebtDropped = false;
  simulationBudgetLimited = false;
  showModeSplash(running ? "NORMAL" : "EDIT");
  ic->commandLine->logSuccess(running ? "Simulation running"
                                      : "Simulation paused in edit mode");
}

void
CellGameModule::stepSimulation(int generations)
{
  prepareGridMutation();
  currentState = CellState::EDIT;
  simAccum = 0.0;
  showModeSplash("EDIT");
  for (int i = 0; i < generations; ++i) {
    cellContext->getGrid()->advance(*cellContext->getRuleSet());
  }
  updateVisualTargets();
}

static std::string
boundedRuleTag(const char* tag, std::size_t capacity)
{
  if (tag == nullptr) {
    return std::string();
  }
  std::size_t length = 0;
  while (length < capacity && tag[length] != '\0') {
    length += 1;
  }
  return std::string(tag, length);
}

void
CellGameModule::printStatus() const
{
  const CanvasView* canvas = cellContext->getCanvasView();
  const SparseAdvanceStats& simulationStats =
    cellContext->getGrid()->getLastAdvanceStats();
  const glm::vec2 cameraPosition = ic->camera->GetPosition();
  ic->commandLine->logNormal(
    std::string("State: ") +
    (currentState == CellState::NORMAL ? "RUNNING" : "PAUSED/EDIT"));
  ic->commandLine->logNormal("Ruleset: " + cellContext->getModeString());
  ic->commandLine->logNormal(
    "View: " + std::to_string(canvas->getVisibleCellWidth()) + " x " +
    std::to_string(canvas->getVisibleCellHeight()) + " cells -> " +
    std::to_string(canvas->getViewWidth()) + " x " +
    std::to_string(canvas->getViewHeight()) + " texels; chunks: " +
    std::to_string(cellContext->getGrid()->getAllocatedChunkCount()) +
    "; fading texels=" + std::to_string(canvas->getFadingTexelCount()) +
    ", last sample=" + std::to_string(canvas->getLastSampledTexelCount()) +
    ", last fade visits=" + std::to_string(canvas->getLastFadeVisitCount()) +
    ", cache=" + std::to_string(canvas->getCachedTexelWidth()) + "x" +
    std::to_string(canvas->getCachedTexelHeight()) + "@" +
    std::to_string(canvas->getCellsPerTexel()) + " cells/texel" +
    ", refills=" + std::to_string(canvas->getCacheRefillCount()));
  ic->commandLine->logNormal(
    "Rate: requested=" + std::to_string(requestedSimulationTps) + " tps (" +
    ic->envVars->getVar("tps").value + " x " +
    ic->envVars->getVar("speedFactor").value +
    "), achieved=" + std::to_string(achievedSimulationTps) + " tps");
  ic->commandLine->logNormal(
    "Simulation: active chunks=" +
    std::to_string(simulationStats.activeChunkCount) +
    ", active cells=" + std::to_string(simulationStats.activeCellCount) +
    ", counted cells=" + std::to_string(simulationStats.countedCellCount) +
    ", candidate-preferred chunks=" +
    std::to_string(simulationStats.candidatePreferredChunkCount) +
    ", targets=" + std::to_string(simulationStats.targetChunkCount) +
    " (candidate=" + std::to_string(simulationStats.candidateTargetCount) +
    ", halo=" + std::to_string(simulationStats.haloTargetCount) + ")" +
    ", candidates=" + std::to_string(simulationStats.candidateCellCount) +
    ", chunk nodes allocated=" +
    std::to_string(simulationStats.allocatedChunkNodeCount) +
    ", reused=" + std::to_string(simulationStats.reusedChunkNodeCount) +
    ", retained=" + std::to_string(simulationStats.retainedChunkNodeCount) +
    ", enrollments/growth/output=" +
    std::to_string(simulationStats.candidateEnrollmentAttemptCount) + "/" +
    std::to_string(simulationStats.candidateIndexGrowthCount) + "/" +
    std::to_string(simulationStats.producedChunkCount) + ", prep/work ranges=" +
    std::to_string(simulationStats.candidatePreparationRangeCount) + "/" +
    std::to_string(simulationStats.candidateWorkRangeCount) + ", changed=" +
    std::to_string(simulationStats.changedChunkCount) + " chunks/" +
    std::to_string(simulationStats.changedCellCount) + " cells (counted=" +
    std::to_string(simulationStats.countedChangedCellCount) + ")" +
    ", frontier targets=" +
    std::to_string(simulationStats.frontierTargetCount) +
    ", frontier sources=" +
    std::to_string(simulationStats.frontierSourceChunkCount) +
    ", estimated work=" +
    std::to_string(simulationStats.frontierEstimatedWork) + "/" +
    std::to_string(simulationStats.completeEstimatedWork) + ", prep workers=" +
    std::to_string(simulationStats.candidatePreparationWorkerCount) +
    ", workers=" + std::to_string(simulationStats.workerCount) +
    ", candidate stages=" +
    std::to_string(simulationStats.candidateDiscoveryMilliseconds) + "/" +
    std::to_string(simulationStats.candidatePreparationMilliseconds) + "/" +
    std::to_string(simulationStats.candidateEvaluationMilliseconds) + "/" +
    std::to_string(simulationStats.candidateChangeTrackingMilliseconds) + "/" +
    std::to_string(simulationStats.candidateRecycleMilliseconds) + "/" +
    std::to_string(simulationStats.candidateOutputMilliseconds) + "/" +
    std::to_string(simulationStats.candidateMergeMilliseconds) + " ms" +
    ", memo=" + std::to_string(simulationStats.memoHitCount) + "/" +
    std::to_string(simulationStats.memoProbeCount) +
    " hits, entries=" + std::to_string(simulationStats.memoEntryCount) +
    ", bytes=" + std::to_string(simulationStats.memoMemoryBytes) +
    (simulationStats.chunkMemoActive ? " active" : " adaptive") +
    ", topology=" +
    (simulationStats.reusedCandidateTopology ? "reused" : "rebuilt") +
    ", path=" +
    (simulationStats.usedChangedFrontier
       ? "frontier"
       : (simulationStats.usedMixedTargets
            ? "mixed"
            : (simulationStats.usedCellCandidates ? "cells" : "chunks"))) +
    ", steps this frame=" + std::to_string(lastSimulationSteps) +
    ", step/frame ms=" + std::to_string(lastSimulationStepMilliseconds) + "/" +
    std::to_string(lastSimulationFrameMilliseconds) +
    ", step p50/p95/max=" + std::to_string(simulationStepMetric.median()) +
    "/" + std::to_string(simulationStepMetric.p95()) + "/" +
    std::to_string(simulationStepMetric.maximum()) + " ms" +
    ", worker mirror/advance/capture=" +
    std::to_string(lastSimulationRunnerTimings.mirrorMilliseconds) + "/" +
    std::to_string(lastSimulationRunnerTimings.advanceMilliseconds) + "/" +
    std::to_string(lastSimulationRunnerTimings.captureMilliseconds) + " ms" +
    (simulationDebtDropped ? ", catch-up dropped" : ""));
  ic->commandLine->logNormal(
    "Worker stages p50/p95 ms: mirror=" +
    std::to_string(simulationMirrorMetric.median()) + "/" +
    std::to_string(simulationMirrorMetric.p95()) +
    ", advance=" + std::to_string(simulationAdvanceMetric.median()) + "/" +
    std::to_string(simulationAdvanceMetric.p95()) +
    ", capture=" + std::to_string(simulationCaptureMetric.median()) + "/" +
    std::to_string(simulationCaptureMetric.p95()));
  ic->commandLine->logNormal(
    "Presentation: refill p50/p95/max=" +
    std::to_string(canvas->getCacheRefillMetric().median()) + "/" +
    std::to_string(canvas->getCacheRefillMetric().p95()) + "/" +
    std::to_string(canvas->getCacheRefillMetric().maximum()) +
    " ms, last upload=" + std::to_string(canvas->getLastUploadByteCount()) +
    " bytes/" + std::to_string(canvas->getLastUploadRectCount()) +
    " rects, upload bytes p50/p95/max=" +
    std::to_string(canvas->getUploadByteMetric().median()) + "/" +
    std::to_string(canvas->getUploadByteMetric().p95()) + "/" +
    std::to_string(canvas->getUploadByteMetric().maximum()) +
    ", upload rects p50/p95/max=" +
    std::to_string(canvas->getUploadRectMetric().median()) + "/" +
    std::to_string(canvas->getUploadRectMetric().p95()) + "/" +
    std::to_string(canvas->getUploadRectMetric().maximum()));
  if (simulationBudgetLimited) {
    ic->commandLine->logNormal(
      "Simulation worker: due generation deferred while one is in flight");
  }
  ic->commandLine->logNormal("Camera: x=" + std::to_string(cameraPosition.x) +
                             " y=" + std::to_string(cameraPosition.y) +
                             " zoom=" + std::to_string(ic->camera->GetZoom()));
}

void
CellGameModule::syncSimRateFromEnv()
{
  long tps = ic->envVars->getVar("tps").valueAsLong;
  if (tps < 1)
    tps = 1;
  if (tps > 1000)
    tps = 1000;

  double speedFactor = ic->envVars->getVar("speedFactor").valueAsDouble;
  if (speedFactor <= 0.0)
    speedFactor = 1.0;
  if (speedFactor > 100.0)
    speedFactor = 100.0;

  const double effectiveTps = static_cast<double>(tps) * speedFactor;
  requestedSimulationTps = effectiveTps;
  simStepSeconds = 1.0 / effectiveTps;

  float fadeSpeed = 8.0f;
  if (ic->envVars->getVar("cellFadeSpeed").value != "") {
    fadeSpeed =
      static_cast<float>(ic->envVars->getVar("cellFadeSpeed").valueAsDouble);
  }
  if (fadeSpeed < 0.0f)
    fadeSpeed = 0.0f;
  cellContext->getCanvasView()->setFadeSpeed(fadeSpeed);
}

void
CellGameModule::Update(double dt)
{
  ZoneNamed(CellGameModuleUpdateZone, "CellGameModule Update");

  // Host erases modules that fail Start; still guard for incomplete fixtures.
  if (cellContext == nullptr || ic == nullptr) {
    return;
  }
  lastSimulationSteps = 0;
  lastSimulationFrameMilliseconds = 0.0;
  consumeCompletedSimulation(false);
  if (isRender3dTestEnabled() && dt > 0.0) {
    render3dTestTime += dt;
  }

  if (configurationMenu != nullptr && ic->commandLine != nullptr &&
      !ic->commandLine->isOpen &&
      ic->inputManager->isActionActive("ToggleSettings")) {
    if (configurationMenu->isOpen()) {
      configurationMenu->close();
    } else {
      drainSimulation();
      ic->inputManager->clearKeyQueue();
      ic->inputManager->clearCharQueue();
      configurationMenu->open(currentConfiguration());
    }
  }

  if (configurationMenu != nullptr && configurationMenu->isOpen()) {
    configurationMenu->tick(static_cast<float>(dt));
    const ConfigurationMenuAction action =
      configurationMenu->update(ic->inputManager);
    if (action == ConfigurationMenuAction::Cancel) {
      configurationMenu->close();
    } else if (action == ConfigurationMenuAction::Exit) {
      configurationMenu->close();
      ic->window->requestClose();
    } else if (action == ConfigurationMenuAction::Apply) {
      SimulatorConfiguration configuration;
      std::string error;
      if (!configurationMenu->readConfiguration(&configuration, &error)) {
        configurationMenu->setError(error);
      } else if (!applyConfiguration(configuration)) {
        configurationMenu->setError(
          "Settings could not be applied; the current world was preserved.");
      } else {
        configurationMenu->close();
      }
    }
    updateEditorCursor();
    updateVisualTargets();
    cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
    return;
  }

  // Apply ruleset changes from console (`ruleset SEEDS`) or env ModeString.
  {
    std::string wanted = ic->envVars->getVar("ModeString").value;
    if (!wanted.empty() && wanted != cellContext->getModeString()) {
      prepareGridMutation();
      if (cellContext->setRuleSet(wanted)) {
        std::string msg = "Active ruleset: " + cellContext->getModeString();
        Logger::LogInfo(msg.c_str());
        // Same life values, new colors → rebuild palette only (no cell
        // re-upload).
        cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
        if (cellContext->getModeString() == "WIREWORLD") {
          wireworldBrush = WireworldRuleSet::CELL_CONDUCTOR;
        }
      }
    }
  }

  // Common behavior: camera panning & scroll zoom (only when console is closed)
  if (!ic->commandLine->isOpen) {
    CameraPan();

    // Zoom behavior using scroll offset
    std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
    glm::dvec2 worldMouse = ic->camera->ScreenToWorldPrecise(
      glm::dvec2(mouseCoords[0], mouseCoords[1]));
    double* scroll = ic->inputManager->getMouseScrollOffset();
    if (*scroll != 0.0f) {
      double zoomFactor = (*scroll > 0.0f) ? 1.15 : 0.85;
      ic->camera->ZoomAt(static_cast<float>(zoomFactor), worldMouse);
    }
  }

  // Toggle between NORMAL and EDIT states with 'E' key (only when console is
  // closed)
  if (!ic->commandLine->isOpen &&
      ic->inputManager->isActionActive("ToggleState")) {
    if (currentState == CellState::NORMAL) {
      drainSimulation();
      currentState = CellState::EDIT;
      showModeSplash("EDIT");
      Logger::LogInfo("State changed to EDIT");
    } else {
      currentState = CellState::NORMAL;
      simAccum = 0.0;
      achievedSimulationTps = 0.0;
      showModeSplash("NORMAL");
      Logger::LogInfo("State changed to NORMAL");
    }
    lastSimulationSteps = 0;
    simulationDebtDropped = false;
    simulationBudgetLimited = false;
  }

  // State dependent behavior
  switch (currentState) {
    case CellState::NORMAL:
      Normal(dt);
      break;
    case CellState::EDIT:
      Edit(dt);
      break;
    case CellState::EXIT:
      Exit();
      break;
    default:
      break;
  }

  // Hide in NORMAL (and while console is open); track mouse only in EDIT.
  updateEditorCursor();

  // Map dirty life cells to palette target colors, then ease display toward
  // them.
  updateVisualTargets();
  cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
}

void
CellGameModule::Exit()
{
  drainSimulation();
  simulationRunner.shutdown();
  unregisterConsoleCommands();
  configurationMenu.reset();
  modeSplash.reset();
  render3dTestAnimated.reset();
  render3dTestStatic.reset();
  delete cellContext;
  cellContext = nullptr;
}

void
CellGameModule::Normal(double dt)
{
  syncSimRateFromEnv();
  if (dt < 0.0) {
    dt = 0.0;
  }
  if (dt > 0.25) {
    dt = 0.25;
  }

  simAccum += dt;
  simulationBudgetLimited = false;
  if (simAccum >= simStepSeconds && !simulationRunner.isBusy()) {
    SparseGenerationDelta transferDelta;
    const bool useMirrorDelta = mirrorDeltaValid;
    if (useMirrorDelta) {
      transferDelta = std::move(mirrorDelta);
    }
    if (simulationRunner.start(cellContext->getSpareGrid(),
                               cellContext->getGrid(),
                               cellContext->getRuleSet(),
                               std::move(transferDelta),
                               useMirrorDelta)) {
      mirrorDeltaValid = false;
      simAccum -= simStepSeconds;
    }
  } else if (simAccum >= simStepSeconds) {
    FrameMarkNamed("Sim.inFlightDeferred");
    simulationBudgetLimited = true;
  }

  if (dt > 0.0) {
    const double instantaneousTps =
      static_cast<double>(lastSimulationSteps) / dt;
    const double blend = 1.0 - std::exp(-dt * 2.0);
    if (achievedSimulationTps == 0.0 && lastSimulationSteps > 0) {
      achievedSimulationTps = instantaneousTps;
    } else {
      achievedSimulationTps +=
        (instantaneousTps - achievedSimulationTps) * blend;
    }
  }

  simulationDebtDropped = false;
  if (simAccum >= simStepSeconds) {
    FrameMarkNamed("Sim.debtDropped");
    simAccum = std::fmod(simAccum, simStepSeconds);
    simulationDebtDropped = true;
  }
}

void
CellGameModule::Edit(double dt)
{
  (void)dt;
  static bool wasPressed = false;
  static std::int64_t lastMouseX = 0;
  static std::int64_t lastMouseY = 0;

  std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
  glm::dvec2 worldPos = ic->camera->ScreenToWorldPrecise(
    glm::dvec2(mouseCoords[0], mouseCoords[1]));

  const std::int64_t currentX = CanvasView::worldToCell(worldPos.x);
  const std::int64_t currentY = CanvasView::worldToCell(worldPos.y);

  if (!ic->commandLine->isOpen) {
    if (cellContext->getModeString() == "WIREWORLD") {
      updateWireworldBrushFromInput();
    }

    bool isLeftPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    bool isRightPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);

    const bool pointerInWorld = cellContext->getGrid()->isCellInWorldBounds(
      CellAddress{ currentX, currentY });
    if ((isLeftPressed || isRightPressed) && pointerInWorld) {
      mirrorDeltaValid = false;
      // Binary CAs: left = alive (0), right = dead (1).
      // Wireworld: left = active brush (1/H head, 3/T tail, 4 conductor),
      // right = empty.
      unsigned char colorVal = isLeftPressed ? 0 : 1;
      if (cellContext->getModeString() == "WIREWORLD") {
        colorVal =
          isLeftPressed ? wireworldBrush : WireworldRuleSet::CELL_EMPTY;
      }

      if (wasPressed) {
        std::int64_t x0 = lastMouseX;
        std::int64_t y0 = lastMouseY;
        const std::int64_t x1 = currentX;
        const std::int64_t y1 = currentY;
        const std::int64_t dx = std::llabs(x1 - x0);
        const std::int64_t dy = std::llabs(y1 - y0);
        const std::int64_t sx = (x0 < x1) ? 1 : -1;
        const std::int64_t sy = (y0 < y1) ? 1 : -1;
        std::int64_t err = dx - dy;

        while (true) {
          this->cellContext->getCanvasView()->setCanvasPixel(x0, y0, colorVal);
          if (x0 == x1 && y0 == y1)
            break;
          const std::int64_t e2 = 2 * err;
          if (e2 > -dy) {
            err -= dy;
            x0 += sx;
          }
          if (e2 < dx) {
            err += dx;
            y0 += sy;
          }
        }
      } else {
        this->cellContext->getCanvasView()->setCanvasPixel(
          currentX, currentY, colorVal);
      }
      wasPressed = true;
      lastMouseX = currentX;
      lastMouseY = currentY;
    } else {
      wasPressed = false;
      if (ic->inputManager->isKeyPressed(KeyCode::C)) {
        mirrorDeltaValid = false;
        this->cellContext->getGrid()->clear();
      }
    }
  } else {
    wasPressed = false;
  }
}

void
CellGameModule::updateEditorCursor()
{
  if (!ic || !ic->window || !ic->camera || !cellContext ||
      !cellContext->getCanvasView()) {
    editorCursor.setVisible(false);
    return;
  }

  const bool canShow =
    (currentState == CellState::EDIT) &&
    (ic->commandLine == nullptr || !ic->commandLine->isOpen) &&
    (configurationMenu == nullptr || !configurationMenu->isOpen());
  if (!canShow) {
    editorCursor.setVisible(false);
    return;
  }

  std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
  glm::dvec2 worldPos = ic->camera->ScreenToWorldPrecise(
    glm::dvec2(mouseCoords[0], mouseCoords[1]));
  const std::int64_t cellX = CanvasView::worldToCell(worldPos.x);
  const std::int64_t cellY = CanvasView::worldToCell(worldPos.y);
  const bool pointerInWorld =
    cellContext->getGrid()->isCellInWorldBounds(CellAddress{ cellX, cellY });
  editorCursor.setVisible(pointerInWorld);
  if (!pointerInWorld) {
    return;
  }
  editorCursor.setCellSize(16.0f);
  editorCursor.setFromCell(cellX, cellY);
}

bool
CellGameModule::SaveCellGame(std::string filename)
{
  if (filename.empty()) {
    ic->commandLine->logError("Save path is empty");
    return false;
  }
  drainSimulation();

  std::ofstream file(filename, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    ic->commandLine->logError("Failed to open for saving: " + filename);
    return false;
  }

  const char magic[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '3', '\0' };
  const std::uint32_t version = 3;
  char ruleTag[MAX_RULETAG_SIZE] = {};
  const std::string activeTag = cellContext->getRuleSet()->getRuleTag();
  const std::size_t tagBytes =
    std::min(activeTag.size(), static_cast<std::size_t>(MAX_RULETAG_SIZE - 1));
  std::memcpy(ruleTag, activeTag.data(), tagBytes);

  const glm::dvec2 cameraPosition = ic->camera->GetPositionPrecise();
  const double cameraX = cameraPosition.x;
  const double cameraY = cameraPosition.y;
  const double cameraZoom = static_cast<double>(ic->camera->GetZoom());
  const std::int64_t worldChunkWidth = cellContext->getWorldChunkWidth();
  const std::int64_t worldChunkHeight = cellContext->getWorldChunkHeight();
  const std::vector<SparseChunkRecord> records =
    cellContext->getGrid()->collectChunkRecords();
  const std::uint64_t chunkCount = static_cast<std::uint64_t>(records.size());

  file.write(magic, sizeof(magic));
  file.write(reinterpret_cast<const char*>(&version), sizeof(version));
  file.write(ruleTag, sizeof(ruleTag));
  file.write(reinterpret_cast<const char*>(&cameraX), sizeof(cameraX));
  file.write(reinterpret_cast<const char*>(&cameraY), sizeof(cameraY));
  file.write(reinterpret_cast<const char*>(&cameraZoom), sizeof(cameraZoom));
  file.write(reinterpret_cast<const char*>(&worldChunkWidth),
             sizeof(worldChunkWidth));
  file.write(reinterpret_cast<const char*>(&worldChunkHeight),
             sizeof(worldChunkHeight));
  file.write(reinterpret_cast<const char*>(&chunkCount), sizeof(chunkCount));
  for (const SparseChunkRecord& record : records) {
    file.write(reinterpret_cast<const char*>(&record.chunkX),
               sizeof(record.chunkX));
    file.write(reinterpret_cast<const char*>(&record.chunkY),
               sizeof(record.chunkY));
    file.write(reinterpret_cast<const char*>(record.cells.data()),
               static_cast<std::streamsize>(record.cells.size()));
  }
  const bool succeeded = file.good();
  file.close();
  if (!succeeded) {
    ic->commandLine->logError("Failed while writing: " + filename);
  }
  return succeeded;
}

bool
CellGameModule::LoadCellGame(std::string filename)
{
  if (filename.empty()) {
    ic->commandLine->logError("Load path is empty");
    return false;
  }

  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    ic->commandLine->logError("Failed to open for loading: " + filename);
    return false;
  }

  const char expectedMagicV3[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '3', '\0' };
  const char expectedMagicV2[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '2', '\0' };
  char magic[sizeof(expectedMagicV3)] = {};
  if (!file.read(magic, sizeof(magic))) {
    ic->commandLine->logError("Invalid or truncated Illumo save header");
    return false;
  }

  std::string ruleString;
  std::unique_ptr<SparseCellGrid> loadedGrid =
    std::make_unique<SparseCellGrid>();
  std::int64_t loadedWorldChunkWidth = 0;
  std::int64_t loadedWorldChunkHeight = 0;
  bool restoreCamera = false;
  double savedCameraX = 0.0;
  double savedCameraY = 0.0;
  double savedCameraZoom = 1.0;

  const bool sparseV3 = std::memcmp(magic, expectedMagicV3, sizeof(magic)) == 0;
  const bool sparseV2 = std::memcmp(magic, expectedMagicV2, sizeof(magic)) == 0;
  if (sparseV3 || sparseV2) {
    std::uint32_t version = 0;
    char ruleTag[MAX_RULETAG_SIZE] = {};
    std::uint64_t chunkCount = 0;
    if (!file.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
        !file.read(ruleTag, sizeof(ruleTag)) ||
        !file.read(reinterpret_cast<char*>(&savedCameraX),
                   sizeof(savedCameraX)) ||
        !file.read(reinterpret_cast<char*>(&savedCameraY),
                   sizeof(savedCameraY)) ||
        !file.read(reinterpret_cast<char*>(&savedCameraZoom),
                   sizeof(savedCameraZoom))) {
      ic->commandLine->logError("Invalid or truncated sparse save header");
      return false;
    }
    if (sparseV3 &&
        (!file.read(reinterpret_cast<char*>(&loadedWorldChunkWidth),
                    sizeof(loadedWorldChunkWidth)) ||
         !file.read(reinterpret_cast<char*>(&loadedWorldChunkHeight),
                    sizeof(loadedWorldChunkHeight)))) {
      ic->commandLine->logError("Invalid or truncated topology metadata");
      return false;
    }
    if (!file.read(reinterpret_cast<char*>(&chunkCount), sizeof(chunkCount))) {
      ic->commandLine->logError("Invalid or truncated sparse save header");
      return false;
    }
    const std::uint32_t expectedVersion = sparseV3 ? 3u : 2u;
    if (version != expectedVersion || chunkCount > 10000000ULL ||
        !std::isfinite(savedCameraX) || !std::isfinite(savedCameraY) ||
        !std::isfinite(savedCameraZoom) || savedCameraZoom < 0.1 ||
        savedCameraZoom > 100.0 ||
        !SparseCellGrid::isValidTopology(loadedWorldChunkWidth,
                                         loadedWorldChunkHeight)) {
      ic->commandLine->logError("Sparse save contains invalid metadata");
      return false;
    }
    loadedGrid = std::make_unique<SparseCellGrid>(loadedWorldChunkWidth,
                                                  loadedWorldChunkHeight);
    ruleString = boundedRuleTag(ruleTag, sizeof(ruleTag));
    bool havePreviousChunk = false;
    std::int64_t previousChunkX = 0;
    std::int64_t previousChunkY = 0;
    for (std::uint64_t i = 0; i < chunkCount; ++i) {
      SparseChunkRecord record{};
      if (!file.read(reinterpret_cast<char*>(&record.chunkX),
                     sizeof(record.chunkX)) ||
          !file.read(reinterpret_cast<char*>(&record.chunkY),
                     sizeof(record.chunkY)) ||
          !file.read(reinterpret_cast<char*>(record.cells.data()),
                     static_cast<std::streamsize>(record.cells.size()))) {
        ic->commandLine->logError("Sparse save is truncated");
        return false;
      }
      bool hasCell = false;
      for (unsigned char state : record.cells) {
        if (state != SparseCellGrid::BackgroundState) {
          hasCell = true;
          break;
        }
      }
      if (!hasCell) {
        ic->commandLine->logError("Sparse save contains an empty chunk");
        return false;
      }
      if (havePreviousChunk && (record.chunkY < previousChunkY ||
                                (record.chunkY == previousChunkY &&
                                 record.chunkX <= previousChunkX))) {
        ic->commandLine->logError(
          "Sparse save chunk records are not strictly sorted");
        return false;
      }
      havePreviousChunk = true;
      previousChunkX = record.chunkX;
      previousChunkY = record.chunkY;
      if (!loadedGrid->assignChunk(record)) {
        ic->commandLine->logError("Sparse save contains an invalid chunk");
        return false;
      }
    }
    restoreCamera = true;
  } else {
    file.clear();
    file.seekg(0, std::ios::beg);
    char legacyRuleTag[MAX_RULETAG_SIZE] = {};
    int fileWidth = 0;
    int fileHeight = 0;
    if (!file.read(legacyRuleTag, sizeof(legacyRuleTag)) ||
        !file.read(reinterpret_cast<char*>(&fileWidth), sizeof(fileWidth)) ||
        !file.read(reinterpret_cast<char*>(&fileHeight), sizeof(fileHeight))) {
      ic->commandLine->logError("Invalid or truncated legacy save header");
      return false;
    }
    ruleString = boundedRuleTag(legacyRuleTag, sizeof(legacyRuleTag));
    const long long fileCellCount =
      static_cast<long long>(fileWidth) * static_cast<long long>(fileHeight);
    if (fileWidth < 1 || fileHeight < 1 || fileCellCount < 1 ||
        fileCellCount > 100000000LL) {
      ic->commandLine->logError("Legacy save contains invalid dimensions");
      return false;
    }
    const std::size_t cellBytes = static_cast<std::size_t>(fileCellCount);
    std::vector<unsigned char> loadedCells;
    try {
      loadedCells.resize(cellBytes);
    } catch (const std::bad_alloc&) {
      ic->commandLine->logError("Legacy save is too large to load");
      return false;
    }
    if (!file.read(reinterpret_cast<char*>(loadedCells.data()),
                   static_cast<std::streamsize>(cellBytes))) {
      ic->commandLine->logError("Legacy save is truncated");
      return false;
    }
    const std::int64_t originX = static_cast<std::int64_t>(fileWidth / 2);
    const std::int64_t originY = static_cast<std::int64_t>(fileHeight / 2);
    for (int y = 0; y < fileHeight; ++y) {
      for (int x = 0; x < fileWidth; ++x) {
        const unsigned char state =
          loadedCells[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(fileWidth) +
                      static_cast<std::size_t>(x)];
        if (state != SparseCellGrid::BackgroundState) {
          loadedGrid->setCell(
            CellAddress{ static_cast<std::int64_t>(x) - originX,
                         static_cast<std::int64_t>(y) - originY },
            state);
        }
      }
    }
  }

  if (!CellContext::IsKnownModeString(ruleString)) {
    ic->commandLine->logError("Save uses unsupported ruleset: " + ruleString);
    return false;
  }

  // All parsing and allocation above completed against temporary state.
  prepareGridMutation();
  if ((cellContext->getWorldChunkWidth() != loadedWorldChunkWidth ||
       cellContext->getWorldChunkHeight() != loadedWorldChunkHeight) &&
      !cellContext->resetWorld(loadedWorldChunkWidth, loadedWorldChunkHeight)) {
    ic->commandLine->logError("Unable to allocate the saved world topology");
    return false;
  }
  cellContext->setRuleSet(ruleString);
  cellContext->getGrid()->swap(*loadedGrid);
  cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
  if (restoreCamera) {
    ic->camera->SetPositionPrecise(savedCameraX, savedCameraY);
    ic->camera->SetZoom(static_cast<float>(savedCameraZoom));
  } else {
    ic->camera->Reset();
  }
  updateVisualTargets();
  cellContext->getCanvasView()->snapVisualToTargets();
  return true;
}

void
CellGameModule::CameraPan()
{
  std::array<double, 2> mousePos = ic->inputManager->getMousePosition();
  glm::dvec2 worldMouse =
    ic->camera->ScreenToWorldPrecise(glm::dvec2(mousePos[0], mousePos[1]));
  static glm::dvec2 lastMousePos = worldMouse;
  static bool wasPressed = false;

  if (ic->inputManager->isMouseButtonPressed(KeyCode::MouseMiddle)) {
    if (!wasPressed) {
      lastMousePos = worldMouse;
      wasPressed = true;
    }
    glm::dvec2 delta = lastMousePos - worldMouse;
    ic->camera->Pan(delta * static_cast<double>(ic->camera->GetZoom()));
    worldMouse =
      ic->camera->ScreenToWorldPrecise(glm::dvec2(mousePos[0], mousePos[1]));
  } else {
    wasPressed = false;
  }
  lastMousePos = worldMouse;
}

void
CellGameModule::CameraRotate()
{
}

bool
CellGameModule::isRender3dTestEnabled() const
{
  return ic != nullptr && ic->envVars != nullptr &&
         ic->envVars->getVar("render3dTest").valueAsBool;
}

void
CellGameModule::ensureRender3dTestDrawables()
{
  if (ic == nullptr || ic->renderer == nullptr ||
      (render3dTestStatic != nullptr && render3dTestAnimated != nullptr)) {
    return;
  }

  render3dTestStatic = std::make_unique<DebugDraw3D>();
  render3dTestStatic->prepare(ic->renderer);
  render3dTestStatic->addAxes(glm::vec3(0.0f), 3.0f);
  render3dTestStatic->addGrid(10, 1.0f, ColorRgba{ 72, 96, 128, 255 });

  render3dTestAnimated = std::make_unique<DebugDraw3D>();
  render3dTestAnimated->prepare(ic->renderer);
  render3dTestAnimated->addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.75f), ColorRgba{ 255, 180, 72, 255 });
  render3dTestAnimated->addWireCube(
    glm::vec3(0.0f), glm::vec3(1.0f), ColorRgba{ 224, 244, 255, 255 });
}

void
CellGameModule::updateRender3dTestMatrices()
{
  if (ic == nullptr || ic->window == nullptr || render3dTestStatic == nullptr ||
      render3dTestAnimated == nullptr) {
    return;
  }
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const float aspectRatio = static_cast<float>(dimensions[0]) /
                            static_cast<float>(std::max(dimensions[1], 1));
  const glm::mat4 viewProjection =
    DebugDraw3D::makePerspectiveViewProjection(glm::vec3(12.0f, 9.0f, 12.0f),
                                               glm::vec3(0.0f),
                                               glm::vec3(0.0f, 1.0f, 0.0f),
                                               55.0f,
                                               aspectRatio,
                                               0.1f,
                                               100.0f);
  render3dTestStatic->setViewProjection(viewProjection);
  render3dTestStatic->setModelMatrix(glm::mat4(1.0f));

  const float elapsed = static_cast<float>(render3dTestTime);
  glm::mat4 model =
    glm::translate(glm::mat4(1.0f),
                   glm::vec3(std::sin(elapsed) * 4.0f,
                             1.5f + std::sin(elapsed * 1.7f) * 0.75f,
                             std::cos(elapsed) * 4.0f));
  model = glm::rotate(model, elapsed * 1.4f, glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, elapsed * 0.8f, glm::vec3(1.0f, 0.0f, 0.0f));
  render3dTestAnimated->setViewProjection(viewProjection);
  render3dTestAnimated->setModelMatrix(model);
}

void
CellGameModule::DispatchDrawables(Scene* scene)
{
  if (cellContext == nullptr || scene == nullptr) {
    return;
  }
  // Owners implement AppendCommands (domain + GameVisual). Scene lists
  // Drawable hosts by layer (World → UI → Debug). The opt-in diagnostic scene
  // replaces CanvasView so its depth-tested primitives start from a clear
  // depth buffer rather than inheriting 2D presentation writes.
  if (isRender3dTestEnabled()) {
    ensureRender3dTestDrawables();
    updateRender3dTestMatrices();
    if (render3dTestStatic != nullptr) {
      scene->AddDrawable(render3dTestStatic.get(), RenderLayerId::World);
    }
    if (render3dTestAnimated != nullptr) {
      scene->AddDrawable(render3dTestAnimated.get(), RenderLayerId::World);
    }
  } else {
    scene->AddDrawable(this->cellContext->getCanvasView(),
                       RenderLayerId::World);
  }
  if (editorCursor.isVisible()) {
    scene->AddDrawable(&editorCursor, RenderLayerId::UI);
  }
  if (modeSplash != nullptr && modeSplash->isVisible()) {
    scene->AddDrawable(modeSplash.get(), RenderLayerId::UI);
  }
  if (configurationMenu != nullptr && configurationMenu->isOpen()) {
    scene->AddDrawable(configurationMenu.get(), RenderLayerId::UI);
  }
}
