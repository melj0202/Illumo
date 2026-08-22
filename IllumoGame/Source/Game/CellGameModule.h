#pragma once
#include "CellContext.h"
#include "CellPattern.h"
#include "ConfigurationMenu.h"
#include "Cursor.h"
#include "ExitConfirmDialog.h"
#include "Game/SimulationRunner.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/SplashText.h>
#include <cstdint>
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
  void updateSelectionVisual();
  void updateInspectorVisual();
  void normalizeSelection(std::int64_t* x0,
                          std::int64_t* y0,
                          std::int64_t* x1,
                          std::int64_t* y1) const;
  bool captureSelection(CellPattern* pattern, std::string* error);
  bool pastePatternAt(const CellPattern& pattern,
                      std::int64_t originX,
                      std::int64_t originY,
                      std::string* error);
  bool fillSelection(unsigned char state);
  bool copySelection();
  bool cutSelection();
  bool pasteAtCursor();
  bool stampNamed(const std::string& name);
  bool importPatternText(const std::string& text);
  void handleEditorHotkeys();
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
  std::unique_ptr<ExitConfirmDialog> exitConfirmDialog;
  std::unique_ptr<DebugDraw3D> render3dTestStatic;
  std::unique_ptr<DebugDraw3D> render3dTestAnimated;
  double render3dTestTime;
  Cursor editorCursor;
  GameVisual selectionVisual;
  GameVisual inspectorVisual;
  bool hasSelection;
  bool selecting;
  std::int64_t selectAnchorX;
  std::int64_t selectAnchorY;
  std::int64_t selectX0;
  std::int64_t selectY0;
  std::int64_t selectX1;
  std::int64_t selectY1;
  std::int64_t hoverX;
  std::int64_t hoverY;
  bool hoverValid;
  CellPattern clipboardPattern;
  bool inspectorEnabled;
  std::uint64_t simulationGeneration;
  bool copyHeld;
  bool cutHeld;
  bool pasteHeld;
  bool rotateHeld;
  bool flipHeld;
  bool inspectHeld;
  bool deleteHeld;
};
