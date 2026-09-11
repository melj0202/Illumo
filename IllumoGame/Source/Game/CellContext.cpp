#include "CellContext.h"
#include "Rulesets/AllSets.h"
#include "Rulesets/RuleSet.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <cctype>
#include <new>

CellContext::CellContext(std::string modeString,
                         IEnvVars* envVars,
                         IRenderWindow* window,
                         Camera* camera,
                         Renderer* renderer)
{
  this->envVars = envVars;
  this->window = window;
  this->camera = camera;
  this->renderer = renderer;
  this->commandLine = nullptr;

  long cx = 80;
  long cy = 60;
  std::int64_t worldChunkWidth = 0;
  std::int64_t worldChunkHeight = 0;
  if (envVars) {
    cx = envVars->getVar("CanvasX").valueAsLong;
    cy = envVars->getVar("CanvasY").valueAsLong;
    if (cx < 1)
      cx = 80;
    if (cy < 1)
      cy = 60;
    worldChunkWidth =
      static_cast<std::int64_t>(envVars->getVar("WorldChunksX").valueAsLong);
    worldChunkHeight =
      static_cast<std::int64_t>(envVars->getVar("WorldChunksY").valueAsLong);
    if (!SparseCellGrid::isValidTopology(worldChunkWidth, worldChunkHeight)) {
      Logger::LogError(
        "Invalid world topology; using infinite canvas (0 x 0 chunks)");
      worldChunkWidth = 0;
      worldChunkHeight = 0;
      envVars->setVar("WorldChunksX", 0);
      envVars->setVar("WorldChunksY", 0);
    }
  }
  grid = new SparseCellGrid(worldChunkWidth, worldChunkHeight);
  spareGrid = new SparseCellGrid(worldChunkWidth, worldChunkHeight);
  canvasView = new CanvasView(
    static_cast<int>(cx), static_cast<int>(cy), grid, window, camera, renderer);
  ruleSet = nullptr;
  ModeString = "";
  setRuleSet(modeString);
}

CellContext::~CellContext()
{
  delete ruleSet;
  delete canvasView;
  delete grid;
  delete spareGrid;
}

std::string
CellContext::NormalizeModeString(std::string modeString)
{
  return RuleSetRegistry::normalizeId(std::move(modeString));
}

bool
CellContext::IsKnownModeString(const std::string& modeString)
{
  return RuleSetRegistry::instance().isKnownRule(modeString);
}

std::vector<std::string>
CellContext::GetKnownModeStrings()
{
  return RuleSetRegistry::instance().getKnownRules();
}

bool
CellContext::setRuleSet(std::string modeString)
{
  modeString = NormalizeModeString(modeString);
  if (modeString.empty()) {
    modeString = "GAME_OF_LIFE";
  }

  // Avoid thrashing if console/env re-asserts the same mode.
  if (ruleSet != nullptr && modeString == ModeString) {
    return false;
  }

  std::unique_ptr<RuleSet> newRuleSet =
    RuleSetRegistry::instance().createRuleSet(modeString, nullptr);
  if (!newRuleSet) {
    Logger::LogError("Invalid rule set name: " + modeString);
    newRuleSet =
      RuleSetRegistry::instance().createRuleSet("GAME_OF_LIFE", nullptr);
    modeString = "GAME_OF_LIFE";
  }

  delete ruleSet;
  ruleSet = newRuleSet.release();

  ModeString = modeString;
  if (envVars) {
    envVars->setVar("ModeString", ModeString);
  }
  return true;
}

std::int64_t
CellContext::getWorldChunkWidth() const
{
  return grid == nullptr ? 0 : grid->getWorldChunkWidth();
}

std::int64_t
CellContext::getWorldChunkHeight() const
{
  return grid == nullptr ? 0 : grid->getWorldChunkHeight();
}

bool
CellContext::resetWorld(std::int64_t worldChunkWidth,
                        std::int64_t worldChunkHeight)
{
  if (!SparseCellGrid::isValidTopology(worldChunkWidth, worldChunkHeight)) {
    return false;
  }
  SparseCellGrid* replacement =
    new (std::nothrow) SparseCellGrid(worldChunkWidth, worldChunkHeight);
  SparseCellGrid* replacementSpare =
    new (std::nothrow) SparseCellGrid(worldChunkWidth, worldChunkHeight);
  if (replacement == nullptr || replacementSpare == nullptr) {
    delete replacement;
    delete replacementSpare;
    return false;
  }

  SparseCellGrid* previous = grid;
  SparseCellGrid* previousSpare = spareGrid;
  grid = replacement;
  spareGrid = replacementSpare;
  SparseGenerationDelta replacementDelta;
  replacementDelta.fullReplacement = true;
  replacementDelta.fromRevision =
    previous == nullptr ? 0 : previous->getRevision();
  replacementDelta.toRevision = replacement->getRevision();
  canvasView->adoptGrid(grid, replacementDelta);
  delete previous;
  delete previousSpare;

  if (envVars != nullptr) {
    envVars->setVar("WorldChunksX", static_cast<long>(worldChunkWidth));
    envVars->setVar("WorldChunksY", static_cast<long>(worldChunkHeight));
  }
  return true;
}

void
CellContext::publishSpareGrid(const SparseGenerationDelta& delta)
{
  SparseCellGrid* previous = grid;
  grid = spareGrid;
  spareGrid = previous;
  canvasView->adoptGrid(grid, delta);
}
