#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
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
  // Interface scale factor (fractions allowed), or 0 for automatic: the
  // scale follows the window size.
  double uiScale = 1.0;
  long msaa = 4;
  long fpsCap = 60;
  bool showInspector = false;
  bool reducedUiMotion = false;
  // CSim draws its own pointer and hides the system cursor.
  bool softwareCursor = true;
  // Sound effect volume, 0 (off) to 100 percent.
  long soundVolume = 80;
  // New and loaded canvases open paused in EDIT; off starts them running.
  bool startPaused = true;
  // Up-close cell look: LED keys or flat pixels, glow 0-2, grid lines.
  bool ledCells = true;
  double cellGlow = 1.0;
  bool gridLines = false;
  // CSim's corner performance readout.
  bool showFps = false;
  bool showMemory = false;
  // Mouse-wheel zoom per notch (0.15 = 15%), its direction, and arrow-key
  // panning in screen pixels per second (0 turns it off).
  double zoomStep = 0.15;
  bool invertZoom = false;
  long panSpeed = 600;
  // Minutes between autosaves to private storage; 0 turns autosave off.
  long autosaveMinutes = 0;
  // clear_canvas asks first.
  bool confirmClear = true;
};

enum class ConfigurationMenuAction
{
  None,
  Apply,
  Cancel,
  Exit
};

// The menu's sections, in tab order.
enum class ConfigurationTab
{
  Simulation,
  Canvas,
  Video,
  Audio,
  Controls,
  General,
  Count
};

// Every editable setting; each belongs to exactly one tab.
enum class ConfigurationSetting
{
  Family,
  Ruleset,
  WorldWidth,
  WorldHeight,
  Tps,
  Speed,
  StartPaused,
  CellStyle,
  CellGlow,
  GridLines,
  Fade,
  Inspector,
  EditHints,
  Fullscreen,
  Vsync,
  FpsCap,
  Msaa,
  ShowFps,
  ShowMemory,
  SoundVolume,
  ZoomStep,
  InvertZoom,
  PanSpeed,
  UiScale,
  ReducedMotion,
  SoftwareCursor,
  Autosave,
  ConfirmClear,
  Count
};

// Release-visible, primitive-composed settings overlay. This owns one visual
// and a draft value; it is deliberately not a retained widget hierarchy.
// Settings are grouped into tabs; Apply, Discard and Exit are footer buttons
// shared by every tab. Selection indexes the active tab's rows first, then
// the footer buttons.
class ConfigurationMenu : public DrawableBase
{
public:
  static const int kFooterButtonCount = 3;
  static const int kApplyButton = 0;
  static const int kDiscardButton = 1;
  static const int kExitButton = 2;

  ConfigurationMenu(IRenderWindow* window, Renderer* renderer);
  ~ConfigurationMenu() override = default;

  ConfigurationMenu(const ConfigurationMenu&) = delete;
  ConfigurationMenu& operator=(const ConfigurationMenu&) = delete;

  // Opens on the tab that was active when the menu last closed.
  void open(const SimulatorConfiguration& current);
  void close();
  bool isOpen() const { return openState; }
  void tick(float deltaSeconds);
  ConfigurationMenuAction update(InputManager* inputManager);
  bool readConfiguration(SimulatorConfiguration* configuration,
                         std::string* error) const;
  void setError(const std::string& message);
  GameVisual& getVisual() { return visual; }

  ConfigurationTab getActiveTabForTesting() const { return activeTab; }
  void selectTabForTesting(ConfigurationTab tab);
  int getFirstVisibleRowForTesting() const { return firstVisibleRow; }
  int getSelectedRowForTesting() const { return selectedRow; }
  // Row of a setting within its own tab, or -1.
  static int rowOfSettingForTesting(ConfigurationSetting setting);
  float getAnimationProgressForTesting() const;
  float getSelectionPositionForTesting() const;
  float getValuePulseForTesting() const;
  const std::string& getWorldWidthTextForTesting() const
  {
    return worldWidthText;
  }
  // Window pixels per menu virtual unit.
  float getLayoutScaleForTesting() const { return panelFit.layoutScale; }
  // Virtual-space {x, y, width, height} of hit targets, as last laid out.
  std::array<float, 4> getTabBoundsForTesting(ConfigurationTab tab) const;
  std::array<float, 4> getFooterButtonBoundsForTesting(int button) const;
  // The slider track of a row in the active tab (zero size if none).
  std::array<float, 4> getSliderTrackBoundsForTesting(int row) const;

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  enum class ControlKind
  {
    Choice,
    Segments,
    Slider,
    Toggle
  };

  static const int kTabCount = static_cast<int>(ConfigurationTab::Count);
  static const int kSettingCount =
    static_cast<int>(ConfigurationSetting::Count);

