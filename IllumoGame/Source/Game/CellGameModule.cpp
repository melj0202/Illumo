#include "CellGameModule.h"
#include "BuiltinPatterns.h"
#include "IllumoCodec.h"
#include "MainMenuModule.h"
#include "PatternCodec.h"
#include "Rulesets/WireworldRuleSet.h"
#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Platform/Clipboard.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Services/InputManager.h>
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
#include <queue>
#include <random>
#include <sstream>
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

static std::string
joinArguments(const std::vector<std::string>& args, std::size_t startIndex)
{
  std::string joined;
  for (std::size_t i = startIndex; i < args.size(); ++i) {
    if (!joined.empty()) {
      joined.push_back(' ');
    }
    joined += args[i];
  }
  return joined;
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

CellGameModule::CellGameModule(std::string initialSavePath)
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
  , exitConfirmDialog(nullptr)
  , render3dTestStatic(nullptr)
  , render3dTestAnimated(nullptr)
  , render3dTestTime(0.0)
  , render3dCameraApplied(false)
  , clipboard()
  , hoverX(0)
  , hoverY(0)
  , hoverValid(false)
  , hamburgerX(0.0f)
  , hamburgerY(0.0f)
  , hamburgerSize(32.0f)
  , hamburgerHovered(false)
  , hamburgerMouseWasDown(false)
  , inspectorEnabled(false)
  , simulationGeneration(0)
  , copyHeld(false)
  , cutHeld(false)
  , pasteHeld(false)
  , rotateHeld(false)
  , flipHeld(false)
  , inspectHeld(false)
  , deleteHeld(false)
  , initialSaveFile(std::move(initialSavePath))
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

  if (!initialSaveFile.empty()) {
    LoadCellGame(initialSaveFile);
  } else {
    // Ruleset-aware startup seed (GoL glider, Wireworld electron-on-wire, …).
    seedInitialPattern();
  }

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

  selectionVisual.setRenderer(ic->renderer);
  selectionVisual.setWindow(ic->window);
  selectionVisual.setCamera(ic->camera);
  selectionVisual.setSpace(PrimitiveSpace::World);
  selectionVisual.setLayerHint(RenderLayerId::UI);
  selectionVisual.setVisible(false);
  if (ic->renderer != nullptr) {
    selectionVisual.prepare(ic->renderer);
  }

  inspectorVisual.setRenderer(ic->renderer);
  inspectorVisual.setWindow(ic->window);
  inspectorVisual.setSpace(PrimitiveSpace::Pixels);
  inspectorVisual.setLayerHint(RenderLayerId::UI);
  inspectorVisual.setVisible(false);
  if (ic->renderer != nullptr) {
    inspectorVisual.prepare(ic->renderer);
  }

  hamburgerVisual.setRenderer(ic->renderer);
  hamburgerVisual.setWindow(ic->window);
  hamburgerVisual.setSpace(PrimitiveSpace::Pixels);
  hamburgerVisual.setLayerHint(RenderLayerId::UI);
  hamburgerVisual.setVisible(false);
  if (ic->renderer != nullptr) {
    hamburgerVisual.prepare(ic->renderer);
  }

  configurationMenu =
    std::make_unique<ConfigurationMenu>(ic->window, ic->renderer);
  exitConfirmDialog =
    std::make_unique<ExitConfirmDialog>(ic->window, ic->renderer);

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

  if (cellContext->getModeString() == "RULE_90" ||
      cellContext->getModeString() == "RULE_184") {
    grid->setCell(CellAddress{ 0, 0 }, 0);
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
  simulationGeneration += 1;
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
  const EnvVar& uiScaleVar = ic->envVars->getVar("uiScale");
  configuration.uiScale = uiScaleVar.value.empty() ? 1 : uiScaleVar.valueAsLong;
  if (configuration.uiScale < 1 || configuration.uiScale > 8) {
    configuration.uiScale = 1;
  }
  const EnvVar& msaaVar = ic->envVars->getVar("msaa");
  configuration.msaa = msaaVar.value.empty() ? 4 : msaaVar.valueAsLong;
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
      configuration.fadeSpeed < 0.0 || configuration.fadeSpeed > 100.0 ||
      configuration.uiScale < 1 || configuration.uiScale > 8) {
    return false;
  }

  const bool topologyChanged =
    configuration.worldChunkWidth != cellContext->getWorldChunkWidth() ||
    configuration.worldChunkHeight != cellContext->getWorldChunkHeight();
  const bool rulesetChanged =
    configuration.ruleSet != cellContext->getModeString();
  const bool fullscreenChanged =
    configuration.fullscreen != ic->envVars->getVar("fullscreen").valueAsBool;
  const bool msaaChanged =
    configuration.msaa != (ic->envVars->getVar("msaa").value.empty()
                             ? 4
                             : ic->envVars->getVar("msaa").valueAsLong);

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
  ic->envVars->setVar("uiScale", configuration.uiScale);
  ic->envVars->setVar("msaa", configuration.msaa);

  if (msaaChanged && ic->commandLine != nullptr) {
    ic->commandLine->logWarning(
      "Note: Restart IllumoGame for Anti-Aliasing (MSAA) changes to take "
      "effect.");
  }

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
      simulationGeneration = 0;
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

  ic->commandRegistry->RegisterCommand(
    "select",
    [this](const std::vector<std::string>& args) {
      if (args.size() == 1 && args[0] == "clear") {
        clipboard.clearSelection();
        ic->commandLine->logSuccess("Selection cleared");
        return;
      }
      std::int64_t x0 = 0;
      std::int64_t y0 = 0;
      std::int64_t x1 = 0;
      std::int64_t y1 = 0;
      if (args.size() != 4 || !parseWorldCoordinate(args[0], &x0) ||
          !parseWorldCoordinate(args[1], &y0) ||
          !parseWorldCoordinate(args[2], &x1) ||
          !parseWorldCoordinate(args[3], &y1)) {
        ic->commandLine->logError(
          "Usage: select <x0> <y0> <x1> <y1> | select clear");
        return;
      }
      clipboard.setSelection(x0, y0, x1, y1);
      ic->commandLine->logSuccess("Selection updated");
    },
    "select <x0> <y0> <x1> <y1> | select clear",
    "Set or clear the editor cell rectangle");

  ic->commandRegistry->RegisterCommand(
    "copy",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: copy");
        return;
      }
      if (!copySelection()) {
        ic->commandLine->logError("Copy failed");
      } else {
        ic->commandLine->logSuccess("Copied selection");
      }
    },
    "copy",
    "Copy the selection into the pattern buffer");

  ic->commandRegistry->RegisterCommand(
    "cut",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: cut");
        return;
      }
      if (!cutSelection()) {
        ic->commandLine->logError("Cut failed");
      } else {
        ic->commandLine->logSuccess("Cut selection");
      }
    },
    "cut",
    "Copy the selection and fill it with background");

  ic->commandRegistry->RegisterCommand(
    "paste",
    [this](const std::vector<std::string>& args) {
      std::int64_t originX = hoverX;
      std::int64_t originY = hoverY;
      if (args.size() == 2) {
        if (!parseWorldCoordinate(args[0], &originX) ||
            !parseWorldCoordinate(args[1], &originY)) {
          ic->commandLine->logError("Usage: paste [x y]");
          return;
        }
      } else if (!args.empty()) {
        ic->commandLine->logError("Usage: paste [x y]");
        return;
      }
      std::string error;
      if (!pastePatternAt(clipboard.getClipboardPattern(), originX, originY, &error)) {
        ic->commandLine->logError(error.empty() ? "Paste failed" : error);
      } else {
        ic->commandLine->logSuccess("Pasted pattern");
      }
    },
    "paste [x y]",
    "Paste the pattern buffer at the cursor or coordinates");

  ic->commandRegistry->RegisterCommand(
    "stamp",
    [this](const std::vector<std::string>& args) {
      if (args.size() != 1) {
        ic->commandLine->logError("Usage: stamp <name>");
        return;
      }
      if (!stampNamed(args[0])) {
        ic->commandLine->logError("Unknown stamp '" + args[0] + "'");
      } else {
        ic->commandLine->logSuccess("Stamped " + args[0]);
      }
    },
    "stamp <name>",
    "Paste a built-in pattern at the cursor",
    BuiltinPatterns::names());

  ic->commandRegistry->RegisterCommand(
    "rle",
    [this](const std::vector<std::string>& args) {
      if (args.empty()) {
        CellPattern pattern;
        std::string error;
        if (!captureSelection(&pattern, &error)) {
          ic->commandLine->logError(error.empty() ? "RLE export failed"
                                                  : error);
          return;
        }
        ic->commandLine->logNormal(PatternCodec::encodeRle(pattern));
        return;
      }
      if (!importPatternText(joinArguments(args, 0))) {
        ic->commandLine->logError("RLE import failed");
      } else {
        ic->commandLine->logSuccess("Imported RLE");
      }
    },
    "rle [pattern]",
    "Export the selection as RLE or import RLE text");

  ic->commandRegistry->RegisterCommand(
    "plaintext",
    [this](const std::vector<std::string>& args) {
      if (args.empty()) {
        CellPattern pattern;
        std::string error;
        if (!captureSelection(&pattern, &error)) {
          ic->commandLine->logError(error.empty() ? "Plaintext export failed"
                                                  : error);
          return;
        }
        ic->commandLine->logNormal(PatternCodec::encodePlaintext(pattern));
        return;
      }
      if (!importPatternText(joinArguments(args, 0))) {
        ic->commandLine->logError("Plaintext import failed");
      } else {
        ic->commandLine->logSuccess("Imported plaintext");
      }
    },
    "plaintext [pattern]",
    "Export the selection as Life 1.0 plaintext or import it");

  ic->commandRegistry->RegisterCommand(
    "inspect",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: inspect");
        return;
      }
      inspectorEnabled = !inspectorEnabled;
      ic->envVars->setVar("showInspector", inspectorEnabled);
      ic->commandLine->logSuccess(inspectorEnabled ? "Inspector shown"
                                                   : "Inspector hidden");
    },
    "inspect",
    "Toggle the census inspector HUD");

  ic->commandRegistry->RegisterCommand(
    "menu",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        ic->commandLine->logError("Usage: menu");
        return;
      }
      if (ic->moduleHost != nullptr) {
        ic->moduleHost->RequestTransition(std::make_unique<MainMenuModule>());
      }
    },
    "menu",
    "Return to the Main Menu");
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
    "speed",   "speedfactor",  "status",        "step",         "tps",
    "select",  "copy",         "cut",           "paste",        "stamp",
    "rle",     "plaintext",    "inspect",       "menu"
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
    simulationGeneration += 1;
  }
  updateVisualTargets();
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
  ic->commandLine->logNormal("Generation: " +
                             std::to_string(simulationGeneration));
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

