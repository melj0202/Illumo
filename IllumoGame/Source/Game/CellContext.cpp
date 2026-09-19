#include "CellContext.h"
#include "Rulesets/RuleSet.h"
#include "Rulesets/RuleSetRegistry.h"
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
  FamilyString = "";
  RuleSetString = "";
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

std::string
CellContext::NormalizeFamilyString(std::string familyString)
{
  return RuleSetRegistry::normalizeId(std::move(familyString));
}

bool
CellContext::IsKnownFamilyString(const std::string& familyString)
{
  return RuleSetRegistry::instance().isKnownFamily(familyString);
}

std::vector<std::string>
CellContext::GetKnownFamilyStrings()
{
  return RuleSetRegistry::instance().getKnownFamilies();
}

std::vector<std::string>
CellContext::GetKnownRuleStrings(const std::string& familyString)
{
  return RuleSetRegistry::instance().getKnownRules(familyString);
}

bool
CellContext::setRuleSet(std::string modeString)
{
  modeString = NormalizeModeString(modeString);
  if (modeString.empty()) {
    modeString = "GAME_OF_LIFE";
  }

  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(modeString);
  if (definition == nullptr) {
    Logger::LogError("Invalid rule set name: " + modeString);
    modeString = "GAME_OF_LIFE";
    definition = RuleSetRegistry::instance().getRuleSetDefinition(modeString);
  }
  if (definition == nullptr) {
    return false;
  }
  return setRuleSet(definition->familyId, modeString);
}

bool
CellContext::setRuleSet(std::string familyString, std::string ruleSetString)
{
  return setRuleSetInternal(
    std::move(familyString), std::move(ruleSetString), false);
}

bool
CellContext::refreshRuleSet()
{
  if (FamilyString.empty() || RuleSetString.empty()) {
    return false;
  }
  return setRuleSetInternal(FamilyString, RuleSetString, true);
}

bool
CellContext::setRuleSetInternal(std::string familyString,
                                std::string ruleSetString,
                                bool forceRefresh)
{
  familyString = NormalizeFamilyString(std::move(familyString));
  ruleSetString = NormalizeModeString(std::move(ruleSetString));
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(ruleSetString);
  if (familyString.empty() || ruleSetString.empty() || definition == nullptr ||
      definition->familyId != familyString) {
    Logger::LogError("The selected ruleset does not belong to that family");
    return false;
  }

  // Avoid thrashing if settings reassert the same validated pair.
  if (!forceRefresh && ruleSet != nullptr && ruleSetString == RuleSetString &&
      familyString == FamilyString) {
    return false;
  }

  std::unique_ptr<RuleSet> newRuleSet =
    RuleSetRegistry::instance().createRuleSet(ruleSetString);
  if (!newRuleSet) {
    Logger::LogError("Failed to compile ruleset: " + ruleSetString);
    return false;
  }

  delete ruleSet;
  ruleSet = newRuleSet.release();

  FamilyString = familyString;
  RuleSetString = ruleSetString;
  if (envVars) {
    envVars->setVar("FamilyString", FamilyString);
    envVars->setVar("RuleSetString", RuleSetString);
    envVars->setVar("ModeString", RuleSetString);
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
