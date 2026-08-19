#pragma once
#include "CellContext.h"
#include "ConfigurationMenu.h"
#include "Cursor.h"
#include "Game/SimulationRunner.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/SplashText.h>
#include <memory>
#include <tracy/Tracy.hpp>

enum class CellState
{
  NORMAL,
  EDIT,
  EXIT
};

class DebugDraw3D;

class CellGameModule : public IModule
{
  friend class CellGameModuleTestAccess;

public:
  CellGameModule();
  ~CellGameModule();
  virtual bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;

private:
  void Normal(double dt);
  void Edit(double dt);
  void updateVisualTargets();
  void syncSimRateFromEnv();
  void registerConsoleCommands();
  void unregisterConsoleCommands();
  bool SaveCellGame(std::string filename);
  bool LoadCellGame(std::string filename);
  void setRunning(bool running);
  void stepSimulation(int generations);
  void printStatus() const;
  void CameraPan();
  void CameraRotate();
  void seedInitialPattern();
  void updateWireworldBrushFromInput();
  void showModeSplash(const char* label);
  void updateEditorCursor();
  bool isRender3dTestEnabled() const;
  void ensureRender3dTestDrawables();
  void updateRender3dTestMatrices();
  bool consumeCompletedSimulation(bool waitForCompletion);
  void drainSimulation();
  void prepareGridMutation();
  SimulatorConfiguration currentConfiguration() const;
  bool applyConfiguration(const SimulatorConfiguration& configuration);
  CellContext* cellContext;
  CellState currentState;
  InputContext inputContext;
  double simAccum;
  double simStepSeconds;
  double requestedSimulationTps;
  double achievedSimulationTps;
  double lastSimulationStepMilliseconds;
  double lastSimulationFrameMilliseconds;
  RollingMetric simulationStepMetric;
  RollingMetric simulationMirrorMetric;
  RollingMetric simulationAdvanceMetric;
  RollingMetric simulationCaptureMetric;
  int lastSimulationSteps;
  bool simulationDebtDropped;
  bool simulationBudgetLimited;
  SimulationRunner simulationRunner;
  SimulationRunnerTimings lastSimulationRunnerTimings;
  SparseGenerationDelta mirrorDelta;
  bool mirrorDeltaValid;
  // Wireworld left-paint state: 0 head, 1 empty, 2 tail, 3 conductor.
  // Selected with keys 1/H, 2, 3/T, 4 while the console is closed.
  unsigned char wireworldBrush;
  // Module-owned mode label (EDIT/NORMAL); not a file-scope global.
  std::unique_ptr<SplashText> modeSplash;
  std::unique_ptr<ConfigurationMenu> configurationMenu;
  std::unique_ptr<DebugDraw3D> render3dTestStatic;
  std::unique_ptr<DebugDraw3D> render3dTestAnimated;
  double render3dTestTime;
  Cursor editorCursor;
};