static bool
consumeKeyPress(InputManager* inputManager, KeyCode key)
{
  if (inputManager == nullptr) {
    return false;
  }
  std::queue<InputManager::KeyPressEvent>& keyQueue =
    inputManager->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> remaining;
  bool consumed = false;
  while (!keyQueue.empty()) {
    const InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();
    if (event.key == key && (event.action == InputAction::Press ||
                             event.action == InputAction::Hold)) {
      consumed = true;
      continue;
    }
    remaining.push(event);
  }
  keyQueue.swap(remaining);
  return consumed;
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

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool exitConfirmOpen =
    exitConfirmDialog != nullptr && exitConfirmDialog->isOpen();

  const bool mouseLeftDown =
    ic->inputManager != nullptr &&
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool hamburgerClicked = !consoleOpen && !exitConfirmOpen &&
                                hamburgerHovered && mouseLeftDown &&
                                !hamburgerMouseWasDown;
  hamburgerMouseWasDown = mouseLeftDown;

  if (!consoleOpen && !exitConfirmOpen && configurationMenu != nullptr &&
      (ic->inputManager->isActionActive("ToggleSettings") ||
       hamburgerClicked)) {
    toggleSettingsMenu();
  }

  if (exitConfirmOpen) {
    exitConfirmDialog->tick(static_cast<float>(dt));
    const ExitConfirmAction action =
      consoleOpen ? ExitConfirmAction::None
                  : exitConfirmDialog->update(ic->inputManager);
    if (action == ExitConfirmAction::Confirm) {
      exitConfirmDialog->close();
      ic->window->requestClose();
    } else if (action == ExitConfirmAction::MainMenu) {
      exitConfirmDialog->close();
      if (ic->moduleHost != nullptr) {
        ic->moduleHost->RequestTransition(std::make_unique<MainMenuModule>());
      }
    } else if (action == ExitConfirmAction::Cancel) {
      exitConfirmDialog->close();
    }
    updateEditorCursor();
    updateHamburgerVisual(dt);
    updateSelectionVisual();
    updateInspectorVisual();
    updateVisualTargets();
    cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
    return;
  }

  if (configurationMenu != nullptr && configurationMenu->isOpen()) {
    configurationMenu->tick(static_cast<float>(dt));
    const ConfigurationMenuAction action =
      consoleOpen ? ConfigurationMenuAction::None
                  : configurationMenu->update(ic->inputManager);
    if (action == ConfigurationMenuAction::Cancel) {
      configurationMenu->close();
    } else if (action == ConfigurationMenuAction::Exit) {
      if (exitConfirmDialog != nullptr) {
        ic->inputManager->clearCharQueue();
        exitConfirmDialog->open();
        exitConfirmDialog->tick(static_cast<float>(dt));
      } else {
        configurationMenu->close();
        ic->window->requestClose();
      }
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
    updateHamburgerVisual(dt);
    updateSelectionVisual();
    updateInspectorVisual();
    updateVisualTargets();
    cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
    return;
  }

  if (!consoleOpen && consumeKeyPress(ic->inputManager, KeyCode::Q)) {
    if (exitConfirmDialog != nullptr) {
      ic->inputManager->clearCharQueue();
      exitConfirmDialog->open();
      exitConfirmDialog->tick(static_cast<float>(dt));
    } else if (ic->window != nullptr) {
      ic->window->requestClose();
    }
    updateEditorCursor();
    updateHamburgerVisual(dt);
    updateSelectionVisual();
    updateInspectorVisual();
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
  if (ic->commandLine == nullptr || !ic->commandLine->isOpen) {
    handleEditorHotkeys();
  }
  updateEditorCursor();
  updateHamburgerVisual(dt);
  updateSelectionVisual();
  updateInspectorVisual();

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
  restoreRender3dTestCamera();
  unregisterConsoleCommands();
  exitConfirmDialog.reset();
  configurationMenu.reset();
  modeSplash.reset();
  render3dSceneGraph.clear();
  render3dRootNode = SceneNodeHandle{};
  render3dOrbitNode = SceneNodeHandle{};
  render3dChildNode = SceneNodeHandle{};
  render3dTestChild.reset();
  render3dTestAnimated.reset();
  render3dTestStatic.reset();
  hamburgerVisual.clearPrimitives();
  hamburgerVisual.setVisible(false);
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
CellGameModule::normalizeSelection(std::int64_t* x0,
                                   std::int64_t* y0,
                                   std::int64_t* x1,
                                   std::int64_t* y1) const
{
  CellClipboard::normalizeSelection(x0, y0, x1, y1);
}

bool
CellGameModule::captureSelection(CellPattern* pattern, std::string* error)
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr) {
    if (error != nullptr) {
      *error = "Grid unavailable";
    }
    return false;
  }
  return clipboard.captureSelection(cellContext->getGrid(), pattern, error);
}

bool
CellGameModule::pastePatternAt(const CellPattern& pattern,
                               std::int64_t originX,
                               std::int64_t originY,
                               std::string* error)
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    if (error != nullptr) {
      *error = "Grid or canvas unavailable";
    }
    return false;
  }
  prepareGridMutation();
  const bool result = clipboard.pastePatternAt(cellContext->getGrid(),
                                               cellContext->getCanvasView(),
                                               pattern,
                                               originX,
                                               originY,
                                               error);
  if (result) {
    updateVisualTargets();
  }
  return result;
}

