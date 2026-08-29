#pragma once

#include "Game/CellGameModule.h"
#include <Illumo/Testing/TestAccess.h>

class CellGameModuleTestAccess
{
public:
  static CellContext* getCellContext(CellGameModule& module)
  {
    return module.cellContext;
  }

  static CellState getState(const CellGameModule& module)
  {
    return module.currentState;
  }

  static int getLastSimulationSteps(const CellGameModule& module)
  {
    return module.lastSimulationSteps;
  }

  static bool getSimulationDebtDropped(const CellGameModule& module)
  {
    return module.simulationDebtDropped;
  }

  static bool getSimulationBudgetLimited(const CellGameModule& module)
  {
    return module.simulationBudgetLimited;
  }

  static double getAchievedSimulationTps(const CellGameModule& module)
  {
    return module.achievedSimulationTps;
  }

  static double getLastSimulationFrameMilliseconds(const CellGameModule& module)
  {
    return module.lastSimulationFrameMilliseconds;
  }

  static void drainSimulation(CellGameModule& module)
  {
    module.drainSimulation();
  }

  static bool isSimulationBusy(const CellGameModule& module)
  {
    return module.simulationRunner.isBusy();
  }

  static bool save(CellGameModule& module, const std::string& filename)
  {
    return module.SaveCellGame(filename);
  }

  static bool load(CellGameModule& module, const std::string& filename)
  {
    return module.LoadCellGame(filename);
  }

  static unsigned char getWireworldBrush(const CellGameModule& module)
  {
    return module.wireworldBrush;
  }

  static void setWireworldBrush(CellGameModule& module, unsigned char state)
  {
    module.wireworldBrush = state;
  }

  static ConfigurationMenu* getConfigurationMenu(CellGameModule& module)
  {
    return module.configurationMenu.get();
  }

  static ExitConfirmDialog* getExitConfirmDialog(CellGameModule& module)
  {
    return module.exitConfirmDialog.get();
  }

  static MeshVisual* getRender3dTestStatic(CellGameModule& module)
  {
    return module.render3dTestStatic.get();
  }

  static MeshVisual* getRender3dTestAnimated(CellGameModule& module)
  {
    return module.render3dTestAnimated.get();
  }

  static MeshVisual* getRender3dTestChild(CellGameModule& module)
  {
    return module.render3dTestChild.get();
  }

  static SceneGraph* getRender3dSceneGraph(CellGameModule& module)
  {
    return &module.render3dSceneGraph;
  }

  static SimulatorConfiguration currentConfiguration(
    const CellGameModule& module)
  {
    return module.currentConfiguration();
  }

  static bool applyConfiguration(CellGameModule& module,
                                 const SimulatorConfiguration& configuration)
  {
    return module.applyConfiguration(configuration);
  }

  static GameVisual* getInspectorVisual(CellGameModule& module)
  {
    return &module.inspectorVisual;
  }
};
