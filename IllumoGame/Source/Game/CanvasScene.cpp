#include "CanvasScene.h"
#include "BuiltinPatterns.h"
#include "CSimPlatform.h"
#include "CSimScenes.h"
#include "CSimSounds.h"
#include "CanvasChromeStyle.h"
#include "CanvasCoordinatePolicy.h"
#include "IllumoCodec.h"
#include "PatternCodec.h"
#include "RuleCatalogOverlay.h"
#include "Rulesets/RuleSetRegistry.h"
#include "SimulatorSettings.h"
#include "TitleScene.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Foundation/ParseNumber.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiPointerHint.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/AssetSource.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Font.h>
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
#include <filesystem>
#include <fstream>
#include <functional>
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
  return parseWholeInteger(text, value);
}

static std::uint64_t
seedHash(std::int64_t x, std::int64_t y, std::uint64_t salt)
{
  std::uint64_t value = static_cast<std::uint64_t>(x) * 0x9E3779B185EBCA87ULL ^
                        static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL ^
                        salt;
  value ^= value >> 30u;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27u;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31u);
}

// Advances one piece of edit chrome (hint bar or paint bubble) after its
// stagger delay. Entering, it pops up on a jelly spring that overshoots its
// slot and bounces back; leaving, it eases away without ringing, so a hidden
// bar or bubble never peeks back into view.
static void
advanceModeChromeSpring(GuiSpring* spring,
                        double* delay,
                        bool targetVisible,
                        double dt)
{
  if (spring == nullptr || delay == nullptr || !std::isfinite(dt) ||
      dt <= 0.0) {
    return;
  }
  double animationDt = std::min(dt, 0.25);
  if (*delay > 0.0) {
    const double delayedDt = std::min(animationDt, *delay);
    *delay -= delayedDt;
    animationDt -= delayedDt;
  }
  if (animationDt <= 0.0) {
    return;
  }
  if (targetVisible) {
    spring->setTarget(1.0f);
    spring->tick(static_cast<float>(animationDt), false);
    return;
  }
  const float blend = static_cast<float>(1.0 - std::exp(-14.0 * animationDt));
  spring->snapTo(spring->value() - spring->value() * blend);
}

static bool
parseWorldCoordinate(const std::string& text, std::int64_t* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  return parseWholeInteger(text, value);
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
  double parsed = 0.0;
  if (value == nullptr || text.empty() || !parseWholeFloating(text, &parsed) ||
      !std::isfinite(parsed)) {
    return false;
  }
  *value = parsed;
  return true;
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
uniqueCustomFamilyId(const std::string& sourceId)
{
  const std::string prefix = "CUSTOM_FAMILY_";
  std::string base = RuleSetRegistry::normalizeId(sourceId);
  const std::size_t maximumBaseLength = 64u - prefix.size() - 5u;
  if (base.size() > maximumBaseLength) {
    base.resize(maximumBaseLength);
  }
  const std::vector<std::string> knownFamilies =
    RuleSetRegistry::instance().getKnownFamilies();
  for (unsigned int suffix = 1u; suffix < 10000u; ++suffix) {
    const std::string suffixText =
      suffix == 1u ? "" : "_" + std::to_string(suffix);
    std::string candidateBase = base;
    if (candidateBase.size() + suffixText.size() > maximumBaseLength) {
      candidateBase.resize(maximumBaseLength - suffixText.size());
    }
    const std::string candidate = prefix + candidateBase + suffixText;
    if (std::find(knownFamilies.begin(), knownFamilies.end(), candidate) ==
        knownFamilies.end()) {
      return candidate;
    }
  }
  return {};
}

static bool
hasInvalidCellState(const SparseCellGrid& grid, unsigned int stateCount)
{
  std::vector<SparseChunkRecord> chunks;
  grid.collectChunkRecords(&chunks);
  for (const SparseChunkRecord& chunk : chunks) {
    for (const unsigned char state : chunk.cells) {
      if (state != SparseCellGrid::BackgroundState &&
          static_cast<unsigned int>(state) >= stateCount) {
        return true;
      }
    }
  }
  return false;
}

// "an infinite world" or "a 8 x 8 chunk torus", for diagnostics.
static std::string
describeTopology(std::int64_t worldChunkWidth, std::int64_t worldChunkHeight)
{
  if (worldChunkWidth <= 0 || worldChunkHeight <= 0) {
    return "an infinite world";
  }
  return "a " + std::to_string(worldChunkWidth) + " x " +
         std::to_string(worldChunkHeight) + " chunk torus";
}

// The catalog's display name for a ruleset ID, or the ID itself.
static std::string
ruleDisplayName(const std::string& ruleId)
{
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(ruleId);
  return definition == nullptr || definition->name.empty() ? ruleId
                                                           : definition->name;
}

// 1234567 as "1,234,567".
static std::string
groupDigits(std::uint64_t value)
{
  const std::string digits = std::to_string(value);
  std::string grouped;
  for (std::size_t index = 0; index < digits.size(); ++index) {
    if (index > 0 && (digits.size() - index) % 3 == 0) {
      grouped += ',';
    }
    grouped += digits[index];
  }
  return grouped;
}

// `text` as drawn at `sizePt` and `emphasis`, shortened with "..." until it
// fits `width`.
static std::string
fitTextToWidth(const std::string& text,
               float sizePt,
               float emphasis,
               float width)
{
  if (GuiKit::measureEmphasizedText(text, sizePt, emphasis) <= width) {
    return text;
  }
  std::string shortened = text;
  while (!shortened.empty()) {
    shortened.pop_back();
    const std::string candidate = shortened + "...";
    if (GuiKit::measureEmphasizedText(candidate, sizePt, emphasis) <= width) {
      return candidate;
    }
  }
  return "...";
}

CanvasScene::CanvasScene(std::string initialSavePath)
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
  , configurationMenu(nullptr)
  , exitConfirmDialog(nullptr)
  , render3dLoadFailed(false)
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
  , m_lifetime(std::make_shared<bool>(true))
{
  ic = nullptr;
}

CanvasScene::CanvasScene(const NewSimulationConfiguration& configuration)
  : CanvasScene(std::string{})
{
  initialCanvas = configuration;
}

CanvasScene::~CanvasScene() {}

bool
CanvasScene::start(IllumoContext& startContext)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.start");
  IllumoContext* context = &startContext;
  if (!cellGameContextComplete(context)) {
    Logger::LogError(
      "CanvasScene::start: IllumoContext missing required services "
      "(envVars, window, camera, renderer, inputManager, commandLine, "
      "commandRegistry, scene)");
    ic = context;
    return false;
  }
  if (initialCanvas.has_value() && !initialCanvas->isValid()) {
    Logger::LogError("Invalid new canvas configuration");
    return false;
  }
  ic = context;
  // A canvas opens on its home view; a save may restore its own camera.
  resetCameraToHome();
  inspectorEnabled = ic->envVars->getVar("showInspector").valueAsBool;

  // Prefer the explicit ruleset setting, then retain the old mode alias.
  std::string startMode = ic->envVars->getVar("RuleSetString").value;
  if (startMode.empty()) {
    startMode = ic->envVars->getVar("ModeString").value;
  }
  if (startMode.empty()) {
    startMode = "GAME_OF_LIFE";
  }

  if (initialCanvas.has_value()) {
    startMode = initialCanvas->ruleSet;
  }

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

  inputContextId = ic->inputManager->registerInputContext(this->inputContext);
  if (inputContextId < 0) {
    Logger::LogError("The canvas input context could not be registered");
    return false;
  }
  ic->inputManager->setActiveInputContext(inputContextId);
  this->cellContext = new CellContext(
    startMode, ic->envVars, ic->window, ic->camera, ic->renderer);
  if (cellContext->getRuleSet() == nullptr) {
    Logger::LogError("No valid ruleset is available to start the canvas");
    delete cellContext;
    cellContext = nullptr;
    ic->inputManager->unregisterInputContext(inputContextId);
    inputContextId = -1;
    return false;
  }
  m_paintBrush = 0u;
  for (unsigned int state = 0u;
       state < cellContext->getRuleSet()->getStateCount();
       ++state) {
    if (cellContext->getRuleSet()->getStateName(
          static_cast<unsigned char>(state)) == "Conductor") {
      m_paintBrush = static_cast<unsigned char>(state);
      break;
    }
  }
  if (initialCanvas.has_value() &&
      !cellContext->resetWorld(initialCanvas->worldChunkWidth,
                               initialCanvas->worldChunkHeight)) {
    Logger::LogError("Unable to allocate new canvas");
    delete cellContext;
    cellContext = nullptr;
    ic->inputManager->unregisterInputContext(inputContextId);
    inputContextId = -1;
    return false;
  }
  // Canvas-dependent settings are applied only after domain construction.
  simAccum = 0.0;
  syncSimRateFromEnv();
  syncCanvasLookFromEnv();
  autosaveElapsed = 0.0;

  // Canvases open paused in EDIT unless the player chose to start running;
  // the mode cue is not played for the opening state.
  currentState = SimulatorSettings::flag(ic->envVars, "startPaused", true)
                   ? CellState::EDIT
                   : CellState::NORMAL;
  soundedState = currentState;

  if (!initialSaveFile.empty()) {
    LoadCellGame(initialSaveFile);
  } else if (!initialCanvas.has_value() || initialCanvas->starterPattern) {
    // Ruleset-aware startup seed (GoL glider, Wireworld electron-on-wire, …).
    seedInitialPattern();
  }

  // Initial palette from active ruleset; seed cells already mark upload dirty.
  cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
  updateVisualTargets();
  registerConsoleCommands();

  // Mode badge (top-left corner), shown briefly on each EDIT/NORMAL change.
  modeBadge.prepare(ic->window, ic->renderer);

  editorCursor.init(ic->renderer, ic->window, ic->camera);
  editorCursor.setCellSize(16.0f);
  // Hidden until Edit() updates cell position (avoids an extra DrawList entry
  // at Start before the first mouse sample).
  editorCursor.setVisible(false);

  editHintsVisual.setRenderer(ic->renderer);
  editHintsVisual.setWindow(ic->window);
  editHintsVisual.setSpace(PrimitiveSpace::Pixels);
  editHintsVisual.setLayerHint(RenderLayerId::UI);
  editHintsVisual.setVisible(false);
  editHintsVisual.prepare(ic->renderer);

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

  m_actionBar.prepare(ic->window, ic->renderer);
  m_contextMenu.prepare(ic->window, ic->renderer);
  m_editToolsCapturing = false;
  m_rightMouseWasDown = false;
  m_rightPressEdge = false;

  for (GameVisual* visual : { &m_hamburgerHaloVisual, &hamburgerVisual }) {
    visual->setRenderer(ic->renderer);
    visual->setWindow(ic->window);
    visual->setSpace(PrimitiveSpace::Pixels);
    visual->setLayerHint(RenderLayerId::UI);
    visual->setVisible(false);
    if (ic->renderer != nullptr) {
      visual->prepare(ic->renderer);
    }
  }
  m_hamburgerHaloKey.invalidate();
  m_hamburgerKey.invalidate();

  for (GameVisual* visual : { &m_paintBubbleHaloVisual,
                              &m_paintPaletteVisual,
                              &m_paintDropVisual,
                              &m_paintPaletteTopVisual }) {
    visual->setRenderer(ic->renderer);
    visual->setWindow(ic->window);
    visual->setSpace(PrimitiveSpace::Pixels);
    visual->setLayerHint(RenderLayerId::UI);
    visual->prepare(ic->renderer);
    visual->setVisible(false);
  }
  m_paintBubbleHaloKey.invalidate();
  m_paintPaletteKey.invalidate();
  m_paintDropKey.invalidate();
  m_paintPaletteTopKey.invalidate();
  m_paintPaletteExpanded = false;
  m_paintPaletteToggleHovered = false;
  m_paintSelectHead.configure(GuiMotion::kLiquidHead);
  m_paintSelectTail.configure(GuiMotion::kLiquidTail);
  m_paintSelectPlaced = false;
  m_paintPaletteReveal = 0.0f;
  m_editChromeSpring.configure(GuiMotion::kJelly);
  // The paint bubble bounces in on the settings button's springier feel.
  m_paintPaletteChromeSpring.configure(GuiMotion::kBoing);
  m_editChromeSpring.snapTo(m_editChromeReveal);
  m_paintPaletteChromeSpring.snapTo(m_paintPaletteChromeReveal);
  m_editChromeLift = m_editChromeReveal;
  m_paintPaletteChromeLift = m_paintPaletteChromeReveal;
  m_paintPaletteHeightMorph.configure(GuiMotion::kJelly);
  m_paintPaletteWidthMorph.configure(GuiMotion::kBoing);
  m_paintPaletteBubbleHover.configure(GuiMotion::kBoing);
  m_paintPaletteHeightMorph.snapTo(0.0f);
  m_paintPaletteWidthMorph.snapTo(0.0f);
  m_paintPaletteBubbleHover.snapTo(0.0f);
  m_hamburgerPop.configure(GuiMotion::kBoing);
  m_hamburgerHover.configure(GuiMotion::kJelly);
  m_hamburgerTip.configure(GuiMotion::kBoing);
  m_hamburgerTip.snapTo(0.0f);
  for (GuiSpring& bar : m_hamburgerBars) {
    bar.configure(GuiMotion::kBoing);
    bar.snapTo(0.0f);
  }
  m_hamburgerPop.snapTo(0.0f);
  m_hamburgerHover.snapTo(0.0f);
  m_hamburgerHoverClock = 0.0;
  m_hamburgerShown = false;
  m_paintPaletteMouseWasDown = false;
  m_paintPaletteCapturing = false;
  m_paintPaletteHovered = false;
  m_paintPaletteEmphasis.clear();
  for (GuiSpring& card : m_paintCardHover) {
    card.configure(GuiMotion::kJelly);
    card.snapTo(0.0f);
  }
  for (GuiSpring& card : m_paintCardSquish) {
    card.configure(GuiMotion::kJelly);
    card.snapTo(0.0f);
  }
  m_paintHeaderHover.configure(GuiMotion::kJelly);
  m_paintHeaderHover.snapTo(0.0f);
  m_paintChipPop.configure(GuiMotion::kBoing);
  m_paintChipPop.snapTo(0.0f);
  m_paintChipBrush = 0;
  m_paintPaletteStateOffset = 0u;
  m_paintRuleTag.clear();

  canvasEntranceVisual.setRenderer(ic->renderer);
  canvasEntranceVisual.setWindow(ic->window);
  canvasEntranceVisual.setSpace(PrimitiveSpace::Pixels);
  canvasEntranceVisual.setLayerHint(RenderLayerId::UI);
  canvasEntranceVisual.prepare(ic->renderer);
  canvasEntranceElapsed = 0.0;
  mainMenuReturnPending = false;
  mainMenuReturnSubmitted = false;
  advanceCanvasEntrance(0.0);
  CSimSounds::play(CSimSound::CanvasEnter);
  syncEditMusic();

  configurationMenu =
    std::make_unique<ConfigurationMenu>(ic->window, ic->renderer);
  rulesetWorkshopMenu =
    std::make_unique<RulesetWorkshopMenu>(ic->window, ic->renderer);
  exitConfirmDialog =
    std::make_unique<ExitConfirmDialog>(ic->window, ic->renderer);

  if (!initialSaveFile.empty()) {
    Logger::LogInfo("Canvas opening " + initialSaveFile);
  } else {
    Logger::LogInfo("New canvas: " +
                    ruleDisplayName(cellContext->getRuleSetString()) + " on " +
                    describeTopology(cellContext->getWorldChunkWidth(),
                                     cellContext->getWorldChunkHeight()));
  }
  return true;
}

void
CanvasScene::showModeSplash(const char* label)
{
  if (label == nullptr) {
    return;
  }
  // Editing is warm amber; a running simulation is cyan with a breathing dot.
  const bool running = std::string(label) == "NORMAL";
  modeBadge.show(label,
                 running ? UiTheme::accentCool()
                         : ColorRgba{ 255, 204, 102, 255 },
                 running);
  // Every EDIT/NORMAL change passes through here; voice actual changes only.
  const CellState shown = running ? CellState::NORMAL : CellState::EDIT;
  if (shown != soundedState) {
    soundedState = shown;
    CSimSounds::play(CSimSound::CanvasModeSwitch);
  }
  syncEditMusic();
}

void
CanvasScene::syncEditMusic()
{
  if (!m_suspended && !mainMenuReturnPending &&
      currentState == CellState::EDIT) {
    CSimSounds::playMusic(CSimMusic::CanvasEdit);
  } else if (CSimSounds::musicPlaying(CSimMusic::CanvasEdit)) {
    CSimSounds::stopMusic();
  }
}

void
CanvasScene::updateModeBadge(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateModeBadge");
  const bool reducedMotion = ic != nullptr && ic->envVars != nullptr &&
                             ic->envVars->getVar("reducedUiMotion").valueAsBool;
  modeBadge.tick(static_cast<float>(dt), reducedMotion);
}

void
CanvasScene::seedInitialPattern()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.seedInitialPattern");
  SparseCellGrid* grid = cellContext->getGrid();
  const RuleSet* rules = cellContext->getRuleSet();
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(rules->getRuleTag());
  const RuleFamilyDefinition* family =
    definition == nullptr
      ? nullptr
      : RuleSetRegistry::instance().getFamilyDefinition(definition->familyId);
  RuleSeedPattern seedPattern = definition == nullptr
                                  ? RuleSeedPattern::Automatic
                                  : definition->seedPattern;
  const unsigned int seedRadius =
    definition == nullptr ? 18u : definition->seedRadius;
  const unsigned int seedDensity =
    definition == nullptr ? 42u : definition->seedDensity;

  if (seedPattern == RuleSeedPattern::Automatic) {
    if (family != nullptr) {
      switch (family->kind) {
        case RuleFamily::Generations:
        case RuleFamily::LargerThanLife:
          seedPattern = RuleSeedPattern::ActiveSoup;
          break;
        case RuleFamily::MooreTable:
          seedPattern = RuleSeedPattern::ExcitableBreak;
          break;
        case RuleFamily::Cyclic:
          seedPattern = RuleSeedPattern::PhaseSoup;
          break;
        case RuleFamily::SpeciesLife:
        case RuleFamily::Dominance:
          seedPattern = RuleSeedPattern::SpeciesSoup;
          break;
        case RuleFamily::Hodgepodge:
        case RuleFamily::Lenia:
          // A disk of random intensities is Lenia's classic soup.
          seedPattern = RuleSeedPattern::PhaseSoup;
          break;
        case RuleFamily::Turmite:
          seedPattern = RuleSeedPattern::TurmiteSwarm;
          break;
        case RuleFamily::LatticeGas:
          seedPattern = RuleSeedPattern::ParticleCloud;
          break;
        case RuleFamily::Elementary1D:
        case RuleFamily::VonNeumannTable:
          seedPattern = RuleSeedPattern::SingleCell;
          break;
        case RuleFamily::Sandpile:
          // A lone phase-zero grain source grows the sandpile fractal.
          grid->setCell(CellAddress{ 0, 0 }, 8u);
          return;
        case RuleFamily::LifeLike:
        default:
          seedPattern = RuleSeedPattern::Glider;
          break;
      }
    } else if (rules->getNeighborhoodKind() ==
               RuleSet::NeighborhoodKind::Elementary1D) {
      seedPattern = RuleSeedPattern::SingleCell;
    } else {
      seedPattern = RuleSeedPattern::Glider;
    }
  }

  if (seedPattern == RuleSeedPattern::Wire ||
      (rules->getStateCount() >= 4u && rules->getStateName(0u) == "Head" &&
       rules->getStateName(2u) == "Tail" &&
       rules->getStateName(3u) == "Conductor")) {
    // Horizontal conductor with a head+tail pair so one electron travels right.
    const std::int64_t y = 0;
    const std::int64_t startX = -4;
    for (int i = 0; i < 8; ++i) {
      grid->setCell(CellAddress{ startX + i, y }, 3u);
    }
    grid->setCell(CellAddress{ startX, y }, 0u);
    grid->setCell(CellAddress{ startX + 1, y }, 2u);
    return;
  }

  if (seedPattern == RuleSeedPattern::SingleCell) {
    grid->setCell(CellAddress{ 0, 0 }, 0);
    return;
  }

  if ((seedPattern == RuleSeedPattern::Rle ||
       seedPattern == RuleSeedPattern::LeniaRle) &&
      definition != nullptr) {
    std::vector<RuleSeedCell> cells;
    const bool decoded =
      seedPattern == RuleSeedPattern::LeniaRle
        ? RuleSetRegistry::decodeLeniaSeedRle(
            definition->seedRle, rules->getStateCount(), cells)
        : RuleSetRegistry::decodeSeedRle(
            definition->seedRle, rules->getStateCount(), cells);
    if (decoded && !cells.empty()) {
      int maximumX = 0;
      int maximumY = 0;
      for (const RuleSeedCell& cell : cells) {
        maximumX = std::max(maximumX, cell.x);
        maximumY = std::max(maximumY, cell.y);
      }
      // Center the stamp on the origin so the camera frames it.
      for (const RuleSeedCell& cell : cells) {
        grid->setCell(
          CellAddress{ cell.x - maximumX / 2, cell.y - maximumY / 2 },
          cell.state);
      }
      return;
    }
  }

  if (seedPattern == RuleSeedPattern::ActiveSoup) {
    const int radius = static_cast<int>(seedRadius);
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if (seedHash(x, y, 0xA11CE5EEDULL) % 100u < seedDensity) {
          grid->setCell(CellAddress{ x, y }, 0u);
        }
      }
    }
    return;
  }

  if (seedPattern == RuleSeedPattern::PhaseSoup ||
      seedPattern == RuleSeedPattern::ExcitableBreak) {
    const unsigned int stateCount = rules->getStateCount();
    const int radius = static_cast<int>(seedRadius);
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if (x * x + y * y > radius * radius) {
          continue;
        }
        const std::uint64_t value = seedHash(x, y, 0xC1C1CA14ULL);
        if (seedPattern == RuleSeedPattern::ExcitableBreak &&
            value % 100u >= seedDensity) {
          continue;
        }
        const unsigned int state =
          static_cast<unsigned int>(value % stateCount);
        if (state != SparseCellGrid::BackgroundState) {
          grid->setCell(CellAddress{ x, y }, static_cast<unsigned char>(state));
        }
      }
    }
    return;
  }

  if (seedPattern == RuleSeedPattern::SpeciesSoup) {
    const unsigned int stateCount = rules->getStateCount();
    const unsigned int speciesCount = stateCount - 1u;
    const int radius = static_cast<int>(seedRadius);
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        const std::uint64_t value = seedHash(x, y, 0x5EEC1E5ULL);
        if (value % 100u >= seedDensity) {
          continue;
        }
        unsigned int species = static_cast<unsigned int>(value % speciesCount);
        if (species >= 1u) {
          species += 1u;
        }
        grid->setCell(CellAddress{ x, y }, static_cast<unsigned char>(species));
      }
    }
    return;
  }

  if (seedPattern == RuleSeedPattern::TurmiteSwarm) {
    const unsigned int tapeColorCount = rules->getStateCount() / 5u;
    const std::array<CellAddress, 9> positions = {
      CellAddress{ -8, -8 }, CellAddress{ 0, -9 }, CellAddress{ 8, -8 },
      CellAddress{ -9, 0 },  CellAddress{ 0, 0 },  CellAddress{ 9, 0 },
      CellAddress{ -8, 8 },  CellAddress{ 0, 9 },  CellAddress{ 8, 8 }
    };
    for (std::size_t index = 0u; index < positions.size(); ++index) {
      const unsigned int direction = static_cast<unsigned int>(index % 4u);
      grid->setCell(positions[index],
                    static_cast<unsigned char>(tapeColorCount + direction));
    }
    return;
  }

  if (seedPattern == RuleSeedPattern::ParticleCloud) {
    const int radius = static_cast<int>(seedRadius);
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if (x * x + y * y > radius * radius) {
          continue;
        }
        const std::uint64_t value = seedHash(x, y, 0x6A5C10DULL);
        if (value % 100u >= seedDensity) {
          continue;
        }
        const unsigned int particleMask =
          1u + static_cast<unsigned int>((value >> 8u) % 15u);
        const unsigned char state =
          particleMask == 1u ? 0u : static_cast<unsigned char>(particleMask);
        grid->setCell(CellAddress{ x, y }, state);
      }
    }
    return;
  }

  // Classic Game-of-Life glider (pointing down-right).
  grid->setCell(CellAddress{ 1, 0 }, 0);
  grid->setCell(CellAddress{ 2, 1 }, 0);
  grid->setCell(CellAddress{ 0, 2 }, 0);
  grid->setCell(CellAddress{ 1, 2 }, 0);
  grid->setCell(CellAddress{ 2, 2 }, 0);
}