bool
CellGameModule::fillSelection(unsigned char state)
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  const bool result = clipboard.fillSelection(
    cellContext->getGrid(), cellContext->getCanvasView(), state);
  if (result) {
    updateVisualTargets();
  }
  return result;
}

bool
CellGameModule::copySelection()
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr) {
    return false;
  }
  std::string error;
  if (!clipboard.copySelection(cellContext->getGrid(), &error)) {
    Logger::LogError(error.c_str());
    return false;
  }
  return true;
}

bool
CellGameModule::cutSelection()
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  std::string error;
  const bool result = clipboard.cutSelection(
    cellContext->getGrid(), cellContext->getCanvasView(), &error);
  if (!result) {
    Logger::LogError(error.c_str());
    return false;
  }
  updateVisualTargets();
  return true;
}

bool
CellGameModule::pasteAtCursor()
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  std::string error;
  const bool result = clipboard.pasteAtCursor(
    cellContext->getGrid(), cellContext->getCanvasView(), hoverX, hoverY, &error);
  if (!result) {
    Logger::LogError(error.c_str());
    return false;
  }
  updateVisualTargets();
  return true;
}

bool
CellGameModule::stampNamed(const std::string& name)
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  const std::int64_t originX = hoverValid ? hoverX : 0;
  const std::int64_t originY = hoverValid ? hoverY : 0;
  std::string error;
  const bool result = clipboard.stampNamed(
    cellContext->getGrid(), cellContext->getCanvasView(), name, originX, originY, &error);
  if (result) {
    updateVisualTargets();
  }
  return result;
}

