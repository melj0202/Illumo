#pragma once
#include "CanvasView.h"
#include "SparseCellGrid.h"
#include <cstdint>
#include <string>
#include <vector>

class Camera;
class CommandLine;
class IEnvVars;
class IRenderWindow;
class Renderer;
class RuleSet;

class CellContext
{
public:
  CellContext(std::string modeString,
              IEnvVars* envVars = nullptr,
              IRenderWindow* window = nullptr,
              Camera* camera = nullptr,
              Renderer* renderer = nullptr);
  ~CellContext();

  static std::string NormalizeModeString(std::string modeString);
  static bool IsKnownModeString(const std::string& modeString);
  static std::vector<std::string> GetKnownModeStrings();

  // Returns true if the active ruleset instance changed.
  bool setRuleSet(std::string modeString);

  CanvasView* getCanvas() const { return canvasView; }
  CanvasView* getCellCanvas() const { return canvasView; }
  CanvasView* getCanvasView() const { return canvasView; }
  SparseCellGrid* getGrid() const { return grid; }
  SparseCellGrid* getSpareGrid() const { return spareGrid; }
  std::int64_t getWorldChunkWidth() const;
  std::int64_t getWorldChunkHeight() const;
  bool resetWorld(std::int64_t worldChunkWidth, std::int64_t worldChunkHeight);
  void publishSpareGrid(const SparseGenerationDelta& delta);
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