void
CanvasScene::updatePaintBrushFromInput()
{
  if (ic == nullptr || ic->inputManager == nullptr ||
      ic->commandLine == nullptr || ic->commandLine->isOpen) {
    return;
  }
  // The numeric shortcuts select the first four declared states.
  const RuleSet* rules = cellContext->getRuleSet();
  const unsigned int stateCount = rules->getStateCount();
  const bool selectHead = ic->inputManager->isKeyPressed(KeyCode::H);
  const bool selectTail = ic->inputManager->isKeyPressed(KeyCode::T);
  if (selectHead || selectTail) {
    const std::string wanted = selectHead ? "Head" : "Tail";
    for (unsigned int state = 0u; state < stateCount; ++state) {
      if (rules->getStateName(static_cast<unsigned char>(state)) == wanted) {
        m_paintBrush = static_cast<unsigned char>(state);
        break;
      }
    }
    return;
  }
  if (ic->inputManager->isKeyPressed(KeyCode::Num1)) {
    m_paintBrush = 0u;
  } else if (ic->inputManager->isKeyPressed(KeyCode::Num2)) {
    if (stateCount > 1u)
      m_paintBrush = 1u;
  } else if (ic->inputManager->isKeyPressed(KeyCode::Num3)) {
    if (stateCount > 2u)
      m_paintBrush = 2u;
  } else if (ic->inputManager->isKeyPressed(KeyCode::Num4)) {
    if (stateCount > 3u)
      m_paintBrush = 3u;
  }
}

void
CanvasScene::updateVisualTargets()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateVisualTargets");
  // life → palette target colors (sparse); tickVisual eases displayRgb toward
  // them.
  cellContext->getCanvasView()->rebuildTargetsFromGrid();
}

bool
CanvasScene::consumeCompletedSimulation(bool waitForCompletion)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.consumeCompletedSimulation");
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
    simulationRetryPending = true;
    currentState = CellState::EDIT;
    achievedSimulationTps = 0.0;
    const char* message = "Simulation generation failed; paused without "
                          "publication. Use run or step to retry.";
    Logger::LogError(message);
    ic->commandLine->logError(message);
    showModeSplash("EDIT");
    return true;
  }
  {
    ILLUMO_PROFILE_ZONE("CanvasScene.publishGeneration");
    cellContext->publishSpareGrid(completedDelta);
  }
  mirrorDelta = std::move(completedDelta);
  mirrorDeltaValid = true;
  lastSimulationRunnerTimings = timings;
  lastSimulationStepMilliseconds = elapsedMilliseconds;
  lastSimulationFrameMilliseconds = elapsedMilliseconds;
  // A lane block or a multi-generation serial start publishes several.
  const std::uint32_t generations = std::max(1u, timings.generations);
  lastSimulationSteps += static_cast<int>(generations);
  simulationGeneration += generations;
  simulationStepMetric.add(elapsedMilliseconds);
  simulationMirrorMetric.add(timings.mirrorMilliseconds);
  simulationAdvanceMetric.add(timings.advanceMilliseconds);
  simulationCaptureMetric.add(timings.captureMilliseconds);
  return true;
}

void
CanvasScene::drainSimulation()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.drainSimulation");
  if (cellContext == nullptr) {
    return;
  }
  if (!simulationRunner.canBlock()) {
    // Lane generations finish on a later frame and cannot be waited for:
    // the outstanding one is discarded, and the displayed (published) world
    // is what the caller mutates, saves or leaves.
    if (simulationRunner.isBusy()) {
      Logger::LogTrace("Outstanding lane generation retired; the displayed "
                       "world is kept");
    }
    simulationRunner.retire();
    mirrorDeltaValid = false;
    return;
  }
  while (simulationRunner.isBusy()) {
    if (!consumeCompletedSimulation(true)) {
      break;
    }
  }
}

void
CanvasScene::prepareGridMutation()
{
  drainSimulation();
  mirrorDelta.clear();
  mirrorDeltaValid = false;
}

SimulatorConfiguration
CanvasScene::currentConfiguration() const
{
  SimulatorConfiguration configuration;
  if (cellContext == nullptr || ic == nullptr || ic->envVars == nullptr) {
    return configuration;
  }
  SimulatorSettings::read(ic->envVars, &configuration);
  // The menu shows the world this canvas runs, not the one new canvases get.
  configuration.family = cellContext->getFamilyString();
  configuration.ruleSet = cellContext->getRuleSetString();
  configuration.worldChunkWidth = cellContext->getWorldChunkWidth();
  configuration.worldChunkHeight = cellContext->getWorldChunkHeight();
  return configuration;
}

bool
CanvasScene::applyConfiguration(const SimulatorConfiguration& configuration)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.applyConfiguration");
  if (cellContext == nullptr || ic == nullptr || ic->envVars == nullptr ||
      !SimulatorSettings::valid(configuration)) {
    return false;
  }

  const bool topologyChanged =
    configuration.worldChunkWidth != cellContext->getWorldChunkWidth() ||
    configuration.worldChunkHeight != cellContext->getWorldChunkHeight();
  const bool rulesetChanged =
    configuration.ruleSet != cellContext->getRuleSetString() ||
    configuration.family != cellContext->getFamilyString();
  const bool fullscreenChanged =
    configuration.fullscreen != ic->envVars->getVar("fullscreen").valueAsBool;
  const bool msaaChanged =
    configuration.msaa != SimulatorSettings::msaa(ic->envVars);

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
    if (!cellContext->setRuleSet(configuration.family, configuration.ruleSet) &&
        (configuration.ruleSet != cellContext->getRuleSetString() ||
         configuration.family != cellContext->getFamilyString())) {
      return false;
    }
  }

  // toggleFullscreen() flips whatever fullscreen state envVars currently
  // holds, so it must run before SimulatorSettings::write below overwrites
  // that entry with the newly configured value; otherwise the toggle flips
  // the new value straight back to the old one.
  if (fullscreenChanged && ic->window != nullptr) {
    ic->window->toggleFullscreen();
  }
  inspectorEnabled = configuration.showInspector;
  SimulatorSettings::write(ic->envVars, configuration);
  syncCanvasLookFromEnv();
  // A changed interval counts from now.
  autosaveElapsed = 0.0;

  if (msaaChanged && ic->commandLine != nullptr) {
    ic->commandLine->logNormal(
      "Anti-aliasing (MSAA) changes apply after CSim restarts.");
  }

  if (topologyChanged) {
    seedInitialPattern();
    resetCameraToHome();
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
  syncSimRateFromEnv();
  ic->envVars->save();
  if (rulesetChanged) {
    Logger::LogInfo("Ruleset switched to " +
                    ruleDisplayName(configuration.ruleSet));
  }
  if (topologyChanged) {
    Logger::LogInfo("New world: " +
                    describeTopology(configuration.worldChunkWidth,
                                     configuration.worldChunkHeight));
  }
  Logger::LogTrace(
    "Canvas settings applied: " + std::to_string(configuration.tps) + " tps");
  return true;
}

void
CanvasScene::registerConsoleCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr ||
      ic->commandLine == nullptr) {
    return;
  }

  const std::vector<std::string> rulesets = CellContext::GetKnownModeStrings();
  CommandFn rulesetCommand = [this](const std::vector<std::string>& args) {
    const RuleSetDefinition* activeRule =
      RuleSetRegistry::instance().getRuleSetDefinition(
        cellContext->getRuleSetString());
    const RuleFamilyDefinition* activeFamily =
      RuleSetRegistry::instance().getFamilyDefinition(
        cellContext->getFamilyString());
    if (args.empty()) {
      if (activeFamily != nullptr) {
        ic->commandLine->logNormal("Current family: " + activeFamily->name +
                                   " [" + activeFamily->id + "]");
      }
      if (activeRule != nullptr) {
        ic->commandLine->logNormal("Current ruleset: " + activeRule->name +
                                   " [" + activeRule->id + "]");
      }
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
    const RuleSetDefinition* selectedRule =
      RuleSetRegistry::instance().getRuleSetDefinition(
        cellContext->getRuleSetString());
    const RuleFamilyDefinition* selectedFamily =
      RuleSetRegistry::instance().getFamilyDefinition(
        cellContext->getFamilyString());
    if (selectedFamily != nullptr && selectedRule != nullptr) {
      ic->commandLine->logSuccess(
        "Family: " + selectedFamily->name + " [" + selectedFamily->id +
        "]; ruleset: " + selectedRule->name + " [" + selectedRule->id + "]");
    }
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
      saveCellGameTo(IllumoCodec::withCSimExtension(args[0]), true);
    },
    "save <filename>",
    "Save the current canvas; .csim is added when omitted");

  ic->commandRegistry->RegisterCommand(
    "load",
    [this](const std::vector<std::string>& args) {
      if (args.size() != 1) {
        ic->commandLine->logError("Usage: load <filename>");
        return;
      }
      // Exact name first, then the current and legacy extensions.
      loadCellGameFrom({ args[0],
                         IllumoCodec::withCSimExtension(args[0]),
                         args[0] + ".illumo" },
                       true);
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
      openSaveDialog();
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
      openLoadDialog();
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
      const int completed = stepSimulation(generations);
      if (completed != generations) {
        ic->commandLine->logError(
          "Generation failed after " + std::to_string(completed) + " of " +
          std::to_string(generations) + "; paused. Use step or run to retry.");
        return;
      }
      ic->commandLine->logSuccess(
        "Advanced " + std::to_string(generations) +
        (generations == 1 ? " generation" : " generations"));
    },
    "step [count]",
    "Advance a paused canvas by one or more generations");

  ic->commandRegistry->RegisterCommand(
    "clear_canvas",
    [this](const std::vector<std::string>& args) {
      const bool confirmed = args.size() == 1u && args[0] == "yes";
      if (!args.empty() && !confirmed) {
        ic->commandLine->logError("Usage: clear_canvas [yes]");
        return;
      }
      // With Confirm clearing on, the canvas asks first; "yes" skips it.
      if (!confirmed && exitConfirmDialog != nullptr &&
          SimulatorSettings::flag(ic->envVars, "confirmClear", true)) {
        exitConfirmDialog->openClearCanvas();
        ic->commandLine->logNormal(
          "Confirm in the dialog to clear the canvas (or: clear_canvas yes)");
        return;
      }
      clearCanvas();
    },
    "clear_canvas [yes]",
    "Set every cell to the empty state (asks first unless yes is given)");

  ic->commandRegistry->RegisterCommand(
    "reset_canvas",
    [this](const std::vector<std::string>& args) {
      const bool confirmed = args.size() == 1u && args[0] == "yes";
      if (!args.empty() && !confirmed) {
        ic->commandLine->logError("Usage: reset_canvas [yes]");
        return;
      }
      // Confirm clearing guards a reset too; "yes" skips it.
      if (!confirmed && exitConfirmDialog != nullptr &&
          SimulatorSettings::flag(ic->envVars, "confirmClear", true)) {
        exitConfirmDialog->openResetCanvas();
        ic->commandLine->logNormal(
          "Confirm in the dialog to reset the canvas (or: reset_canvas yes)");
        return;
      }
      resetCanvas();
    },
    "reset_canvas [yes]",
    "Put back the rule's starting pattern (asks first unless yes is given)");

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
      // Seeded from the monotonic clock: the WASM host grants bounded clocks
      // but no entropy import, and a sandbox randomizer needs no secrecy.
      std::mt19937 generator(static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
      std::uniform_real_distribution<double> distribution(0.0, 100.0);
      const RuleSet* rules = cellContext->getRuleSet();
      unsigned char occupiedState = 0u;
      for (unsigned int state = 0u; state < rules->getStateCount(); ++state) {
        if (rules->getStateName(static_cast<unsigned char>(state)) ==
            "Conductor") {
          occupiedState = static_cast<unsigned char>(state);
          break;
        }
      }
      const CellAddress firstCell = canvas->getVisibleFirstCell();
      for (int y = 0; y < canvas->getVisibleCellHeight(); ++y) {
        for (int x = 0; x < canvas->getVisibleCellWidth(); ++x) {
          const bool selected = distribution(generator) < density;
          const unsigned char state =
            selected ? occupiedState : SparseCellGrid::BackgroundState;
          const CellAddress address{ firstCell.x + x, firstCell.y - y };
          canvas->setCanvasPixel(address.x, address.y, state);
        }
      }
      ic->commandLine->logSuccess("Randomized canvas at " +
                                  std::to_string(density) + "% density");
    },
    "randomize [density-percent]",
    "Fill the canvas randomly using its declared active state");

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
      resetCameraToHome();
      ic->commandLine->logSuccess("Camera reset");
    },
    "camera_reset",
    "Center the canvas and restore the home zoom");

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
          zoom < 0.1 || zoom > 100.0 ||
          !CanvasCoordinatePolicy::validPosition(x, y)) {
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
      if (!pastePatternAt(
            clipboard.getClipboardPattern(), originX, originY, &error)) {
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
      if (!importPatternText(joinArguments(args, 0), PatternFormat::Rle)) {
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
      if (!importPatternText(joinArguments(args, 0),
                             PatternFormat::Plaintext)) {
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
      requestMainMenuReturn();
    },
    "menu",
    "Return to the Main Menu");
}

void
CanvasScene::unregisterConsoleCommands()
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
    "rle",     "plaintext",    "inspect",       "menu",         "reset_canvas"
  };
  for (const char* commandName : commandNames) {
    ic->commandRegistry->UnregisterCommand(commandName);
  }
}

void
CanvasScene::setRunning(bool running)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.setRunning");
  prepareGridMutation();
  currentState = running ? CellState::NORMAL : CellState::EDIT;
  paintStrokeActive = false;
  clipboard.clearSelection();
  selectionVisual.setVisible(false);
  editHintsVisual.setVisible(false);
  simAccum = 0.0;
  achievedSimulationTps = 0.0;
  lastSimulationStepMilliseconds = 0.0;
  lastSimulationFrameMilliseconds = 0.0;
  lastSimulationSteps = 0;
  simulationDebtDropped = false;
  simulationDebtFrames = 0;
  simulationDebtReported = false;
  simulationBudgetLimited = false;
  showModeSplash(running ? "NORMAL" : "EDIT");
  ic->commandLine->logSuccess(running ? "Simulation running"
                                      : "Simulation paused in edit mode");
}

int
CanvasScene::stepSimulation(int generations)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.stepSimulation");
  prepareGridMutation();
  currentState = CellState::EDIT;
  simAccum = 0.0;
  showModeSplash("EDIT");
  int completed = 0;
  for (; completed < generations; ++completed) {
    const bool succeeded =
      cellContext->getGrid()->advance(*cellContext->getRuleSet());
    if (!succeeded) {
      simulationRetryPending = true;
      break;
    }
    simulationRetryPending = false;
    simulationGeneration += 1;
  }
  updateVisualTargets();
  return completed;
}

void
CanvasScene::printStatus() const
{
  ILLUMO_PROFILE_ZONE("CanvasScene.printStatus");
  const CanvasView* canvas = cellContext->getCanvasView();
  const SparseAdvanceStats& simulationStats =
    cellContext->getGrid()->getLastAdvanceStats();
  const glm::vec2 cameraPosition = ic->camera->GetPosition();
  ic->commandLine->logNormal(
    std::string("State: ") +
    (currentState == CellState::NORMAL ? "RUNNING" : "PAUSED/EDIT"));
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(
      cellContext->getFamilyString());
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(
      cellContext->getRuleSetString());
  if (family != nullptr) {
    ic->commandLine->logNormal("Family: " + family->name + " [" + family->id +
                               "]");
  }
  if (rule != nullptr) {
    ic->commandLine->logNormal("Ruleset: " + rule->name + " [" + rule->id +
                               "]");
  }
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
  ic->commandLine->logNormal("Execution: " +
                             simulationRunner.describeExecution());
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
    " ms, scroll p50/p95/max=" +
    std::to_string(canvas->getCacheScrollMetric().median()) + "/" +
    std::to_string(canvas->getCacheScrollMetric().p95()) + "/" +
    std::to_string(canvas->getCacheScrollMetric().maximum()) +
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
CanvasScene::syncSimRateFromEnv()
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
CanvasScene::update(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.update");

  // Host erases modules that fail Start; still guard for incomplete fixtures.
  if (cellContext == nullptr || ic == nullptr) {
    return;
  }
  advanceCanvasEntrance(dt);
  if (mainMenuReturnPending) {
    // The accepted exit owns product input until the host replaces this module.
    // Leave the global console toggle and an open console's input alone.
    if (ic->commandLine == nullptr || !ic->commandLine->isOpen) {
      std::queue<InputManager::KeyPressEvent>& keys =
        ic->inputManager->getKeyQueue();
      std::queue<InputManager::KeyPressEvent> preserved;
      while (!keys.empty()) {
        const InputManager::KeyPressEvent event = keys.front();
        keys.pop();
        if (event.key == KeyCode::Grave) {
          preserved.push(event);
        }
      }
      keys.swap(preserved);
      ic->inputManager->clearCharQueue();
    }
    completeMainMenuReturn();
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

  if (exitConfirmDialog != nullptr) {
    exitConfirmDialog->setReducedMotion(
      ic->envVars != nullptr &&
      ic->envVars->getVar("reducedUiMotion").valueAsBool);
  }

  const bool mouseLeftDown =
    ic->inputManager != nullptr &&
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool hamburgerClicked = !consoleOpen && !exitConfirmOpen &&
                                hamburgerHovered && mouseLeftDown &&
                                !hamburgerMouseWasDown;
  hamburgerMouseWasDown = mouseLeftDown;

  if (!consoleOpen && !exitConfirmOpen &&
      (rulesetWorkshopMenu == nullptr || !rulesetWorkshopMenu->isOpen()) &&
      configurationMenu != nullptr &&
      (ic->inputManager->isActionActive("ToggleSettings") ||
       hamburgerClicked)) {
    if (hamburgerClicked) {
      CSimSounds::play(CSimSound::MenuSelect);
    }
    toggleSettingsMenu();
  }

  if (consoleOpen || exitConfirmOpen ||
      (configurationMenu != nullptr && configurationMenu->isOpen())) {
    paintStrokeActive = false;
    clipboard.stopSelectionDrag();
  }

  if (exitConfirmOpen) {
    ILLUMO_PROFILE_ZONE("CanvasScene.updateExitConfirm");
    exitConfirmDialog->tick(static_cast<float>(dt));
    const ExitConfirmAction action =
      consoleOpen ? ExitConfirmAction::None
                  : exitConfirmDialog->update(ic->inputManager);
    if (action == ExitConfirmAction::Confirm) {
      exitConfirmDialog->close();
      Logger::LogTrace("Exit confirmed from the canvas");
      ic->window->requestClose();
    } else if (action == ExitConfirmAction::MainMenu) {
      exitConfirmDialog->close();
      requestMainMenuReturn();
    } else if (action == ExitConfirmAction::ClearCanvas) {
      exitConfirmDialog->close();
      clearCanvas();
    } else if (action == ExitConfirmAction::ResetCanvas) {
      exitConfirmDialog->close();
      resetCanvas();
    } else if (action == ExitConfirmAction::Restart) {
      exitConfirmDialog->close();
      Logger::LogInfo("Restarting to apply settings read at startup");
      ic->window->requestRestart();
    } else if (action == ExitConfirmAction::Cancel) {
      exitConfirmDialog->close();
    }
    updateEditHintsVisual(dt);
    updatePaintPalette(dt);
    updateModeBadge(dt);
    updateEditTools(dt);
    updateEditorCursor(dt);
    updateHamburgerVisual(dt);
    updateSelectionVisual();
    updateInspectorVisual();
    updateVisualTargets();
    cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
    return;
  }

  if (configurationMenu != nullptr && configurationMenu->isOpen()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.updateConfigurationMenu");
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
        Logger::LogWarning("Canvas settings were rejected; the current world "
                           "was preserved");
        configurationMenu->setError(
          "Settings could not be applied; the current world was preserved.");
      } else {
        CSimSounds::play(CSimSound::MenuSelect);
        configurationMenu->close();
        // Settings only a restart applies: offer to restart now.
        if (exitConfirmDialog != nullptr &&
            SimulatorSettings::restartNeeded(configuration, ic->window)) {
          ic->inputManager->clearCharQueue();
          exitConfirmDialog->openRestart(true);
          exitConfirmDialog->tick(static_cast<float>(dt));
        }
      }
    }
    updateEditHintsVisual(dt);
    updatePaintPalette(dt);
    updateModeBadge(dt);
    updateEditTools(dt);
    updateEditorCursor(dt);
    updateHamburgerVisual(dt);
    updateSelectionVisual();
    updateInspectorVisual();
    updateVisualTargets();
    cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
    return;
  }

  if (!consoleOpen && !exitConfirmOpen &&
      (configurationMenu == nullptr || !configurationMenu->isOpen()) &&
      rulesetWorkshopMenu != nullptr && !rulesetWorkshopMenu->isOpen() &&
      consumeKeyPress(ic->inputManager, KeyCode::F2)) {
    ILLUMO_PROFILE_ZONE("CanvasScene.openRulesetWorkshop");
    const RuleSetDefinition* definition =
      RuleSetRegistry::instance().getRuleSetDefinition(
        cellContext->getModeString());
    const RuleFamilyDefinition* family =
      definition == nullptr
        ? nullptr
        : RuleSetRegistry::instance().getFamilyDefinition(definition->familyId);
    if (definition != nullptr && family != nullptr) {
      drainSimulation();
      const bool reducedMotion =
        ic->envVars != nullptr &&
        ic->envVars->getVar("reducedUiMotion").valueAsBool;
      rulesetWorkshopMenu->open(*family, *definition, reducedMotion);
      ic->inputManager->clearCharQueue();
      Logger::LogTrace("Ruleset workshop opened on " + definition->id);
    }
  }

  if (rulesetWorkshopMenu != nullptr && rulesetWorkshopMenu->isOpen()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.updateRulesetWorkshop");
    rulesetWorkshopMenu->tick(static_cast<float>(dt));
    const RulesetWorkshopAction action =
      consoleOpen ? RulesetWorkshopAction::None
                  : rulesetWorkshopMenu->update(ic->inputManager);
    if (action == RulesetWorkshopAction::Cancel) {
      rulesetWorkshopMenu->close();
    } else if (action == RulesetWorkshopAction::Import) {
      const SaveLoadDialogSpec specification{ "CSim Rules Catalog",
                                              "rulesets.json",
                                              "*.JSON" };
      const std::weak_ptr<bool> alive = m_lifetime;
      CSimPlatform::current().chooseLoadLocation(
        specification, [this, alive](const std::string& location) {
          if (!alive.expired() && !location.empty()) {
            importRuleCatalog(location);
          }
        });
    } else if (action == RulesetWorkshopAction::Export) {
      const SaveLoadDialogSpec specification{
        "CSim Rule Definition",
        rulesetWorkshopMenu->getDraft().id + ".json",
        "*.JSON"
      };
      const std::weak_ptr<bool> alive = m_lifetime;
      CSimPlatform::current().chooseSaveLocation(
        specification, [this, alive](const std::string& location) {
          if (!alive.expired() && !location.empty()) {
            exportRuleCatalog(location);
          }
        });
    } else if (action == RulesetWorkshopAction::Apply) {
      ILLUMO_PROFILE_ZONE("CanvasScene.applyWorkshopRuleset");
      // The published grid can advance while the workshop is open. Drain
      // first so validation sees the latest generation and the runner no
      // longer borrows the active RuleSet when it is replaced below.
      prepareGridMutation();
      RuleFamilyDefinition familyDraft = rulesetWorkshopMenu->getFamilyDraft();
      RuleSetDefinition draft = rulesetWorkshopMenu->getDraft();
      bool validFamilyId = true;
      if (rulesetWorkshopMenu->isFamilyDraftChanged() && familyDraft.builtIn) {
        const std::string customFamilyId = uniqueCustomFamilyId(familyDraft.id);
        if (customFamilyId.empty()) {
          rulesetWorkshopMenu->setError(
            "Unable to allocate an ID for the copied family.");
          validFamilyId = false;
        } else {
          familyDraft.id = customFamilyId;
          familyDraft.name = "Custom " + familyDraft.name;
          familyDraft.builtIn = false;
        }
      }
      draft.familyId = familyDraft.id;
      const bool containsInvalidState =
        hasInvalidCellState(*cellContext->getGrid(), familyDraft.stateCount);
      RuleSetRegistry staged = RuleSetRegistry::instance();
      if (!validFamilyId) {
        // The explanatory allocation error was set above.
        Logger::LogWarning("Ruleset workshop change rejected: no free ID for "
                           "the copied family");
      } else if (containsInvalidState) {
        Logger::LogWarning("Ruleset workshop change rejected: the world holds "
                           "states the rule would remove");
        rulesetWorkshopMenu->setError(
          "The current world contains states this rule would remove.");
      } else if (!staged.registerFamily(familyDraft) ||
                 !staged.registerRule(draft)) {
        Logger::LogWarning("Ruleset workshop change rejected: " + draft.id +
                           " is invalid or incompatible with its family");
        rulesetWorkshopMenu->setError(
          "This family and ruleset are invalid or incompatible.");
      } else {
        const RuleSetDefinition* compiled =
          staged.getRuleSetDefinition(draft.id);
        const RuleFamilyDefinition* compiledFamily =
          staged.getFamilyDefinition(familyDraft.id);
        if (compiled == nullptr || compiledFamily == nullptr) {
          Logger::LogError("Ruleset workshop could not stage " + draft.id +
                           " for the user catalog");
          rulesetWorkshopMenu->setError("The user catalog could not be saved.");
        } else {
          std::vector<RuleFamilyDefinition> userFamilies;
          if (!compiledFamily->builtIn) {
            userFamilies.push_back(*compiledFamily);
          }
          const std::weak_ptr<bool> alive = m_lifetime;
          CSimPlatform::current().saveUserCatalog(
            std::move(userFamilies),
            { *compiled },
            [this, alive, staged, draft](bool saved,
                                         const std::string& error) mutable {
              ILLUMO_PROFILE_ZONE("CanvasScene.activateWorkshopRuleset");
              if (alive.expired() || rulesetWorkshopMenu == nullptr) {
                return;
              }
              if (!saved) {
                Logger::LogError("User rule catalog was not saved: " +
                                 (error.empty() ? draft.id : error));
                rulesetWorkshopMenu->setError(
                  error.empty() ? "The user catalog could not be saved."
                                : error);
                return;
              }
              // The overlay write may complete on a later update; the world
              // must not be mid-generation when the rule object is replaced.
              prepareGridMutation();
              RuleSetRegistry::instance() = std::move(staged);
              const bool activePairUnchanged =
                cellContext->getFamilyString() == draft.familyId &&
                cellContext->getRuleSetString() == draft.id;
              const bool activated =
                activePairUnchanged
                  ? cellContext->refreshRuleSet()
                  : cellContext->setRuleSet(draft.familyId, draft.id);
              if (activated) {
                cellContext->getCanvasView()->rebuildPalette(
                  cellContext->getRuleSet());
                m_paintBrush = 0u;
                updateVisualTargets();
                CSimSounds::play(CSimSound::MenuSelect);
                rulesetWorkshopMenu->close();
                Logger::LogInfo("Ruleset workshop saved and activated " +
                                ruleDisplayName(draft.id) + " (family " +
                                draft.familyId + ")");
              } else {
                Logger::LogError("Saved rule " + draft.id +
                                 " could not be activated; the world was "
                                 "preserved");
                rulesetWorkshopMenu->setError(
                  "The saved rule could not be "
                  "activated; the world was preserved.");
              }
            });
        }
      }
    }
    paintStrokeActive = false;
    updateEditHintsVisual(dt);
    updatePaintPalette(dt);
    updateModeBadge(dt);
    updateEditTools(dt);
    updateEditorCursor(dt);
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
    updateEditHintsVisual(dt);
    updatePaintPalette(dt);
    updateModeBadge(dt);
    updateEditTools(dt);
    updateEditorCursor(dt);
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
      ILLUMO_PROFILE_ZONE("CanvasScene.applyModeString");
      prepareGridMutation();
      if (cellContext->setRuleSet(wanted)) {
        std::string msg = "Active ruleset: " + cellContext->getModeString();
        Logger::LogInfo(msg.c_str());
        // Same life values, new colors → rebuild palette only (no cell
        // re-upload).
        cellContext->getCanvasView()->rebuildPalette(cellContext->getRuleSet());
        m_paintBrush = 0u;
      }
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
    clipboard.clearSelection();
    selectionVisual.setVisible(false);
    lastSimulationSteps = 0;
    simulationDebtDropped = false;
    simulationDebtFrames = 0;
    simulationDebtReported = false;
    simulationBudgetLimited = false;
  }

  updateEditHintsVisual(dt);
  updatePaintPalette(dt);
  updateModeBadge(dt);
  updateEditTools(dt);

  // Palette gestures own pointer input until both mouse buttons are released.
  // An open context menu holds the camera still (its arrows walk the rows).
  if (!ic->commandLine->isOpen && !m_paintPaletteHovered &&
      !m_paintPaletteCapturing && !m_contextMenu.isOpen()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.cameraInput");
    CameraPan();

    // Zoom behavior using scroll offset
    std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
    glm::dvec2 worldMouse = ic->camera->ScreenToWorldPrecise(
      glm::dvec2(mouseCoords[0], mouseCoords[1]));
    keyboardPan(dt);
    double* scroll = ic->inputManager->getMouseScrollOffset();
    const bool overChrome =
      isPointerOverEditHints() || isPointerOverEditTools();
    if (*scroll != 0.0f && !overChrome) {
      // One notch zooms by the persisted step (15% by default); invert
      // reverses the wheel.
      const double step =
        SimulatorSettings::number(ic->envVars,
                                  "zoomStep",
                                  0.15,
                                  SimulatorSettings::kMinimumZoomStep,
                                  SimulatorSettings::kMaximumZoomStep);
      const bool zoomIn =
        (*scroll > 0.0f) !=
        SimulatorSettings::flag(ic->envVars, "invertZoom", false);
      const double zoomFactor = zoomIn ? 1.0 + step : 1.0 - step;
      const glm::dvec2 target = ic->camera->GetTargetPositionPrecise();
      const float oldZoom = ic->camera->GetTargetZoom();
      const float newZoom =
        std::clamp(oldZoom * static_cast<float>(zoomFactor), 0.1f, 100.0f);
      const glm::dvec2 next =
        worldMouse -
        (worldMouse - target) * static_cast<double>(oldZoom / newZoom);
      if (CanvasCoordinatePolicy::validPosition(next.x, next.y)) {
        ic->camera->ZoomAt(static_cast<float>(zoomFactor), worldMouse);
      }
    }
    if (overChrome) {
      *scroll = 0.0;
    }
  } else if (m_contextMenu.isOpen()) {
    *ic->inputManager->getMouseScrollOffset() = 0.0;
  }

  updateAutosave(dt);

  // State dependent behavior
  switch (currentState) {
    case CellState::NORMAL:
      Normal(dt);
      break;
    case CellState::EDIT:
      Edit(dt);
      break;
    default:
      break;
  }

  // Hide in NORMAL (and while console is open); track mouse only in EDIT.
  if (ic->commandLine == nullptr || !ic->commandLine->isOpen) {
    handleEditorHotkeys();
  }
  updateEditorCursor(dt);
  updateHamburgerVisual(dt);
  updateSelectionVisual();
  updateInspectorVisual();
  // Map dirty life cells to palette target colors, then ease display toward
  // them.
  updateVisualTargets();
  cellContext->getCanvasView()->tickVisual(static_cast<float>(dt));
}