bool
CellGameModule::importPatternText(const std::string& text)
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  const std::int64_t originX = hoverValid ? hoverX : 0;
  const std::int64_t originY = hoverValid ? hoverY : 0;
  std::string error;
  const bool result = clipboard.importPatternText(
    cellContext->getGrid(), cellContext->getCanvasView(), text, originX, originY, &error);
  if (!result) {
    Logger::LogError(error.c_str());
    return false;
  }
  updateVisualTargets();
  return true;
}

void
CellGameModule::handleEditorHotkeys()
{
  if (ic == nullptr || ic->inputManager == nullptr || ic->commandLine->isOpen) {
    return;
  }
  const bool control = ic->inputManager->isControlPressed();
  const bool copyDown = control && ic->inputManager->isKeyPressed(KeyCode::C);
  const bool cutDown = control && ic->inputManager->isKeyPressed(KeyCode::X);
  const bool pasteDown = control && ic->inputManager->isKeyPressed(KeyCode::V);
  const bool rotateDown = ic->inputManager->isKeyPressed(KeyCode::R);
  const bool flipDown = ic->inputManager->isKeyPressed(KeyCode::F);
  const bool inspectDown = ic->inputManager->isKeyPressed(KeyCode::I);
  const bool deleteDown = ic->inputManager->isKeyPressed(KeyCode::Delete);

  if (copyDown && !copyHeld) {
    copySelection();
  }
  if (cutDown && !cutHeld) {
    cutSelection();
  }
  if (pasteDown && !pasteHeld) {
    pasteAtCursor();
  }
  if (rotateDown && !rotateHeld && !control) {
    clipboard.rotateCw();
  }
  if (flipDown && !flipHeld && !control) {
    clipboard.flipHorizontal();
  }
  if (inspectDown && !inspectHeld && !control) {
    inspectorEnabled = !inspectorEnabled;
    ic->envVars->setVar("showInspector", inspectorEnabled);
  }
  if (deleteDown && !deleteHeld) {
    fillSelection(SparseCellGrid::BackgroundState);
  }

  copyHeld = copyDown;
  cutHeld = cutDown;
  pasteHeld = pasteDown;
  rotateHeld = rotateDown;
  flipHeld = flipDown;
  inspectHeld = inspectDown;
  deleteHeld = deleteDown;
}

