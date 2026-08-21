#pragma once
#include "CanvasView.h"
#include "Rulesets/AllSets.h"
#include "SparseCellGrid.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <cctype>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

class Renderer;

class CellContext
{
public:
  CellContext(std::string modeString,
              IEnvVars* envVars = nullptr,
              IRenderWindow* window = nullptr,
              Camera* camera = nullptr,
              Renderer* renderer = nullptr)
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
    canvasView = new CanvasView(static_cast<int>(cx),
                                static_cast<int>(cy),
                                grid,
                                window,
                                camera,
                                renderer);
    ruleSet = nullptr;
    ModeString = "";
    setRuleSet(modeString);
  }
  ~CellContext()
  {
    delete ruleSet;
    delete canvasView;
    delete grid;
    delete spareGrid;
  }

  static std::string NormalizeModeString(std::string modeString)
  {
    for (size_t i = 0; i < modeString.size(); ++i) {
      modeString[i] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(modeString[i])));
    }
    return modeString;
  }

  static bool IsKnownModeString(const std::string& modeString)
  {
    return modeString == "GAME_OF_LIFE" || modeString == "BRIANS_BRAIN" ||
           modeString == "DAY_AND_NIGHT" || modeString == "HIGHLIFE" ||
           modeString == "LIFE_WITHOUT_DEATH" || modeString == "SEEDS" ||
           modeString == "WIREWORLD" || modeString == "RULE_90" ||
           modeString == "RULE_184";
  }

  static std::vector<std::string> GetKnownModeStrings()
  {
    return { "GAME_OF_LIFE",
             "BRIANS_BRAIN",
             "DAY_AND_NIGHT",
             "HIGHLIFE",
             "LIFE_WITHOUT_DEATH",
             "SEEDS",
             "WIREWORLD",
             "RULE_90",
             "RULE_184" };
  }

  // Returns true if the active ruleset instance changed.
  bool setRuleSet(std::string modeString)
  {
    modeString = NormalizeModeString(modeString);
    if (modeString.empty()) {
      modeString = "GAME_OF_LIFE";
    }

    // Avoid thrashing if console/env re-asserts the same mode.
    if (ruleSet != nullptr && modeString == ModeString) {
      return false;
    }

    delete ruleSet;
    ruleSet = nullptr;

    if (modeString == "GAME_OF_LIFE") {
      ruleSet = new GameOfLifeRuleSet(nullptr);
    } else if (modeString == "BRIANS_BRAIN") {
      ruleSet = new BrainsBrainRuleSet(nullptr);
    } else if (modeString == "DAY_AND_NIGHT") {
      ruleSet = new DayAndNightRuleSet(nullptr);
    } else if (modeString == "HIGHLIFE") {
      ruleSet = new HighlifeRuleSet(nullptr);
    } else if (modeString == "LIFE_WITHOUT_DEATH") {
      ruleSet = new LifeWithoutDeathRuleSet(nullptr);
    } else if (modeString == "SEEDS") {
      ruleSet = new SeedsRuleSet(nullptr);
    } else if (modeString == "WIREWORLD") {
      ruleSet = new WireworldRuleSet(nullptr);
    } else if (modeString == "RULE_90") {
      ruleSet = new Rule90RuleSet(nullptr);
    } else if (modeString == "RULE_184") {
      ruleSet = new Rule184RuleSet(nullptr);
    } else {
      Logger::LogError("Invalid rule set name: " + modeString);
      ruleSet = new GameOfLifeRuleSet(nullptr);
      modeString = "GAME_OF_LIFE";
    }

    ModeString = modeString;
    if (envVars) {
      envVars->setVar("ModeString", ModeString);
    }
    return true;
  }

  CanvasView* getCanvas() const { return canvasView; }
  CanvasView* getCellCanvas() const { return canvasView; }
  CanvasView* getCanvasView() const { return canvasView; }
  SparseCellGrid* getGrid() const { return grid; }
  SparseCellGrid* getSpareGrid() const { return spareGrid; }
  std::int64_t getWorldChunkWidth() const
  {
    return grid == nullptr ? 0 : grid->getWorldChunkWidth();
  }
  std::int64_t getWorldChunkHeight() const
  {
    return grid == nullptr ? 0 : grid->getWorldChunkHeight();
  }
  bool resetWorld(std::int64_t worldChunkWidth, std::int64_t worldChunkHeight)
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
  void publishSpareGrid(const SparseGenerationDelta& delta)
  {
    SparseCellGrid* previous = grid;
    grid = spareGrid;
    spareGrid = previous;
    canvasView->adoptGrid(grid, delta);
  }
  RuleSet* getRuleSet() const { return ruleSet; }
  std::string getModeString() const { return ModeString; }
  CommandLine* getCommandLine() const { return commandLine; }

private:
  RuleSet* ruleSet;
  SparseCellGrid* grid;
  SparseCellGrid* spareGrid;
  CanvasView* canvasView;
  std::string ModeString;
  CommandLine* commandLine;
  IEnvVars* envVars;
  IRenderWindow* window;
  Camera* camera;
  Renderer* renderer;
};