void
CanvasScene::stop()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.stop");
  // Late platform completions must not touch a module that has exited.
  if (m_lifetime) {
    Logger::LogTrace("Canvas closed");
  }
  m_lifetime.reset();
  if (CSimSounds::musicPlaying(CSimMusic::CanvasEdit)) {
    CSimSounds::stopMusic();
  }
  if (inputContextId >= 0 && ic != nullptr && ic->inputManager != nullptr) {
    ic->inputManager->unregisterInputContext(inputContextId);
    inputContextId = -1;
  }
  drainSimulation();
  simulationRunner.shutdown();
  restoreRender3dTestCamera();
  unregisterConsoleCommands();
  exitConfirmDialog.reset();
  rulesetWorkshopMenu.reset();
  configurationMenu.reset();
  modeBadge.hide();
  m_contextMenu.close();
  m_actionBar.hide();
  render3dScene.reset();
  render3dLoadFailed = false;
  m_hamburgerHaloVisual.clearPrimitives();
  m_hamburgerHaloVisual.setVisible(false);
  hamburgerVisual.clearPrimitives();
  hamburgerVisual.setVisible(false);
  m_hamburgerHaloKey.invalidate();
  m_hamburgerKey.invalidate();
  m_hamburgerShown = false;
  for (GameVisual* visual : { &m_paintBubbleHaloVisual,
                              &m_paintPaletteVisual,
                              &m_paintDropVisual,
                              &m_paintPaletteTopVisual }) {
    visual->clearPrimitives();
    visual->setVisible(false);
  }
  m_paintBubbleHaloKey.invalidate();
  m_paintPaletteKey.invalidate();
  m_paintDropKey.invalidate();
  m_paintPaletteTopKey.invalidate();
  canvasEntranceVisual.clearPrimitives();
  canvasEntranceVisual.setVisible(false);
  canvasEntranceElapsed = kCanvasEntranceSeconds;
  delete cellContext;
  cellContext = nullptr;
}

void
CanvasScene::Normal(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.Normal");
  syncSimRateFromEnv();
  if (dt < 0.0) {
    dt = 0.0;
  }
  if (dt > 0.25) {
    dt = 0.25;
  }

  simAccum += dt;
  simulationBudgetLimited = false;
  if ((simulationRetryPending || simAccum >= simStepSeconds) &&
      !simulationRunner.isBusy()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.submitGeneration");
    SparseGenerationDelta transferDelta;
    const bool useMirrorDelta = mirrorDeltaValid;
    if (useMirrorDelta) {
      transferDelta = std::move(mirrorDelta);
    }
    mirrorDeltaValid = false;
    // Every whole step due goes to one start; the runner accepts what it can
    // publish at once (lane blocks, budgeted serial generations) and the
    // rest is dropped below as debt. A block already running ahead may accept
    // more than is due, leaving the accumulator behind until it catches up.
    const double due = std::floor(simAccum / simStepSeconds);
    const std::uint32_t requested =
      simulationRetryPending || !(due >= 1.0)
        ? 1u
        : static_cast<std::uint32_t>(std::min(due, 4096.0));
    std::uint32_t accepted = 1u;
    if (simulationRunner.start(cellContext->getSpareGrid(),
                               cellContext->getGrid(),
                               cellContext->getRuleSet(),
                               std::move(transferDelta),
                               useMirrorDelta,
                               requested,
                               &accepted)) {
      if (!simulationRetryPending) {
        simAccum -= simStepSeconds * static_cast<double>(accepted);
      }
      simulationRetryPending = false;
    }
  } else if (simAccum >= simStepSeconds) {
    ILLUMO_PROFILE_FRAME_MARK("Sim.inFlightDeferred");
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
    ILLUMO_PROFILE_FRAME_MARK("Sim.debtDropped");
    simAccum = std::fmod(simAccum, simStepSeconds);
    simulationDebtDropped = true;
    // A sustained shortfall, not a single hitch, is worth one warning.
    if (simulationDebtFrames < kSimulationDebtWarningFrames) {
      simulationDebtFrames += 1;
    }
    if (simulationDebtFrames == kSimulationDebtWarningFrames &&
        !simulationDebtReported) {
      simulationDebtReported = true;
      Logger::LogWarning("Simulation is behind its requested " +
                         std::to_string(static_cast<long long>(
                           std::llround(requestedSimulationTps))) +
                         " tps; overdue generations are skipped");
    }
  }
}

void
CanvasScene::normalizeSelection(std::int64_t* x0,
                                   std::int64_t* y0,
                                   std::int64_t* x1,
                                   std::int64_t* y1) const
{
  CellClipboard::normalizeSelection(x0, y0, x1, y1);
}

bool
CanvasScene::captureSelection(CellPattern* pattern, std::string* error)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.captureSelection");
  if (cellContext == nullptr || cellContext->getGrid() == nullptr) {
    if (error != nullptr) {
      *error = "Grid unavailable";
    }
    return false;
  }
  return clipboard.captureSelection(cellContext->getGrid(), pattern, error);
}

bool
CanvasScene::pastePatternAt(const CellPattern& pattern,
                               std::int64_t originX,
                               std::int64_t originY,
                               std::string* error)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.pastePatternAt");
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
CanvasScene::fillSelection(unsigned char state)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.fillSelection");
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
CanvasScene::copySelection()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.copySelection");
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
CanvasScene::cutSelection()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.cutSelection");
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
CanvasScene::pasteAtCursor()
{
  if (isPointerOverEditHints()) {
    return false;
  }
  return pasteAt(hoverX, hoverY);
}

bool
CanvasScene::pasteAt(std::int64_t originX, std::int64_t originY)
{
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  // The origin is fixed at request time; the text may arrive on a later update.
  const std::shared_ptr<int> outcome = std::make_shared<int>(-1);
  const std::weak_ptr<bool> alive = m_lifetime;
  CSimPlatform::current().readClipboard(
    [this, alive, outcome, originX, originY](bool available,
                                             const std::string& text) {
      ILLUMO_PROFILE_ZONE("CanvasScene.pasteClipboardText");
      *outcome = 0;
      if (alive.expired() || cellContext == nullptr ||
          cellContext->getGrid() == nullptr ||
          cellContext->getCanvasView() == nullptr) {
        return;
      }
      if (!available) {
        Logger::LogError("Clipboard text is unavailable");
        return;
      }
      prepareGridMutation();
      std::string error;
      if (!clipboard.pasteText(cellContext->getGrid(),
                               cellContext->getCanvasView(),
                               text,
                               originX,
                               originY,
                               &error)) {
        Logger::LogError(error.c_str());
        return;
      }
      *outcome = 1;
      updateVisualTargets();
      Logger::LogTrace("Pasted clipboard text at (" + std::to_string(originX) +
                       ", " + std::to_string(originY) + ")");
    });
  return *outcome != 0;
}

bool
CanvasScene::stampNamed(const std::string& name)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.stampNamed");
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  const std::int64_t originX = hoverValid ? hoverX : 0;
  const std::int64_t originY = hoverValid ? hoverY : 0;
  std::string error;
  const bool result = clipboard.stampNamed(cellContext->getGrid(),
                                           cellContext->getCanvasView(),
                                           name,
                                           originX,
                                           originY,
                                           &error);
  if (result) {
    updateVisualTargets();
  }
  return result;
}

bool
CanvasScene::importPatternText(const std::string& text, PatternFormat format)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.importPatternText");
  if (cellContext == nullptr || cellContext->getGrid() == nullptr ||
      cellContext->getCanvasView() == nullptr) {
    return false;
  }
  prepareGridMutation();
  const std::int64_t originX = hoverValid ? hoverX : 0;
  const std::int64_t originY = hoverValid ? hoverY : 0;
  std::string error;
  const bool result = clipboard.importPatternText(cellContext->getGrid(),
                                                  cellContext->getCanvasView(),
                                                  text,
                                                  originX,
                                                  originY,
                                                  &error,
                                                  format);
  if (!result) {
    Logger::LogError(error.c_str());
    return false;
  }
  updateVisualTargets();
  return true;
}

void
CanvasScene::handleEditorHotkeys()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.handleEditorHotkeys");
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

  if (currentState == CellState::EDIT && copyDown && !copyHeld) {
    if (copySelection()) {
      Logger::LogTrace("Selection copied to the clipboard");
    }
  }
  if (currentState == CellState::EDIT && cutDown && !cutHeld) {
    if (cutSelection()) {
      Logger::LogTrace("Selection cut to the clipboard");
    }
  }
  if (currentState == CellState::EDIT && pasteDown && !pasteHeld) {
    pasteAtCursor();
  }
  if (currentState == CellState::EDIT && rotateDown && !rotateHeld &&
      !control) {
    clipboard.rotateCw();
  }
  if (currentState == CellState::EDIT && flipDown && !flipHeld && !control) {
    clipboard.flipHorizontal();
  }
  if (inspectDown && !inspectHeld && !control) {
    inspectorEnabled = !inspectorEnabled;
    ic->envVars->setVar("showInspector", inspectorEnabled);
  }
  if (currentState == CellState::EDIT && deleteDown && !deleteHeld) {
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

bool
CanvasScene::isCellInSelection(std::int64_t cellX, std::int64_t cellY) const
{
  if (!clipboard.hasSelection()) {
    return false;
  }
  std::int64_t x0 = 0;
  std::int64_t y0 = 0;
  std::int64_t x1 = 0;
  std::int64_t y1 = 0;
  clipboard.getNormalizedSelection(&x0, &y0, &x1, &y1);
  return cellX >= x0 && cellX <= x1 && cellY >= y0 && cellY <= y1;
}

bool
CanvasScene::pointerCell(std::int64_t* cellX, std::int64_t* cellY) const
{
  if (ic == nullptr || ic->window == nullptr || ic->camera == nullptr) {
    return false;
  }
  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const glm::dvec2 world =
    ic->camera->ScreenToWorldPrecise(glm::dvec2(mouse[0], mouse[1]));
  return CanvasCoordinatePolicy::tryWorldToCell(world.x, cellX) &&
         CanvasCoordinatePolicy::tryWorldToCell(world.y, cellY);
}

bool
CanvasScene::isPointerOverEditTools() const
{
  return m_actionBar.isPointerOver() || m_contextMenu.containsPointer();
}

std::string
CanvasScene::paintBrushName() const
{
  if (cellContext == nullptr || cellContext->getRuleSet() == nullptr) {
    return std::string();
  }
  return cellContext->getRuleSet()->getStateName(m_paintBrush);
}

ColorRgba
CanvasScene::paintBrushColor() const
{
  if (cellContext == nullptr || cellContext->getRuleSet() == nullptr) {
    return UiTheme::textPrimary();
  }
  unsigned char rgb[3]{};
  cellContext->getRuleSet()->evalCell(m_paintBrush, rgb);
  return ColorRgba{ rgb[0], rgb[1], rgb[2], 255 };
}

void
CanvasScene::openContextMenu(std::int64_t cellX, std::int64_t cellY)
{
  m_contextMenuCellX = cellX;
  m_contextMenuCellY = cellY;
  m_contextMenu.open(paintBrushName(),
                     paintBrushColor(),
                     ic->inputManager,
                     ic->envVars != nullptr &&
                       ic->envVars->getVar("reducedUiMotion").valueAsBool);
  // The right button that opened it stays with the menu until released.
  m_editToolsCapturing = true;
  CSimSounds::play(CSimSound::MenuHover);
}

void
CanvasScene::updateEditTools(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateEditTools");
  if (ic == nullptr || ic->inputManager == nullptr || cellContext == nullptr) {
    return;
  }
  const float step =
    std::isfinite(dt) && dt > 0.0 ? static_cast<float>(dt) : 0.0f;
  const bool leftDown =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool rightDown =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
  m_rightPressEdge = rightDown && !m_rightMouseWasDown;
  m_rightMouseWasDown = rightDown;
  if (!leftDown && !rightDown) {
    m_editToolsCapturing = false;
  }
  const bool overlaysOpen =
    (ic->commandLine != nullptr && ic->commandLine->isOpen) ||
    (rulesetWorkshopMenu != nullptr && rulesetWorkshopMenu->isOpen()) ||
    (configurationMenu != nullptr && configurationMenu->isOpen()) ||
    (exitConfirmDialog != nullptr && exitConfirmDialog->isOpen());
  const bool editActive =
    currentState == CellState::EDIT && !overlaysOpen && !mainMenuReturnPending;
  const bool reducedMotion = ic->envVars != nullptr &&
                             ic->envVars->getVar("reducedUiMotion").valueAsBool;

  // The menu belongs to a live selection in EDIT mode.
  if (m_contextMenu.isOpen() && (!editActive || !clipboard.hasSelection())) {
    m_contextMenu.close();
  }
  CanvasEditAction action = CanvasEditAction::None;
  bool fromContextMenu = false;
  bool dismissed = false;
  if (m_contextMenu.isOpen()) {
    action = m_contextMenu.update(ic->inputManager, step, reducedMotion);
    fromContextMenu = action != CanvasEditAction::None;
    if (fromContextMenu || m_contextMenu.dismissedByPress()) {
      // Neither the choosing press nor the dismissing one reaches the world.
      m_editToolsCapturing = m_editToolsCapturing || leftDown || rightDown;
    }
    if (m_contextMenu.dismissedByPress()) {
      dismissed = true;
      // Right-clicking elsewhere on the selection moves the menu there.
      std::int64_t cellX = 0;
      std::int64_t cellY = 0;
      if (m_contextMenu.dismissedByRightPress() && !leftDown &&
          !m_actionBar.isPointerOver() && pointerCell(&cellX, &cellY) &&
          isCellInSelection(cellX, cellY)) {
        openContextMenu(cellX, cellY);
      }
    }
  }

  CanvasActionBarState bar;
  bar.interactive = editActive;
  bar.lift = m_editChromeLift;
  bar.selection = clipboard.hasSelection();
  if (bar.selection) {
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    clipboard.getNormalizedSelection(&x0, &y0, &x1, &y1);
    // Unsigned differences stay exact across the whole signed range.
    const std::uint64_t width =
      static_cast<std::uint64_t>(x1) - static_cast<std::uint64_t>(x0) + 1u;
    const std::uint64_t height =
      static_cast<std::uint64_t>(y1) - static_cast<std::uint64_t>(y0) + 1u;
    bar.selectionLabel = groupDigits(width) + " x " + groupDigits(height);
  }
  bar.brushColor = paintBrushColor();
  bar.reducedMotion = reducedMotion;
  const CanvasEditAction barAction = m_actionBar.update(
    bar,
    ic->inputManager,
    step,
    !dismissed && !m_contextMenu.isOpen() && action == CanvasEditAction::None);
  if (m_actionBar.isPointerOver() && (leftDown || rightDown) &&
      !paintStrokeActive && !clipboard.isSelecting()) {
    m_editToolsCapturing = true;
  }
  if (action == CanvasEditAction::None) {
    action = barAction;
  }
  if (action != CanvasEditAction::None) {
    runEditAction(action, fromContextMenu);
  }
}

void
CanvasScene::runEditAction(CanvasEditAction action, bool fromContextMenu)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.runEditAction");
  switch (action) {
    case CanvasEditAction::Copy:
      if (copySelection()) {
        Logger::LogTrace("Selection copied to the clipboard");
      }
      break;
    case CanvasEditAction::Cut:
      if (cutSelection()) {
        Logger::LogTrace("Selection cut to the clipboard");
      }
      break;
    case CanvasEditAction::Paste: {
      // The menu pastes where it was opened; the toolbar pastes over the
      // selection, or at the middle of the view without one.
      std::int64_t originX = 0;
      std::int64_t originY = 0;
      if (fromContextMenu) {
        originX = m_contextMenuCellX;
        originY = m_contextMenuCellY;
      } else if (clipboard.hasSelection()) {
        std::int64_t x1 = 0;
        std::int64_t y1 = 0;
        clipboard.getNormalizedSelection(&originX, &originY, &x1, &y1);
      } else if (ic != nullptr && ic->camera != nullptr) {
        const glm::dvec2 center = ic->camera->GetPositionPrecise();
        if (!CanvasCoordinatePolicy::tryWorldToCell(center.x, &originX) ||
            !CanvasCoordinatePolicy::tryWorldToCell(center.y, &originY)) {
          originX = 0;
          originY = 0;
        }
      }
      pasteAt(originX, originY);
      break;
    }
    case CanvasEditAction::Fill:
      fillSelection(m_paintBrush);
      break;
    case CanvasEditAction::Erase:
      fillSelection(SparseCellGrid::BackgroundState);
      break;
    case CanvasEditAction::Deselect:
      clipboard.clearSelection();
      break;
    case CanvasEditAction::ClearCanvas:
      // With Confirm clearing on, the canvas asks first (as clear_canvas does).
      if (exitConfirmDialog != nullptr &&
          SimulatorSettings::flag(ic->envVars, "confirmClear", true)) {
        ic->inputManager->clearCharQueue();
        exitConfirmDialog->openClearCanvas();
      } else {
        clearCanvas();
      }
      break;
    case CanvasEditAction::ResetCanvas:
      // Confirm clearing guards a reset too: both discard the current world.
      if (exitConfirmDialog != nullptr &&
          SimulatorSettings::flag(ic->envVars, "confirmClear", true)) {
        ic->inputManager->clearCharQueue();
        exitConfirmDialog->openResetCanvas();
      } else {
        resetCanvas();
      }
      break;
    case CanvasEditAction::Save:
      openSaveDialog();
      break;
    case CanvasEditAction::Load:
      openLoadDialog();
      break;
    case CanvasEditAction::None:
      break;
  }
}