void
CellGameModule::updateSelectionVisual()
{
  selectionVisual.clearPrimitives();
  if (!clipboard.hasSelection() || ic == nullptr || ic->camera == nullptr) {
    selectionVisual.setVisible(false);
    return;
  }
  std::int64_t x0 = 0;
  std::int64_t y0 = 0;
  std::int64_t x1 = 0;
  std::int64_t y1 = 0;
  clipboard.getNormalizedSelection(&x0, &y0, &x1, &y1);
  const float cellSize = 16.0f;
  const float worldX = static_cast<float>(x0) * cellSize - cellSize * 0.5f;
  const float worldY = static_cast<float>(y0) * cellSize - cellSize * 0.5f;
  const float width = static_cast<float>(x1 - x0 + 1) * cellSize;
  const float height = static_cast<float>(y1 - y0 + 1) * cellSize;
  selectionVisual.setCamera(ic->camera);
  selectionVisual.setSpace(PrimitiveSpace::World);
  selectionVisual.setLayerHint(RenderLayerId::UI);
  selectionVisual.addOutlineRect(
    worldX, worldY, width, height, UiTheme::accent(), 2.0f);
  selectionVisual.setVisible(true);
}

void
CellGameModule::updateInspectorVisual()
{
  inspectorVisual.clearPrimitives();
  const bool consoleOpen =
    ic != nullptr && ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool settingsOpen =
    configurationMenu != nullptr && configurationMenu->isOpen();
  const bool exitConfirmOpen =
    exitConfirmDialog != nullptr && exitConfirmDialog->isOpen();
  if (!inspectorEnabled || consoleOpen || settingsOpen || exitConfirmOpen ||
      ic == nullptr) {
    inspectorVisual.setVisible(false);
    return;
  }

  std::ostringstream text;
  text << "gen " << simulationGeneration << "\n";
  if (hoverValid && cellContext != nullptr) {
    const CellAddress address{ hoverX, hoverY };
    const unsigned char state = cellContext->getGrid()->getCell(address);
    const ChunkAddress chunk = SparseCellGrid::chunkAddressForCell(address);
    const bool inBounds = cellContext->getGrid()->isCellInWorldBounds(address);
    text << "cell " << hoverX << "," << hoverY << " state "
         << static_cast<int>(state) << "\n";
    text << cellContext->getModeString() << "\n";
    text << "chunk " << chunk.x << "," << chunk.y
         << (inBounds ? " in-bounds" : " outside") << "\n";
  } else {
    text << "no hover\n";
  }
  const SparseAdvanceStats& stats =
    cellContext->getGrid()->getLastAdvanceStats();
  text << "cells " << stats.activeCellCount << " tps " << achievedSimulationTps;
  inspectorVisual.setWindow(ic->window);
  inspectorVisual.setSpace(PrimitiveSpace::Pixels);
  inspectorVisual.setLayerHint(RenderLayerId::UI);
  const float scale =
    ic->renderer != nullptr ? ic->renderer->getUiScale() : 1.0f;
  const std::array<int, 2> winDims = ic->window != nullptr
                                       ? ic->window->getWindowDimensions()
                                       : std::array<int, 2>{ 1280, 720 };
  const float virtWidth =
    static_cast<float>(winDims[0]) / (scale > 0.0f ? scale : 1.0f);
  const float virtHeight =
    static_cast<float>(winDims[1]) / (scale > 0.0f ? scale : 1.0f);
  const float inspW = std::min(300.0f, std::max(100.0f, virtWidth - 24.0f));
  const float inspH = std::min(128.0f, std::max(60.0f, virtHeight - 84.0f));
  const float inspX = 12.0f;
  const float inspY =
    std::clamp(72.0f, 0.0f, std::max(0.0f, virtHeight - inspH));
  GuiKit::drawCard(inspectorVisual, inspX, inspY, inspW, inspH);
  std::string remaining = text.str();
  float lineY = inspY + 8.0f;
  while (!remaining.empty()) {
    const std::size_t newline = remaining.find('\n');
    std::string line = remaining;
    if (newline != std::string::npos) {
      line = remaining.substr(0, newline);
      remaining = remaining.substr(newline + 1);
    } else {
      remaining.clear();
    }
    inspectorVisual.addText(
      line, inspX + 10.0f, lineY, 16.0f, UiTheme::textPrimary());
    lineY += 18.0f;
  }
  inspectorVisual.setVisible(true);
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
  hoverX = currentX;
  hoverY = currentY;
  hoverValid = cellContext->getGrid()->isCellInWorldBounds(
    CellAddress{ currentX, currentY });

  if (!ic->commandLine->isOpen) {
    if (cellContext->getModeString() == "WIREWORLD") {
      updateWireworldBrushFromInput();
    }
    handleEditorHotkeys();

    bool isLeftPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    bool isRightPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
    const bool shift = ic->inputManager->isShiftPressed();

    const bool pointerInWorld = hoverValid && !isHamburgerHovered();
    if (shift && isLeftPressed && pointerInWorld) {
      if (!clipboard.isSelecting()) {
        clipboard.startSelection(currentX, currentY);
      } else {
        clipboard.updateSelectionDrag(currentX, currentY);
      }
      wasPressed = false;
    } else {
      clipboard.stopSelectionDrag();
      if ((isLeftPressed || isRightPressed) && pointerInWorld) {
        mirrorDeltaValid = false;
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
            this->cellContext->getCanvasView()->setCanvasPixel(
              x0, y0, colorVal);
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
      }
    }
  } else {
    wasPressed = false;
    clipboard.stopSelectionDrag();
  }
}

