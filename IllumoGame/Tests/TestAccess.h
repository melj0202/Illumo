#pragma once

#include "Game/CanvasScene.h"
#include <Illumo/Testing/TestAccess.h>

class CanvasSceneTestAccess
{
public:
  static bool isPaintPaletteExpanded(const CanvasScene& module)
  {
    return module.m_paintPaletteExpanded;
  }
  static float getPaintPaletteReveal(const CanvasScene& module)
  {
    return module.m_paintPaletteReveal;
  }
  static ModeBadge& getModeBadge(CanvasScene& module)
  {
    return module.modeBadge;
  }
  static float getPaintPaletteWidthMorph(const CanvasScene& module)
  {
    return module.m_paintPaletteWidthMorph.value();
  }
  static unsigned char getPaintBrush(const CanvasScene& module)
  {
    return module.m_paintBrush;
  }
  static GameVisual& getPaintPaletteVisual(CanvasScene& module)
  {
    return module.m_paintPaletteVisual;
  }
  // The swatches, state labels and hints, drawn over the chosen brush's drop.
  static GameVisual& getPaintPaletteTopVisual(CanvasScene& module)
  {
    return module.m_paintPaletteTopVisual;
  }
  static std::uint64_t getSimulationGeneration(const CanvasScene& module)
  {
    return module.simulationGeneration;
  }

  static bool isSimulationRetryPending(const CanvasScene& module)
  {
    return module.simulationRetryPending;
  }

  static CellClipboard& getClipboard(CanvasScene& module)
  {
    return module.clipboard;
  }

  static SelectionBox& getSelectionVisual(CanvasScene& module)
  {
    return module.selectionVisual;
  }

  static CanvasActionBar& getActionBar(CanvasScene& module)
  {
    return module.m_actionBar;
  }

  static CanvasContextMenu& getContextMenu(CanvasScene& module)
  {
    return module.m_contextMenu;
  }

  static GameVisual& getEditHintsVisual(CanvasScene& module)
  {
    return module.editHintsVisual;
  }

  static CellContext* getCellContext(CanvasScene& module)
  {
    return module.cellContext;
  }

  static CellState getState(const CanvasScene& module)
  {
    return module.currentState;
  }

  static int getLastSimulationSteps(const CanvasScene& module)
  {
    return module.lastSimulationSteps;
  }

  static bool getSimulationDebtDropped(const CanvasScene& module)
  {
    return module.simulationDebtDropped;
  }

  static bool getSimulationBudgetLimited(const CanvasScene& module)
  {
    return module.simulationBudgetLimited;
  }

  static double getAchievedSimulationTps(const CanvasScene& module)
  {
    return module.achievedSimulationTps;
  }

  static double getLastSimulationFrameMilliseconds(const CanvasScene& module)
  {
    return module.lastSimulationFrameMilliseconds;
  }

  static void drainSimulation(CanvasScene& module)
  {
    module.drainSimulation();
  }

  static bool isSimulationBusy(const CanvasScene& module)
  {
    return module.simulationRunner.isBusy();
  }

  static bool save(CanvasScene& module, const std::string& filename)
  {
    return module.SaveCellGame(filename);
  }

  static bool load(CanvasScene& module, const std::string& filename)
  {
    return module.LoadCellGame(filename);
  }

  static unsigned char getWireworldBrush(const CanvasScene& module)
  {
    return module.m_paintBrush;
  }

  static void setWireworldBrush(CanvasScene& module, unsigned char state)
  {
    module.m_paintBrush = state;
  }

  static ConfigurationMenu* getConfigurationMenu(CanvasScene& module)
  {
    return module.configurationMenu.get();
  }

  static RulesetWorkshopMenu* getRulesetWorkshopMenu(CanvasScene& module)
  {
    return module.rulesetWorkshopMenu.get();
  }

  static ExitConfirmDialog* getExitConfirmDialog(CanvasScene& module)
  {
    return module.exitConfirmDialog.get();
  }

  static SceneInstance* getRender3dScene(CanvasScene& module)
  {
    return module.render3dScene.get();
  }

  static SimulatorConfiguration currentConfiguration(
    const CanvasScene& module)
  {
    return module.currentConfiguration();
  }

  static bool applyConfiguration(CanvasScene& module,
                                 const SimulatorConfiguration& configuration)
  {
    return module.applyConfiguration(configuration);
  }

  static GameVisual* getInspectorVisual(CanvasScene& module)
  {
    return &module.inspectorVisual;
  }

  static GameVisual& getCanvasEntranceVisual(CanvasScene& module)
  {
    return module.canvasEntranceVisual;
  }
  static void advanceCanvasEntrance(CanvasScene& module, double dt)
  {
    module.advanceCanvasEntrance(dt);
  }

  static GameVisual* getHamburgerVisual(CanvasScene& module)
  {
    return &module.hamburgerVisual;
  }

  static float getHamburgerX(const CanvasScene& module)
  {
    return module.hamburgerX;
  }

  static float getHamburgerY(const CanvasScene& module)
  {
    return module.hamburgerY;
  }

  static float getHamburgerSize(const CanvasScene& module)
  {
    return module.hamburgerSize;
  }

  static bool isHamburgerHovered(const CanvasScene& module)
  {
    return module.isHamburgerHovered();
  }
};