void
CanvasScene::updateEditHintsVisual(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateEditHintsVisual");
  const bool overlaysOpen =
    (ic->commandLine != nullptr && ic->commandLine->isOpen) ||
    (rulesetWorkshopMenu != nullptr && rulesetWorkshopMenu->isOpen()) ||
    (configurationMenu != nullptr && configurationMenu->isOpen()) ||
    (exitConfirmDialog != nullptr && exitConfirmDialog->isOpen());
  const bool editChromeActive =
    currentState == CellState::EDIT && !overlaysOpen;
  const bool reducedMotion = ic->envVars != nullptr &&
                             ic->envVars->getVar("reducedUiMotion").valueAsBool;
  if (editChromeActive != m_modeChromeTarget) {
    m_modeChromeTarget = editChromeActive;
    m_hintsModeDelay = editChromeActive ? 0.0 : 0.08;
    m_paletteModeDelay = editChromeActive ? 0.08 : 0.0;
  }
  if (overlaysOpen || reducedMotion) {
    m_editChromeSpring.snapTo(editChromeActive ? 1.0f : 0.0f);
    m_paintPaletteChromeSpring.snapTo(editChromeActive ? 1.0f : 0.0f);
    m_hintsModeDelay = 0.0;
    m_paletteModeDelay = 0.0;
  } else {
    advanceModeChromeSpring(
      &m_editChromeSpring, &m_hintsModeDelay, editChromeActive, dt);
    advanceModeChromeSpring(
      &m_paintPaletteChromeSpring, &m_paletteModeDelay, editChromeActive, dt);
  }
  // The lifts overshoot for the bounce; the reveals stay within 0..1 because
  // they also size the canvas inset, which must never bounce.
  m_editChromeLift = m_editChromeSpring.value();
  m_paintPaletteChromeLift = m_paintPaletteChromeSpring.value();
  m_editChromeReveal = std::clamp(m_editChromeLift, 0.0f, 1.0f);
  m_paintPaletteChromeReveal = std::clamp(m_paintPaletteChromeLift, 0.0f, 1.0f);

  bool hintsEnabled = true;
  if (ic->envVars != nullptr) {
    const EnvVar& hintsVar = ic->envVars->getVar("editHints");
    hintsEnabled = hintsVar.value.empty() || hintsVar.valueAsBool;
  }
  const bool buildHints =
    editChromeActive && hintsEnabled && ic->window != nullptr;
  if (!buildHints) {
    // Retain the last footer geometry while it slides away. A disabled hint
    // preference releases its band immediately without hiding the palette.
    if ((editChromeActive && !hintsEnabled) || ic->window == nullptr) {
      editHintsVisual.clearPrimitives();
      m_editHintsLayout = EditHintsLayout{};
      editHintsFullInsetPixels = 0;
    }
    const float scale = ic->renderer != nullptr
                          ? std::max(1.0f, ic->renderer->getUiScale())
                          : 1.0f;
    editHintsInsetPixels = static_cast<int>(std::ceil(
      static_cast<float>(editHintsFullInsetPixels) * m_editChromeReveal));
    Transform2D transform;
    transform.y = (1.0f - m_editChromeLift) *
                  static_cast<float>(editHintsFullInsetPixels) / scale;
    editHintsVisual.setTransform(transform);
    const bool visible =
      editHintsFullInsetPixels > 0 && m_editChromeReveal > 0.001f;
    editHintsVisual.setVisible(visible);
    if (!visible) {
      editHintsVisual.clearPrimitives();
      m_editHintsLayout = EditHintsLayout{};
      editHintsFullInsetPixels = 0;
      editHintsInsetPixels = 0;
    }
    if (cellContext != nullptr) {
      cellContext->getCanvasView()->setBottomInsetPixels(editHintsInsetPixels);
    }
    return;
  }

  const float scale = std::max(1.0f, ic->renderer->getUiScale());
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const float width = static_cast<float>(dimensions[0]) / scale;
  const float height = static_cast<float>(dimensions[1]) / scale;
  if (width < 120.0f || height < 100.0f) {
    editHintsVisual.clearPrimitives();
    m_editHintsLayout = EditHintsLayout{};
    editHintsFullInsetPixels = 0;
    editHintsInsetPixels = 0;
    editHintsVisual.setVisible(false);
    cellContext->getCanvasView()->setBottomInsetPixels(0);
    return;
  }
  // The footer depends only on these inputs; while they hold, its
  // primitives are kept and only the slide below updates.
  const std::shared_ptr<Font> font = Font::getDefaultFont();
  EditHintsLayout layout;
  layout.windowWidth = dimensions[0];
  layout.windowHeight = dimensions[1];
  layout.scale = scale;
  layout.selection = clipboard.hasSelection();
  layout.buffer = !clipboard.getClipboardPattern().empty();
  layout.ruleSetRevision = cellContext->getRuleSetRevision();
  layout.font = font.get();
  layout.built = true;
  if (!(layout == m_editHintsLayout)) {
    m_editHintsLayout = layout;
    buildEditHints(width, height, scale, dimensions[1], font);
  }
  editHintsInsetPixels = static_cast<int>(std::ceil(
    static_cast<float>(editHintsFullInsetPixels) * m_editChromeReveal));
  Transform2D transform;
  transform.y = (1.0f - m_editChromeLift) * m_editHintsPanelHeight;
  editHintsVisual.setTransform(transform);
  editHintsVisual.setVisible(m_editChromeReveal > 0.001f);
  cellContext->getCanvasView()->setBottomInsetPixels(editHintsInsetPixels);
}

void
CanvasScene::buildEditHints(float width,
                               float height,
                               float scale,
                               int windowHeight,
                               const std::shared_ptr<Font>& font)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.buildEditHints");
  editHintsVisual.clearPrimitives();
  std::vector<std::string> hints = {
    "Left drag: paint", "Right drag: erase", "Shift+Left drag: select",
    "Middle drag: pan", "Wheel: zoom",       "Ctrl+V: paste",
    "E: run",           "I: inspector",      "F1: settings",
    "F2: rules"
  };
  if (clipboard.hasSelection()) {
    hints.emplace_back("Ctrl+C/X: copy/cut");
    hints.emplace_back("Delete: erase selection");
    hints.emplace_back("Right-click selection: menu");
  }
  if (!clipboard.getClipboardPattern().empty()) {
    hints.emplace_back("R/F: rotate/flip buffer");
  }
  std::string stateHints;
  const RuleSet* rules = cellContext->getRuleSet();
  const unsigned int shortcutCount = std::min(rules->getStateCount(), 4u);
  for (unsigned int state = 0u; state < shortcutCount; ++state) {
    std::string stateName =
      rules->getStateName(static_cast<unsigned char>(state));
    if (!stateName.empty() && stateName[0] >= 'A' && stateName[0] <= 'Z') {
      stateName[0] = static_cast<char>(stateName[0] + ('a' - 'A'));
    }
    if (!stateHints.empty()) {
      stateHints += "  ";
    }
    stateHints += std::to_string(state + 1u);
    if (stateName == "head") {
      stateHints += "/H";
    } else if (stateName == "tail") {
      stateHints += "/T";
    }
    stateHints += ": " + stateName;
  }
  hints.emplace_back(stateHints.empty() ? "1-4: choose a paint state"
                                        : stateHints);
  // Wrap complete hints, preserving each key/action pair at narrow sizes.
  const float availableWidth = width - 24.0f;
  const std::function<float(const std::string&, float)> measureWidth =
    [&font](const std::string& text, float size) {
      return font != nullptr ? font->measureText(text, size).width
                             : GuiKit::estimateTextWidth(text, size);
    };
  float fontSize = 11.0f;
  for (const std::string& hint : hints) {
    const float hintWidth = measureWidth(hint, fontSize);
    if (hintWidth > availableWidth) {
      fontSize *= availableWidth / hintWidth;
    }
  }
  std::vector<std::string> lines;
  std::string line;
  for (const std::string& hint : hints) {
    const std::string candidate = line.empty() ? hint : line + "    " + hint;
    if (!line.empty() && measureWidth(candidate, fontSize) > availableWidth) {
      lines.push_back(line);
      line = hint;
    } else {
      line = candidate;
    }
  }
  lines.push_back(line);
  float lineHeight = font != nullptr ? font->getLineHeight(fontSize)
                                     : GuiKit::defaultLineHeight(fontSize);
  float panelHeight = lineHeight * static_cast<float>(lines.size()) + 12.0f;
  if (panelHeight > height - 16.0f) {
    const float fit = (height - 28.0f) / (panelHeight - 12.0f);
    fontSize *= fit;
    lineHeight *= fit;
    panelHeight = height - 16.0f;
  }
  editHintsFullInsetPixels =
    std::min(windowHeight, static_cast<int>(std::ceil(panelHeight * scale)));
  panelHeight = static_cast<float>(editHintsFullInsetPixels) / scale;
  const float top = height - panelHeight;
  // An opaque glass footer reserves its own band; no canvas shows through.
  // The legend wears the same teal-to-indigo face as the settings button,
  // the toolbar and the paint bubble.
  const ColorRgba footerTop = CanvasChromeStyle::faceTop(0.0f);
  const ColorRgba footerBottom = CanvasChromeStyle::faceBottom(0.0f);
  editHintsVisual.addGradientRect(0.0f,
                                  top,
                                  width,
                                  panelHeight,
                                  footerTop,
                                  footerTop,
                                  footerBottom,
                                  footerBottom);
  // The footer continues below the screen edge, so a bar that springs past
  // its slot stretches upward instead of opening a gap beneath it.
  editHintsVisual.addFilledRect(
    0.0f, top + panelHeight, width, panelHeight, footerBottom);
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = CanvasChromeStyle::halo(0.0f);
  // A soft glow hugs the lit top edge: it rises into the canvas and sinks a
  // little way into the bar, cyan in the middle fading toward cyan and violet
  // at the ends like the hairline it surrounds. It breathes in step with the
  // hamburger's halo (the same clock and period), swelling and brightening.
  const bool stillHints = ic->envVars != nullptr &&
                          ic->envVars->getVar("reducedUiMotion").valueAsBool;
  const float hintBreath =
    stillHints ? 0.5f
               : 0.5f + 0.5f * std::sin(static_cast<float>(m_hamburgerClock) *
                                        6.2831853f / 3.2f);
  const float glowStrength = 0.28f + 0.1f * hintBreath;
  const float glowAbove = 11.0f + 4.0f * hintBreath;
  const float glowBelow = 5.0f + 1.5f * hintBreath;
  const ColorRgba glowCenter = UiTheme::fade(cyan, glowStrength);
  const ColorRgba glowLeft = UiTheme::fade(cyan, glowStrength * 0.22f);
  const ColorRgba glowRight = UiTheme::fade(violet, glowStrength * 0.3f);
  editHintsVisual.addGradientRect(0.0f,
                                  top - glowAbove,
                                  width * 0.5f,
                                  glowAbove,
                                  UiTheme::transparentOf(glowLeft),
                                  UiTheme::transparentOf(glowCenter),
                                  glowCenter,
                                  glowLeft);
  editHintsVisual.addGradientRect(width * 0.5f,
                                  top - glowAbove,
                                  width * 0.5f,
                                  glowAbove,
                                  UiTheme::transparentOf(glowCenter),
                                  UiTheme::transparentOf(glowRight),
                                  glowRight,
                                  glowCenter);
  editHintsVisual.addGradientRect(0.0f,
                                  top,
                                  width * 0.5f,
                                  glowBelow,
                                  UiTheme::fade(glowLeft, 0.6f),
                                  UiTheme::fade(glowCenter, 0.6f),
                                  UiTheme::transparentOf(glowCenter),
                                  UiTheme::transparentOf(glowLeft));
  editHintsVisual.addGradientRect(width * 0.5f,
                                  top,
                                  width * 0.5f,
                                  glowBelow,
                                  UiTheme::fade(glowCenter, 0.6f),
                                  UiTheme::fade(glowRight, 0.6f),
                                  UiTheme::transparentOf(glowRight),
                                  UiTheme::transparentOf(glowCenter));
  editHintsVisual.addGradientRect(0.0f,
                                  top,
                                  width * 0.5f,
                                  1.0f,
                                  UiTheme::fade(cyan, 0.15f),
                                  cyan,
                                  cyan,
                                  UiTheme::fade(cyan, 0.15f));
  editHintsVisual.addGradientRect(width * 0.5f,
                                  top,
                                  width * 0.5f,
                                  1.0f,
                                  cyan,
                                  UiTheme::fade(violet, 0.15f),
                                  UiTheme::fade(violet, 0.15f),
                                  cyan);
  // Each hint reads as an accent key and a muted action. Pieces sit at the
  // measured offset of the complete line, so wrapping and fitting above
  // still govern the layout, and their text concatenates to the full hint.
  float y = top + 6.0f;
  for (const std::string& hintLine : lines) {
    std::size_t start = 0u;
    while (start < hintLine.size()) {
      if (hintLine[start] == ' ') {
        ++start;
        continue;
      }
      const std::size_t gap = hintLine.find("  ", start);
      const std::size_t end = gap == std::string::npos ? hintLine.size() : gap;
      const std::string hint = hintLine.substr(start, end - start);
      const float hintX =
        12.0f + measureWidth(hintLine.substr(0, start), fontSize);
      const std::size_t colon = hint.find(':');
      if (colon == std::string::npos) {
        editHintsVisual.addText(
          hint, hintX, y, fontSize, UiTheme::textSecondary());
      } else {
        const std::string key = hint.substr(0, colon + 1u);
        editHintsVisual.addText(
          key,
          hintX,
          y,
          fontSize,
          UiTheme::mix(cyan, UiTheme::textPrimary(), 0.2f));
        editHintsVisual.addText(hint.substr(colon + 1u),
                                hintX + measureWidth(key, fontSize),
                                y,
                                fontSize,
                                UiTheme::textMuted());
      }
      start = end;
    }
    y += lineHeight;
  }
  m_editHintsPanelHeight = panelHeight;
}

bool
CanvasScene::isPointerOverEditHints() const
{
  if (editHintsInsetPixels <= 0 || ic == nullptr || ic->window == nullptr) {
    return false;
  }
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  return mouse[0] >= 0.0 && mouse[0] < dimensions[0] &&
         mouse[1] >= dimensions[1] - editHintsInsetPixels &&
         mouse[1] < dimensions[1];
}

void
CanvasScene::updateSelectionVisual()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateSelectionVisual");
  selectionVisual.clearPrimitives();
  if (currentState != CellState::EDIT || !clipboard.hasSelection() ||
      ic == nullptr || ic->camera == nullptr || ic->commandLine->isOpen ||
      (configurationMenu != nullptr && configurationMenu->isOpen()) ||
      (exitConfirmDialog != nullptr && exitConfirmDialog->isOpen())) {
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
CanvasScene::updateInspectorVisual()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateInspectorVisual");
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

  // Label/value rows; the rows stay fixed (a dash without a hovered cell) so
  // the panel never jumps in size as the pointer moves.
  std::vector<std::pair<std::string, std::string>> rows;
  rows.push_back({ "Generation", groupDigits(simulationGeneration) });
  if (hoverValid && cellContext != nullptr) {
    const CellAddress address{ hoverX, hoverY };
    const unsigned char state = cellContext->getGrid()->getCell(address);
    const ChunkAddress chunk = SparseCellGrid::chunkAddressForCell(address);
    const bool inBounds = cellContext->getGrid()->isCellInWorldBounds(address);
    rows.push_back(
      { "Cell", std::to_string(hoverX) + ", " + std::to_string(hoverY) });
    rows.push_back({ "State",
                     std::to_string(static_cast<int>(state)) + "  " +
                       cellContext->getRuleSet()->getStateName(state) });
    rows.push_back({ "Chunk",
                     std::to_string(chunk.x) + ", " + std::to_string(chunk.y) +
                       (inBounds ? "  in bounds" : "  outside the world") });
  } else {
    rows.push_back({ "Cell", "-" });
    rows.push_back({ "State", "-" });
    rows.push_back({ "Chunk", "-" });
  }
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(
      cellContext->getFamilyString());
  rows.push_back({ "Rule", ruleDisplayName(cellContext->getRuleSetString()) });
  rows.push_back({ "Family",
                   family != nullptr && !family->name.empty()
                     ? family->name
                     : cellContext->getFamilyString() });
  rows.push_back(
    { "Cells", groupDigits(cellContext->getGrid()->getStoredCellCount()) });
  rows.push_back({ "Speed",
                   std::to_string(static_cast<long long>(
                     std::llround(std::max(0.0, achievedSimulationTps)))) +
                     " tps" });
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
  // A glass panel titled like the menus: a spaced-caps eyebrow over a
  // cyan-to-violet crown, muted labels and bright values, sized to fit its
  // values and ellipsizing any that still overflow the screen.
  const float labelSize = 10.0f;
  const float valueSize = 11.5f;
  const float padding = 14.0f;
  const float labelColumn = 78.0f;
  const float rowHeight = 18.0f;
  const float headerHeight = 30.0f;
  float widestValue = 0.0f;
  for (const std::pair<std::string, std::string>& row : rows) {
    widestValue = std::max(
      widestValue, GuiKit::measureEmphasizedText(row.second, valueSize, 0.2f));
  }
  const float maximumWidth = std::max(160.0f, virtWidth - 24.0f);
  const float inspW = std::clamp(
    padding * 2.0f + labelColumn + widestValue, 220.0f, maximumWidth);
  const float valueRoom = inspW - padding * 2.0f - labelColumn;
  const float inspH =
    headerHeight + static_cast<float>(rows.size()) * rowHeight + 10.0f;
  const float inspX = 12.0f;
  const float inspY =
    std::clamp(72.0f, 0.0f, std::max(0.0f, virtHeight - inspH));
  const float radius = 12.0f;
  GuiKit::drawSoftShadow(inspectorVisual,
                         inspX,
                         inspY,
                         inspW,
                         inspH,
                         radius,
                         16.0f,
                         6.0f,
                         UiTheme::glowShadow());
  GuiKit::drawRoundedRect(
    inspectorVisual, inspX, inspY, inspW, inspH, radius, UiTheme::glassRim());
  GuiKit::drawRoundedGradientRect(inspectorVisual,
                                  inspX + 1.0f,
                                  inspY + 1.0f,
                                  inspW - 2.0f,
                                  inspH - 2.0f,
                                  radius - 1.0f,
                                  UiTheme::glassTop(),
                                  UiTheme::glassBottom());
  inspectorVisual.addGradientRect(inspX + radius,
                                  inspY + 1.0f,
                                  inspW - 2.0f * radius,
                                  1.5f,
                                  UiTheme::accentCool(),
                                  UiTheme::accentViolet(),
                                  UiTheme::accentViolet(),
                                  UiTheme::accentCool());
  inspectorVisual.addText("I N S P E C T O R",
                          inspX + padding,
                          inspY + 11.0f,
                          9.0f,
                          UiTheme::accentCool());
  // A hairline under the eyebrow, fading out to the right.
  inspectorVisual.addGradientRect(inspX + padding,
                                  inspY + headerHeight - 4.0f,
                                  inspW - padding * 2.0f,
                                  1.0f,
                                  UiTheme::divider(),
                                  UiTheme::transparentOf(UiTheme::divider()),
                                  UiTheme::transparentOf(UiTheme::divider()),
                                  UiTheme::divider());
  float rowY = inspY + headerHeight;
  bool leading = true;
  for (const std::pair<std::string, std::string>& row : rows) {
    inspectorVisual.addText(row.first,
                            inspX + padding,
                            rowY + (valueSize - labelSize) * 0.5f + 1.0f,
                            labelSize,
                            UiTheme::textMuted());
    // The generation leads in the accent, heavier than the rest.
    const float emphasis = leading ? 0.8f : 0.2f;
    GuiKit::drawEmphasizedText(
      inspectorVisual,
      fitTextToWidth(row.second, valueSize, emphasis, valueRoom),
      inspX + padding + labelColumn,
      rowY,
      valueSize,
      leading ? UiTheme::accentCool() : UiTheme::textPrimary(),
      emphasis);
    leading = false;
    rowY += rowHeight;
  }
  inspectorVisual.setVisible(true);
}