void
CellGameModule::toggleSettingsMenu()
{
  if (configurationMenu == nullptr) {
    return;
  }
  if (configurationMenu->isOpen()) {
    configurationMenu->close();
  } else {
    drainSimulation();
    if (ic != nullptr && ic->inputManager != nullptr) {
      ic->inputManager->clearKeyQueue();
      ic->inputManager->clearCharQueue();
    }
    configurationMenu->open(currentConfiguration());
  }
}

bool
CellGameModule::isHamburgerHovered() const
{
  if (ic == nullptr || ic->window == nullptr) {
    return false;
  }
  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool exitConfirmOpen =
    exitConfirmDialog != nullptr && exitConfirmDialog->isOpen();
  if (consoleOpen || exitConfirmOpen) {
    return false;
  }
  const std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
  const float scale =
    ic->renderer != nullptr ? ic->renderer->getUiScale() : 1.0f;
  const float mx =
    static_cast<float>(mouseCoords[0]) / (scale > 0.0f ? scale : 1.0f);
  const float my =
    static_cast<float>(mouseCoords[1]) / (scale > 0.0f ? scale : 1.0f);

  return mx >= hamburgerX && mx <= (hamburgerX + hamburgerSize) &&
         my >= hamburgerY && my <= (hamburgerY + hamburgerSize);
}

