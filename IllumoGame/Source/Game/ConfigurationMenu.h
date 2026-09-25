#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

struct SimulatorConfiguration
{
  std::string family = "LIFE_LIKE_BINARY";
  std::string ruleSet = "GAME_OF_LIFE";
  std::int64_t worldChunkWidth = 0;
  std::int64_t worldChunkHeight = 0;
  long tps = 12;
  double speedFactor = 1.0;
  double fadeSpeed = 6.0;
  bool vsync = true;
  bool editHints = true;
  bool fullscreen = false;
  long uiScale = 1;
  long msaa = 4;
  long fpsCap = 60;
  bool showInspector = false;
  bool reducedUiMotion = false;
  // CSim draws its own pointer and hides the system cursor.
  bool softwareCursor = true;
  // Sound effect volume, 0 (off) to 100 percent.
  long soundVolume = 80;
};

enum class ConfigurationMenuAction
{
  None,
  Apply,
  Cancel,
  Exit
};

// Release-visible, primitive-composed settings overlay. This owns one visual
// and a draft value; it is deliberately not a retained widget hierarchy.
class ConfigurationMenu : public DrawableBase
{
public:
  ConfigurationMenu(IRenderWindow* window, Renderer* renderer);
  ~ConfigurationMenu() override = default;

  ConfigurationMenu(const ConfigurationMenu&) = delete;
  ConfigurationMenu& operator=(const ConfigurationMenu&) = delete;

  void open(const SimulatorConfiguration& current);
  void close();
  bool isOpen() const { return openState; }
  void tick(float deltaSeconds);
  ConfigurationMenuAction update(InputManager* inputManager);
  bool readConfiguration(SimulatorConfiguration* configuration,
                         std::string* error) const;
  void setError(const std::string& message);
  GameVisual& getVisual() { return visual; }

  int getFirstVisibleRowForTesting() const { return firstVisibleRow; }
  int getSelectedRowForTesting() const { return selectedRow; }
  float getAnimationProgressForTesting() const;
  float getSelectionPositionForTesting() const;
  float getValuePulseForTesting() const;
  const std::string& getWorldWidthTextForTesting() const
  {
    return worldWidthText;
  }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  static const int kFamilyRow = 0;
  static const int kRulesetRow = 1;
  static const int kWorldWidthRow = 2;
  static const int kWorldHeightRow = 3;
  static const int kTpsRow = 4;
  static const int kSpeedRow = 5;
  static const int kFadeRow = 6;
  static const int kVsyncRow = 7;
  static const int kFullscreenRow = 8;
  static const int kUiScaleRow = 9;
  static const int kMsaaRow = 10;
  static const int kFpsCapRow = 11;
  static const int kInspectorRow = 12;
  static const int kReducedMotionRow = 13;
  static const int kEditHintsRow = 14;
  static const int kSoftwareCursorRow = 15;
  static const int kSoundVolumeRow = 16;
  static const int kApplyRow = 17;
  static const int kCancelRow = 18;
  static const int kExitRow = 19;
  static const int kRowCount = 20;

  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual;
  GuiMenuAnimator animator;
  GuiPointerTracker pointer;
  bool openState;
  bool replaceFieldOnType;
  int selectedRow;
  float panelX;
  float panelY;
  float panelWidth;
  float panelHeight;
  float firstRowY;
  float rowHeight;
  int firstVisibleRow = 0;
  int visibleRows = kRowCount;
  GuiPanelFit panelFit;
  // Row hover/focus emphasis, toggle knob travel (indexed by row) and the
  // scrollbar thumb, all spring-driven.
  GuiSpringArray rowFocus;
  GuiSpringArray toggleKnobs;
  GuiSpring scrollThumb;
  // The panel swivels toward the pointer; its layout origin carries the body
  // shift, so rows are hit where they are drawn.
  GuiPanelTilt tilt;

  std::string family;
  std::string ruleSet;
  std::string worldWidthText;
  std::string worldHeightText;
  std::string tpsText;
  std::string speedText;
  std::string fadeText;
  bool vsync;
  bool editHints = true;
  bool softwareCursor = true;
  bool fullscreen;
  long uiScale;
  long msaa;
  std::string fpsCapText;
  bool showInspector = false;
  bool reducedUiMotion = false;
  long soundVolume = 80;
  std::string errorMessage;

  void updateLayout();
  void rebuildVisual();
  bool isToggleRow(int row) const;
  bool toggleValue(int row) const;
  void updateSprings(float deltaSeconds);
  float rowReveal(int row) const;
  float selectionRowPosition() const;
  void selectRow(int row);
  void cycleSelected(int direction);
  ConfigurationMenuAction activateSelected();
  void addCharacter(unsigned int codepoint);
  void eraseCharacter();
  std::string* editableField();
  static std::string topologyText(std::int64_t chunks);
  static std::string decimalText(double value);
  static std::string displayRuleSetName(const std::string& mode);
};