void
CanvasScene::Edit(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.Edit");
  (void)dt;
  if (isPointerOverEditHints()) {
    hoverValid = false;
    paintStrokeActive = false;
    clipboard.stopSelectionDrag();
    return;
  }

  std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
  glm::dvec2 worldPos = ic->camera->ScreenToWorldPrecise(
    glm::dvec2(mouseCoords[0], mouseCoords[1]));

  std::int64_t currentX = 0, currentY = 0;
  if (!CanvasCoordinatePolicy::tryWorldToCell(worldPos.x, &currentX) ||
      !CanvasCoordinatePolicy::tryWorldToCell(worldPos.y, &currentY)) {
    hoverValid = false;
    paintStrokeActive = false;
    clipboard.stopSelectionDrag();
    editorCursor.setVisible(false);
    return;
  }
  hoverX = currentX;
  hoverY = currentY;
  hoverValid = cellContext->getGrid()->isCellInWorldBounds(
    CellAddress{ currentX, currentY });

  if (!ic->commandLine->isOpen) {
    handleEditorHotkeys();

    bool isLeftPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    bool isRightPressed =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
    const bool shift = ic->inputManager->isShiftPressed();

    const bool pointerInWorld =
      hoverValid && !isHamburgerHovered() && !m_paintPaletteHovered &&
      !m_paintPaletteCapturing && !m_editToolsCapturing &&
      !m_contextMenu.isOpen() && !isPointerOverEditTools();
    // A right press inside the selection opens its menu instead of erasing;
    // the held button then belongs to the menu.
    if (m_rightPressEdge && pointerInWorld && !shift && !isLeftPressed &&
        !clipboard.isSelecting() && isCellInSelection(currentX, currentY)) {
      openContextMenu(currentX, currentY);
      paintStrokeActive = false;
      return;
    }
    // Shift starts a selection, but releasing Shift before the mouse button
    // must not turn the same drag into a paint stroke.
    if ((shift || clipboard.isSelecting()) && isLeftPressed && pointerInWorld) {
      if (!clipboard.isSelecting()) {
        clipboard.startSelection(currentX, currentY);
      } else {
        clipboard.updateSelectionDrag(currentX, currentY);
      }
      paintStrokeActive = false;
    } else {
      clipboard.stopSelectionDrag();
      if ((isLeftPressed || isRightPressed) && pointerInWorld) {
        ILLUMO_PROFILE_ZONE("CanvasScene.paintStroke");
        clipboard.clearSelection();
        mirrorDeltaValid = false;
        const unsigned char colorVal =
          isLeftPressed ? m_paintBrush : SparseCellGrid::BackgroundState;

        if (paintStrokeActive) {
          std::int64_t x0 = lastPaintX;
          std::int64_t y0 = lastPaintY;
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
        paintStrokeActive = true;
        lastPaintX = currentX;
        lastPaintY = currentY;
      } else {
        paintStrokeActive = false;
      }
    }
  } else {
    paintStrokeActive = false;
    clipboard.stopSelectionDrag();
  }
}

// One vertical gradient sampled by absolute y, so the drawer's separate
// surfaces (tab, joint, body) share a seamless color ramp.
struct DrawerGradient
{
  ColorRgba top;
  ColorRgba bottom;
  float startY;
  float span;

  ColorRgba operator()(float atY) const
  {
    return UiTheme::mix(top, bottom, (atY - startY) / span);
  }
};

// The collapsed palette is a glass bubble peeking over the footer: its
// diameter, and how much of it shows above the footer.
static const float kPaletteBubbleDiameter = 56.0f;
static const float kPaletteBubblePeek = 40.0f;

// The colour a top-to-bottom gradient must reach at its full height so that
// it shows `bottom` where its visible top `share` ends (the bubble peeks only
// partly above the footer).
static ColorRgba
extendGradient(ColorRgba top, ColorRgba bottom, float share)
{
  const float scale = 1.0f / std::clamp(share, 0.25f, 1.0f);
  const auto channel = [scale](unsigned char from, unsigned char to) {
    const float value =
      static_cast<float>(from) +
      (static_cast<float>(to) - static_cast<float>(from)) * scale;
    return static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
  };
  return ColorRgba{ channel(top.r, bottom.r),
                    channel(top.g, bottom.g),
                    channel(top.b, bottom.b),
                    bottom.a };
}

// One frame of the bubble-to-drawer morph, in palette space.
struct PaletteMorph
{
  float left = 0.0f;
  float top = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float radius = 0.0f;
};

// Interpolates the peeking bubble (morph 0) toward the open drawer (morph 1).
// The springs overshoot, so the blob briefly stretches past either shape;
// the corner radius eases from a full circle to the drawer's corners.
static PaletteMorph
paletteMorphBounds(float centerX,
                   float screenHeight,
                   float drawerWidth,
                   float drawerTop,
                   float drawerHeight,
                   float widthMorph,
                   float heightMorph,
                   float hover)
{
  const float bubble = kPaletteBubbleDiameter * (1.0f + 0.08f * hover);
  const float bubbleTop = screenHeight - kPaletteBubblePeek - 3.0f * hover;
  const float bubbleBottom = bubbleTop + bubble;
  const float drawerBottom = drawerTop + drawerHeight;
  PaletteMorph morph;
  morph.width =
    std::max(bubble * 0.6f, bubble + (drawerWidth - bubble) * widthMorph);
  morph.top = bubbleTop + (drawerTop - bubbleTop) * heightMorph;
  const float bottom =
    bubbleBottom + (drawerBottom - bubbleBottom) * heightMorph;
  morph.height = std::max(bubble * 0.6f, bottom - morph.top);
  morph.left = centerX - morph.width * 0.5f;
  const float settled =
    std::clamp(std::min(widthMorph, heightMorph), 0.0f, 1.0f);
  const float radius = bubble * 0.5f + (12.0f - bubble * 0.5f) * settled;
  // Stay just under a half-extent so the rounded rect keeps its core rect.
  morph.radius = std::max(
    0.0f, std::min(radius, std::min(morph.width, morph.height) * 0.5f - 0.5f));
  return morph;
}

void
CanvasScene::updatePaintPalette(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updatePaintPalette");
  // Hidden frames keep each layer's primitives and key for when it returns.
  m_paintBubbleHaloVisual.setVisible(false);
  m_paintPaletteVisual.setVisible(false);
  m_paintDropVisual.setVisible(false);
  m_paintPaletteTopVisual.setVisible(false);
  m_paintPaletteHovered = false;
  // Hidden or blocked frames leave the toggle unhovered.
  const bool toggleWasHovered = m_paintPaletteToggleHovered;
  m_paintPaletteToggleHovered = false;
  const int previousHoveredCard = m_paintPaletteHoveredCard;
  m_paintPaletteHoveredCard = -1;
  if (ic == nullptr || ic->window == nullptr || ic->inputManager == nullptr ||
      cellContext == nullptr) {
    return;
  }
  const bool leftDown =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool rightDown =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
  const bool clicked = leftDown && !m_paintPaletteMouseWasDown;
  const bool overlaysOpen =
    (ic->commandLine != nullptr && ic->commandLine->isOpen) ||
    (rulesetWorkshopMenu != nullptr && rulesetWorkshopMenu->isOpen()) ||
    (configurationMenu != nullptr && configurationMenu->isOpen()) ||
    (exitConfirmDialog != nullptr && exitConfirmDialog->isOpen());
  // An open context menu sits above the drawer and takes its clicks.
  const bool paletteInteractive =
    currentState == CellState::EDIT && !overlaysOpen && !m_contextMenu.isOpen();
  m_paintPaletteMouseWasDown = leftDown;
  if (!leftDown && !rightDown) {
    m_paintPaletteCapturing = false;
  }
  if (overlaysOpen || m_paintPaletteChromeReveal <= 0.001f) {
    return;
  }

  const RuleSet* rules = cellContext->getRuleSet();
  const std::string tag = rules->getRuleTag();
  const unsigned int stateCount = rules->getStateCount();
  if (stateCount == 0u) {
    return;
  }
  if (rules != m_paintRules) {
    m_paintRules = rules;
    ++m_paintRulesRevision;
  }
  if (tag != m_paintRuleTag) {
    m_paintRuleTag = tag;
    ++m_paintRulesRevision;
    m_paintBrush = 0;
    m_paintPaletteStateOffset = 0u;
    m_paintSelectPlaced = false;
    for (unsigned int state = 0u; state < stateCount; ++state) {
      if (rules->getStateName(static_cast<unsigned char>(state)) ==
          "Conductor") {
        m_paintBrush = static_cast<unsigned char>(state);
        break;
      }
    }
  }
  if (m_paintPaletteEmphasis.size() != stateCount) {
    m_paintPaletteEmphasis.assign(stateCount, GuiSpring{});
    for (GuiSpring& emphasis : m_paintPaletteEmphasis) {
      emphasis.configure(GuiMotion::kJelly);
    }
  }
  if (m_paintBrush >= stateCount) {
    m_paintBrush = 0u;
  }
  if (paletteInteractive) {
    updatePaintBrushFromInput();
  }
  const unsigned int visibleStateCount = std::min(stateCount, 4u);
  const unsigned int maximumOffset = stateCount - visibleStateCount;
  m_paintPaletteStateOffset =
    std::min(m_paintPaletteStateOffset, maximumOffset);
  const float scale = ic->renderer != nullptr
                        ? std::max(0.01f, ic->renderer->getUiScale())
                        : 1.0f;
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const float width = static_cast<float>(visibleStateCount) * 132.0f + 24.0f;
  const int availableHeight =
    std::max(0, dimensions[1] - editHintsFullInsetPixels);
  const float header = 32.0f;
  const float body = 110.0f;
  const float panelBottomExtension = 20.0f;
  const float footerClearance = 8.0f;
  const float drawerTravel = body + panelBottomExtension + footerClearance;
  const float fit = std::max(
    0.01f,
    std::min({ 1.0f,
               static_cast<float>(dimensions[0]) / ((width + 24.0f) * scale),
               static_cast<float>(availableHeight) /
                 ((header + drawerTravel + 4.0f) * scale) }));
  const float screenWidth = static_cast<float>(dimensions[0]) / (scale * fit);
  const float screenHeight =
    static_cast<float>(availableHeight) / (scale * fit);
  const float centerX = screenWidth * 0.5f;
  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const float mx = static_cast<float>(mouse[0]) / (scale * fit);
  const float my = static_cast<float>(mouse[1]) / (scale * fit);
  const float footerHeight =
    static_cast<float>(editHintsFullInsetPixels) / (scale * fit);
  const bool reducedMotion = ic->envVars != nullptr &&
                             ic->envVars->getVar("reducedUiMotion").valueAsBool;
  const float drawerTop = screenHeight - header - drawerTravel;
  const float drawerHeight = header + body + panelBottomExtension;
  // Leaving edit mode slides whatever is showing down behind the footer.
  const float chromeHide = 1.0f - m_paintPaletteChromeLift;
  // Clicks test the shape as it was drawn last frame.
  PaletteMorph previous = paletteMorphBounds(centerX,
                                             screenHeight,
                                             width,
                                             drawerTop,
                                             drawerHeight,
                                             m_paintPaletteWidthMorph.value(),
                                             m_paintPaletteHeightMorph.value(),
                                             m_paintPaletteBubbleHover.value());
  previous.top += chromeHide * (screenHeight - previous.top + footerHeight);
  const float previousRadius = previous.width * 0.5f;
  const float bubbleDx = mx - centerX;
  const float bubbleDy = my - (previous.top + previousRadius);
  const bool bubbleHovered =
    paletteInteractive && !m_paintPaletteExpanded && my < screenHeight &&
    bubbleDx * bubbleDx + bubbleDy * bubbleDy <=
      (previousRadius + 2.0f) * (previousRadius + 2.0f);
  const bool headerHovered =
    paletteInteractive && my < screenHeight &&
    (m_paintPaletteExpanded
       ? GuiKit::isPointInRect(
           mx, my, previous.left, previous.top, previous.width, header)
       : bubbleHovered);
  if (headerHovered && !toggleWasHovered && !clicked) {
    CSimSounds::play(CSimSound::MenuHover);
  }
  m_paintPaletteToggleHovered = headerHovered;
  if (headerHovered) {
    GuiPointerHint::markInteractive();
  }
  if (clicked && headerHovered) {
    m_paintPaletteExpanded = !m_paintPaletteExpanded;
    CSimSounds::play(m_paintPaletteExpanded
                       ? CSimSound::CanvasPaintMenuExpand
                       : CSimSound::CanvasPaintMenuCollapse);
    // The control reshapes under a still pointer; that is not a new hover.
    m_paintPaletteToggleHovered = true;
    m_paintPaletteCardHoverQuiet = true;
  }
  const float springStep =
    std::isfinite(dt) && dt > 0.0 ? static_cast<float>(dt) : 0.0f;
  // Opening, the width leads on a springier morph so the bubble stretches
  // wide, then rises and settles with a bounce; closing pours it back.
  const float morphStep = std::isfinite(dt) && dt > 0.0
                            ? static_cast<float>(std::min(dt, 0.1))
                            : 0.0f;
  const float target = m_paintPaletteExpanded ? 1.0f : 0.0f;
  m_paintPaletteWidthMorph.setTarget(target);
  m_paintPaletteHeightMorph.setTarget(target);
  m_paintPaletteBubbleHover.setTarget(bubbleHovered ? 1.0f : 0.0f);
  m_paintPaletteWidthMorph.tick(morphStep, reducedMotion);
  m_paintPaletteHeightMorph.tick(morphStep, reducedMotion);
  m_paintPaletteBubbleHover.tick(morphStep, reducedMotion);
  // The open drawer's header swells on hover, and the brush chip pops with
  // a springy jiggle whenever the brush in hand changes.
  m_paintHeaderHover.setTarget(headerHovered && m_paintPaletteExpanded ? 1.0f
                                                                       : 0.0f);
  m_paintHeaderHover.tick(morphStep, reducedMotion);
  if (m_paintChipBrush != m_paintBrush) {
    m_paintChipBrush = m_paintBrush;
    m_paintChipPop.kick(9.0f);
  }
  m_paintChipPop.tick(morphStep, reducedMotion);
  m_paintPaletteReveal = m_paintPaletteHeightMorph.value();
  const float widthMorph = m_paintPaletteWidthMorph.value();
  const float heightMorph = m_paintPaletteHeightMorph.value();
  const float hover = std::max(0.0f, m_paintPaletteBubbleHover.value());
  PaletteMorph blob = paletteMorphBounds(centerX,
                                         screenHeight,
                                         width,
                                         drawerTop,
                                         drawerHeight,
                                         widthMorph,
                                         heightMorph,
                                         hover);
  blob.top += chromeHide * (screenHeight - blob.top + footerHeight);
  // The drawer's content rides the blob and fades in as it settles; the
  // bubble's icon fades out as soon as it starts to grow.
  const float opened = std::min(widthMorph, heightMorph);
  const float contentReveal = std::clamp((opened - 0.6f) / 0.35f, 0.0f, 1.0f);
  const unsigned char contentOpacity =
    static_cast<unsigned char>(std::round(255.0f * contentReveal));
  const float iconReveal =
    std::clamp(1.0f - std::max(widthMorph, heightMorph) * 3.0f, 0.0f, 1.0f);
  const float y = blob.top;
  const float bodyX = blob.left + (blob.width - width) * 0.5f;
  m_paintPaletteHovered =
    paletteInteractive && my < screenHeight &&
    (bubbleHovered || GuiKit::isPointInRect(
                        mx, my, blob.left, blob.top, blob.width, blob.height));
  // How much of the blob is still the collapsed bubble: it wears the settings
  // button's look and jelly, which melt away as it grows into the drawer.
  const float bubbleLook =
    std::clamp(1.0f - std::max(widthMorph, heightMorph), 0.0f, 1.0f);
  // Jelly, as on the settings button: the bubble stretches tall as it springs
  // up and as the hover swells it, bulges wide as it recoils, and squashes
  // under a held press. Its hidden bottom stays put behind the footer. This
  // deforms the drawing only; the bubble is hit by its unsquashed circle.
  const float bubbleSquash =
    CanvasChromeStyle::squash(m_paintPaletteChromeSpring.velocity(),
                              m_paintPaletteBubbleHover.velocity(),
                              bubbleHovered && leftDown) *
    bubbleLook;
  if (bubbleSquash != 0.0f) {
    const float blobBottom = blob.top + blob.height;
    const float squashedWidth = blob.width * (1.0f + bubbleSquash);
    blob.height *= 1.0f - bubbleSquash;
    blob.top = blobBottom - blob.height;
    blob.left = centerX - squashedWidth * 0.5f;
    blob.width = squashedWidth;
    blob.radius = std::max(
      0.0f,
      std::min(blob.radius, std::min(blob.width, blob.height) * 0.5f - 0.5f));
  }
  double* paletteScroll = ic->inputManager->getMouseScrollOffset();
  if (paletteScroll != nullptr && m_paintPaletteHovered &&
      contentReveal > 0.5f && *paletteScroll != 0.0) {
    const int direction = *paletteScroll > 0.0 ? -1 : 1;
    const int nextOffset =
      static_cast<int>(m_paintPaletteStateOffset) + direction;
    const unsigned int previousOffset = m_paintPaletteStateOffset;
    m_paintPaletteStateOffset = static_cast<unsigned int>(
      std::clamp(nextOffset, 0, static_cast<int>(maximumOffset)));
    // Browsing ticks once per step that moves; the ends stay quiet.
    if (m_paintPaletteStateOffset != previousOffset) {
      CSimSounds::play(CSimSound::MenuHover);
    }
    *paletteScroll = 0.0;
  }
  // Capture before a reduced-motion toggle moves the shape away from the
  // click. Keep the gesture captured when dragging off onto the world.
  if ((m_paintPaletteHovered || (clicked && headerHovered)) &&
      (leftDown || rightDown)) {
    m_paintPaletteCapturing = true;
  }
  if (paletteScroll != nullptr &&
      (m_paintPaletteHovered || m_paintPaletteCapturing)) {
    *paletteScroll = 0.0;
  }
  // The cards take hover, picks and emphasis before anything is drawn. The
  // header shows the brush that was in hand when the frame began.
  const unsigned char headerBrush = m_paintBrush;
  for (unsigned int card = 0u; card < visibleStateCount; ++card) {
    const unsigned int state = m_paintPaletteStateOffset + card;
    const float cardX = bodyX + 12.0f + static_cast<float>(card) * 132.0f;
    const float cardY = y + header + 12.0f;
    const bool hovered =
      paletteInteractive && m_paintPaletteExpanded && contentReveal > 0.5f &&
      my < screenHeight &&
      GuiKit::isPointInRect(mx, my, cardX, cardY, 124.0f, 64.0f);
    GuiSpring& cardHover = m_paintCardHover[card];
    GuiSpring& cardSquish = m_paintCardSquish[card];
    if (hovered) {
      GuiPointerHint::markInteractive();
      m_paintPaletteHoveredCard = static_cast<int>(card);
      if (clicked) {
        m_paintBrush = static_cast<unsigned char>(state);
        CSimSounds::play(CSimSound::MenuSelect);
        // The picked swatch jumps: it stretches tall, then bulges as it
        // lands and jiggles still.
        cardSquish.kick(-6.0f);
      } else if (static_cast<int>(card) != previousHoveredCard &&
                 !m_paintPaletteCardHoverQuiet) {
        CSimSounds::play(CSimSound::MenuHover);
      }
    }
    GuiSpring& emphasis =
      m_paintPaletteEmphasis[static_cast<std::size_t>(state)];
    emphasis.setTarget(m_paintBrush == state ? 1.0f : (hovered ? 0.5f : 0.0f));
    emphasis.tick(springStep, reducedMotion);
    cardHover.setTarget(hovered ? 1.0f : 0.0f);
    cardHover.tick(springStep, reducedMotion);
    cardSquish.tick(springStep, reducedMotion);
  }
  // The chosen brush's drop: the head pours to the new card and the tail
  // follows, so it stretches between them, then settles into a rounded card
  // that breathes a soft glow. Scrolling the chosen card out of view lets it
  // slide off and fade.
  const float selectedSlot = static_cast<float>(m_paintBrush) -
                             static_cast<float>(m_paintPaletteStateOffset);
  if (!m_paintSelectPlaced || reducedMotion) {
    m_paintSelectHead.snapTo(selectedSlot);
    m_paintSelectTail.snapTo(selectedSlot);
    m_paintSelectPlaced = true;
  }
  m_paintSelectHead.setTarget(selectedSlot);
  m_paintSelectTail.setTarget(selectedSlot);
  m_paintPaletteBreath = std::fmod(m_paintPaletteBreath + springStep, 3.0);
  m_paintSelectHead.tick(springStep, reducedMotion);
  m_paintSelectTail.tick(springStep, reducedMotion);
  // Once the pointer is off every card of a usable (or closed) drawer, the
  // next card it reaches is a real hover.
  if (m_paintPaletteHoveredCard < 0 &&
      (!m_paintPaletteExpanded || contentReveal > 0.5f)) {
    m_paintPaletteCardHoverQuiet = false;
  }

  // Every layer scales about the screen origin, which pointer conversion
  // assumes.
  const auto placeLayer = [fit](GameVisual& layer) {
    GuiPanelLayout::scaleFromScreenOrigin(layer, fit);
  };
  // Drawer content fades with the morph.
  const auto fadeContent = [contentOpacity](GameVisual& layer,
                                            std::size_t shapeStart,
                                            std::size_t textStart) {
    for (std::size_t index = shapeStart; index < layer.shapeCount(); ++index) {
      ShapePrimitive* faded = layer.getShape(index);
      faded->color = UiTheme::applyOpacity(faded->color, contentOpacity);
      for (ColorRgba& vertex : faded->vertexColors) {
        vertex = UiTheme::applyOpacity(vertex, contentOpacity);
      }
    }
    for (std::size_t index = textStart; index < layer.textCount(); ++index) {
      TextPrimitive* faded = layer.getText(index);
      faded->color = UiTheme::applyOpacity(faded->color, contentOpacity);
    }
  };
  const auto packedColor = [](ColorRgba color) {
    return static_cast<float>((static_cast<std::uint32_t>(color.r) << 16) |
                              (static_cast<std::uint32_t>(color.g) << 8) |
                              color.b);
  };
  unsigned char brushRgb[3]{};
  rules->evalCell(headerBrush, brushRgb);
  const ColorRgba brushColor{ brushRgb[0], brushRgb[1], brushRgb[2], 255 };
  std::array<ColorRgba, 4> swatches{};
  for (unsigned int card = 0u; card < visibleStateCount; ++card) {
    unsigned char rgb[3]{};
    rules->evalCell(
      static_cast<unsigned char>(m_paintPaletteStateOffset + card), rgb);
    swatches[card] = ColorRgba{ rgb[0], rgb[1], rgb[2], 255 };
  }
  const float headerSpring = m_paintHeaderHover.value();
  const float chipPop = m_paintChipPop.value();

  // Beneath the bubble, the settings button's violet-to-cyan halo blooms on
  // hover and fades as the drawer opens. It holds a steady half breath at
  // rest: a breathing layer resends its shapes every frame, and the settings
  // button and the toolbar already breathe beside it.
  const float bubbleHover = std::clamp(hover, 0.0f, 1.0f);
  const float bubbleBreathe = CanvasChromeStyle::breathe(0.0f, true);
  const float haloStrength =
    bubbleLook * std::clamp(m_paintPaletteChromeLift, 0.0f, 1.0f);
  m_paintBubbleHaloKey.begin().add({ fit,
                                     haloStrength,
                                     blob.left,
                                     blob.top,
                                     blob.width,
                                     blob.height,
                                     blob.radius,
                                     bubbleHover,
                                     bubbleBreathe });
  if (m_paintBubbleHaloKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildPaintBubbleHalo");
    m_paintBubbleHaloVisual.clearPrimitives();
    if (haloStrength > 0.01f) {
      const ColorRgba halo = CanvasChromeStyle::halo(bubbleHover);
      GuiKit::drawRoundedBand(
        m_paintBubbleHaloVisual,
        blob.left,
        blob.top,
        blob.width,
        blob.height,
        blob.radius,
        0.0f,
        CanvasChromeStyle::haloWidth(bubbleBreathe, bubbleHover),
        UiTheme::fade(halo,
                      CanvasChromeStyle::haloOpacity(bubbleBreathe,
                                                     bubbleHover) *
                        haloStrength),
        UiTheme::transparentOf(halo));
    }
    placeLayer(m_paintBubbleHaloVisual);
  }

  // The blob, its header and the cards.
  m_paintPaletteKey.begin().add({ fit,
                                  centerX,
                                  width,
                                  drawerTop,
                                  drawerHeight,
                                  blob.left,
                                  blob.top,
                                  blob.width,
                                  blob.height,
                                  blob.radius });
  m_paintPaletteKey.add({ hover,
                          headerHovered ? 1.0f : 0.0f,
                          iconReveal,
                          contentReveal,
                          headerSpring,
                          chipPop,
                          static_cast<float>(headerBrush),
                          packedColor(brushColor),
                          static_cast<float>(m_paintBrush),
                          static_cast<float>(m_paintPaletteStateOffset),
                          static_cast<float>(visibleStateCount),
                          static_cast<float>(m_paintRulesRevision) });
  for (unsigned int card = 0u; card < visibleStateCount; ++card) {
    m_paintPaletteKey.add(m_paintCardHover[card].value());
  }
  if (m_paintPaletteKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildPaintPalette");
    m_paintPaletteVisual.clearPrimitives();
    // One opaque glass blob: a rim, then a face sampled from one vertical
    // gradient by absolute y so every size of the morph shades the same way.
    ColorRgba glassTop = UiTheme::glassTop();
    glassTop.a = 255;
    ColorRgba glassBottom = UiTheme::glassBottom();
    glassBottom.a = 255;
    const DrawerGradient surfaceAt{
      glassTop, glassBottom, drawerTop, std::max(1.0f, drawerHeight)
    };
    ColorRgba rim = UiTheme::mix(UiTheme::glassRim(),
                                 UiTheme::accentCool(),
                                 headerHovered ? 0.45f : 0.25f * hover);
    rim.a = 255;
    // The collapsed bubble wears the settings button's tile: a rim lit teal
    // at the top and indigo at the bottom around a teal-to-indigo face. Its
    // gradients reach indigo where the footer cuts it off, not at its hidden
    // bottom.
    const float bubbleTint = std::clamp(hover, 0.0f, 1.0f);
    const float peekShare = kPaletteBubblePeek / std::max(1.0f, blob.height);
    const ColorRgba rimTop =
      UiTheme::mix(rim, CanvasChromeStyle::rimTop(bubbleTint), bubbleLook);
    const ColorRgba rimBottom = UiTheme::mix(
      rim,
      extendGradient(CanvasChromeStyle::rimTop(bubbleTint),
                     CanvasChromeStyle::rimBottom(bubbleTint),
                     peekShare),
      bubbleLook);
    const ColorRgba bubbleFaceBottom =
      extendGradient(CanvasChromeStyle::faceTop(bubbleTint),
                     CanvasChromeStyle::faceBottom(bubbleTint),
                     peekShare);
    // The same soft drop shadow as the hamburger button, so the bubble and
    // the drawer sit above the canvas like the rest of the chrome; hover
    // lifts it.
    GuiKit::drawSoftShadow(m_paintPaletteVisual,
                           blob.left,
                           blob.top,
                           blob.width,
                           blob.height,
                           blob.radius,
                           10.0f,
                           3.0f + 2.0f * std::min(1.0f, hover),
                           UiTheme::glowShadow());
    // Its halo (m_paintBubbleHaloVisual) glows beneath it as it swells.
    GuiKit::drawRoundedGradientRect(m_paintPaletteVisual,
                                    blob.left,
                                    blob.top,
                                    blob.width,
                                    blob.height,
                                    blob.radius,
                                    rimTop,
                                    rimBottom);
    GuiKit::drawRoundedGradientRect(
      m_paintPaletteVisual,
      blob.left + 1.0f,
      blob.top + 1.0f,
      blob.width - 2.0f,
      blob.height - 2.0f,
      std::max(0.0f, blob.radius - 1.0f),
      UiTheme::mix(surfaceAt(blob.top + 1.0f),
                   CanvasChromeStyle::faceTop(bubbleTint),
                   bubbleLook),
      UiTheme::mix(surfaceAt(blob.top + blob.height - 1.0f),
                   bubbleFaceBottom,
                   bubbleLook));
    // A cyan-to-violet hairline crowns the flat top of the blob.
    const float crownInset = std::max(14.0f, blob.radius);
    if (blob.width - 2.0f * crownInset > 4.0f) {
      m_paintPaletteVisual.addGradientRect(blob.left + crownInset,
                                           blob.top + 1.0f,
                                           blob.width - 2.0f * crownInset,
                                           1.5f,
                                           UiTheme::accentCool(),
                                           UiTheme::accentViolet(),
                                           UiTheme::accentViolet(),
                                           UiTheme::accentCool());
    }
    if (iconReveal > 0.01f) {
      const unsigned char iconOpacity =
        static_cast<unsigned char>(std::round(255.0f * iconReveal));
      // Light catches the top of the bubble.
      GuiKit::drawSoftGlow(
        m_paintPaletteVisual,
        centerX - blob.width * 0.18f,
        blob.top + blob.width * 0.22f,
        blob.width * 0.2f,
        blob.width * 0.12f,
        UiTheme::applyOpacity(ColorRgba{ 220, 248, 255, 70 }, iconOpacity),
        12);
      // An up chevron invites a click; it lifts a little on hover.
      const ColorRgba arrow =
        UiTheme::applyOpacity(UiTheme::accentCool(), iconOpacity);
      const float arrowY = blob.top + 11.0f - 1.5f * hover;
      GuiKit::drawChevron(
        m_paintPaletteVisual, centerX, arrowY - 2.0f, 5.0f, 5.0f, 2.0f, arrow);
      // A hairline separates the chevron from the brush, fading at both ends.
      const ColorRgba separator = UiTheme::applyOpacity(
        UiTheme::fade(UiTheme::glassRimLit(), 0.7f), iconOpacity);
      const ColorRgba separatorClear = UiTheme::transparentOf(separator);
      const float separatorY = blob.top + 17.5f;
      m_paintPaletteVisual.addGradientRect(centerX - 11.0f,
                                           separatorY,
                                           11.0f,
                                           1.0f,
                                           separatorClear,
                                           separator,
                                           separator,
                                           separatorClear);
      m_paintPaletteVisual.addGradientRect(centerX,
                                           separatorY,
                                           11.0f,
                                           1.0f,
                                           separator,
                                           separatorClear,
                                           separatorClear,
                                           separator);
      // A paintbrush whose bristles carry the current paint, with a drip. It
      // is drawn at kBrushScale about its anchor, below the separator. Its
      // shapes reach about 6 above the anchor and 8.25 below, so the anchor
      // centers it between the separator's lower edge and the footer's top
      // (the resting bubble peeks kPaletteBubblePeek above the footer).
      const float kBrushScale = 0.75f;
      const float kBrushAbove = 6.0f;
      const float kBrushBelow = 8.25f;
      const float brushGap =
        (kPaletteBubblePeek - (separatorY + 1.0f - blob.top) - kBrushAbove -
         kBrushBelow) *
        0.5f;
      const float brushX = centerX + 1.5f;
      const float brushY = separatorY + 1.0f + brushGap + kBrushAbove;
      m_paintPaletteVisual.addLine(
        brushX + 7.0f * kBrushScale,
        brushY - 7.0f * kBrushScale,
        brushX - 1.0f * kBrushScale,
        brushY + 1.0f * kBrushScale,
        UiTheme::applyOpacity(UiTheme::textPrimary(), iconOpacity),
        3.0f * kBrushScale);
      m_paintPaletteVisual.addLine(
        brushX - 1.0f * kBrushScale,
        brushY + 1.0f * kBrushScale,
        brushX - 3.5f * kBrushScale,
        brushY + 3.5f * kBrushScale,
        UiTheme::applyOpacity(UiTheme::textSecondary(), iconOpacity),
        4.5f * kBrushScale);
      const ColorRgba tipRim =
        UiTheme::applyOpacity(UiTheme::accentCool(), iconOpacity);
      const ColorRgba tip = UiTheme::applyOpacity(brushColor, iconOpacity);
      m_paintPaletteVisual.addFilledEllipse(brushX - 9.5f * kBrushScale,
                                            brushY + 1.5f * kBrushScale,
                                            8.0f * kBrushScale,
                                            8.0f * kBrushScale,
                                            tipRim);
      m_paintPaletteVisual.addFilledTriangle(brushX - 9.0f * kBrushScale,
                                             brushY + 6.5f * kBrushScale,
                                             brushX - 5.0f * kBrushScale,
                                             brushY + 9.5f * kBrushScale,
                                             brushX - 11.0f * kBrushScale,
                                             brushY + 11.0f * kBrushScale,
                                             tipRim);
      m_paintPaletteVisual.addFilledEllipse(brushX - 8.5f * kBrushScale,
                                            brushY + 2.5f * kBrushScale,
                                            6.0f * kBrushScale,
                                            6.0f * kBrushScale,
                                            tip);
      m_paintPaletteVisual.addFilledTriangle(brushX - 8.0f * kBrushScale,
                                             brushY + 6.5f * kBrushScale,
                                             brushX - 5.5f * kBrushScale,
                                             brushY + 8.5f * kBrushScale,
                                             brushX - 9.8f * kBrushScale,
                                             brushY + 9.8f * kBrushScale,
                                             tip);
    }
    if (contentReveal > 0.01f) {
      // The hovered header's highlight swells out from its centre on a jelly
      // spring, overshooting before it settles, and warms from cyan to
      // violet.
      const float headerLit = std::clamp(headerSpring, 0.0f, 1.0f);
      if (headerLit > 0.01f) {
        const float highlightWidth =
          (width - 16.0f) * std::max(0.0f, 0.82f + 0.18f * headerSpring);
        const float highlightHeight =
          (header - 10.0f) * std::max(0.0f, 0.7f + 0.3f * headerSpring);
        const unsigned char highlightOpacity =
          static_cast<unsigned char>(255.0f * contentReveal * headerLit);
        GuiKit::drawRoundedBand(
          m_paintPaletteVisual,
          bodyX + (width - highlightWidth) * 0.5f,
          y + (header - highlightHeight) * 0.5f,
          highlightWidth,
          highlightHeight,
          6.0f,
          0.0f,
          8.0f,
          UiTheme::applyOpacity(UiTheme::fade(UiTheme::accentCool(), 0.18f),
                                highlightOpacity),
          UiTheme::transparentOf(UiTheme::accentCool()));
        GuiKit::drawRoundedGradientRect(
          m_paintPaletteVisual,
          bodyX + (width - highlightWidth) * 0.5f,
          y + (header - highlightHeight) * 0.5f,
          highlightWidth,
          highlightHeight,
          std::min(6.0f, highlightHeight * 0.5f),
          UiTheme::applyOpacity(UiTheme::fade(UiTheme::accentCool(), 0.2f),
                                highlightOpacity),
          UiTheme::applyOpacity(UiTheme::fade(UiTheme::accentViolet(), 0.2f),
                                highlightOpacity));
      }
      // A spaced-caps eyebrow, as the menus title their panels, and a chip
      // naming the brush in hand beside the close chevron. The eyebrow leans
      // in on hover.
      m_paintPaletteVisual.addText(
        "C E L L   P A I N T",
        bodyX + 16.0f + 2.0f * headerSpring,
        y + 11.5f,
        9.5f,
        UiTheme::applyOpacity(
          UiTheme::mix(UiTheme::accentCool(), UiTheme::glassRimLit(), headerLit),
          contentOpacity));
      const float arrowX = bodyX + width - 23.0f;
      // The close chevron dips toward where the drawer will go.
      const float arrowY = y + 16.0f + 2.5f * headerSpring;
      const std::string brushName = rules->getStateName(headerBrush);
      const float brushNameWidth =
        GuiKit::measureEmphasizedText(brushName, 10.0f, 0.6f);
      const float chipRight = arrowX - 16.0f;
      const float chipWidth = brushNameWidth + 32.0f;
      const float chipLeft = chipRight - chipWidth;
      if (chipLeft > bodyX + 130.0f) {
        // A new brush makes the chip jiggle about its centre, its dot hopping
        // a little further than the pill.
        const float chipScale = std::max(0.5f, 1.0f + 0.12f * chipPop);
        const float dotScale = std::max(0.3f, 1.0f + 0.3f * chipPop);
        const float chipCenterX = chipLeft + chipWidth * 0.5f;
        const float chipCenterY = y + 16.0f;
        const float scaledWidth = chipWidth * chipScale;
        const float scaledHeight = 18.0f * chipScale;
        const float scaledLeft = chipCenterX - scaledWidth * 0.5f;
        const float scaledTop = chipCenterY - scaledHeight * 0.5f;
        GuiKit::drawRoundedGradientRect(
          m_paintPaletteVisual,
          scaledLeft,
          scaledTop,
          scaledWidth,
          scaledHeight,
          scaledHeight * 0.5f,
          UiTheme::applyOpacity(
            UiTheme::mix(UiTheme::glassRim(), UiTheme::accentCool(), 0.35f),
            contentOpacity),
          UiTheme::applyOpacity(
            UiTheme::mix(UiTheme::glassRim(), UiTheme::accentViolet(), 0.35f),
            contentOpacity));
        GuiKit::drawRoundedRect(
          m_paintPaletteVisual,
          scaledLeft + 1.0f,
          scaledTop + 1.0f,
          scaledWidth - 2.0f,
          scaledHeight - 2.0f,
          scaledHeight * 0.5f - 1.0f,
          UiTheme::applyOpacity(UiTheme::glassBottom(), contentOpacity));
        const float dotSize = 10.0f * dotScale;
        const float dotCenterX = scaledLeft + 10.0f * chipScale;
        GuiKit::drawRoundedRect(
          m_paintPaletteVisual,
          dotCenterX - dotSize * 0.5f,
          chipCenterY - dotSize * 0.5f,
          dotSize,
          dotSize,
          dotSize * 0.5f,
          UiTheme::applyOpacity(brushColor, contentOpacity));
        const float nameSize = 10.0f * chipScale;
        GuiKit::drawEmphasizedText(
          m_paintPaletteVisual,
          brushName,
          scaledLeft + 21.0f * chipScale,
          chipCenterY - nameSize * 0.5f,
          nameSize,
          UiTheme::applyOpacity(UiTheme::textPrimary(), contentOpacity),
          0.6f);
      }
      // A down chevron closes the drawer back into its bubble.
      const ColorRgba arrow = UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::accent(), UiTheme::accentCool(), headerLit),
        contentOpacity);
      GuiKit::drawChevron(
        m_paintPaletteVisual, arrowX, arrowY + 3.0f, 5.0f, -5.0f, 2.0f, arrow);
    }
    // Everything drawn from here on is drawer content.
    const std::size_t contentShapeStart = m_paintPaletteVisual.shapeCount();
    const std::size_t contentTextStart = m_paintPaletteVisual.textCount();
    for (unsigned int card = 0u;
         contentReveal > 0.01f && card < visibleStateCount;
         ++card) {
      const unsigned int state = m_paintPaletteStateOffset + card;
      const float cardX = bodyX + 12.0f + static_cast<float>(card) * 132.0f;
      const float cardY = y + header + 12.0f;
      // Menu cards: a soft drop shadow, a rim that warms toward the accent on
      // hover and a top-lit face. A hovered card bobs up on a jelly spring,
      // its shadow deepening and a cyan glow blooming around it. The chosen
      // card is marked by the liquid drop poured over it, not by tinting the
      // card.
      const float hoverSpring = m_paintCardHover[card].value();
      const float hoverLit =
        m_paintBrush != state ? std::clamp(hoverSpring, 0.0f, 1.0f) : 0.0f;
      const float liftedY = cardY - 4.0f * hoverSpring;
      GuiKit::drawSoftShadow(m_paintPaletteVisual,
                             cardX,
                             liftedY,
                             124.0f,
                             64.0f,
                             9.0f,
                             8.0f,
                             3.0f + 3.0f * std::max(0.0f, hoverSpring),
                             UiTheme::fade(UiTheme::glowShadow(), 0.7f));
      if (hoverLit > 0.01f) {
        GuiKit::drawRoundedBand(
          m_paintPaletteVisual,
          cardX,
          liftedY,
          124.0f,
          64.0f,
          9.0f,
          0.0f,
          9.0f,
          UiTheme::fade(UiTheme::accentCool(), 0.28f * hoverLit),
          UiTheme::transparentOf(UiTheme::accentCool()));
      }
      GuiKit::drawRoundedGradientRect(
        m_paintPaletteVisual,
        cardX,
        liftedY,
        124.0f,
        64.0f,
        9.0f,
        UiTheme::mix(UiTheme::cardRim(), UiTheme::accentCool(), hoverLit),
        UiTheme::mix(UiTheme::cardRim(), UiTheme::accentViolet(), hoverLit));
      GuiKit::drawRoundedGradientRect(
        m_paintPaletteVisual,
        cardX + 1.0f,
        liftedY + 1.0f,
        122.0f,
        62.0f,
        8.0f,
        UiTheme::mix(
          UiTheme::cardTop(), ColorRgba{ 36, 74, 102, 255 }, hoverLit),
        UiTheme::mix(
          UiTheme::cardBottom(), ColorRgba{ 34, 36, 78, 255 }, hoverLit));
    }
    fadeContent(m_paintPaletteVisual, contentShapeStart, contentTextStart);
    placeLayer(m_paintPaletteVisual);
  }

  // The drop breathes, so it redraws every frame it shows.
  const float lastSlot = static_cast<float>(visibleStateCount) - 1.0f;
  const float dropSlot =
    (m_paintSelectHead.value() + m_paintSelectTail.value()) * 0.5f;
  const float dropFade =
    std::clamp(1.0f - std::max(-dropSlot, dropSlot - lastSlot), 0.0f, 1.0f);
  const bool dropShown = contentReveal > 0.01f && dropFade > 0.01f;
  const float head =
    std::clamp(m_paintSelectHead.value(), -0.5f, lastSlot + 0.5f);
  const float tail =
    std::clamp(m_paintSelectTail.value(), -0.5f, lastSlot + 0.5f);
  const float breathe =
    reducedMotion ? 0.5f
                  : 0.5f + 0.5f * std::sin(static_cast<float>(
                                    m_paintPaletteBreath * 6.2831853 / 3.0));
  // The drop rides its card's hover bob, so hovering the chosen card lifts
  // both together.
  const int dropCard = static_cast<int>(std::lround(dropSlot));
  const float dropLift =
    dropCard >= 0 && dropCard < static_cast<int>(visibleStateCount)
      ? 4.0f * m_paintCardHover[static_cast<std::size_t>(dropCard)].value()
      : 0.0f;
  const float dropSquash =
    std::clamp((m_paintSelectHead.value() - m_paintSelectTail.value()) * 0.4f,
               -1.0f,
               1.0f);
  m_paintDropKey.begin().add({ dropShown ? 1.0f : 0.0f,
                               fit,
                               bodyX,
                               y,
                               contentReveal,
                               dropFade,
                               head,
                               tail,
                               dropLift,
                               dropSquash,
                               breathe });
  if (m_paintDropKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildPaintDrop");
    m_paintDropVisual.clearPrimitives();
    if (dropShown) {
      const unsigned char dropOpacity =
        static_cast<unsigned char>(std::round(255.0f * dropFade));
      const float slotX0 = bodyX + 12.0f;
      GuiLiquidSelection drop;
      drop.horizontal = true;
      drop.crossStart = y + header + 12.0f - dropLift;
      drop.crossSize = 64.0f;
      drop.headStart = slotX0 + head * 132.0f;
      drop.tailStart = slotX0 + tail * 132.0f;
      drop.cellLength = 124.0f;
      drop.radius = 9.0f;
      drop.squash = dropSquash;
      drop.glowSpread = 12.0f + 3.0f * breathe;
      drop.glow = UiTheme::applyOpacity(
        UiTheme::fade(UiTheme::accentCool(), 0.2f + 0.1f * breathe),
        dropOpacity);
      drop.rim = UiTheme::applyOpacity(UiTheme::accentCool(), dropOpacity);
      drop.rimBottom = UiTheme::accentBlendOf(drop.rim);
      drop.glowBottom = UiTheme::accentBlendOf(drop.glow);
      drop.faceTop =
        UiTheme::applyOpacity(UiTheme::selectionTop(), dropOpacity);
      drop.faceBottom =
        UiTheme::applyOpacity(UiTheme::selectionBottom(), dropOpacity);
      GuiKit::drawLiquidSelection(m_paintDropVisual, drop);
      fadeContent(m_paintDropVisual, 0u, 0u);
    }
    placeLayer(m_paintDropVisual);
  }

  // Swatches, labels and hints over the drop.
  m_paintPaletteTopKey.begin().add({ fit,
                                     bodyX,
                                     y,
                                     width,
                                     contentReveal,
                                     static_cast<float>(stateCount),
                                     static_cast<float>(visibleStateCount),
                                     static_cast<float>(
                                       m_paintPaletteStateOffset),
                                     static_cast<float>(m_paintRulesRevision) });
  for (unsigned int card = 0u; card < visibleStateCount; ++card) {
    const unsigned int state = m_paintPaletteStateOffset + card;
    m_paintPaletteTopKey.add(
      { m_paintCardHover[card].value(),
        m_paintCardSquish[card].value(),
        m_paintPaletteEmphasis[static_cast<std::size_t>(state)].value(),
        packedColor(swatches[card]) });
  }
  if (m_paintPaletteTopKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildPaintPaletteTop");
    m_paintPaletteTopVisual.clearPrimitives();
    for (unsigned int card = 0u;
         contentReveal > 0.01f && card < visibleStateCount;
         ++card) {
      const unsigned int state = m_paintPaletteStateOffset + card;
      const float cardX = bodyX + 12.0f + static_cast<float>(card) * 132.0f;
      const float hoverSpring = m_paintCardHover[card].value();
      const float cardY = y + header + 12.0f - 4.0f * hoverSpring;
      const float emphasis =
        m_paintPaletteEmphasis[static_cast<std::size_t>(state)].value();
      const float lit = std::clamp(emphasis, 0.0f, 1.0f);
      const ColorRgba swatch = swatches[card];
      // A round-cornered swatch with a glass gloss; the chosen one lifts off
      // the card and glows in its own color. It rides the emphasis spring, so
      // it hops past its height and bounces back, swells a little on hover,
      // and jiggles (tall, then wide) when picked.
      const float squish =
        std::clamp(0.35f * m_paintCardSquish[card].value(), -0.25f, 0.25f);
      const float swatchScale =
        std::max(0.5f, 1.0f + 0.1f * std::max(0.0f, hoverSpring));
      const float swatchWidth = 28.0f * swatchScale * (1.0f + squish);
      const float swatchHeight = 28.0f * swatchScale * (1.0f - squish);
      const float swatchLift = 3.0f * emphasis;
      const float swatchCenterX = cardX + 62.0f;
      // Keep the swatch's bottom edge anchored as it squishes.
      const float swatchBottom = cardY + 36.0f - swatchLift;
      const float swatchX = swatchCenterX - swatchWidth * 0.5f;
      const float swatchY = swatchBottom - swatchHeight;
      const float swatchRadius = 8.0f * swatchScale;
      if (lit > 0.02f) {
        GuiKit::drawSoftGlow(m_paintPaletteTopVisual,
                             swatchCenterX,
                             swatchY + swatchHeight * 0.5f,
                             26.0f * swatchScale,
                             22.0f * swatchScale,
                             UiTheme::fade(swatch, 0.35f * lit),
                             16);
        GuiKit::drawSoftShadow(m_paintPaletteTopVisual,
                               swatchX,
                               swatchY,
                               swatchWidth,
                               swatchHeight,
                               swatchRadius,
                               6.0f,
                               2.0f + std::max(0.0f, swatchLift),
                               UiTheme::fade(UiTheme::glowShadow(), lit));
      }
      GuiKit::drawRoundedRect(
        m_paintPaletteTopVisual,
        swatchX,
        swatchY,
        swatchWidth,
        swatchHeight,
        swatchRadius,
        UiTheme::mix(UiTheme::panelBorder(), UiTheme::accentCool(), 0.5f * lit));
      GuiKit::drawRoundedRect(m_paintPaletteTopVisual,
                              swatchX + 1.5f,
                              swatchY + 1.5f,
                              swatchWidth - 3.0f,
                              swatchHeight - 3.0f,
                              std::max(0.0f, swatchRadius - 1.5f),
                              swatch);
      GuiKit::drawRoundedGradientRect(m_paintPaletteTopVisual,
                                      swatchX + 1.5f,
                                      swatchY + 1.5f,
                                      swatchWidth - 3.0f,
                                      (swatchHeight - 3.0f) * 0.44f,
                                      std::max(0.0f, swatchRadius - 1.5f),
                                      ColorRgba{ 255, 255, 255, 58 },
                                      ColorRgba{ 255, 255, 255, 0 });
      // The label thickens with emphasis, as the menu rows do.
      GuiKit::drawEmphasizedTextCentered(
        m_paintPaletteTopVisual,
        rules->getStateName(static_cast<unsigned char>(state)),
        cardX + 62.0f,
        cardY + 49.0f,
        11.0f,
        UiTheme::mix(UiTheme::textPrimary(), UiTheme::accentCool(), lit),
        std::max(0.0f, emphasis));
    }
    if (contentReveal > 0.01f) {
      const float dividerWidth = width - 36.0f;
      const ColorRgba dividerColor = UiTheme::divider();
      m_paintPaletteTopVisual.addGradientRect(
        bodyX + 18.0f,
        y + header + 82.0f,
        dividerWidth * 0.5f,
        1.0f,
        UiTheme::transparentOf(dividerColor),
        dividerColor,
        dividerColor,
        UiTheme::transparentOf(dividerColor));
      m_paintPaletteTopVisual.addGradientRect(
        bodyX + 18.0f + dividerWidth * 0.5f,
        y + header + 82.0f,
        dividerWidth * 0.5f,
        1.0f,
        dividerColor,
        UiTheme::transparentOf(dividerColor),
        UiTheme::transparentOf(dividerColor),
        dividerColor);
      // Keycap hints, as the menus' footers show them; browsing only appears
      // when there are more states than cards, along with where the cards
      // are.
      const bool browsable = maximumOffset > 0u;
      const float hintBaseSize = 8.5f;
      const float hintsWidth =
        (browsable ? GuiKit::measureKeyHint("WHEEL", "Browse", hintBaseSize)
                   : 0.0f) +
        GuiKit::measureKeyHint("LMB", "Paint", hintBaseSize) +
        GuiKit::measureKeyHint("RMB", "Erase", hintBaseSize) -
        hintBaseSize * 1.6f;
      const float hintRoom = width - 28.0f;
      const float hintSize = hintsWidth > hintRoom
                               ? hintBaseSize * hintRoom / hintsWidth
                               : hintBaseSize;
      const float hintScale = hintSize / hintBaseSize;
      float hintX = bodyX + (width - hintsWidth * hintScale) * 0.5f;
      const float hintY = y + header + (browsable ? 100.0f : 91.0f);
      if (browsable) {
        GuiKit::drawTextCentered(
          m_paintPaletteTopVisual,
          std::to_string(m_paintPaletteStateOffset + 1u) + "-" +
            std::to_string(m_paintPaletteStateOffset + visibleStateCount) +
            " of " + std::to_string(stateCount) + " states",
          bodyX + width * 0.5f,
          y + header + 90.0f,
          8.5f,
          UiTheme::textMuted());
        hintX += GuiKit::drawKeyHint(
          m_paintPaletteTopVisual, hintX, hintY, "WHEEL", "Browse", hintSize);
      }
      hintX += GuiKit::drawKeyHint(
        m_paintPaletteTopVisual, hintX, hintY, "LMB", "Paint", hintSize);
      GuiKit::drawKeyHint(
        m_paintPaletteTopVisual, hintX, hintY, "RMB", "Erase", hintSize);
    }
    fadeContent(m_paintPaletteTopVisual, 0u, 0u);
    placeLayer(m_paintPaletteTopVisual);
  }
  m_paintBubbleHaloVisual.setVisible(true);
  m_paintPaletteVisual.setVisible(true);
  m_paintDropVisual.setVisible(true);
  m_paintPaletteTopVisual.setVisible(true);
}