void
CellGameModule::updateHamburgerVisual(double dt)
{
  (void)dt;
  hamburgerVisual.clearPrimitives();
  if (ic == nullptr || ic->window == nullptr) {
    hamburgerVisual.setVisible(false);
    return;
  }

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool exitConfirmOpen =
    exitConfirmDialog != nullptr && exitConfirmDialog->isOpen();
  const bool settingsOpen =
    configurationMenu != nullptr && configurationMenu->isOpen();

  if (consoleOpen || exitConfirmOpen || settingsOpen) {
    hamburgerVisual.setVisible(false);
    hamburgerHovered = false;
    return;
  }

  const std::array<int, 2> winDims = ic->window->getWindowDimensions();
  const float scale =
    ic->renderer != nullptr ? ic->renderer->getUiScale() : 1.0f;
  const float virtWidth =
    static_cast<float>(winDims[0]) / (scale > 0.0f ? scale : 1.0f);

  hamburgerSize = 32.0f;
  hamburgerX = std::max(0.0f, virtWidth - hamburgerSize - 12.0f);
  hamburgerY = 12.0f;

  hamburgerHovered = isHamburgerHovered();
  const bool isMouseDown =
    ic->inputManager != nullptr &&
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool isPressed = hamburgerHovered && isMouseDown;

  // Background and border opacity
  // Resting: subtle and translucent (~35%)
  // Hovered: noticeable (~85%)
  // Pressed: accented highlight
  const unsigned char bgAlpha = isPressed ? 180 : (hamburgerHovered ? 150 : 60);
  const unsigned char borderAlpha =
    isPressed ? 230 : (hamburgerHovered ? 200 : 90);
  const unsigned char barAlpha =
    isPressed ? 255 : (hamburgerHovered ? 240 : 130);

  ColorRgba bgColor = UiTheme::panelSurface();
  bgColor.a = bgAlpha;
  ColorRgba borderColor =
    isPressed ? UiTheme::accent() : UiTheme::panelBorder();
  borderColor.a = borderAlpha;
  ColorRgba barColor = isPressed ? UiTheme::accent() : UiTheme::textPrimary();
  barColor.a = barAlpha;

  // Panel card background
  GuiKit::drawCard(hamburgerVisual,
                   hamburgerX,
                   hamburgerY,
                   hamburgerSize,
                   hamburgerSize,
                   bgColor,
                   borderColor,
                   1.0f);

  // 3 horizontal bars (hamburger lines)
  const float barWidth = 16.0f;
  const float barHeight = 2.0f;
  const float barX = hamburgerX + (hamburgerSize - barWidth) * 0.5f;
  const float startY = hamburgerY + 9.0f;
  const float spacing = 5.0f;

  for (int i = 0; i < 3; ++i) {
    const float y = startY + static_cast<float>(i) * spacing;
    hamburgerVisual.addFilledRect(barX, y, barWidth, barHeight, barColor);
  }

  hamburgerVisual.setVisible(true);
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
    (configurationMenu == nullptr || !configurationMenu->isOpen()) &&
    (exitConfirmDialog == nullptr || !exitConfirmDialog->isOpen());
  if (!canShow) {
    editorCursor.setVisible(false);
  }

  std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
  glm::dvec2 worldPos = ic->camera->ScreenToWorldPrecise(
    glm::dvec2(mouseCoords[0], mouseCoords[1]));
  const std::int64_t cellX = CanvasView::worldToCell(worldPos.x);
  const std::int64_t cellY = CanvasView::worldToCell(worldPos.y);
  hoverX = cellX;
  hoverY = cellY;
  hoverValid =
    cellContext->getGrid()->isCellInWorldBounds(CellAddress{ cellX, cellY });
  if (!canShow) {
    return;
  }
  editorCursor.setVisible(hoverValid);
  if (!hoverValid) {
    return;
  }
  editorCursor.setCellSize(16.0f);
  editorCursor.setFromCell(cellX, cellY);
}

bool
CellGameModule::SaveCellGame(std::string filename)
{
  if (filename.empty()) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError("Save path is empty");
    }
    return false;
  }
  drainSimulation();

  IllumoDocument doc;
  doc.version = IllumoCodec::kVersion;
  doc.ruleString = cellContext->getRuleSet()->getRuleTag();
  if (ic != nullptr && ic->camera != nullptr) {
    const glm::dvec2 cameraPosition = ic->camera->GetPositionPrecise();
    doc.cameraX = cameraPosition.x;
    doc.cameraY = cameraPosition.y;
    doc.cameraZoom = static_cast<double>(ic->camera->GetZoom());
  }
  doc.worldChunkWidth = cellContext->getWorldChunkWidth();
  doc.worldChunkHeight = cellContext->getWorldChunkHeight();
  doc.sourceGrid = cellContext->getGrid();

  std::string error;
  if (!IllumoCodec::writeFile(filename, doc, &error)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(error);
    }
    return false;
  }
  return true;
}

bool
CellGameModule::LoadCellGame(std::string filename)
{
  if (filename.empty()) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError("Load path is empty");
    }
    return false;
  }

  IllumoDocument doc;
  std::string error;
  if (!IllumoCodec::readFile(filename, &doc, &error)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(error);
    }
    return false;
  }

  // All parsing and allocation completed against temporary state.
  prepareGridMutation();
  if ((cellContext->getWorldChunkWidth() != doc.worldChunkWidth ||
       cellContext->getWorldChunkHeight() != doc.worldChunkHeight) &&
      !cellContext->resetWorld(doc.worldChunkWidth, doc.worldChunkHeight)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError("Unable to allocate the saved world topology");
    }
    return false;
  }
  cellContext->setRuleSet(doc.ruleString);
  if (doc.grid != nullptr) {
    cellContext->getGrid()->swap(*doc.grid);
  }
  simulationGeneration = 0;
  cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
  if (ic != nullptr && ic->camera != nullptr) {
    if (doc.restoreCamera) {
      ic->camera->SetPositionPrecise(doc.cameraX, doc.cameraY);
      ic->camera->SetZoom(static_cast<float>(doc.cameraZoom));
    } else {
      ic->camera->Reset();
    }
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
      (render3dTestStatic != nullptr && render3dTestAnimated != nullptr &&
       render3dTestChild != nullptr)) {
    return;
  }

  render3dTestStatic = std::make_unique<MeshVisual>();
  render3dTestStatic->prepare(ic->renderer);
  render3dTestStatic->addAxes(glm::vec3(0.0f), 3.0f);
  render3dTestStatic->addGrid(10, 1.0f, ColorRgba{ 72, 96, 128, 255 });

  render3dTestAnimated = std::make_unique<MeshVisual>();
  render3dTestAnimated->prepare(ic->renderer);
  render3dTestAnimated->addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.75f), ColorRgba{ 255, 180, 72, 255 });
  render3dTestAnimated->addWireCube(
    glm::vec3(0.0f), glm::vec3(1.0f), ColorRgba{ 224, 244, 255, 255 });

  render3dTestChild = std::make_unique<MeshVisual>();
  render3dTestChild->prepare(ic->renderer);
  render3dTestChild->addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.35f), ColorRgba{ 100, 220, 255, 255 });
  render3dTestChild->addWireCube(
    glm::vec3(0.0f), glm::vec3(0.45f), ColorRgba{ 255, 255, 255, 255 });

  render3dSceneGraph.clear();
  render3dRootNode = render3dSceneGraph.createNode();
  render3dSceneGraph.setRenderAttachment(render3dRootNode,
                                         render3dTestStatic.get());

  render3dOrbitNode = render3dSceneGraph.createNode(render3dRootNode);
  render3dSceneGraph.setRenderAttachment(render3dOrbitNode,
                                         render3dTestAnimated.get());

  render3dChildNode = render3dSceneGraph.createNode(render3dOrbitNode);
  render3dSceneGraph.setRenderAttachment(render3dChildNode,
                                         render3dTestChild.get());
}