  IRenderWindow* window;
  Renderer* renderer;
  GameVisual visual;
  GuiMenuAnimator animator;
  GuiPointerTracker pointer;
  bool openState;
  bool replaceFieldOnType;
  ConfigurationTab activeTab = ConfigurationTab::Simulation;
  int selectedRow;
  float panelX;
  float panelY;
  float panelWidth;
  float panelHeight;
  float firstRowY;
  float rowHeight;
  int firstVisibleRow = 0;
  int visibleRows = 1;
  GuiPanelFit panelFit;
  // Row hover/focus emphasis (by row in the active tab), toggle knob travel,
  // slider knob travel (0..1) and the lit segment (by setting), tab and
  // footer emphasis, and the scrollbar thumb, all spring-driven.
  GuiSpringArray rowFocus;
  GuiSpringArray toggleKnobs;
  GuiSpringArray sliderKnobs;
  GuiSpringArray segmentKnobs;
  GuiSpringArray tabFocus;
  GuiSpringArray footerFocus;
  GuiSpring scrollThumb;
  // The lit tab pill glides between tabs (in tab units); a tab change
  // swells the new rows in from the side it came from.
  GuiSpring tabIndicator;
  GuiSpring tabSwap;
  float tabSwapDirection = 1.0f;
  int hoveredTab = -1;
  int hoveredFooterButton = -1;
  // The slider being dragged with the pointer, or Count when none.
  ConfigurationSetting dragSetting = ConfigurationSetting::Count;
  bool dragChanged = false;
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
  double uiScale;
  long msaa;
  std::string fpsCapText;
  bool showInspector = false;
  bool reducedUiMotion = false;
  long soundVolume = 80;
  bool startPaused = true;
  bool ledCells = true;
  bool gridLines = false;
  bool showFps = false;
  bool showMemory = false;
  bool invertZoom = false;
  bool confirmClear = true;
  // Slider-only numbers (no typed entry) are kept as doubles.
  double cellGlow = 1.0;
  double zoomStep = 0.15;
  double panSpeed = 600.0;
  double autosaveMinutes = 0.0;
  std::string errorMessage;

  void updateLayout();
  void rebuildVisual();
  void drawHeader(float reveal, unsigned char panelOpacity);
  void drawTabs(unsigned char panelOpacity, float breathe);
  void drawRows(unsigned char panelOpacity, float breathe);
  void drawRowControl(ConfigurationSetting setting,
                      float y,
                      float shiftX,
                      float rowFontSize,
                      bool selected,
                      unsigned char rowOpacity);
  void drawFooter(unsigned char panelOpacity, float breathe);

  int rowCount() const;
  bool footerSelected() const { return selectedRow >= rowCount(); }
  ConfigurationSetting settingAt(int row) const;
  ConfigurationSetting selectedSetting() const;
  static ControlKind controlKind(ConfigurationSetting setting);
  bool toggleValue(ConfigurationSetting setting) const;

  // Slider model: each slider walks an ordered list of stops. Text-backed
  // sliders also accept typed digits for an exact value.
  // The draft behind a slider-only number (UI scale, glow, zoom step, pan
  // speed, autosave), or nullptr.
  double* sliderNumber(ConfigurationSetting setting);
  const double* sliderNumber(ConfigurationSetting setting) const;
  bool* toggleSlot(ConfigurationSetting setting);
  std::string* sliderText(ConfigurationSetting setting);
  const std::string* sliderText(ConfigurationSetting setting) const;
  bool sliderValue(ConfigurationSetting setting, double* value) const;
  float sliderFraction(ConfigurationSetting setting) const;
  bool setSliderStop(ConfigurationSetting setting, int stop);
  bool stepSlider(ConfigurationSetting setting, int direction);
  std::string sliderReadout(ConfigurationSetting setting) const;
  void linkWorldAxes(ConfigurationSetting changed);

  int segmentCount(ConfigurationSetting setting) const;
  int segmentIndex(ConfigurationSetting setting) const;
  std::string segmentLabel(ConfigurationSetting setting, int index) const;
  void setSegment(ConfigurationSetting setting, int index);

  // Value-column geometry shared by drawing and hit testing.
  float valueLeft() const;
  float valueRight() const;
  float sliderTrackLeft() const;
  float sliderTrackRight() const;
  float rowTop(int row) const;
  float footerTop() const;
  std::array<float, 4> tabBounds(int tab) const;
  std::array<float, 4> footerButtonBounds(int button) const;

  void updateSprings(float deltaSeconds);
  void snapSprings();
  float rowReveal(int row) const;
  float selectionRowPosition() const;
  void selectRow(int row);
  void selectTab(int tab);
  void cycleSelected(int direction);
  bool dragSliderTo(ConfigurationSetting setting, float pointerX);
  void handlePointer(bool wheelScrolled, ConfigurationMenuAction* action);
  ConfigurationMenuAction activateSelected();
  void addCharacter(unsigned int codepoint);
  void eraseCharacter();
  std::string* editableField();
  static std::string topologyText(std::int64_t chunks);
  static std::string decimalText(double value);
  static std::string displayRuleSetName(const std::string& mode);
};