void
CanvasScene::toggleSettingsMenu()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.toggleSettingsMenu");
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
CanvasScene::isHamburgerHovered() const
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
CanvasScene::updateHamburgerVisual(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateHamburgerVisual");
  const auto hide = [this]() {
    m_hamburgerHaloVisual.setVisible(false);
    hamburgerVisual.setVisible(false);
  };
  if (ic == nullptr || ic->window == nullptr) {
    hide();
    m_hamburgerShown = false;
    return;
  }

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool exitConfirmOpen =
    exitConfirmDialog != nullptr && exitConfirmDialog->isOpen();
  const bool settingsOpen =
    configurationMenu != nullptr && configurationMenu->isOpen();

  if (consoleOpen || exitConfirmOpen || settingsOpen) {
    hide();
    hamburgerHovered = false;
    m_hamburgerShown = false;
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

  const bool wasHovered = hamburgerHovered;
  hamburgerHovered = isHamburgerHovered();
  if (hamburgerHovered) {
    GuiPointerHint::markInteractive();
  }
  if (hamburgerHovered && !wasHovered) {
    CSimSounds::play(CSimSound::MenuHover);
  }
  const bool isMouseDown =
    ic->inputManager != nullptr &&
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool isPressed = hamburgerHovered && isMouseDown;

  const bool reducedMotion = ic->envVars != nullptr &&
                             ic->envVars->getVar("reducedUiMotion").valueAsBool;
  const float step =
    std::isfinite(dt) && dt > 0.0 ? static_cast<float>(dt) : 0.0f;

  // Reappearing (entering the canvas, or closing settings, the console or
  // the exit dialog) pops the button back in from nothing.
  if (!m_hamburgerShown) {
    m_hamburgerShown = true;
    m_hamburgerPop.snapTo(0.0f);
    m_hamburgerHover.snapTo(0.0f);
    m_hamburgerTip.snapTo(0.0f);
    for (GuiSpring& bar : m_hamburgerBars) {
      bar.snapTo(0.0f);
    }
    m_hamburgerHoverClock = 0.0;
  }
  m_hamburgerPop.setTarget(1.0f);
  m_hamburgerPop.tick(step, reducedMotion);

  m_hamburgerHover.setTarget(hamburgerHovered ? 1.0f : 0.0f);
  m_hamburgerHover.tick(step, reducedMotion);
  m_hamburgerHoverClock =
    hamburgerHovered != wasHovered ? 0.0 : m_hamburgerHoverClock + step;
  // The bars follow in a cascade: top first as the pointer arrives, bottom
  // first as it leaves.
  for (std::size_t i = 0; i < m_hamburgerBars.size(); ++i) {
    const std::size_t order =
      hamburgerHovered ? i : m_hamburgerBars.size() - 1u - i;
    if (reducedMotion ||
        m_hamburgerHoverClock >= 0.05 * static_cast<double>(order)) {
      m_hamburgerBars[i].setTarget(hamburgerHovered ? 1.0f : 0.0f);
    }
    m_hamburgerBars[i].tick(step, reducedMotion);
  }

  // The hint follows a beat after the pointer arrives, on its own springier
  // spring, and tucks away at once when it leaves.
  m_hamburgerTip.setTarget(hamburgerHovered &&
                               (reducedMotion || m_hamburgerHoverClock >= 0.04)
                             ? 1.0f
                             : 0.0f);
  m_hamburgerTip.tick(step, reducedMotion);
  // A slow clock for the idle breath and the hovered bars' ripple.
  m_hamburgerClock = std::fmod(m_hamburgerClock + step, 60.0);
  const float clock = static_cast<float>(m_hamburgerClock);
  const float breathe = CanvasChromeStyle::breathe(clock, reducedMotion);

  const float hoverSpring = m_hamburgerHover.value();
  const float hover = std::clamp(hoverSpring, 0.0f, 1.0f);
  const float pop = std::max(0.0f, m_hamburgerPop.value());
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();

  // Jelly: the tile stretches tall while it grows and bulges wide as it
  // recoils, driven by how fast the pop and hover springs are moving. A held
  // press squashes it flat.
  const float squash = CanvasChromeStyle::squash(
    m_hamburgerPop.velocity(), m_hamburgerHover.velocity(), isPressed);
  const float grow = pop * (1.0f + 0.1f * hoverSpring);
  const float tileWidth = hamburgerSize * grow * (1.0f + squash);
  const float tileHeight = hamburgerSize * grow * (1.0f - squash);
  const bool tileShown = tileWidth > 2.0f && tileHeight > 2.0f;
  // An opaque icon tile scaled about its centre. A press sinks it.
  const float centerX = hamburgerX + hamburgerSize * 0.5f;
  const float centerY =
    hamburgerY + hamburgerSize * 0.5f + (isPressed ? 1.0f : 0.0f);
  const float tileX = centerX - tileWidth * 0.5f;
  const float tileY = centerY - tileHeight * 0.5f;
  const float radius =
    std::min(10.0f * grow, std::min(tileWidth, tileHeight) * 0.5f);

  // Behind the tile: a soft drop shadow and a cyan-to-violet halo that
  // breathes at rest and blooms on hover.
  m_hamburgerHaloKey.begin().add(
    { tileShown ? 1.0f : 0.0f, tileX, tileY, tileWidth, tileHeight, radius });
  m_hamburgerHaloKey.add({ isPressed ? 1.0f : 0.0f, hover, breathe, pop });
  if (m_hamburgerHaloKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildHamburgerHalo");
    m_hamburgerHaloVisual.clearPrimitives();
    if (tileShown) {
      GuiKit::drawSoftShadow(m_hamburgerHaloVisual,
                             tileX,
                             tileY,
                             tileWidth,
                             tileHeight,
                             radius,
                             10.0f,
                             (isPressed ? 1.0f : 3.0f) + 2.0f * hover,
                             UiTheme::glowShadow());
      const ColorRgba halo = CanvasChromeStyle::halo(hover);
      GuiKit::drawRoundedBand(
        m_hamburgerHaloVisual,
        tileX,
        tileY,
        tileWidth,
        tileHeight,
        radius,
        0.0f,
        CanvasChromeStyle::haloWidth(breathe, hover),
        UiTheme::fade(halo, CanvasChromeStyle::haloOpacity(breathe, hover) * pop),
        UiTheme::transparentOf(halo));
    }
  }
  m_hamburgerHaloVisual.setVisible(true);

  // Rounded bars widen on their own springs, overshooting before they
  // settle, then ripple gently while the pointer stays.
  const float barHeight = 2.5f * grow * (1.0f - squash);
  const float spacing = 5.5f * grow * (1.0f - squash);
  std::array<float, 3> barWidths{};
  for (std::size_t i = 0; i < m_hamburgerBars.size(); ++i) {
    const float ripple =
      reducedMotion ? 0.0f
                    : 1.2f * hover *
                        std::sin(clock * 7.0f - static_cast<float>(i) * 0.9f);
    barWidths[i] = (14.0f + 6.0f * m_hamburgerBars[i].value() + ripple) *
                   grow * (1.0f + squash);
  }

  const float tipSpring = m_hamburgerTip.value();
  const float tipShown = std::clamp(tipSpring * 1.4f, 0.0f, 1.0f);
  const bool tipDrawn = tipShown > 0.01f && pop > 0.5f;
  const float tipSquash =
    std::clamp(0.01f * m_hamburgerTip.velocity(), -0.15f, 0.15f);

  // The tile, bars and hint change only on interaction, so they redraw only
  // when their inputs do.
  m_hamburgerKey.begin().add({ tileShown ? 1.0f : 0.0f,
                               hamburgerX,
                               hamburgerY,
                               centerY,
                               tileWidth,
                               tileHeight,
                               radius,
                               hover,
                               isPressed ? 1.0f : 0.0f,
                               barHeight,
                               spacing });
  m_hamburgerKey.add({ barWidths[0], barWidths[1], barWidths[2] });
  m_hamburgerKey.add(
    { tipDrawn ? 1.0f : 0.0f, tipDrawn ? tipSpring : 0.0f, tipDrawn ? tipSquash : 0.0f });
  hamburgerVisual.setVisible(true);
  if (!m_hamburgerKey.changed()) {
    return;
  }
  hamburgerVisual.clearPrimitives();

  if (tileShown) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildHamburgerTile");
    // A rim lit cyan at the top and violet at the bottom, and a
    // teal-to-indigo face that brightens toward the accents on hover.
    GuiKit::drawRoundedGradientRect(
      hamburgerVisual,
      tileX,
      tileY,
      tileWidth,
      tileHeight,
      radius,
      isPressed ? cyan : CanvasChromeStyle::rimTop(hover),
      isPressed ? cyan : CanvasChromeStyle::rimBottom(hover));
    GuiKit::drawRoundedGradientRect(hamburgerVisual,
                                    tileX + 1.0f,
                                    tileY + 1.0f,
                                    tileWidth - 2.0f,
                                    tileHeight - 2.0f,
                                    std::max(0.0f, radius - 1.0f),
                                    CanvasChromeStyle::faceTop(hover),
                                    CanvasChromeStyle::faceBottom(hover));
    // A cyan-to-violet hairline crowns the flat top, as on the paint drawer.
    const float crownInset = std::max(3.0f, radius);
    if (tileWidth - 2.0f * crownInset > 2.0f) {
      const unsigned char crownOpacity =
        static_cast<unsigned char>(255.0f * (0.55f + 0.45f * hover));
      hamburgerVisual.addGradientRect(
        tileX + crownInset,
        tileY + 1.0f,
        tileWidth - 2.0f * crownInset,
        1.5f,
        UiTheme::applyOpacity(cyan, crownOpacity),
        UiTheme::applyOpacity(violet, crownOpacity),
        UiTheme::applyOpacity(violet, crownOpacity),
        UiTheme::applyOpacity(cyan, crownOpacity));
    }

    // The bars run cyan to violet from top to bottom, pastel at rest and
    // vivid on hover.
    const ColorRgba barTints[3] = { cyan,
                                    UiTheme::mix(cyan, violet, 0.5f),
                                    violet };
    for (std::size_t i = 0; i < m_hamburgerBars.size(); ++i) {
      const ColorRgba barColor =
        isPressed
          ? ColorRgba{ 236, 250, 255, 255 }
          : UiTheme::mix(
              UiTheme::mix(ColorRgba{ 214, 230, 246, 255 }, barTints[i], 0.45f),
              barTints[i],
              hover);
      const float barWidth = barWidths[i];
      const float y =
        centerY + (static_cast<float>(i) - 1.0f) * spacing - barHeight * 0.5f;
      GuiKit::drawRoundedRect(hamburgerVisual,
                              centerX - barWidth * 0.5f,
                              y,
                              barWidth,
                              barHeight,
                              barHeight * 0.5f,
                              barColor);
    }
  }

  if (tipDrawn) {
    ILLUMO_PROFILE_ZONE("CanvasScene.rebuildHamburgerTip");
    // A glass hint springs out of the button: it grows from its end nearest
    // the button, sails past its spot and bounces back, stretching along its
    // travel and bulging as it recoils. It shows the action, then its key as
    // a keycap, the way the menus' footers show shortcuts.
    const unsigned char opacity = static_cast<unsigned char>(255.0f * tipShown);
    const float tipScale = std::max(0.2f, 0.6f + 0.4f * tipSpring);
    const float labelSize = 11.0f * tipScale;
    const float keySize = 8.5f * tipScale;
    const float tipHeight = 26.0f * tipScale * (1.0f - tipSquash);
    const float labelWidth =
      GuiKit::measureEmphasizedText("Settings", labelSize, 0.5f);
    // The keycap alone: a hint with no action, less its spacing.
    const float keyWidth =
      GuiKit::measureKeyHint("F1", std::string(), keySize) - keySize * 2.2f;
    const float restWidth = 14.0f * tipScale + labelWidth + 8.0f * tipScale +
                            keyWidth + 6.0f * tipScale;
    const float tipWidth = restWidth * (1.0f + tipSquash);
    const float tipRight = hamburgerX - 8.0f + (1.0f - tipSpring) * 18.0f;
    const float tipX = tipRight - tipWidth;
    const float tipY = hamburgerY + (hamburgerSize - tipHeight) * 0.5f;
    const float contentX = tipX + (tipWidth - restWidth) * 0.5f;
    GuiKit::drawSoftShadow(
      hamburgerVisual,
      tipX,
      tipY,
      tipWidth,
      tipHeight,
      tipHeight * 0.5f,
      10.0f,
      3.0f,
      UiTheme::applyOpacity(UiTheme::glowShadow(), opacity));
    GuiKit::drawRoundedGradientRect(
      hamburgerVisual,
      tipX,
      tipY,
      tipWidth,
      tipHeight,
      tipHeight * 0.5f,
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), cyan, 0.45f),
                            opacity),
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), violet, 0.45f),
                            opacity));
    GuiKit::drawRoundedGradientRect(
      hamburgerVisual,
      tipX + 1.0f,
      tipY + 1.0f,
      tipWidth - 2.0f,
      tipHeight - 2.0f,
      std::max(0.0f, tipHeight * 0.5f - 1.0f),
      UiTheme::applyOpacity(UiTheme::glassTop(), opacity),
      UiTheme::applyOpacity(UiTheme::glassBottom(), opacity));
    GuiKit::drawEmphasizedText(
      hamburgerVisual,
      "Settings",
      contentX + 14.0f * tipScale,
      tipY + (tipHeight - labelSize) * 0.5f,
      labelSize,
      UiTheme::applyOpacity(UiTheme::textPrimary(), opacity),
      0.5f);
    GuiKit::drawKeycap(
      hamburgerVisual,
      contentX + 14.0f * tipScale + labelWidth + 8.0f * tipScale,
      tipY + (tipHeight - keySize * 1.75f) * 0.5f - 0.75f * tipScale,
      "F1",
      keySize,
      opacity);
  }
}

