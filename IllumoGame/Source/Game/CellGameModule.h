#pragma once
#include "CellClipboard.h"
#include "CellContext.h"
#include "CellPattern.h"
#include "ConfigurationMenu.h"
#include "Cursor.h"
#include "ExitConfirmDialog.h"
#include "Game/IllumoCodec.h"
#include "Game/SimulationRunner.h"
#include "ModeBadge.h"
#include "NewSimulationMenu.h"
#include "RulesetWorkshopMenu.h"
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <tracy/Tracy.hpp>
#include <vector>

enum class CellState
{
  NORMAL,
  EDIT,
  EXIT
};

class CellGameModule : public IModule
{
  friend class CellGameModuleTestAccess;

public:
  explicit CellGameModule(std::string initialSaveFile = {});
  explicit CellGameModule(const NewSimulationConfiguration& configuration);
  ~CellGameModule() override;
  bool Start(IllumoContext* context) override;
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
  // Platform I/O may complete later; `announce` reports success in the
  // console. Returns false only for failures known before returning.
  bool saveCellGameTo(std::string location, bool announce);
  bool loadCellGameFrom(std::vector<std::string> candidates, bool announce);
  bool applyLoadedDocument(IllumoDocument& document);
  void importRuleCatalog(const std::string& location);
  void exportRuleCatalog(const std::string& location);
  void setRunning(bool running);
  int stepSimulation(int generations);
  void printStatus() const;
  void CameraPan();
  void CameraRotate();
  void seedInitialPattern();
  void updatePaintBrushFromInput();
  void showModeSplash(const char* label);
  void updateEditorCursor();
  void updateHamburgerVisual(double dt);
  void updatePaintPalette(double dt);
  void updateModeBadge(double dt);
  void advanceCanvasEntrance(double dt);
  void requestMainMenuReturn();
  void completeMainMenuReturn();
  void rebuildCanvasEntrance();
  bool isHamburgerHovered() const;
  void toggleSettingsMenu();
  void updateSelectionVisual();
  void updateEditHintsVisual(double dt);
  bool isPointerOverEditHints() const;
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
  bool importPatternText(const std::string& text,
                         PatternFormat format = PatternFormat::Auto);
  void handleEditorHotkeys();
  bool isRender3dTestEnabled() const;
  void ensureRender3dTestDrawables();
  void applyRender3dTestCamera();
  void restoreRender3dTestCamera();
  void updateRender3dTestMatrices();
  bool consumeCompletedSimulation(bool waitForCompletion);
  void drainSimulation();
  void prepareGridMutation();
  SimulatorConfiguration currentConfiguration() const;
  bool applyConfiguration(const SimulatorConfiguration& configuration);
  CellClipboard& getClipboard() { return clipboard; }
  const CellClipboard& getClipboard() const { return clipboard; }
  CellContext* cellContext;
  CellState currentState;
  InputContext inputContext;
  long inputContextId = -1;
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
  bool simulationRetryPending = false;
  // Module-owned corner EDIT / NORMAL badge shown on each mode change.
  ModeBadge modeBadge;
  std::unique_ptr<ConfigurationMenu> configurationMenu;
  std::unique_ptr<RulesetWorkshopMenu> rulesetWorkshopMenu;
  std::unique_ptr<ExitConfirmDialog> exitConfirmDialog;
  // The 3D diagnostic: Scenes/render3d-test.ilsc from the package, loaded on
  // first use; the nodes "orbit" and "child" are animated by id.
  std::unique_ptr<SceneInstance> render3dScene;
  bool render3dLoadFailed;
  double render3dTestTime;
  bool render3dCameraApplied;
  Cursor editorCursor;
  GameVisual hamburgerVisual;
  GameVisual m_paintPaletteVisual;
  bool m_paintPaletteExpanded = false;
  bool m_paintPaletteMouseWasDown = false;
  bool m_paintPaletteCapturing = false;
  bool m_paintPaletteHovered = false;
  // Height morph from the peeking bubble (0) to the open drawer (1); the
  // width leads on its own springier morph, so the bubble stretches, then
  // rises and bounces like a blob. Hover swells the collapsed bubble.
  float m_paintPaletteReveal = 0.0f;
  GuiSpring m_paintPaletteHeightMorph;
  GuiSpring m_paintPaletteWidthMorph;
  GuiSpring m_paintPaletteBubbleHover;
  // Edit chrome enters on springs: the lifts overshoot so the hint bar and
  // paint bubble bounce past their slots; the reveals are the same values
  // clamped to 0..1 for the canvas inset and visibility.
  float m_paintPaletteChromeReveal = 1.0f;
  float m_editChromeReveal = 1.0f;
  float m_paintPaletteChromeLift = 1.0f;
  float m_editChromeLift = 1.0f;
  GuiSpring m_paintPaletteChromeSpring;
  GuiSpring m_editChromeSpring;
  double m_paletteModeDelay = 0.0;
  double m_hintsModeDelay = 0.0;
  bool m_modeChromeTarget = true;
  std::vector<float> m_paintPaletteEmphasis;
  unsigned int m_paintPaletteStateOffset = 0u;
  unsigned char m_paintBrush = 0;
  std::string m_paintRuleTag;
  GameVisual canvasEntranceVisual{ 4096u };
  static constexpr double kCanvasEntranceSeconds = 0.9;
  double canvasEntranceElapsed = kCanvasEntranceSeconds;
  static constexpr double kCanvasExitSeconds = 0.48;
  bool mainMenuReturnPending = false;
  bool mainMenuReturnSubmitted = false;
  float hamburgerX;
  float hamburgerY;
  float hamburgerSize;
  bool hamburgerHovered;
  bool hamburgerMouseWasDown;
  float hamburgerHoverBlend = 0.0f;
  GameVisual editHintsVisual;
  int editHintsInsetPixels = 0;
  int editHintsFullInsetPixels = 0;
  bool paintStrokeActive = false;
  std::int64_t lastPaintX = 0;
  std::int64_t lastPaintY = 0;
  GameVisual selectionVisual;
  GameVisual inspectorVisual;
  CellClipboard clipboard;
  std::int64_t hoverX;
  std::int64_t hoverY;
  bool hoverValid;
  bool inspectorEnabled;
  std::uint64_t simulationGeneration;
  bool copyHeld;
  bool cutHeld;
  bool pasteHeld;
  bool rotateHeld;
  bool flipHeld;
  bool inspectHeld;
  bool deleteHeld;
  std::string initialSaveFile;
  std::optional<NewSimulationConfiguration> initialCanvas;
  // Platform completions hold a weak reference and are dropped after Exit.
  std::shared_ptr<bool> m_lifetime;
};