void
CellGameModule::applyRender3dTestCamera()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  ic->camera->lookAt(glm::vec3(12.0f, 9.0f, 12.0f),
                     glm::vec3(0.0f, 0.0f, 0.0f),
                     glm::vec3(0.0f, 1.0f, 0.0f));
  ic->camera->setPerspective(55.0f, 0.1f, 100.0f);
  ic->camera->setProjectionType(ProjectionType::Perspective);
  render3dCameraApplied = true;
}

void
CellGameModule::restoreRender3dTestCamera()
{
  if (!render3dCameraApplied || ic == nullptr || ic->camera == nullptr) {
    return;
  }
  ic->camera->setProjectionType(ProjectionType::Orthographic);
  render3dCameraApplied = false;
}

void
CellGameModule::updateRender3dTestMatrices()
{
  if (ic == nullptr || render3dTestStatic == nullptr ||
      render3dTestAnimated == nullptr || render3dTestChild == nullptr) {
    return;
  }

  render3dSceneGraph.setLocalTransform(render3dRootNode, Matrix4(1.0f));

  const float elapsed = static_cast<float>(render3dTestTime);
  Transform3D orbitTransform;
  orbitTransform.position = Vector3(std::sin(elapsed) * 4.0f,
                                    1.5f + std::sin(elapsed * 1.7f) * 0.75f,
                                    std::cos(elapsed) * 4.0f);
  orbitTransform.rotation =
    Quaternion(Vector3(elapsed * 0.8f, elapsed * 1.4f, 0.0f));
  render3dSceneGraph.setLocalTransform(render3dOrbitNode, orbitTransform);

  Transform3D childTransform;
  childTransform.position = Vector3(std::sin(elapsed * 2.5f) * 2.2f,
                                    std::cos(elapsed * 2.5f) * 0.8f,
                                    std::cos(elapsed * 2.5f) * 2.2f);
  childTransform.rotation =
    Quaternion(Vector3(0.0f, elapsed * 3.0f, elapsed * 2.0f));
  render3dSceneGraph.setLocalTransform(render3dChildNode, childTransform);
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
    applyRender3dTestCamera();
    updateRender3dTestMatrices();
    scene->AddDrawable(&render3dSceneGraph, RenderLayerId::World);
  } else {
    restoreRender3dTestCamera();
    scene->AddDrawable(this->cellContext->getCanvasView(),
                       RenderLayerId::World);
  }
  if (editorCursor.isVisible()) {
    scene->AddDrawable(&editorCursor, RenderLayerId::UI);
  }
  if (selectionVisual.isVisible()) {
    scene->AddDrawable(&selectionVisual, RenderLayerId::UI);
  }
  if (inspectorVisual.isVisible()) {
    scene->AddDrawable(&inspectorVisual, RenderLayerId::UI);
  }
  if (modeSplash != nullptr && modeSplash->isVisible()) {
    scene->AddDrawable(modeSplash.get(), RenderLayerId::UI);
  }
  if (hamburgerVisual.isVisible()) {
    scene->AddDrawable(&hamburgerVisual, RenderLayerId::UI);
  }
  if (configurationMenu != nullptr && configurationMenu->isOpen()) {
    scene->AddDrawable(configurationMenu.get(), RenderLayerId::UI);
  }
  if (exitConfirmDialog != nullptr && exitConfirmDialog->isOpen()) {
    scene->AddDrawable(exitConfirmDialog.get(), RenderLayerId::UI);
  }
}