void
CanvasScene::updateEditorCursor(double dt)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.updateEditorCursor");
  // Advance the glide first: a cursor hidden last frame snaps to wherever it
  // is placed below instead of gliding in from its old cell.
  editorCursor.tick(static_cast<float>(dt),
                    ic != nullptr && ic->envVars != nullptr &&
                      ic->envVars->getVar("reducedUiMotion").valueAsBool);
  if (isPointerOverEditHints()) {
    hoverValid = false;
    editorCursor.setVisible(false);
    return;
  }
  if (!ic || !ic->window || !ic->camera || !cellContext ||
      !cellContext->getCanvasView()) {
    editorCursor.setVisible(false);
    return;
  }

  const bool canShow =
    (currentState == CellState::EDIT) && !m_paintPaletteHovered &&
    !m_paintPaletteCapturing && !m_contextMenu.isOpen() &&
    !isPointerOverEditTools() &&
    (ic->commandLine == nullptr || !ic->commandLine->isOpen) &&
    (configurationMenu == nullptr || !configurationMenu->isOpen()) &&
    (exitConfirmDialog == nullptr || !exitConfirmDialog->isOpen());
  if (!canShow) {
    editorCursor.setVisible(false);
  }

  std::array<double, 2> mouseCoords = ic->window->getMouseCoords();
  glm::dvec2 worldPos = ic->camera->ScreenToWorldPrecise(
    glm::dvec2(mouseCoords[0], mouseCoords[1]));
  std::int64_t cellX = 0, cellY = 0;
  if (!CanvasCoordinatePolicy::tryWorldToCell(worldPos.x, &cellX) ||
      !CanvasCoordinatePolicy::tryWorldToCell(worldPos.y, &cellY)) {
    hoverValid = false;
    editorCursor.setVisible(false);
    return;
  }
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
  // Over paintable canvas the software pointer becomes a pencil in the brush
  // color.
  const ColorRgba brush = paintBrushColor();
  GuiPointerHint::markBrush(brush.r, brush.g, brush.b);
  editorCursor.setCellSize(16.0f);
  editorCursor.setFromCell(cellX, cellY);
}

bool
CanvasScene::SaveCellGame(std::string filename)
{
  return saveCellGameTo(std::move(filename), false);
}

bool
CanvasScene::LoadCellGame(std::string filename)
{
  if (filename.empty()) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError("Load path is empty");
    }
    return false;
  }
  return loadCellGameFrom({ std::move(filename) }, false);
}

bool
CanvasScene::saveCellGameTo(std::string location, bool announce)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.saveCellGameTo");
  if (location.empty()) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError("Save path is empty");
    }
    return false;
  }
  drainSimulation();

  IllumoDocument doc;
  doc.version = IllumoCodec::kVersion;
  doc.familyString = cellContext->getFamilyString();
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

  // Encode the published world now; only the byte transfer is deferred.
  std::string error;
  std::ostringstream encoded(std::ios::binary);
  if (!IllumoCodec::writeStream(encoded, doc, &error)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(error);
    }
    return false;
  }
  // -1 pending, 0 failed, 1 written; shared with a possibly later completion.
  const std::shared_ptr<int> outcome = std::make_shared<int>(-1);
  const std::weak_ptr<bool> alive = m_lifetime;
  CSimPlatform::current().writeFile(
    location,
    encoded.str(),
    [this, alive, outcome, location, announce](bool written,
                                               const std::string& failure) {
      *outcome = written ? 1 : 0;
      if (alive.expired() || ic == nullptr || ic->commandLine == nullptr) {
        return;
      }
      if (!written) {
        ic->commandLine->logError(failure);
      } else if (announce) {
        ic->commandLine->logSuccess("Saved canvas to " + location);
      } else {
        Logger::LogInfo("Simulation saved to " + location);
      }
    });
  return *outcome != 0;
}

void
CanvasScene::openSaveDialog()
{
  const SaveLoadDialogSpec dialogSpec{ "CSim Simulation",
                                       "MyCanvas.csim",
                                       "*.CSIM" };
  const std::weak_ptr<bool> alive = m_lifetime;
  CSimPlatform::current().chooseSaveLocation(
    dialogSpec, [this, alive](const std::string& location) {
      if (alive.expired()) {
        return;
      }
      if (location.empty()) {
        ic->commandLine->logWarning("Save cancelled");
        return;
      }
      saveCellGameTo(location, true);
    });
}

void
CanvasScene::openLoadDialog()
{
  const SaveLoadDialogSpec dialogSpec{ "CSim Simulation",
                                       "myCanvas.csim",
                                       "*.CSIM;*.ILLUMO" };
  const std::weak_ptr<bool> alive = m_lifetime;
  CSimPlatform::current().chooseLoadLocation(
    dialogSpec, [this, alive](const std::string& location) {
      if (alive.expired()) {
        return;
      }
      if (location.empty()) {
        ic->commandLine->logWarning("Load cancelled");
        return;
      }
      loadCellGameFrom({ location }, true);
    });
}

bool
CanvasScene::loadCellGameFrom(std::vector<std::string> candidates,
                                 bool announce)
{
  const std::shared_ptr<int> outcome = std::make_shared<int>(-1);
  const std::weak_ptr<bool> alive = m_lifetime;
  CSimPlatform::current().readFirst(
    std::move(candidates),
    [this, alive, outcome, announce](const CSimReadResult& read) {
      ILLUMO_PROFILE_ZONE("CanvasScene.decodeLoadedCanvas");
      *outcome = 0;
      if (alive.expired() || ic == nullptr) {
        return;
      }
      IllumoDocument document;
      std::string error = read.error;
      bool decoded = false;
      if (read.success) {
        std::istringstream stream(read.bytes, std::ios::binary);
        decoded = IllumoCodec::readStream(stream, &document, &error);
      }
      if (!decoded) {
        if (ic->commandLine != nullptr) {
          ic->commandLine->logError(error);
        }
        return;
      }
      if (!applyLoadedDocument(document)) {
        return;
      }
      *outcome = 1;
      if (announce && ic->commandLine != nullptr) {
        ic->commandLine->logSuccess("Loaded canvas from " + read.location);
      } else {
        Logger::LogInfo("Simulation loaded from " + read.location);
      }
    });
  return *outcome != 0;
}

void
CanvasScene::importRuleCatalog(const std::string& location)
{
  const std::weak_ptr<bool> alive = m_lifetime;
  CSimPlatform::current().readFirst(
    { location }, [this, alive, location](const CSimReadResult& read) {
      ILLUMO_PROFILE_ZONE("CanvasScene.importRuleCatalog");
      if (alive.expired() || rulesetWorkshopMenu == nullptr) {
        return;
      }
      RuleSetRegistry imported;
      if (!read.success || !imported.loadRulePackage(read.bytes) ||
          imported.getDefinitions().empty() ||
          imported.getFamilyDefinitions().empty()) {
        Logger::LogWarning("Rule catalog import rejected: " + location +
                           " is unreadable or not a rule package");
        rulesetWorkshopMenu->setError("The selected catalog is invalid.");
        return;
      }
      std::vector<RuleFamilyDefinition> families =
        imported.getFamilyDefinitions();
      const std::vector<RuleSetDefinition> definitions =
        imported.getDefinitions();
      const RuleSet* currentActiveRule = cellContext->getRuleSet();
      RuleSetRegistry staged = RuleSetRegistry::instance();
      bool valid = true;
      for (RuleFamilyDefinition& familyDefinition : families) {
        const RuleFamilyDefinition* existing =
          staged.getFamilyDefinition(familyDefinition.id);
        if (existing != nullptr && existing->builtIn) {
          const bool identical =
            existing->name == familyDefinition.name &&
            existing->kind == familyDefinition.kind &&
            existing->stateCount == familyDefinition.stateCount &&
            existing->stateNames == familyDefinition.stateNames &&
            existing->stateColors == familyDefinition.stateColors;
          if (!identical) {
            valid = false;
            break;
          }
          familyDefinition.builtIn = true;
        }
        if (!staged.registerFamily(familyDefinition)) {
          valid = false;
          break;
        }
      }
      for (const RuleSetDefinition& definition : definitions) {
        const RuleSetDefinition* existing =
          staged.getRuleSetDefinition(definition.id);
        if (valid && existing != nullptr && existing->builtIn) {
          valid = false;
        }
        if (valid && !staged.registerRule(definition)) {
          valid = false;
          break;
        }
      }
      if (valid && currentActiveRule != nullptr) {
        const RuleFamilyDefinition* stagedActiveFamily =
          staged.getFamilyDefinition(cellContext->getFamilyString());
        if (stagedActiveFamily != nullptr &&
            stagedActiveFamily->stateCount <
              currentActiveRule->getStateCount()) {
          Logger::LogWarning("Rule catalog import rejected: it would remove "
                             "states of the active ruleset");
          rulesetWorkshopMenu->setError(
            "The import would invalidate states of the active ruleset.");
          return;
        }
      }
      if (!valid) {
        Logger::LogWarning("Rule catalog import rejected: " + location +
                           " conflicts with built-in definitions or is "
                           "invalid");
        rulesetWorkshopMenu->setError(
          "The selected catalog could not be imported.");
        return;
      }
      std::vector<RuleFamilyDefinition> userFamilies;
      for (const RuleFamilyDefinition& familyDefinition : families) {
        if (!familyDefinition.builtIn) {
          userFamilies.push_back(familyDefinition);
        }
      }
      CSimPlatform::current().saveUserCatalog(
        std::move(userFamilies),
        definitions,
        [this, alive, staged, definitions, location](
          bool saved, const std::string& error) mutable {
          if (alive.expired() || rulesetWorkshopMenu == nullptr) {
            return;
          }
          if (!saved) {
            Logger::LogError("User rule catalog was not saved: " +
                             (error.empty() ? location : error));
            rulesetWorkshopMenu->setError(
              error.empty() ? "The selected catalog could not be imported."
                            : error);
            return;
          }
          RuleSetRegistry::instance() = std::move(staged);
          Logger::LogInfo("Imported rule catalog " + location + ": " +
                          std::to_string(definitions.size()) + " rulesets");
          const RuleSetDefinition* selected =
            RuleSetRegistry::instance().getRuleSetDefinition(
              definitions.front().id);
          const RuleFamilyDefinition* selectedFamily =
            selected == nullptr
              ? nullptr
              : RuleSetRegistry::instance().getFamilyDefinition(
                  selected->familyId);
          if (selected != nullptr && selectedFamily != nullptr) {
            rulesetWorkshopMenu->setDraft(*selectedFamily, *selected);
          }
        });
    });
}

void
CanvasScene::exportRuleCatalog(const std::string& location)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.exportRuleCatalog");
  if (rulesetWorkshopMenu == nullptr) {
    return;
  }
  RuleFamilyDefinition familyDraft = rulesetWorkshopMenu->getFamilyDraft();
  RuleSetDefinition ruleDraft = rulesetWorkshopMenu->getDraft();
  if (rulesetWorkshopMenu->isFamilyDraftChanged() && familyDraft.builtIn) {
    const std::string customFamilyId = uniqueCustomFamilyId(familyDraft.id);
    if (customFamilyId.empty()) {
      rulesetWorkshopMenu->setError(
        "Unable to allocate an ID for the copied family.");
      return;
    }
    familyDraft.id = customFamilyId;
    familyDraft.name = "Custom " + familyDraft.name;
    familyDraft.builtIn = false;
    ruleDraft.familyId = customFamilyId;
  }
  std::string text;
  std::string error;
  if (!RuleCatalogOverlay::packageText(familyDraft, ruleDraft, &text, &error)) {
    Logger::LogWarning("Rule export rejected: " +
                       (error.empty() ? ruleDraft.id : error));
    rulesetWorkshopMenu->setError(
      error.empty() ? "The rule could not be exported." : error);
    return;
  }
  const std::weak_ptr<bool> alive = m_lifetime;
  const std::string ruleId = ruleDraft.id;
  CSimPlatform::current().writeFile(
    location,
    std::move(text),
    [this, alive, location, ruleId](bool written, const std::string& failure) {
      if (written) {
        Logger::LogInfo("Exported rule " + ruleId + " to " + location);
      } else {
        Logger::LogError("Rule " + ruleId + " was not exported: " +
                         (failure.empty() ? location : failure));
      }
      if (!written && !alive.expired() && rulesetWorkshopMenu != nullptr) {
        rulesetWorkshopMenu->setError(
          failure.empty() ? "The rule could not be exported." : failure);
      }
    });
}

bool
CanvasScene::applyLoadedDocument(IllumoDocument& doc)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.applyLoadedDocument");
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
  if ((doc.familyString != cellContext->getFamilyString() ||
       doc.ruleString != cellContext->getRuleSetString()) &&
      !cellContext->setRuleSet(doc.familyString, doc.ruleString)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(
        "Saved family and ruleset could not be activated");
    }
    return false;
  }
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
      resetCameraToHome();
    }
  }
  updateVisualTargets();
  cellContext->getCanvasView()->snapVisualToTargets();
  Logger::LogTrace("Restored world: " +
                   ruleDisplayName(cellContext->getRuleSetString()) + " on " +
                   describeTopology(cellContext->getWorldChunkWidth(),
                                    cellContext->getWorldChunkHeight()) +
                   " (format v" + std::to_string(doc.version) + ")");
  return true;
}

void
CanvasScene::CameraPan()
{
  std::array<double, 2> mousePos = ic->inputManager->getMousePosition();
  glm::dvec2 worldMouse =
    ic->camera->ScreenToWorldPrecise(glm::dvec2(mousePos[0], mousePos[1]));
  static glm::dvec2 lastMousePos = worldMouse;
  static bool wasPressed = false;

  if (ic->inputManager->isMouseButtonPressed(KeyCode::MouseMiddle) &&
      !isPointerOverEditHints()) {
    if (!wasPressed) {
      lastMousePos = worldMouse;
      wasPressed = true;
    }
    glm::dvec2 delta = lastMousePos - worldMouse;
    const glm::dvec2 offset =
      delta * static_cast<double>(ic->camera->GetZoom());
    const glm::dvec2 next =
      ic->camera->GetTargetPositionPrecise() +
      offset / static_cast<double>(ic->camera->GetTargetZoom());
    if (CanvasCoordinatePolicy::validPosition(next.x, next.y)) {
      ic->camera->Pan(offset);
    }
    worldMouse =
      ic->camera->ScreenToWorldPrecise(glm::dvec2(mousePos[0], mousePos[1]));
  } else {
    wasPressed = false;
  }
  lastMousePos = worldMouse;
}

void
CanvasScene::keyboardPan(double dt)
{
  if (ic == nullptr || ic->inputManager == nullptr || ic->camera == nullptr ||
      !(dt > 0.0)) {
    return;
  }
  // Screen pixels per second; Camera::Pan divides by zoom, so the speed on
  // screen is the same at every zoom.
  const double speed = SimulatorSettings::number(
    ic->envVars,
    "panSpeed",
    600.0,
    0.0,
    static_cast<double>(SimulatorSettings::kMaximumPanSpeed));
  if (speed <= 0.0 || ic->inputManager->isControlPressed() ||
      ic->inputManager->isAltPressed()) {
    return;
  }
  glm::dvec2 direction(0.0, 0.0);
  if (ic->inputManager->isKeyPressed(KeyCode::Left)) {
    direction.x -= 1.0;
  }
  if (ic->inputManager->isKeyPressed(KeyCode::Right)) {
    direction.x += 1.0;
  }
  if (ic->inputManager->isKeyPressed(KeyCode::Up)) {
    direction.y += 1.0;
  }
  if (ic->inputManager->isKeyPressed(KeyCode::Down)) {
    direction.y -= 1.0;
  }
  if (direction.x == 0.0 && direction.y == 0.0) {
    return;
  }
  const glm::dvec2 offset = glm::normalize(direction) * (speed * dt);
  const glm::dvec2 next =
    ic->camera->GetTargetPositionPrecise() +
    offset / static_cast<double>(ic->camera->GetTargetZoom());
  if (CanvasCoordinatePolicy::validPosition(next.x, next.y)) {
    ic->camera->Pan(offset);
  }
}

void
CanvasScene::updateAutosave(double dt)
{
  if (ic == nullptr || ic->envVars == nullptr || !(dt > 0.0)) {
    return;
  }
  const double minutes = SimulatorSettings::number(
    ic->envVars,
    "autosaveMinutes",
    0.0,
    0.0,
    static_cast<double>(SimulatorSettings::kMaximumAutosaveMinutes));
  if (minutes <= 0.0) {
    autosaveElapsed = 0.0;
    return;
  }
  autosaveElapsed += dt;
  if (autosaveElapsed < minutes * 60.0) {
    return;
  }
  autosaveElapsed = 0.0;
  // Quiet: the console only logs it. The write completes asynchronously.
  if (!saveCellGameTo(SimulatorSettings::kAutosaveFile, false)) {
    Logger::LogWarning("Autosave could not start");
  }
}

void
CanvasScene::syncCanvasLookFromEnv()
{
  if (cellContext == nullptr || cellContext->getCanvasView() == nullptr) {
    return;
  }
  SimulatorConfiguration look;
  SimulatorSettings::read(ic == nullptr ? nullptr : ic->envVars, &look);
  cellContext->getCanvasView()->setCellLook(
    static_cast<float>(look.cellGlow), look.ledCells, look.gridLines);
}

void
CanvasScene::clearCanvas()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.clearCanvas");
  prepareGridMutation();
  cellContext->getGrid()->clear();
  simulationGeneration = 0;
  updateVisualTargets();
  cellContext->getCanvasView()->snapVisualToTargets();
  ic->commandLine->logSuccess("Canvas cleared");
}

void
CanvasScene::resetCanvas()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.resetCanvas");
  prepareGridMutation();
  cellContext->getGrid()->clear();
  seedInitialPattern();
  simulationGeneration = 0;
  resetCameraToHome();
  updateVisualTargets();
  cellContext->getCanvasView()->snapVisualToTargets();
  ic->commandLine->logSuccess("Canvas reset to " +
                              ruleDisplayName(cellContext->getRuleSetString()) +
                              "'s starting pattern");
}

void
CanvasScene::CameraRotate()
{
}

bool
CanvasScene::isRender3dTestEnabled() const
{
  return ic != nullptr && ic->envVars != nullptr &&
         ic->envVars->getVar("render3dTest").valueAsBool;
}

void
CanvasScene::ensureRender3dTestDrawables()
{
  if (render3dScene || render3dLoadFailed || ic == nullptr ||
      ic->renderer == nullptr) {
    return;
  }
  // Read through the asset source: the guest preloads the scene from the
  // package; the native oracle finds it staged beside its executable.
  render3dLoadFailed = true;
  static constexpr const char* kScenePath = "Scenes/render3d-test.ilsc";
  IAssetSource* source =
    ic->assetManager != nullptr ? ic->assetManager->assetSource() : nullptr;
  std::vector<unsigned char> bytes;
  SceneDocument document;
  std::string error;
  if (source == nullptr ||
      !source->read(source->canonical(kScenePath), bytes)) {
    Logger::LogError(std::string("render3dTest: cannot read ") + kScenePath);
    return;
  }
  if (!IlscCodec::parse(
        std::string(bytes.begin(), bytes.end()), document, error)) {
    Logger::LogError("render3dTest: " + error);
    return;
  }
  std::unique_ptr<SceneInstance> instance =
    std::make_unique<SceneInstance>(ic->assetManager, SceneInstanceOptions{});
  instance->setRenderer(ic->renderer);
  instance->setRenderWorld(ic->renderWorld);
  if (!instance->load(document, "/app", error)) {
    Logger::LogError("render3dTest: " + error);
    return;
  }
  render3dScene = std::move(instance);
  render3dLoadFailed = false;
  Logger::LogInfo(std::string("render3dTest scene loaded from ") + kScenePath);
}
void
CanvasScene::applyRender3dTestCamera()
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
CanvasScene::restoreRender3dTestCamera()
{
  if (!render3dCameraApplied || ic == nullptr || ic->camera == nullptr) {
    return;
  }
  ic->camera->setProjectionType(ProjectionType::Orthographic);
  render3dCameraApplied = false;
}

void
CanvasScene::updateRender3dTestMatrices()
{
  if (!render3dScene) {
    return;
  }
  const float elapsed = static_cast<float>(render3dTestTime);
  Transform3D orbitTransform;
  orbitTransform.position = Vector3(std::sin(elapsed) * 4.0f,
                                    1.5f + std::sin(elapsed * 1.7f) * 0.75f,
                                    std::cos(elapsed) * 4.0f);
  orbitTransform.rotation =
    Quaternion(Vector3(elapsed * 0.8f, elapsed * 1.4f, 0.0f));
  render3dScene->setTransform("orbit", orbitTransform);

  Transform3D childTransform;
  childTransform.position = Vector3(std::sin(elapsed * 2.5f) * 2.2f,
                                    std::cos(elapsed * 2.5f) * 0.8f,
                                    std::cos(elapsed * 2.5f) * 2.2f);
  childTransform.rotation =
    Quaternion(Vector3(0.0f, elapsed * 3.0f, elapsed * 2.0f));
  render3dScene->setTransform("child", childTransform);
  render3dScene->update();
}
void
CanvasScene::enter()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.enter");
  if (!m_suspended || ic == nullptr || cellContext == nullptr) {
    return;
  }
  m_suspended = false;
  if (ic->inputManager != nullptr) {
    inputContextId = ic->inputManager->registerInputContext(inputContext);
    if (inputContextId >= 0) {
      ic->inputManager->setActiveInputContext(inputContextId);
    }
  }
  registerConsoleCommands();
  // The canvas comes back as it first came in, under a dissolving veil.
  canvasEntranceElapsed = 0.0;
  mainMenuReturnPending = false;
  mainMenuReturnSubmitted = false;
  advanceCanvasEntrance(0.0);
  CSimSounds::play(CSimSound::CanvasEnter);
  syncEditMusic();
  Logger::LogTrace("Canvas resumed");
}

void
CanvasScene::leave()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.leave");
  if (ic == nullptr) {
    return;
  }
  m_suspended = true;
  syncEditMusic();
  drainSimulation();
  unregisterConsoleCommands();
  if (inputContextId >= 0 && ic->inputManager != nullptr) {
    ic->inputManager->unregisterInputContext(inputContextId);
    inputContextId = -1;
  }
  if (configurationMenu != nullptr) {
    configurationMenu->close();
  }
  if (exitConfirmDialog != nullptr) {
    exitConfirmDialog->close();
  }
  if (rulesetWorkshopMenu != nullptr) {
    rulesetWorkshopMenu->close();
  }
  m_contextMenu.close();
}

void
CanvasScene::resetCameraToHome()
{
  if (ic != nullptr && ic->camera != nullptr) {
    ic->camera->SetPositionPrecise(0.0, 0.0);
    ic->camera->SetZoom(kHomeZoom);
  }
}

void
CanvasScene::requestMainMenuReturn()
{
  if (ic->scenes == nullptr || mainMenuReturnPending) {
    return;
  }
  mainMenuReturnPending = true;
  Logger::LogTrace("Returning to the main menu");
  CSimSounds::play(CSimSound::CanvasExit);
  syncEditMusic();
  configurationMenu->close();
  exitConfirmDialog->close();
  // Reverse the existing reveal from its current position, even during entry.
  advanceCanvasEntrance(0.0);
  completeMainMenuReturn();
}

void
CanvasScene::completeMainMenuReturn()
{
  if (!mainMenuReturnSubmitted && canvasEntranceElapsed <= 0.0) {
    mainMenuReturnSubmitted = true;
    CSimScenes::returnToTitle(*ic->scenes);
  }
}

void
CanvasScene::advanceCanvasEntrance(double dt)
{
  const bool reducedMotion = ic->envVars != nullptr &&
                             ic->envVars->getVar("reducedUiMotion").valueAsBool;
  if (mainMenuReturnPending) {
    if (reducedMotion || isRender3dTestEnabled()) {
      canvasEntranceElapsed = 0.0;
    } else if (std::isfinite(dt) && dt > 0.0) {
      canvasEntranceElapsed =
        std::max(0.0,
                 canvasEntranceElapsed -
                   dt * kCanvasEntranceSeconds / kCanvasExitSeconds);
    }
    canvasEntranceVisual.setVisible(true);
    return;
  }
  if (reducedMotion || isRender3dTestEnabled()) {
    canvasEntranceElapsed = kCanvasEntranceSeconds;
  } else if (std::isfinite(dt) && dt > 0.0) {
    canvasEntranceElapsed =
      std::min(kCanvasEntranceSeconds, canvasEntranceElapsed + dt);
  }
  canvasEntranceVisual.setVisible(canvasEntranceElapsed <
                                  kCanvasEntranceSeconds);
}

void
CanvasScene::rebuildCanvasEntrance()
{
  ILLUMO_PROFILE_ZONE("CanvasScene.rebuildCanvasEntrance");
  canvasEntranceVisual.clearPrimitives();
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const float scale = ic->renderer->getUiScale();
  const float width = static_cast<float>(std::max(1, dimensions[0])) / scale;
  const float height = static_cast<float>(std::max(1, dimensions[1])) / scale;
  const float progress = std::clamp(
    static_cast<float>(canvasEntranceElapsed / kCanvasEntranceSeconds),
    0.0f,
    1.0f);
  // Roughly 44-pixel cells tile the viewport exactly, echoing canvas pixels.
  const int columns =
    std::clamp(static_cast<int>(std::lround(width / 44.0f)), 8, 48);
  const int rows =
    std::clamp(static_cast<int>(std::lround(height / 44.0f)), 6, 32);
  const float cellWidth = width / static_cast<float>(columns);
  const float cellHeight = height / static_cast<float>(rows);
  const float halfDiagonal = std::sqrt(width * width + height * height) * 0.5f;
  ColorRgba cover = UiTheme::glassBottom();
  cover.a = 255;
  const ColorRgba firing = UiTheme::accentCool();
  const ColorRgba afterglow = UiTheme::accentViolet();

  // A bounded, screen-space veil reveals the world without changing its
  // camera, simulation, or input. The reveal is itself a cellular wave: it
  // spreads from the center with a ragged, deterministic front; each cell
  // fires cyan as the wave reaches it, cools to violet, then shrinks to a dot
  // and fades. Leaving runs the same wave backwards, so cells regrow from the
  // edges inward until the canvas is covered. Two passes keep every halo
  // beneath every cell.
  for (int pass = 0; pass < 2; ++pass) {
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) {
        const float x = static_cast<float>(column) * cellWidth;
        const float y = static_cast<float>(row) * cellHeight;
        const float dx = x + cellWidth * 0.5f - width * 0.5f;
        const float dy = y + cellHeight * 0.5f - height * 0.5f;
        const float distance = std::sqrt(dx * dx + dy * dy) / halfDiagonal;
        // A per-cell hash roughens the wavefront like a growing colony.
        const unsigned int hash =
          (static_cast<unsigned int>(column) * 73856093u) ^
          (static_cast<unsigned int>(row) * 19349663u);
        const float jitter = static_cast<float>((hash >> 4u) % 1024u) / 1023.0f;
        const float arrival = distance * 0.7f + jitter * 0.08f;
        const float local =
          std::clamp((progress - arrival) / 0.22f, 0.0f, 1.0f);
        // Heat rises as the cell fires, then decays through the afterglow.
        const float heat = local <= 0.18f
                             ? local / 0.18f
                             : std::max(0.0f, 1.0f - (local - 0.18f) / 0.6f);
        const ColorRgba tint = UiTheme::mix(
          firing, afterglow, std::clamp((local - 0.18f) / 0.5f, 0.0f, 1.0f));
        if (pass == 0) {
          if (heat > 0.05f) {
            const float spread = std::min(cellWidth, cellHeight) * 0.35f * heat;
            canvasEntranceVisual.addFilledRect(
              x - spread,
              y - spread,
              cellWidth + spread * 2.0f,
              cellHeight + spread * 2.0f,
              UiTheme::fade(tint, 0.3f * heat));
          }
          continue;
        }
        const float shrink =
          local <= 0.3f ? 0.0f : GuiEasing::inOutCubic((local - 0.3f) / 0.7f);
        const float fade = std::clamp((local - 0.35f) / 0.65f, 0.0f, 1.0f);
        // Once fired, a cell keeps its glow color while it shrinks away.
        const float glow = std::min(1.0f, heat + local * 2.0f);
        const unsigned char opacity =
          static_cast<unsigned char>(255.0f * (1.0f - fade));
        const float tileWidth = cellWidth * (1.0f - shrink);
        const float tileHeight = cellHeight * (1.0f - shrink);
        if (opacity == 0 || tileWidth <= 0.01f || tileHeight <= 0.01f) {
          continue;
        }
        canvasEntranceVisual.addFilledRect(
          x + (cellWidth - tileWidth) * 0.5f,
          y + (cellHeight - tileHeight) * 0.5f,
          tileWidth,
          tileHeight,
          UiTheme::applyOpacity(UiTheme::mix(cover, tint, 0.85f * glow),
                                opacity));
      }
    }
  }
}

void
CanvasScene::dispatch(DrawList& frame)
{
  ILLUMO_PROFILE_ZONE("CanvasScene.dispatch");
  DrawList* scene = &frame;
  if (cellContext == nullptr) {
    return;
  }
  // Once-per-frame simulation plots, after this frame's update() has run.
  ILLUMO_PROFILE_PLOT("CanvasScene.generation", simulationGeneration);
  ILLUMO_PROFILE_PLOT("CanvasScene.achievedTps", achievedSimulationTps);
  ILLUMO_PROFILE_PLOT("CanvasScene.requestedTps", requestedSimulationTps);
  ILLUMO_PROFILE_PLOT("CanvasScene.stepsThisFrame", lastSimulationSteps);
  ILLUMO_PROFILE_PLOT("CanvasScene.pendingSteps",
                      simStepSeconds > 0.0 ? simAccum / simStepSeconds : 0.0);
  ILLUMO_PROFILE_PLOT("CanvasScene.debtDropped",
                      simulationDebtDropped ? 1.0 : 0.0);
  ILLUMO_PROFILE_PLOT("CanvasScene.inFlightDeferred",
                      simulationBudgetLimited ? 1.0 : 0.0);
  ILLUMO_PROFILE_PLOT("CanvasScene.allocatedChunks",
                      cellContext->getGrid()->getAllocatedChunkCount());
  ILLUMO_PROFILE_PLOT("CanvasScene.liveCells",
                      cellContext->getGrid()->getStoredCellCount());
  // Owners implement AppendCommands (domain + GameVisual). DrawList lists
  // Drawable hosts by layer (World → UI → Debug). The opt-in diagnostic
  // scene replaces CanvasView so its depth-tested primitives start from a clear
  // depth buffer rather than inheriting 2D presentation writes.
  if (isRender3dTestEnabled()) {
    ILLUMO_PROFILE_ZONE("CanvasScene.render3dTest");
    ensureRender3dTestDrawables();
    applyRender3dTestCamera();
    updateRender3dTestMatrices();
    if (render3dScene) {
      scene->AddDrawable(&render3dScene->drawable(), RenderLayerId::World);
    }
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
  if (m_paintPaletteVisual.isVisible()) {
    scene->AddDrawable(&m_paintBubbleHaloVisual, RenderLayerId::UI);
    scene->AddDrawable(&m_paintPaletteVisual, RenderLayerId::UI);
    scene->AddDrawable(&m_paintDropVisual, RenderLayerId::UI);
    scene->AddDrawable(&m_paintPaletteTopVisual, RenderLayerId::UI);
  }
  // The opaque footer masks drawer overflow throughout the slide animation.
  if (editHintsVisual.isVisible()) {
    scene->AddDrawable(&editHintsVisual, RenderLayerId::UI);
  }
  if (inspectorVisual.isVisible()) {
    scene->AddDrawable(&inspectorVisual, RenderLayerId::UI);
  }
  if (m_actionBar.isVisible()) {
    scene->AddDrawable(&m_actionBar.getHaloVisual(), RenderLayerId::UI);
    scene->AddDrawable(&m_actionBar.getVisual(), RenderLayerId::UI);
  }
  if (modeBadge.isVisible()) {
    scene->AddDrawable(&modeBadge.getVisual(), RenderLayerId::UI);
  }
  if (hamburgerVisual.isVisible()) {
    scene->AddDrawable(&m_hamburgerHaloVisual, RenderLayerId::UI);
    scene->AddDrawable(&hamburgerVisual, RenderLayerId::UI);
  }
  if (m_contextMenu.isOpen()) {
    scene->AddDrawable(&m_contextMenu.getPanelVisual(), RenderLayerId::UI);
    scene->AddDrawable(&m_contextMenu.getDropVisual(), RenderLayerId::UI);
    scene->AddDrawable(&m_contextMenu.getLabelVisual(), RenderLayerId::UI);
  }
  advanceCanvasEntrance(0.0);
  if (canvasEntranceVisual.isVisible()) {
    rebuildCanvasEntrance();
    scene->AddDrawable(&canvasEntranceVisual, RenderLayerId::UI);
  }
  if (configurationMenu != nullptr && configurationMenu->isOpen()) {
    scene->AddDrawable(configurationMenu.get(), RenderLayerId::UI);
  }
  if (rulesetWorkshopMenu != nullptr && rulesetWorkshopMenu->isOpen()) {
    scene->AddDrawable(rulesetWorkshopMenu.get(), RenderLayerId::UI);
  }
  if (exitConfirmDialog != nullptr && exitConfirmDialog->isOpen()) {
    scene->AddDrawable(exitConfirmDialog.get(), RenderLayerId::UI);
  }
}
