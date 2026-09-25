#include "ConfigurationMenu.h"
#include "Game/CSimSounds.h"
#include "Game/CellContext.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/UiScale.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <queue>
#include <span>
#include <sstream>
#include <vector>

// Panel metrics in menu virtual space. Every tab lays out against the tallest
// tab so the panel keeps one size while switching.
static const float kHeaderHeight = 122.0f;
static const float kFooterHeight = 92.0f;
static const float kPreferredRowHeight = 42.0f;
static const int kMaximumTabRows = 7;
static const float kTabStripOffset = 84.0f;
static const float kTabStripHeight = 28.0f;
static const float kFooterButtonOffset = 44.0f;
static const float kFooterButtonHeight = 34.0f;
static const float kFooterButtonGap = 10.0f;
// Slider readouts sit right of the track in a fixed-width column.
static const float kReadoutWidth = 92.0f;

static const ConfigurationSetting kSimulationRows[] = {
  ConfigurationSetting::Family,     ConfigurationSetting::Ruleset,
  ConfigurationSetting::WorldWidth, ConfigurationSetting::WorldHeight,
  ConfigurationSetting::Tps,        ConfigurationSetting::Speed,
  ConfigurationSetting::StartPaused
};
static const ConfigurationSetting kCanvasRows[] = {
  ConfigurationSetting::CellStyle, ConfigurationSetting::CellGlow,
  ConfigurationSetting::GridLines, ConfigurationSetting::Fade,
  ConfigurationSetting::Inspector, ConfigurationSetting::EditHints
};
static const ConfigurationSetting kVideoRows[] = {
  ConfigurationSetting::Fullscreen, ConfigurationSetting::Vsync,
  ConfigurationSetting::FpsCap,     ConfigurationSetting::Msaa,
  ConfigurationSetting::ShowFps,    ConfigurationSetting::ShowMemory
};
static const ConfigurationSetting kAudioRows[] = {
  ConfigurationSetting::SoundVolume
};
static const ConfigurationSetting kControlsRows[] = {
  ConfigurationSetting::ZoomStep,
  ConfigurationSetting::InvertZoom,
  ConfigurationSetting::PanSpeed
};
static const ConfigurationSetting kGeneralRows[] = {
  ConfigurationSetting::UiScale,
  ConfigurationSetting::ReducedMotion,
  ConfigurationSetting::SoftwareCursor,
  ConfigurationSetting::Autosave,
  ConfigurationSetting::ConfirmClear
};

static const char* const kTabLabels[] = { "SIMULATION", "CANVAS",   "VIDEO",
                                          "AUDIO",      "CONTROLS", "GENERAL" };

// Indexed by ConfigurationSetting.
static const char* const kSettingLabels[] = { "Cell family",
                                              "Ruleset",
                                              "World width (chunks)",
                                              "World height (chunks)",
                                              "Simulation rate",
                                              "Speed multiplier",
                                              "Start paused",
                                              "Cell style",
                                              "Cell glow",
                                              "Grid lines",
                                              "Cell fade speed",
                                              "Simulation inspector",
                                              "Edit control hints",
                                              "Fullscreen",
                                              "Vertical sync",
                                              "FPS cap",
                                              "Anti-aliasing*",
                                              "FPS counter",
                                              "Memory readout",
                                              "Sound volume",
                                              "Zoom sensitivity",
                                              "Invert zoom",
                                              "Keyboard pan speed",
                                              "UI scale",
                                              "Reduced menu motion",
                                              "Software cursor",
                                              "Autosave",
                                              "Confirm clearing" };

static const char* const kSettingHelp[] = {
  "Choose the cell family; the ruleset list is filtered to this family.",
  "Choose transition behavior that belongs to the selected family.",
  "Far left is infinite. Finite sizes wrap around the edges like a torus.",
  "Far left is infinite. Finite sizes wrap around the edges like a torus.",
  "Target ticks per second, 1 to 1000. Type digits for an exact rate.",
  "Multiplies the simulation rate. Type digits for an exact multiplier.",
  "Open canvases in edit mode; off starts them running. E toggles.",
  "Up close, cells draw as lit LED keys or as flat pixels.",
  "How much light lit LED cells throw onto the cells around them.",
  "Draw thin lines between cells once they are big enough.",
  "How fast cell colors blend; far left snaps instantly. Maximum 100.",
  "Show generation, cell coordinates, and population in the simulation.",
  "Show input hints at the bottom while editing.",
  "Use the entire display.",
  "Synchronize frame presentation to the monitor.",
  "Frame-rate limit; far right is uncapped. VSync still caps at refresh.",
  "Multisample anti-aliasing: Off, 2x, 4x, 8x. (*Requires restart)",
  "Show frames per second and frame time in the top-left corner.",
  "Show the game's memory use in the top-left corner.",
  "Sound effect volume, off to 100%; each step previews the new level.",
  "How far one mouse-wheel notch zooms the canvas.",
  "Reverse the mouse-wheel zoom direction.",
  "Arrow keys pan the canvas at this speed; far left turns them off.",
  "Far left sizes the interface to the window; or pick 1x to 4x.",
  "Disable decorative motion and snap menu transitions.",
  "Draw CSim's animated pointer in place of the system cursor.",
  "Save the canvas to autosave.csim this often (console: load autosave).",
  "Ask before clear_canvas empties the canvas."
};

static const char* const kFooterLabels[] = { "Apply changes",
                                             "Discard changes",
                                             "Exit simulator" };
static const char* const kFooterHelp[] = {
  "Validate, save, and apply the displayed settings.",
  "Close the menu without changing any settings.",
  "Leave CSim (confirmation appears during a simulation)."
};

// Slider stops, in slider order (left to right).
static const double kWorldStops[] = { 0,    1,     2,     4,      8,      16,
                                      32,   64,    128,   256,    512,    1024,
                                      4096, 16384, 65536, 262144, 1000000 };
static const double kTpsStops[] = { 1,   2,   3,   4,   5,   6,   8,
                                    10,  12,  15,  20,  24,  30,  40,
                                    50,  60,  75,  90,  120, 150, 200,
                                    250, 300, 400, 500, 600, 750, 1000 };
static const double kSpeedStops[] = { 0.1, 0.25, 0.5, 0.75, 1,  1.25, 1.5, 2,
                                      2.5, 3,    4,   5,    6,  8,    10,  12,
                                      16,  20,   25,  32,   50, 75,   100 };
static const double kFadeStops[] = {
  0, 0.5, 1, 1.5, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 40, 50, 75, 100
};
// Zero (uncapped) is the far right, past every finite cap.
static const double kFpsStops[] = { 30,  60,  75,  90,  120, 144,  165,
                                    200, 240, 300, 360, 500, 1000, 0 };
static const double kVolumeStops[] = { 0,  5,  10, 15, 20, 25, 30,
                                       35, 40, 45, 50, 55, 60, 65,
                                       70, 75, 80, 85, 90, 95, 100 };

// Zero (automatic: sized to the window) is the far left.
static const double kUiScaleStops[] = { 0,    1,   1.25, 1.5, 1.75, 2,
                                        2.25, 2.5, 2.75, 3,   3.5,  4 };

// Glow 0 is off; 1 is the standard look.
static const double kGlowStops[] = { 0, 0.25, 0.5, 0.75, 1, 1.25, 1.5, 2 };
// Zoom per mouse-wheel notch.
static const double kZoomStepStops[] = { 0.05, 0.1, 0.15, 0.2,
                                         0.25, 0.3, 0.4,  0.5 };
// Arrow-key panning, screen pixels per second; zero turns it off.
static const double kPanSpeedStops[] = {
  0, 200, 400, 600, 800, 1000, 1400, 2000
};
static const double kAutosaveStops[] = { 0, 1, 2, 5, 10, 15, 30, 60 };

static const long kMsaaOptions[] = { 0, 2, 4, 8 };

static std::span<const ConfigurationSetting>
tabRows(ConfigurationTab tab)
{
  switch (tab) {
    case ConfigurationTab::Simulation:
      return std::span<const ConfigurationSetting>(kSimulationRows);
    case ConfigurationTab::Canvas:
      return std::span<const ConfigurationSetting>(kCanvasRows);
    case ConfigurationTab::Video:
      return std::span<const ConfigurationSetting>(kVideoRows);
    case ConfigurationTab::Audio:
      return std::span<const ConfigurationSetting>(kAudioRows);
    case ConfigurationTab::Controls:
      return std::span<const ConfigurationSetting>(kControlsRows);
    case ConfigurationTab::General:
      return std::span<const ConfigurationSetting>(kGeneralRows);
    default:
      return std::span<const ConfigurationSetting>();
  }
}

static std::span<const double>
sliderStops(ConfigurationSetting setting)
{
  switch (setting) {
    case ConfigurationSetting::WorldWidth:
    case ConfigurationSetting::WorldHeight:
      return std::span<const double>(kWorldStops);
    case ConfigurationSetting::Tps:
      return std::span<const double>(kTpsStops);
    case ConfigurationSetting::Speed:
      return std::span<const double>(kSpeedStops);
    case ConfigurationSetting::Fade:
      return std::span<const double>(kFadeStops);
    case ConfigurationSetting::FpsCap:
      return std::span<const double>(kFpsStops);
    case ConfigurationSetting::SoundVolume:
      return std::span<const double>(kVolumeStops);
    case ConfigurationSetting::UiScale:
      return std::span<const double>(kUiScaleStops);
    case ConfigurationSetting::CellGlow:
      return std::span<const double>(kGlowStops);
    case ConfigurationSetting::ZoomStep:
      return std::span<const double>(kZoomStepStops);
    case ConfigurationSetting::PanSpeed:
      return std::span<const double>(kPanSpeedStops);
    case ConfigurationSetting::Autosave:
      return std::span<const double>(kAutosaveStops);
    default:
      return std::span<const double>();
  }
}

// Position of a value along its slider; only the FPS cap reorders (its
// uncapped zero sits beyond every finite cap).
static double
sliderRank(ConfigurationSetting setting, double value)
{
  return setting == ConfigurationSetting::FpsCap && value == 0.0 ? 1.0e12
                                                                 : value;
}

static bool
parseTopologyText(const std::string& text, std::int64_t* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  std::string normalized = text;
  std::transform(normalized.begin(),
                 normalized.end(),
                 normalized.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  if (normalized == "inf" || normalized == "infinity" || normalized == "0") {
    *value = 0;
    return true;
  }
  try {
    std::size_t consumed = 0u;
    const long long parsed = std::stoll(normalized, &consumed);
    if (consumed != normalized.size() || parsed < 1 ||
        parsed > SparseCellGrid::kMaximumWorldChunksPerAxis) {
      return false;
    }
    *value = static_cast<std::int64_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

static bool
parseLongText(const std::string& text, long minimum, long maximum, long* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0u;
    const long parsed = std::stol(text, &consumed);
    if (consumed != text.size() || parsed < minimum || parsed > maximum) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

static bool
parseDoubleText(const std::string& text,
                double minimum,
                double maximum,
                bool minimumInclusive,
                double* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0u;
    const double parsed = std::stod(text, &consumed);
    const bool minimumValid =
      minimumInclusive ? parsed >= minimum : parsed > minimum;
    if (consumed != text.size() || !std::isfinite(parsed) || !minimumValid ||
        parsed > maximum) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

// A small arrowhead pointing left (direction -1) or right (+1).
static void
drawArrowhead(GameVisual& visual,
              float centerX,
              float centerY,
              float direction,
              ColorRgba color)
{
  const GuiPoint2 points[3] = {
    GuiPoint2{ centerX - 2.5f * direction, centerY - 5.0f },
    GuiPoint2{ centerX + 2.5f * direction, centerY },
    GuiPoint2{ centerX - 2.5f * direction, centerY + 5.0f }
  };
  GuiKit::drawPolyline(visual, points, 3, 2.0f, color);
}

ConfigurationMenu::ConfigurationMenu(IRenderWindow* targetWindow,
                                     Renderer* targetRenderer)
  : window(targetWindow)
  , renderer(targetRenderer)
  , visual(4096u)
  , openState(false)
  , replaceFieldOnType(true)
  , selectedRow(0)
  , panelX(0.0f)
  , panelY(0.0f)
  , panelWidth(620.0f)
  , panelHeight(508.0f)
  , firstRowY(0.0f)
  , rowHeight(kPreferredRowHeight)
  , vsync(true)
  , fullscreen(false)
  , uiScale(1.0)
  , msaa(4)
{
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  visual.setWindow(window);
  visual.setRenderer(renderer);
  visual.prepare(renderer);
  setVisible(false);
}

std::string
ConfigurationMenu::topologyText(std::int64_t chunks)
{
  return chunks == 0 ? "inf" : std::to_string(chunks);
}

std::string
ConfigurationMenu::decimalText(double value)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2) << value;
  std::string text = stream.str();
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text.empty() ? "0" : text;
}

std::string
ConfigurationMenu::displayRuleSetName(const std::string& mode)
{
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(mode);
  return definition == nullptr ? mode : definition->name;
}

void
ConfigurationMenu::open(const SimulatorConfiguration& current)
{
  family = CellContext::NormalizeFamilyString(current.family);
  ruleSet = CellContext::NormalizeModeString(current.ruleSet);
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(ruleSet);
  if (definition == nullptr || definition->familyId != family) {
    ruleSet = "GAME_OF_LIFE";
    definition = RuleSetRegistry::instance().getRuleSetDefinition(ruleSet);
    family = definition == nullptr ? std::string() : definition->familyId;
  }
  worldWidthText = topologyText(current.worldChunkWidth);
  worldHeightText = topologyText(current.worldChunkHeight);
  tpsText = std::to_string(current.tps);
  speedText = decimalText(current.speedFactor);
  fadeText = decimalText(current.fadeSpeed);
  vsync = current.vsync;
  editHints = current.editHints;
  softwareCursor = current.softwareCursor;
  fullscreen = current.fullscreen;
  // Zero is automatic; anything else unusable opens as 1x.
  uiScale = current.uiScale == 0.0 || current.uiScale >= 1.0
              ? std::min(current.uiScale, 8.0)
              : 1.0;
  msaa = current.msaa;
  fpsCapText = std::to_string(std::max(0L, current.fpsCap));
  showInspector = current.showInspector;
  reducedUiMotion = current.reducedUiMotion;
  soundVolume = std::clamp(current.soundVolume, 0L, 100L);
  startPaused = current.startPaused;
  ledCells = current.ledCells;
  gridLines = current.gridLines;
  showFps = current.showFps;
  showMemory = current.showMemory;
  invertZoom = current.invertZoom;
  confirmClear = current.confirmClear;
  cellGlow = std::isfinite(current.cellGlow)
               ? std::clamp(current.cellGlow, 0.0, 2.0)
               : 1.0;
  zoomStep = std::isfinite(current.zoomStep)
               ? std::clamp(current.zoomStep, 0.01, 0.5)
               : 0.15;
  panSpeed = static_cast<double>(std::clamp(current.panSpeed, 0L, 5000L));
  autosaveMinutes =
    static_cast<double>(std::clamp(current.autosaveMinutes, 0L, 240L));
  firstVisibleRow = 0;
  errorMessage.clear();
  selectedRow = 0;
  replaceFieldOnType = true;
  dragSetting = ConfigurationSetting::Count;
  dragChanged = false;
  hoveredTab = -1;
  hoveredFooterButton = -1;
  animator.setReducedMotion(reducedUiMotion);
  animator.restart();
  // Ignore a held click that opened the modal until its first release.
  pointer.reset(true);
  tilt.level();
  openState = true;
  setVisible(true);
  updateLayout();
  rowFocus.configure(GuiMotion::kJelly);
  toggleKnobs.configure(GuiMotion::kBoing);
  sliderKnobs.configure(GuiMotion::kGlide);
  segmentKnobs.configure(GuiMotion::kJelly);
  tabFocus.configure(GuiMotion::kJelly);
  footerFocus.configure(GuiMotion::kJelly);
  scrollThumb.configure(GuiMotion::kGlide);
  tabIndicator.configure(GuiMotion::kLiquidHead);
  tabSwap.configure(GuiMotion::kSwell);
  snapSprings();
}

int
ConfigurationMenu::rowCount() const
{
  return static_cast<int>(tabRows(activeTab).size());
}

ConfigurationSetting
ConfigurationMenu::settingAt(int row) const
{
  const std::span<const ConfigurationSetting> rows = tabRows(activeTab);
  if (row < 0 || row >= static_cast<int>(rows.size())) {
    return ConfigurationSetting::Count;
  }
  return rows[static_cast<std::size_t>(row)];
}

ConfigurationSetting
ConfigurationMenu::selectedSetting() const
{
  return settingAt(selectedRow);
}

int
ConfigurationMenu::rowOfSettingForTesting(ConfigurationSetting setting)
{
  for (int tab = 0; tab < kTabCount; ++tab) {
    const std::span<const ConfigurationSetting> rows =
      tabRows(static_cast<ConfigurationTab>(tab));
    for (std::size_t row = 0u; row < rows.size(); ++row) {
      if (rows[row] == setting) {
        return static_cast<int>(row);
      }
    }
  }
  return -1;
}

ConfigurationMenu::ControlKind
ConfigurationMenu::controlKind(ConfigurationSetting setting)
{
  switch (setting) {
    case ConfigurationSetting::Family:
    case ConfigurationSetting::Ruleset:
      return ControlKind::Choice;
    case ConfigurationSetting::Msaa:
    case ConfigurationSetting::CellStyle:
      return ControlKind::Segments;
    case ConfigurationSetting::UiScale:
    case ConfigurationSetting::WorldWidth:
    case ConfigurationSetting::WorldHeight:
    case ConfigurationSetting::Tps:
    case ConfigurationSetting::Speed:
    case ConfigurationSetting::Fade:
    case ConfigurationSetting::FpsCap:
    case ConfigurationSetting::SoundVolume:
    case ConfigurationSetting::CellGlow:
    case ConfigurationSetting::ZoomStep:
    case ConfigurationSetting::PanSpeed:
    case ConfigurationSetting::Autosave:
      return ControlKind::Slider;
    default:
      return ControlKind::Toggle;
  }
}

bool*
ConfigurationMenu::toggleSlot(ConfigurationSetting setting)
{
  switch (setting) {
    case ConfigurationSetting::Vsync:
      return &vsync;
    case ConfigurationSetting::Fullscreen:
      return &fullscreen;
    case ConfigurationSetting::Inspector:
      return &showInspector;
    case ConfigurationSetting::ReducedMotion:
      return &reducedUiMotion;
    case ConfigurationSetting::EditHints:
      return &editHints;
    case ConfigurationSetting::SoftwareCursor:
      return &softwareCursor;
    case ConfigurationSetting::StartPaused:
      return &startPaused;
    case ConfigurationSetting::GridLines:
      return &gridLines;
    case ConfigurationSetting::ShowFps:
      return &showFps;
    case ConfigurationSetting::ShowMemory:
      return &showMemory;
    case ConfigurationSetting::InvertZoom:
      return &invertZoom;
    case ConfigurationSetting::ConfirmClear:
      return &confirmClear;
    default:
      return nullptr;
  }
}

bool
ConfigurationMenu::toggleValue(ConfigurationSetting setting) const
{
  const bool* slot = const_cast<ConfigurationMenu*>(this)->toggleSlot(setting);
  return slot != nullptr && *slot;
}

double*
ConfigurationMenu::sliderNumber(ConfigurationSetting setting)
{
  switch (setting) {
    case ConfigurationSetting::UiScale:
      return &uiScale;
    case ConfigurationSetting::CellGlow:
      return &cellGlow;
    case ConfigurationSetting::ZoomStep:
      return &zoomStep;
    case ConfigurationSetting::PanSpeed:
      return &panSpeed;
    case ConfigurationSetting::Autosave:
      return &autosaveMinutes;
    default:
      return nullptr;
  }
}

const double*
ConfigurationMenu::sliderNumber(ConfigurationSetting setting) const
{
  return const_cast<ConfigurationMenu*>(this)->sliderNumber(setting);
}

std::string*
ConfigurationMenu::sliderText(ConfigurationSetting setting)
{
  switch (setting) {
    case ConfigurationSetting::WorldWidth:
      return &worldWidthText;
    case ConfigurationSetting::WorldHeight:
      return &worldHeightText;
    case ConfigurationSetting::Tps:
      return &tpsText;
    case ConfigurationSetting::Speed:
      return &speedText;
    case ConfigurationSetting::Fade:
      return &fadeText;
    case ConfigurationSetting::FpsCap:
      return &fpsCapText;
    default:
      return nullptr;
  }
}

const std::string*
ConfigurationMenu::sliderText(ConfigurationSetting setting) const
{
  return const_cast<ConfigurationMenu*>(this)->sliderText(setting);
}

bool
ConfigurationMenu::sliderValue(ConfigurationSetting setting,
                               double* value) const
{
  long parsedLong = 0;
  double parsedDouble = 0.0;
  std::int64_t parsedChunks = 0;
  switch (setting) {
    case ConfigurationSetting::WorldWidth:
    case ConfigurationSetting::WorldHeight:
      if (!parseTopologyText(*sliderText(setting), &parsedChunks)) {
        return false;
      }
      *value = static_cast<double>(parsedChunks);
      return true;
    case ConfigurationSetting::Tps:
      if (!parseLongText(tpsText, 1, 1000, &parsedLong)) {
        return false;
      }
      *value = static_cast<double>(parsedLong);
      return true;
    case ConfigurationSetting::FpsCap:
      if (!parseLongText(fpsCapText, 0, 1000, &parsedLong)) {
        return false;
      }
      *value = static_cast<double>(parsedLong);
      return true;
    case ConfigurationSetting::Speed:
      if (!parseDoubleText(speedText, 0.0, 100.0, false, &parsedDouble)) {
        return false;
      }
      *value = parsedDouble;
      return true;
    case ConfigurationSetting::Fade:
      if (!parseDoubleText(fadeText, 0.0, 100.0, true, &parsedDouble)) {
        return false;
      }
      *value = parsedDouble;
      return true;
    case ConfigurationSetting::SoundVolume:
      *value = static_cast<double>(soundVolume);
      return true;
    default: {
      const double* number = sliderNumber(setting);
      if (number == nullptr) {
        return false;
      }
      *value = *number;
      return true;
    }
  }
}

float
ConfigurationMenu::sliderFraction(ConfigurationSetting setting) const
{
  const std::span<const double> stops = sliderStops(setting);
  double value = 0.0;
  if (stops.size() < 2u || !sliderValue(setting, &value)) {
    return -1.0f;
  }
  const double rank = sliderRank(setting, value);
  const double last = static_cast<double>(stops.size() - 1u);
  if (rank <= sliderRank(setting, stops.front())) {
    return 0.0f;
  }
  if (rank >= sliderRank(setting, stops.back())) {
    return 1.0f;
  }
  for (std::size_t index = 0u; index + 1u < stops.size(); ++index) {
    const double low = sliderRank(setting, stops[index]);
    const double high = sliderRank(setting, stops[index + 1u]);
    if (rank >= low && rank <= high) {
      const double within = high > low ? (rank - low) / (high - low) : 0.0;
      return static_cast<float>((static_cast<double>(index) + within) / last);
    }
  }
  return 1.0f;
}

void
ConfigurationMenu::linkWorldAxes(ConfigurationSetting changed)
{
  // Both axes are finite or both infinite, so the slider moves the other
  // axis across that boundary with it instead of leaving an invalid pair.
  const bool widthChanged = changed == ConfigurationSetting::WorldWidth;
  const std::string& moved = widthChanged ? worldWidthText : worldHeightText;
  std::string& other = widthChanged ? worldHeightText : worldWidthText;
  std::int64_t movedValue = 0;
  std::int64_t otherValue = 0;
  if (!parseTopologyText(moved, &movedValue)) {
    return;
  }
  const bool otherParsed = parseTopologyText(other, &otherValue);
  if (movedValue == 0 && (!otherParsed || otherValue != 0)) {
    other = topologyText(0);
  } else if (movedValue != 0 && (!otherParsed || otherValue == 0)) {
    other = moved;
  }
}

bool
ConfigurationMenu::setSliderStop(ConfigurationSetting setting, int stop)
{
  const std::span<const double> stops = sliderStops(setting);
  if (stops.empty()) {
    return false;
  }
  const double value = stops[static_cast<std::size_t>(
    std::clamp(stop, 0, static_cast<int>(stops.size()) - 1))];
  if (setting == ConfigurationSetting::SoundVolume) {
    const long next = static_cast<long>(value);
    if (next == soundVolume) {
      return false;
    }
    soundVolume = next;
    return true;
  }
  double* number = sliderNumber(setting);
  if (number != nullptr) {
    if (value == *number) {
      return false;
    }
    *number = value;
    return true;
  }
  std::string* text = sliderText(setting);
  std::string next;
  if (setting == ConfigurationSetting::WorldWidth ||
      setting == ConfigurationSetting::WorldHeight) {
    next = topologyText(static_cast<std::int64_t>(value));
  } else if (setting == ConfigurationSetting::Speed ||
             setting == ConfigurationSetting::Fade) {
    next = decimalText(value);
  } else {
    next = std::to_string(static_cast<long>(value));
  }
  if (text == nullptr || *text == next) {
    return false;
  }
  *text = next;
  if (setting == ConfigurationSetting::WorldWidth ||
      setting == ConfigurationSetting::WorldHeight) {
    linkWorldAxes(setting);
  }
  return true;
}

bool
ConfigurationMenu::stepSlider(ConfigurationSetting setting, int direction)
{
  const std::span<const double> stops = sliderStops(setting);
  if (stops.empty() || direction == 0) {
    return false;
  }
  double value = 0.0;
  if (!sliderValue(setting, &value)) {
    // An unreadable typed draft restarts from the first stop.
    return setSliderStop(setting, 0);
  }
  const double rank = sliderRank(setting, value);
  const double epsilon = 1.0e-9;
  int next = -1;
  for (std::size_t index = 0u; index < stops.size(); ++index) {
    const double stopRank = sliderRank(setting, stops[index]);
    if (direction > 0 && stopRank > rank + epsilon) {
      next = static_cast<int>(index);
      break;
    }
    if (direction < 0 && stopRank < rank - epsilon) {
      next = static_cast<int>(index);
    }
  }
  return next >= 0 && setSliderStop(setting, next);
}

std::string
ConfigurationMenu::sliderReadout(ConfigurationSetting setting) const
{
  const std::string* text = sliderText(setting);
  const bool typing =
    text != nullptr && selectedSetting() == setting && !replaceFieldOnType;
  double value = 0.0;
  if (typing || !sliderValue(setting, &value)) {
    return text == nullptr ? std::string() : *text;
  }
  switch (setting) {
    case ConfigurationSetting::WorldWidth:
    case ConfigurationSetting::WorldHeight:
      return value == 0.0 ? "Infinite" : *text;
    case ConfigurationSetting::Tps:
      return *text + " TPS";
    case ConfigurationSetting::Speed:
      return *text + "x";
    case ConfigurationSetting::Fade:
      return value == 0.0 ? "Instant" : *text;
    case ConfigurationSetting::FpsCap:
      return value == 0.0 ? "Uncapped" : *text + " FPS";
    case ConfigurationSetting::SoundVolume:
      return soundVolume == 0 ? "Off" : std::to_string(soundVolume) + "%";
    case ConfigurationSetting::UiScale: {
      if (uiScale != 0.0) {
        return decimalText(uiScale) + "x";
      }
      // Automatic shows the scale it picks for the current window.
      const std::array<int, 2> dimensions = window != nullptr
                                              ? window->getWindowDimensions()
                                              : std::array<int, 2>{ 0, 0 };
      return "Auto " +
             decimalText(UiScale::automatic(dimensions[0], dimensions[1])) +
             "x";
    }
    case ConfigurationSetting::CellGlow:
      return value == 0.0 ? "Off"
                          : std::to_string(std::lround(value * 100.0)) + "%";
    case ConfigurationSetting::ZoomStep:
      return std::to_string(std::lround(value * 100.0)) + "%";
    case ConfigurationSetting::PanSpeed:
      return value == 0.0 ? "Off"
                          : std::to_string(std::lround(value)) + " px/s";
    case ConfigurationSetting::Autosave:
      return value == 0.0 ? "Off" : std::to_string(std::lround(value)) + " min";
    default:
      return std::string();
  }
}

int
ConfigurationMenu::segmentCount(ConfigurationSetting setting) const
{
  return setting == ConfigurationSetting::Msaa
           ? static_cast<int>(std::size(kMsaaOptions))
         : setting == ConfigurationSetting::CellStyle ? 2
                                                      : 0;
}

int
ConfigurationMenu::segmentIndex(ConfigurationSetting setting) const
{
  if (setting == ConfigurationSetting::CellStyle) {
    return ledCells ? 0 : 1;
  }
  for (int index = 0; index < static_cast<int>(std::size(kMsaaOptions));
       ++index) {
    if (kMsaaOptions[index] == msaa) {
      return index;
    }
  }
  // An unlisted stored value reads as the default, 4x.
  return 2;
}

std::string
ConfigurationMenu::segmentLabel(ConfigurationSetting setting, int index) const
{
  if (setting == ConfigurationSetting::CellStyle) {
    return index == 0 ? "LED keys" : "Flat";
  }
  return kMsaaOptions[index] == 0 ? "Off"
                                  : std::to_string(kMsaaOptions[index]) + "x";
}

void
ConfigurationMenu::setSegment(ConfigurationSetting setting, int index)
{
  const int clamped = std::clamp(index, 0, segmentCount(setting) - 1);
  if (setting == ConfigurationSetting::Msaa) {
    msaa = kMsaaOptions[clamped];
  } else if (setting == ConfigurationSetting::CellStyle) {
    ledCells = clamped == 0;
  }
}

float
ConfigurationMenu::valueLeft() const
{
  return panelX + panelWidth * 0.46f;
}

float
ConfigurationMenu::valueRight() const
{
  return panelX + panelWidth - 30.0f;
}

float
ConfigurationMenu::sliderTrackLeft() const
{
  return valueLeft() + 16.0f;
}

float
ConfigurationMenu::sliderTrackRight() const
{
  return valueRight() - kReadoutWidth - 14.0f;
}

float
ConfigurationMenu::rowTop(int row) const
{
  return firstRowY + animator.panelOffsetY() +
         rowHeight * static_cast<float>(row - firstVisibleRow);
}

float
ConfigurationMenu::footerTop() const
{
  return panelY + animator.panelOffsetY() + panelHeight - kFooterHeight;
}

std::array<float, 4>
ConfigurationMenu::tabBounds(int tab) const
{
  const float stripWidth = panelWidth - 40.0f;
  const float tabWidth = stripWidth / static_cast<float>(kTabCount);
  return { panelX + 20.0f + tabWidth * static_cast<float>(tab),
           panelY + animator.panelOffsetY() + kTabStripOffset,
           tabWidth,
           kTabStripHeight };
}

std::array<float, 4>
ConfigurationMenu::footerButtonBounds(int button) const
{
  const float buttonWidth =
    (panelWidth - 40.0f -
     kFooterButtonGap * static_cast<float>(kFooterButtonCount - 1)) /
    static_cast<float>(kFooterButtonCount);
  return { panelX + 20.0f +
             (buttonWidth + kFooterButtonGap) * static_cast<float>(button),
           footerTop() + kFooterButtonOffset,
           buttonWidth,
           kFooterButtonHeight };
}

std::array<float, 4>
ConfigurationMenu::getTabBoundsForTesting(ConfigurationTab tab) const
{
  return tabBounds(static_cast<int>(tab));
}

std::array<float, 4>
ConfigurationMenu::getFooterButtonBoundsForTesting(int button) const
{
  return footerButtonBounds(button);
}

std::array<float, 4>
ConfigurationMenu::getSliderTrackBoundsForTesting(int row) const
{
  if (controlKind(settingAt(row)) != ControlKind::Slider) {
    return { 0.0f, 0.0f, 0.0f, 0.0f };
  }
  return { sliderTrackLeft(),
           rowTop(row),
           sliderTrackRight() - sliderTrackLeft(),
           rowHeight - 4.0f };
}

void
ConfigurationMenu::selectTabForTesting(ConfigurationTab tab)
{
  selectTab(static_cast<int>(tab));
}

void
ConfigurationMenu::snapSprings()
{
  rowFocus.focusOnly(footerSelected() ? -1 : selectedRow, kMaximumTabRows);
  rowFocus.snapAll();
  for (int index = 0; index < kSettingCount; ++index) {
    const ConfigurationSetting setting =
      static_cast<ConfigurationSetting>(index);
    toggleKnobs.snap(index, toggleValue(setting) ? 1.0f : 0.0f);
    const float fraction = sliderFraction(setting);
    sliderKnobs.snap(index, fraction >= 0.0f ? fraction : 0.0f);
    if (controlKind(setting) == ControlKind::Segments) {
      segmentKnobs.snap(index, static_cast<float>(segmentIndex(setting)));
    }
  }
  for (int tab = 0; tab < kTabCount; ++tab) {
    tabFocus.snap(tab, tab == static_cast<int>(activeTab) ? 1.0f : 0.0f);
  }
  for (int button = 0; button < kFooterButtonCount; ++button) {
    footerFocus.snap(button, 0.0f);
  }
  tabIndicator.snapTo(static_cast<float>(activeTab));
  tabSwap.snapTo(1.0f);
  scrollThumb.snapTo(static_cast<float>(firstVisibleRow));
}

void
ConfigurationMenu::updateSprings(float deltaSeconds)
{
  const bool still = animator.reducedMotion();
  rowFocus.focusOnly(footerSelected() ? -1 : selectedRow, kMaximumTabRows);
  rowFocus.tick(deltaSeconds, still);
  for (int index = 0; index < kSettingCount; ++index) {
    const ConfigurationSetting setting =
      static_cast<ConfigurationSetting>(index);
    const ControlKind kind = controlKind(setting);
    if (kind == ControlKind::Toggle) {
      toggleKnobs.setTarget(index, toggleValue(setting) ? 1.0f : 0.0f);
    } else if (kind == ControlKind::Slider) {
      const float fraction = sliderFraction(setting);
      if (setting == dragSetting && fraction >= 0.0f) {
        // The knob stays under the pointer while dragging.
        sliderKnobs.snap(index, fraction);
      } else if (fraction >= 0.0f) {
        sliderKnobs.setTarget(index, fraction);
      }
    } else if (kind == ControlKind::Segments) {
      segmentKnobs.setTarget(index, static_cast<float>(segmentIndex(setting)));
    }
  }
  toggleKnobs.tick(deltaSeconds, still);
  sliderKnobs.tick(deltaSeconds, still);
  segmentKnobs.tick(deltaSeconds, still);
  for (int tab = 0; tab < kTabCount; ++tab) {
    tabFocus.setTarget(tab,
                       tab == static_cast<int>(activeTab) ? 1.0f
                       : tab == hoveredTab                ? 0.45f
                                                          : 0.0f);
  }
  tabFocus.tick(deltaSeconds, still);
  const int selectedButton = footerSelected() ? selectedRow - rowCount() : -1;
  for (int button = 0; button < kFooterButtonCount; ++button) {
    footerFocus.setTarget(button,
                          button == selectedButton        ? 1.0f
                          : button == hoveredFooterButton ? 0.4f
                                                          : 0.0f);
  }
  footerFocus.tick(deltaSeconds, still);
  tabIndicator.setTarget(static_cast<float>(activeTab));
  tabIndicator.tick(deltaSeconds, still);
  tabSwap.tick(deltaSeconds, still);
  scrollThumb.setTarget(static_cast<float>(firstVisibleRow));
  scrollThumb.tick(deltaSeconds, still);
  tilt.aim(pointer.x(),
           pointer.y(),
           panelFit.virtualWidth,
           panelFit.virtualHeight,
           openState);
  tilt.tick(deltaSeconds, still);
}

void
ConfigurationMenu::tick(float deltaSeconds)
{
  if (!openState) {
    return;
  }
  animator.tick(deltaSeconds);
  updateSprings(deltaSeconds);
}

float
ConfigurationMenu::getAnimationProgressForTesting() const
{
  return animator.openProgress();
}

float
ConfigurationMenu::rowReveal(int row) const
{
  return animator.rowReveal(row, firstVisibleRow);
}

float
ConfigurationMenu::selectionRowPosition() const
{
  return animator.selectionPosition(static_cast<float>(selectedRow));
}

float
ConfigurationMenu::getSelectionPositionForTesting() const
{
  return selectionRowPosition();
}

float
ConfigurationMenu::getValuePulseForTesting() const
{
  return animator.valuePulse();
}

void
ConfigurationMenu::close()
{
  openState = false;
  dragSetting = ConfigurationSetting::Count;
  pointer.reset(false);
  setVisible(false);
}

void
ConfigurationMenu::setError(const std::string& message)
{
  errorMessage = message;
  if (!message.empty()) {
    CSimSounds::play(CSimSound::MenuError);
  }
}

void
ConfigurationMenu::updateLayout()
{
  // Fit oversized UI preferences uniformly; hit testing uses the same scale.
  panelFit = GuiPanelLayout::fit(window, renderer, &visual);
  const float virtualWidth = panelFit.virtualWidth;
  const float virtualHeight = panelFit.virtualHeight;
  panelWidth = std::min(680.0f, std::max(200.0f, virtualWidth - 32.0f));
  if (panelWidth > virtualWidth) {
    panelWidth = virtualWidth;
  }
  const float preferredHeight =
    kHeaderHeight + kPreferredRowHeight * kMaximumTabRows + kFooterHeight;
  panelHeight = std::min(
    preferredHeight,
    std::max(kHeaderHeight + kFooterHeight + GuiPanelLayout::kMinimumRowHeight,
             virtualHeight - 24.0f));
  if (panelHeight > virtualHeight) {
    panelHeight = virtualHeight;
  }
  // The layout origin swivels with the body layer, so hit testing and
  // drawing agree while the panel tilts toward the pointer.
  panelX =
    std::max(0.0f, (virtualWidth - panelWidth) * 0.5f) + tilt.bodyShiftX();
  panelY =
    std::max(0.0f, (virtualHeight - panelHeight) * 0.5f) + tilt.bodyShiftY();

  firstRowY = panelY + kHeaderHeight;
  const float availableRowsH = panelHeight - kHeaderHeight - kFooterHeight;
  // One row height for every tab, sized for the tallest one.
  const int fittingRows =
    GuiPanelLayout::visibleRowCount(availableRowsH, kMaximumTabRows);
  rowHeight = std::min(kPreferredRowHeight,
                       availableRowsH / static_cast<float>(fittingRows));
  visibleRows = std::clamp(fittingRows, 1, std::max(1, rowCount()));
  firstVisibleRow = GuiPanelLayout::clampFirstVisibleRow(
    firstVisibleRow, rowCount(), visibleRows);
}

void
ConfigurationMenu::selectRow(int row)
{
  const int nextRow = std::clamp(row, 0, rowCount() + kFooterButtonCount - 1);
  if (nextRow != selectedRow) {
    if (nextRow < rowCount()) {
      // From the footer the drop pours up from below the last row.
      animator.beginSelectionTravel(
        static_cast<float>(std::min(selectedRow, rowCount())),
        static_cast<float>(nextRow));
    }
    selectedRow = nextRow;
    animator.resetCaret();
    CSimSounds::play(CSimSound::MenuHover);
  }
  if (!footerSelected()) {
    firstVisibleRow =
      GuiPanelLayout::scrollToRow(firstVisibleRow, selectedRow, visibleRows);
  }
  replaceFieldOnType = true;
  errorMessage.clear();
}

void
ConfigurationMenu::selectTab(int tab)
{
  const int count = kTabCount;
  const int next = ((tab % count) + count) % count;
  const int current = static_cast<int>(activeTab);
  if (next == current) {
    return;
  }
  const int footerButton = footerSelected() ? selectedRow - rowCount() : -1;
  tabSwapDirection = next > current ? 1.0f : -1.0f;
  activeTab = static_cast<ConfigurationTab>(next);
  selectedRow = footerButton >= 0 ? rowCount() + footerButton : 0;
  firstVisibleRow = 0;
  updateLayout();
  animator.settleSelection();
  animator.resetCaret();
  rowFocus.focusOnly(footerSelected() ? -1 : selectedRow, kMaximumTabRows);
  rowFocus.snapAll();
  tabSwap.snapTo(0.0f);
  tabSwap.setTarget(1.0f);
  scrollThumb.snapTo(0.0f);
  dragSetting = ConfigurationSetting::Count;
  replaceFieldOnType = true;
  errorMessage.clear();
  CSimSounds::play(CSimSound::MenuSelect);
}

std::string*
ConfigurationMenu::editableField()
{
  return sliderText(selectedSetting());
}

void
ConfigurationMenu::addCharacter(unsigned int codepoint)
{
  std::string* field = editableField();
  if (field == nullptr || codepoint > 127u) {
    return;
  }
  const ConfigurationSetting setting = selectedSetting();
  const char character = static_cast<char>(codepoint);
  bool accepted = std::isdigit(static_cast<unsigned char>(character)) != 0;
  if (setting == ConfigurationSetting::WorldWidth ||
      setting == ConfigurationSetting::WorldHeight) {
    const char lower =
      static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    accepted = accepted || lower == 'i' || lower == 'n' || lower == 'f';
  } else if (setting == ConfigurationSetting::Speed ||
             setting == ConfigurationSetting::Fade) {
    accepted = accepted || character == '.';
  }
  if (!accepted || (!replaceFieldOnType && field->size() >= 16u)) {
    CSimSounds::play(CSimSound::MenuError);
    return;
  }
  if (replaceFieldOnType) {
    field->clear();
    replaceFieldOnType = false;
  }
  field->push_back(character);
  animator.resetCaret();
  animator.triggerValuePulse();
  errorMessage.clear();
}

void
ConfigurationMenu::eraseCharacter()
{
  std::string* field = editableField();
  if (field == nullptr) {
    return;
  }
  if (replaceFieldOnType) {
    field->clear();
    replaceFieldOnType = false;
  } else if (!field->empty()) {
    field->pop_back();
  }
  animator.resetCaret();
  animator.triggerValuePulse();
  errorMessage.clear();
}

void
ConfigurationMenu::cycleSelected(int direction)
{
  if (direction == 0) {
    return;
  }
  if (footerSelected()) {
    // LEFT/RIGHT walk the footer buttons.
    const int button = selectedRow - rowCount() + direction;
    if (button >= 0 && button < kFooterButtonCount) {
      selectRow(rowCount() + button);
    }
    return;
  }
  const ConfigurationSetting setting = selectedSetting();
  const ControlKind kind = controlKind(setting);
  bool changed = false;
  if (setting == ConfigurationSetting::Family) {
    const std::vector<std::string> families =
      CellContext::GetKnownFamilyStrings();
    if (families.empty()) {
      return;
    }
    std::size_t index = 0u;
    const std::vector<std::string>::const_iterator found =
      std::find(families.begin(), families.end(), family);
    if (found != families.end()) {
      index = static_cast<std::size_t>(found - families.begin());
    }
    for (std::size_t attempt = 0u; attempt < families.size(); ++attempt) {
      index = direction > 0 ? (index + 1u) % families.size()
                            : (index + families.size() - 1u) % families.size();
      const std::vector<std::string> candidates =
        CellContext::GetKnownRuleStrings(families[index]);
      if (!candidates.empty()) {
        family = families[index];
        ruleSet = candidates.front();
        changed = true;
        break;
      }
    }
  } else if (setting == ConfigurationSetting::Ruleset) {
    const std::vector<std::string> rulesets =
      CellContext::GetKnownRuleStrings(family);
    if (rulesets.empty()) {
      return;
    }
    std::vector<std::string>::const_iterator found =
      std::find(rulesets.begin(), rulesets.end(), ruleSet);
    std::size_t index = found == rulesets.end()
                          ? 0u
                          : static_cast<std::size_t>(found - rulesets.begin());
    if (direction > 0) {
      index = (index + 1u) % rulesets.size();
    } else {
      index = (index + rulesets.size() - 1u) % rulesets.size();
    }
    ruleSet = rulesets[index];
    changed = true;
  } else if (kind == ControlKind::Segments) {
    const int next = segmentIndex(setting) + (direction > 0 ? 1 : -1);
    if (next < 0 || next >= segmentCount(setting)) {
      CSimSounds::play(CSimSound::MenuError);
    } else {
      setSegment(setting, next);
      changed = true;
    }
  } else if (kind == ControlKind::Slider) {
    if (!stepSlider(setting, direction)) {
      // Already at the end of the range.
      CSimSounds::play(CSimSound::MenuError);
    } else if (setting == ConfigurationSetting::SoundVolume) {
      // Previewed at the level just chosen, before it is applied.
      CSimSounds::playAt(CSimSound::MenuSelect, static_cast<int>(soundVolume));
      animator.triggerValuePulse(direction);
    } else {
      changed = true;
    }
  } else if (bool* toggle = toggleSlot(setting); toggle != nullptr) {
    *toggle = !*toggle;
    if (setting == ConfigurationSetting::ReducedMotion) {
      // The draft value also governs this overlay's own motion immediately.
      animator.setReducedMotion(reducedUiMotion);
    }
    changed = true;
  }
  if (changed) {
    animator.triggerValuePulse(direction);
    CSimSounds::play(CSimSound::MenuSelect);
  }
  replaceFieldOnType = true;
  errorMessage.clear();
}

ConfigurationMenuAction
ConfigurationMenu::activateSelected()
{
  if (footerSelected()) {
    const int button = selectedRow - rowCount();
    if (button == kApplyButton) {
      return ConfigurationMenuAction::Apply;
    }
    if (button == kDiscardButton) {
      return ConfigurationMenuAction::Cancel;
    }
    return ConfigurationMenuAction::Exit;
  }
  const ConfigurationSetting setting = selectedSetting();
  const ControlKind kind = controlKind(setting);
  if (kind == ControlKind::Slider) {
    // ENTER finishes a typed value; the next digit starts a new one.
    replaceFieldOnType = true;
    return ConfigurationMenuAction::None;
  }
  if (kind == ControlKind::Segments) {
    // ENTER steps through the options and wraps.
    setSegment(setting, (segmentIndex(setting) + 1) % segmentCount(setting));
    animator.triggerValuePulse(1);
    CSimSounds::play(CSimSound::MenuSelect);
    return ConfigurationMenuAction::None;
  }
  cycleSelected(1);
  return ConfigurationMenuAction::None;
}

bool
ConfigurationMenu::dragSliderTo(ConfigurationSetting setting, float pointerX)
{
  const std::span<const double> stops = sliderStops(setting);
  const float trackWidth = sliderTrackRight() - sliderTrackLeft();
  if (stops.size() < 2u || trackWidth <= 0.0f) {
    return false;
  }
  const float fraction =
    std::clamp((pointerX - sliderTrackLeft()) / trackWidth, 0.0f, 1.0f);
  const int stop = static_cast<int>(
    std::round(fraction * static_cast<float>(stops.size() - 1u)));
  if (!setSliderStop(setting, stop)) {
    return false;
  }
  dragChanged = true;
  animator.triggerValuePulse();
  errorMessage.clear();
  if (setting == ConfigurationSetting::SoundVolume) {
    CSimSounds::playAt(CSimSound::MenuSelect, static_cast<int>(soundVolume));
  }
  return true;
}

void
ConfigurationMenu::handlePointer(bool wheelScrolled,
                                 ConfigurationMenuAction* action)
{
  const float mouseX = pointer.x();
  const float mouseY = pointer.y();
  if (dragSetting != ConfigurationSetting::Count) {
    if (pointer.pressed()) {
      if (pointer.moved()) {
        dragSliderTo(dragSetting, mouseX);
      }
      return;
    }
    // Released: a drag that moved the value lands with the select cue
    // (volume already previewed each level).
    if (dragChanged && dragSetting != ConfigurationSetting::SoundVolume) {
      CSimSounds::play(CSimSound::MenuSelect);
    }
    dragSetting = ConfigurationSetting::Count;
    replaceFieldOnType = true;
    return;
  }
  const bool hovering = pointer.moved() && !wheelScrolled;
  const bool clicked = pointer.clicked();

  hoveredTab = -1;
  hoveredFooterButton = -1;
  for (int tab = 0; tab < kTabCount; ++tab) {
    const std::array<float, 4> bounds = tabBounds(tab);
    if (GuiKit::isPointInRect(
          mouseX, mouseY, bounds[0], bounds[1], bounds[2], bounds[3])) {
      hoveredTab = tab;
      if (clicked) {
        selectTab(tab);
      }
      return;
    }
  }

  for (int button = 0; button < kFooterButtonCount; ++button) {
    const std::array<float, 4> bounds = footerButtonBounds(button);
    if (GuiKit::isPointInRect(
          mouseX, mouseY, bounds[0], bounds[1], bounds[2], bounds[3])) {
      hoveredFooterButton = button;
      if (hovering || clicked) {
        selectRow(rowCount() + button);
      }
      if (clicked && *action == ConfigurationMenuAction::None) {
        *action = activateSelected();
      }
      return;
    }
  }

  const float rowAreaTop = firstRowY + animator.panelOffsetY();
  if (!GuiKit::isPointInRect(mouseX,
                             mouseY,
                             panelX + 20.0f,
                             rowAreaTop,
                             panelWidth - 40.0f,
                             rowHeight * static_cast<float>(visibleRows) -
                               0.001f)) {
    return;
  }
  const int row =
    firstVisibleRow + static_cast<int>((mouseY - rowAreaTop) / rowHeight);
  if (row >= rowCount()) {
    return;
  }
  if ((hovering || clicked) && row != selectedRow) {
    selectRow(row);
  }
  if (!clicked || *action != ConfigurationMenuAction::None) {
    return;
  }
  const ConfigurationSetting setting = settingAt(row);
  const ControlKind kind = controlKind(setting);
  if (kind == ControlKind::Slider) {
    if (mouseX >= sliderTrackLeft() - 10.0f &&
        mouseX <= sliderTrackRight() + 10.0f) {
      dragSetting = setting;
      dragChanged = false;
      dragSliderTo(setting, mouseX);
    }
  } else if (kind == ControlKind::Segments) {
    const float boxWidth = valueRight() - valueLeft();
    if (mouseX >= valueLeft() && mouseX <= valueRight()) {
      const int index =
        std::clamp(static_cast<int>(
                     (mouseX - valueLeft()) /
                     (boxWidth / static_cast<float>(segmentCount(setting)))),
                   0,
                   segmentCount(setting) - 1);
      if (index != segmentIndex(setting)) {
        setSegment(setting, index);
        animator.triggerValuePulse();
        CSimSounds::play(CSimSound::MenuSelect);
      }
    }
  } else if (kind == ControlKind::Choice) {
    if (mouseX >= valueLeft()) {
      cycleSelected(mouseX < valueLeft() + 36.0f ? -1 : 1);
    }
  } else {
    cycleSelected(1);
  }
}

ConfigurationMenuAction
ConfigurationMenu::update(InputManager* inputManager)
{
  if (!openState || inputManager == nullptr) {
    return ConfigurationMenuAction::None;
  }
  updateLayout();

  ConfigurationMenuAction action = ConfigurationMenuAction::None;
  std::queue<InputManager::KeyPressEvent>& keyQueue =
    inputManager->getKeyQueue();
  while (!keyQueue.empty()) {
    const InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();
    if (event.action != InputAction::Press &&
        event.action != InputAction::Hold) {
      continue;
    }
    if (event.key == KeyCode::F1 || event.key == KeyCode::Escape) {
      action = ConfigurationMenuAction::Cancel;
    } else if (event.key == KeyCode::Tab) {
      selectTab(static_cast<int>(activeTab) +
                (inputManager->isShiftPressed() ? -1 : 1));
    } else if (event.key == KeyCode::PageDown) {
      selectTab(static_cast<int>(activeTab) + 1);
    } else if (event.key == KeyCode::PageUp) {
      selectTab(static_cast<int>(activeTab) - 1);
    } else if (event.key == KeyCode::Up) {
      selectRow(footerSelected() ? rowCount() - 1 : selectedRow - 1);
    } else if (event.key == KeyCode::Down) {
      if (!footerSelected()) {
        selectRow(selectedRow + 1);
      }
    } else if (event.key == KeyCode::Home) {
      selectRow(0);
    } else if (event.key == KeyCode::End) {
      selectRow(rowCount() + kExitButton);
    } else if (event.key == KeyCode::Left) {
      cycleSelected(-1);
    } else if (event.key == KeyCode::Right) {
      cycleSelected(1);
    } else if (event.key == KeyCode::Backspace ||
               event.key == KeyCode::Delete) {
      eraseCharacter();
    } else if (event.key == KeyCode::Enter) {
      action = activateSelected();
    }
  }

  std::queue<unsigned int>& charQueue = inputManager->getCharQueue();
  while (!charQueue.empty()) {
    const unsigned int codepoint = charQueue.front();
    charQueue.pop();
    addCharacter(codepoint);
  }

  bool wheelScrolled = false;
  firstVisibleRow = GuiPanelLayout::applyWheelScroll(
    inputManager, firstVisibleRow, rowCount(), visibleRows, &wheelScrolled);
  updateLayout();
  pointer.sample(window, inputManager, panelFit.layoutScale);
  handlePointer(wheelScrolled, &action);
  // Apply is voiced by the caller, which knows whether it succeeded.
  if (action == ConfigurationMenuAction::Cancel) {
    CSimSounds::play(CSimSound::MenuBack);
  } else if (action == ConfigurationMenuAction::Exit) {
    CSimSounds::play(CSimSound::MenuSelect);
  }
  return action;
}

bool
ConfigurationMenu::readConfiguration(SimulatorConfiguration* configuration,
                                     std::string* error) const
{
  if (configuration == nullptr) {
    return false;
  }
  SimulatorConfiguration parsed;
  parsed.family = CellContext::NormalizeFamilyString(family);
  parsed.ruleSet = CellContext::NormalizeModeString(ruleSet);
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(parsed.ruleSet);
  if (definition == nullptr ||
      !CellContext::IsKnownFamilyString(parsed.family) ||
      definition->familyId != parsed.family) {
    if (error != nullptr) {
      *error = "Select a ruleset that belongs to the chosen family.";
    }
    return false;
  }
  if (!parseTopologyText(worldWidthText, &parsed.worldChunkWidth) ||
      !parseTopologyText(worldHeightText, &parsed.worldChunkHeight) ||
      !SparseCellGrid::isValidTopology(parsed.worldChunkWidth,
                                       parsed.worldChunkHeight)) {
    if (error != nullptr) {
      *error = "World size must be finite on both axes, or inf / inf.";
    }
    return false;
  }
  if (!parseLongText(tpsText, 1, 1000, &parsed.tps)) {
    if (error != nullptr) {
      *error = "TPS must be an integer from 1 to 1000.";
    }
    return false;
  }
  if (!parseDoubleText(speedText, 0.0, 100.0, false, &parsed.speedFactor)) {
    if (error != nullptr) {
      *error = "Speed must be greater than 0 and at most 100.";
    }
    return false;
  }
  if (!parseDoubleText(fadeText, 0.0, 100.0, true, &parsed.fadeSpeed)) {
    if (error != nullptr) {
      *error = "Fade speed must be from 0 to 100.";
    }
    return false;
  }
  if (!parseLongText(fpsCapText, 0, 1000, &parsed.fpsCap)) {
    if (error != nullptr) {
      *error = "FPS cap: enter 0 (uncapped) to 1000.";
    }
    return false;
  }
  parsed.showInspector = showInspector;
  parsed.reducedUiMotion = reducedUiMotion;
  parsed.vsync = vsync;
  parsed.editHints = editHints;
  parsed.softwareCursor = softwareCursor;
  parsed.fullscreen = fullscreen;
  parsed.uiScale = uiScale;
  parsed.msaa = msaa;
  parsed.soundVolume = soundVolume;
  parsed.startPaused = startPaused;
  parsed.ledCells = ledCells;
  parsed.cellGlow = cellGlow;
  parsed.gridLines = gridLines;
  parsed.showFps = showFps;
  parsed.showMemory = showMemory;
  parsed.zoomStep = zoomStep;
  parsed.invertZoom = invertZoom;
  parsed.panSpeed = std::lround(panSpeed);
  parsed.autosaveMinutes = std::lround(autosaveMinutes);
  parsed.confirmClear = confirmClear;
  *configuration = parsed;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void
ConfigurationMenu::drawHeader(float reveal, unsigned char panelOpacity)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const float animatedPanelY = panelY + animator.panelOffsetY();
  const float headerSlide = (1.0f - reveal) * 10.0f;
  // The header floats nearer than the rows and swings further with the tilt.
  const float headerX = panelX + tilt.layerX(GuiPanelTilt::kHeaderDepth);
  const float headerY =
    animatedPanelY + tilt.layerY(GuiPanelTilt::kHeaderDepth);
  visual.addText("M A K E   I T   Y O U R S",
                 headerX + 28.0f - headerSlide,
                 headerY + 14.0f,
                 10.0f,
                 UiTheme::applyOpacity(cyan, panelOpacity));
  visual.addText("SIMULATOR SETTINGS",
                 headerX + 28.0f - headerSlide * 0.6f,
                 headerY + 28.0f,
                 24.0f,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), panelOpacity));
  // Keycap hints, as many as fit on one line.
  const std::array<const char*, 5> keys = {
    "UPDOWN", "LEFTRIGHT", "TAB", "ENTER", "ESC"
  };
  const std::array<const char*, 5> actions = {
    "select", "adjust", "section", "activate", "discard"
  };
  const float hintSize = 10.0f;
  const float hintRight = headerX + panelWidth - 24.0f;
  float hintX = headerX + 28.0f;
  for (std::size_t index = 0u; index < keys.size(); ++index) {
    const float advance =
      GuiKit::measureKeyHint(keys[index], actions[index], hintSize);
    if (hintX + advance - hintSize * 1.6f > hintRight) {
      break;
    }
    hintX += GuiKit::drawKeyHint(visual,
                                 hintX,
                                 headerY + 58.0f,
                                 keys[index],
                                 actions[index],
                                 hintSize,
                                 panelOpacity);
  }
}

void
ConfigurationMenu::drawTabs(unsigned char panelOpacity, float breathe)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const std::array<float, 4> first = tabBounds(0);
  const float stripX = first[0];
  const float stripY = first[1];
  const float tabWidth = first[2];
  const float stripWidth = tabWidth * static_cast<float>(kTabCount);
  GuiKit::drawRoundedGradientRect(
    visual,
    stripX,
    stripY,
    stripWidth,
    kTabStripHeight,
    10.0f,
    UiTheme::applyOpacity(ColorRgba{ 5, 11, 22, 210 }, panelOpacity),
    UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));

  // The lit pill glides to the active tab, stretching while it travels.
  const float position = std::clamp(
    tabIndicator.value(), -0.3f, static_cast<float>(kTabCount) - 0.7f);
  const float stretch =
    std::min(0.3f, std::abs(tabIndicator.velocity()) * 0.05f) * tabWidth;
  const float pillX = stripX + position * tabWidth + 3.0f - stretch * 0.5f;
  const float pillWidth = tabWidth - 6.0f + stretch;
  const float pillY = stripY + 3.0f;
  const float pillHeight = kTabStripHeight - 6.0f;
  GuiKit::drawRoundedBand(
    visual,
    pillX,
    pillY,
    pillWidth,
    pillHeight,
    8.0f,
    0.0f,
    7.0f,
    UiTheme::applyOpacity(UiTheme::fade(cyan, 0.22f + 0.1f * breathe),
                          panelOpacity),
    UiTheme::transparentOf(cyan));
  GuiKit::drawRoundedGradientRect(
    visual,
    pillX,
    pillY,
    pillWidth,
    pillHeight,
    8.0f,
    UiTheme::applyOpacity(UiTheme::selectionTop(), panelOpacity),
    UiTheme::applyOpacity(UiTheme::selectionBottom(), panelOpacity));
  GuiKit::drawRoundedOutline(
    visual,
    pillX,
    pillY,
    pillWidth,
    pillHeight,
    8.0f,
    1.0f,
    UiTheme::applyOpacity(UiTheme::fade(cyan, 0.8f), panelOpacity));

  for (int tab = 0; tab < kTabCount; ++tab) {
    const float emphasis = std::clamp(tabFocus.value(tab), 0.0f, 1.2f);
    const float lit = std::min(1.0f, emphasis);
    GuiKit::drawEmphasizedTextCentered(
      visual,
      kTabLabels[tab],
      stripX + tabWidth * (static_cast<float>(tab) + 0.5f),
      stripY + kTabStripHeight * 0.5f,
      12.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit),
        panelOpacity),
      emphasis);
  }
}

void
ConfigurationMenu::drawRowControl(ConfigurationSetting setting,
                                  float y,
                                  float shiftX,
                                  float rowFontSize,
                                  bool selected,
                                  unsigned char rowOpacity)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const int index = static_cast<int>(setting);
  const ControlKind kind = controlKind(setting);
  const float cardHeight = rowHeight - 4.0f;
  const float centerY = y + cardHeight * 0.5f;
  const float textY = centerY - rowFontSize * 0.5f;
  const float boxX = valueLeft() + shiftX;
  const float boxWidth = valueRight() - valueLeft();
  const float boxHeight = std::min(30.0f, cardHeight - 8.0f);
  const float boxY = centerY - boxHeight * 0.5f;
  const float emphasis =
    std::clamp(rowFocus.value(static_cast<int>(selectedRow)), 0.0f, 1.0f);
  const float focus = selected ? emphasis : 0.0f;
  const ColorRgba ink = UiTheme::applyOpacity(
    UiTheme::mix(UiTheme::textPrimary(), cyan, focus), rowOpacity);

  GuiKit::drawRoundedGradientRect(
    visual,
    boxX,
    boxY,
    boxWidth,
    boxHeight,
    6.0f,
    UiTheme::applyOpacity(ColorRgba{ 5, 11, 22, 225 }, rowOpacity),
    UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
  if (selected && animator.valuePulse() > 0.0f) {
    // A changed value blooms: a soft ring that fades out.
    GuiKit::drawRoundedBand(
      visual,
      boxX,
      boxY,
      boxWidth,
      boxHeight,
      6.0f,
      -1.0f,
      7.0f,
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.55f * animator.valuePulse()),
                            rowOpacity),
      UiTheme::transparentOf(cyan));
  }
  // A stepped value springs a few pixels the way it moved and bounces back.
  const float nudge = selected && dragSetting == ConfigurationSetting::Count
                        ? 5.0f * std::clamp(animator.valueWobble(), -1.0f, 1.0f)
                        : 0.0f;

  if (kind == ControlKind::Toggle) {
    visual.addText(toggleValue(setting) ? "On" : "Off",
                   boxX + 12.0f,
                   textY,
                   rowFontSize,
                   ink);
    // The track keeps its 36x16 pill; the knob boings across on a spring,
    // stretching like a droplet while it moves, and the track warms toward
    // the accent as it travels.
    const float knob = std::clamp(toggleKnobs.value(index), -0.2f, 1.2f);
    const float lit = std::clamp(knob, 0.0f, 1.0f);
    const float knobStretch =
      std::min(7.0f, std::abs(toggleKnobs.velocity(index)) * 0.6f);
    const float toggleX = boxX + boxWidth - 46.0f;
    const float toggleY = centerY - 8.0f;
    if (lit > 0.01f) {
      GuiKit::drawRoundedBand(
        visual,
        toggleX,
        toggleY,
        36.0f,
        16.0f,
        8.0f,
        0.0f,
        6.0f,
        UiTheme::applyOpacity(UiTheme::fade(cyan, 0.35f * lit), rowOpacity),
        UiTheme::transparentOf(cyan));
    }
    GuiKit::drawRoundedRect(
      visual,
      toggleX,
      toggleY,
      36.0f,
      16.0f,
      8.0f,
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::menuBorder(), cyan, lit),
                            rowOpacity));
    GuiKit::drawRoundedRect(
      visual,
      toggleX + 3.0f + 18.0f * knob - knobStretch * 0.5f,
      toggleY + 3.0f + knobStretch * 0.12f,
      10.0f + knobStretch,
      10.0f - knobStretch * 0.24f,
      5.0f,
      UiTheme::applyOpacity(UiTheme::textPrimary(), rowOpacity));
    return;
  }

  if (kind == ControlKind::Choice) {
    const ColorRgba arrow = UiTheme::applyOpacity(
      UiTheme::mix(UiTheme::textMuted(), cyan, focus), rowOpacity);
    drawArrowhead(visual, boxX + 14.0f, centerY, -1.0f, arrow);
    drawArrowhead(visual, boxX + boxWidth - 14.0f, centerY, 1.0f, arrow);
    std::string value;
    if (setting == ConfigurationSetting::Family) {
      const RuleFamilyDefinition* definition =
        RuleSetRegistry::instance().getFamilyDefinition(family);
      value = definition == nullptr ? family : definition->name;
    } else {
      value = displayRuleSetName(ruleSet);
    }
    const float valueSize = rowFontSize * 0.94f;
    const float innerLeft = boxX + 26.0f;
    const float innerWidth = boxWidth - 52.0f;
    const float valueWidth =
      GuiKit::measureEmphasizedText(value, valueSize, 0.0f);
    const float valueX =
      innerLeft + std::max(0.0f, (innerWidth - valueWidth) * 0.5f) + nudge;
    visual.addText(value, valueX, centerY - valueSize * 0.5f, valueSize, ink);
    return;
  }

  if (kind == ControlKind::Segments) {
    const int count = segmentCount(setting);
    const float chipWidth = boxWidth / static_cast<float>(count);
    const float position = std::clamp(
      segmentKnobs.value(index), -0.3f, static_cast<float>(count) - 0.7f);
    const float stretch =
      std::min(0.3f, std::abs(segmentKnobs.velocity(index)) * 0.05f) *
      chipWidth;
    const float litX = boxX + position * chipWidth + 3.0f - stretch * 0.5f;
    GuiKit::drawRoundedBand(
      visual,
      litX,
      boxY + 3.0f,
      chipWidth - 6.0f + stretch,
      boxHeight - 6.0f,
      5.0f,
      0.0f,
      5.0f,
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.2f + 0.2f * focus),
                            rowOpacity),
      UiTheme::transparentOf(cyan));
    GuiKit::drawRoundedGradientRect(
      visual,
      litX,
      boxY + 3.0f,
      chipWidth - 6.0f + stretch,
      boxHeight - 6.0f,
      5.0f,
      UiTheme::applyOpacity(UiTheme::selectionTop(), rowOpacity),
      UiTheme::applyOpacity(UiTheme::selectionBottom(), rowOpacity));
    const float labelSize = rowFontSize * 0.86f;
    for (int option = 0; option < count; ++option) {
      const float closeness = std::clamp(
        1.0f - std::abs(segmentKnobs.value(index) - static_cast<float>(option)),
        0.0f,
        1.0f);
      GuiKit::drawEmphasizedTextCentered(
        visual,
        segmentLabel(setting, option),
        boxX + chipWidth * (static_cast<float>(option) + 0.5f),
        centerY,
        labelSize,
        UiTheme::applyOpacity(
          UiTheme::mix(UiTheme::textMuted(), UiTheme::textPrimary(), closeness),
          rowOpacity),
        closeness);
    }
    return;
  }

  // Slider: a rounded track, a lit fill up to the knob, and a readout.
  const float trackLeft = sliderTrackLeft() + shiftX;
  const float trackWidth = sliderTrackRight() - sliderTrackLeft();
  const float fraction = std::clamp(sliderKnobs.value(index), -0.05f, 1.05f);
  const float knobX = trackLeft + trackWidth * fraction;
  const bool dragging = dragSetting == setting;
  GuiKit::drawRoundedRect(
    visual,
    trackLeft,
    centerY - 3.0f,
    trackWidth,
    6.0f,
    3.0f,
    UiTheme::applyOpacity(UiTheme::menuBorder(), rowOpacity));
  const float fillWidth = std::clamp(knobX - trackLeft, 0.0f, trackWidth);
  if (fillWidth >= 6.0f) {
    GuiKit::drawRoundedRect(
      visual,
      trackLeft,
      centerY - 3.0f,
      fillWidth,
      6.0f,
      3.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::fade(cyan, 0.55f), cyan, focus), rowOpacity));
  }
  if (selected || dragging) {
    GuiKit::drawSoftGlow(
      visual,
      knobX,
      centerY,
      dragging ? 16.0f : 13.0f,
      dragging ? 16.0f : 13.0f,
      UiTheme::applyOpacity(UiTheme::fade(cyan, dragging ? 0.55f : 0.35f),
                            rowOpacity));
  }
  const float knobSize = dragging ? 16.0f : 14.0f;
  GuiKit::drawRoundedRect(
    visual,
    knobX - knobSize * 0.5f,
    centerY - knobSize * 0.5f,
    knobSize,
    knobSize,
    knobSize * 0.5f,
    UiTheme::applyOpacity(UiTheme::textPrimary(), rowOpacity));
  const std::string readout = sliderReadout(setting);
  const float readoutSize = rowFontSize * 0.9f;
  const float readoutX = valueRight() + shiftX - kReadoutWidth + nudge;
  const float readoutY = centerY - readoutSize * 0.5f;
  visual.addText(readout, readoutX, readoutY, readoutSize, ink);
  const bool typing =
    selected && !replaceFieldOnType && sliderText(setting) != nullptr;
  if (typing && animator.caretVisible()) {
    const float caretX =
      std::min(GuiKit::caretOriginAfterText(readout, readoutX, readoutSize),
               valueRight() + shiftX - 4.0f);
    visual.addText("|",
                   caretX,
                   readoutY,
                   readoutSize,
                   UiTheme::applyOpacity(cyan, rowOpacity));
  }
}

void
ConfigurationMenu::drawRows(unsigned char panelOpacity, float breathe)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const int lastVisibleRow =
    std::min(firstVisibleRow + visibleRows, rowCount());
  const float cardX = panelX + 20.0f;
  const float cardWidth = panelWidth - 40.0f;
  const float cardHeight = rowHeight - 4.0f;
  // A new tab's rows swell in from the side of the tab they came from.
  const float swap = std::clamp(tabSwap.value(), 0.0f, 1.0f);
  const float shiftX = (1.0f - tabSwap.value()) * 26.0f * tabSwapDirection;
  const float rowFontSize = std::clamp(rowHeight * 0.5f, 14.0f, 17.0f);

  for (int row = firstVisibleRow; row < lastVisibleRow; ++row) {
    const float y = rowTop(row);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(std::round(rowReveal(row) * swap * 255.0f));
    const float e = std::clamp(rowFocus.value(row), 0.0f, 1.0f);
    GuiKit::drawRoundedRect(
      visual,
      cardX + shiftX,
      y,
      cardWidth,
      cardHeight,
      8.0f,
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::cardRim(),
                                         ColorRgba{ 80, 160, 190, 255 },
                                         e * 0.5f),
                            rowOpacity));
    GuiKit::drawRoundedGradientRect(
      visual,
      cardX + shiftX + 1.0f,
      y + 1.0f,
      cardWidth - 2.0f,
      cardHeight - 2.0f,
      7.0f,
      UiTheme::applyOpacity(UiTheme::cardTop(), rowOpacity),
      UiTheme::applyOpacity(UiTheme::cardBottom(), rowOpacity));
  }

  // The selection pours between rows like a drop of liquid, clamped to the
  // visible window, and sweeps a sheen when it lands. Footer buttons light
  // themselves instead.
  const bool selectionVisible = !footerSelected() &&
                                selectedRow >= firstVisibleRow &&
                                selectedRow < lastVisibleRow;
  if (selectionVisible) {
    const float windowTop = static_cast<float>(firstVisibleRow);
    const float windowBottom = static_cast<float>(lastVisibleRow - 1);
    const GuiSelectionSpan span =
      animator.selectionSpan(static_cast<float>(selectedRow));
    const unsigned char selectionOpacity = static_cast<unsigned char>(
      std::round(rowReveal(selectedRow) * swap * 255.0f));
    const float animatedFirstRowY = firstRowY + animator.panelOffsetY();
    GuiLiquidSelection drop;
    drop.crossStart = cardX + shiftX;
    drop.crossSize = cardWidth;
    drop.headStart =
      animatedFirstRowY +
      rowHeight *
        (std::clamp(span.leading, windowTop, windowBottom) - windowTop);
    drop.tailStart =
      animatedFirstRowY +
      rowHeight *
        (std::clamp(span.trailing, windowTop, windowBottom) - windowTop);
    drop.cellLength = cardHeight;
    drop.radius = 8.0f;
    drop.squash = span.squash;
    drop.glowSpread = 9.0f + 3.0f * breathe;
    drop.glow = UiTheme::applyOpacity(
      UiTheme::fade(cyan, 0.18f + 0.1f * breathe), selectionOpacity);
    drop.rim =
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.85f), selectionOpacity);
    drop.faceTop =
      UiTheme::applyOpacity(UiTheme::selectionTop(), selectionOpacity);
    drop.faceBottom =
      UiTheme::applyOpacity(UiTheme::selectionBottom(), selectionOpacity);
    drop.sheen = animator.selectionSheen();
    drop.sheenColor =
      UiTheme::applyOpacity(ColorRgba{ 210, 250, 255, 38 }, selectionOpacity);
    GuiKit::drawLiquidSelection(visual, drop);
  }

  for (int row = firstVisibleRow; row < lastVisibleRow; ++row) {
    const float y = rowTop(row);
    const ConfigurationSetting setting = settingAt(row);
    const bool selected = row == selectedRow;
    const unsigned char rowOpacity =
      static_cast<unsigned char>(std::round(rowReveal(row) * swap * 255.0f));
    const float e = std::clamp(rowFocus.value(row), 0.0f, 1.2f);
    const float eClamped = std::min(1.0f, e);
    // Labels lean in horizontally only; their row baseline stays fixed. They
    // thicken with focus.
    GuiKit::drawEmphasizedText(
      visual,
      kSettingLabels[static_cast<int>(setting)],
      panelX + 36.0f + 4.0f * e + shiftX,
      y + (cardHeight - rowFontSize) * 0.5f,
      rowFontSize,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped), rowOpacity),
      e);
    drawRowControl(setting, y, shiftX, rowFontSize, selected, rowOpacity);
  }

  if (visibleRows < rowCount()) {
    // Rounded scrollbar whose thumb glides to the scroll position.
    const float trackTop = firstRowY + animator.panelOffsetY();
    const float trackHeight = rowHeight * static_cast<float>(visibleRows);
    const float trackX = panelX + panelWidth - 14.0f;
    const float count = static_cast<float>(rowCount());
    GuiKit::drawRoundedRect(
      visual,
      trackX,
      trackTop,
      4.0f,
      trackHeight,
      2.0f,
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    const float thumbRow = std::clamp(
      scrollThumb.value(), 0.0f, static_cast<float>(rowCount() - visibleRows));
    GuiKit::drawRoundedRect(
      visual,
      trackX,
      trackTop + trackHeight * thumbRow / count,
      4.0f,
      trackHeight * static_cast<float>(visibleRows) / count,
      2.0f,
      UiTheme::applyOpacity(UiTheme::accent(), panelOpacity));
  }
}

void
ConfigurationMenu::drawFooter(unsigned char panelOpacity, float breathe)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const float top = footerTop();
  const std::string help =
    !errorMessage.empty() ? errorMessage
    : footerSelected() ? kFooterHelp[std::clamp(selectedRow - rowCount(), 0, 2)]
                       : kSettingHelp[static_cast<int>(selectedSetting())];
  visual.addText(help,
                 panelX + 28.0f,
                 top + 6.0f,
                 13.0f,
                 UiTheme::applyOpacity(!errorMessage.empty()
                                         ? UiTheme::error()
                                         : UiTheme::textSecondary(),
                                       panelOpacity));
  if (activeTab == ConfigurationTab::Video) {
    visual.addText("* Applies after a restart; CSim offers to restart when "
                   "you apply it.",
                   panelX + 28.0f,
                   top + 24.0f,
                   11.0f,
                   UiTheme::applyOpacity(UiTheme::warning(), panelOpacity));
  }

  // Action buttons carry a tint of their outcome on the rim and label.
  const std::array<ColorRgba, 3> rims = { ColorRgba{ 46, 110, 86, 255 },
                                          ColorRgba{ 112, 92, 50, 255 },
                                          ColorRgba{ 118, 58, 66, 255 } };
  const std::array<ColorRgba, 3> inks = { UiTheme::success(),
                                          UiTheme::warning(),
                                          UiTheme::error() };
  for (int button = 0; button < kFooterButtonCount; ++button) {
    const std::array<float, 4> bounds = footerButtonBounds(button);
    const float e = std::clamp(footerFocus.value(button), 0.0f, 1.2f);
    const float lit = std::min(1.0f, e);
    const float x = bounds[0];
    const float y = bounds[1];
    const float width = bounds[2];
    const float height = bounds[3];
    if (lit > 0.02f) {
      GuiKit::drawRoundedBand(
        visual,
        x,
        y,
        width,
        height,
        9.0f,
        0.0f,
        8.0f + 3.0f * breathe,
        UiTheme::applyOpacity(UiTheme::fade(cyan, 0.28f * lit), panelOpacity),
        UiTheme::transparentOf(cyan));
    }
    GuiKit::drawRoundedRect(
      visual,
      x,
      y,
      width,
      height,
      9.0f,
      UiTheme::applyOpacity(UiTheme::mix(rims[static_cast<std::size_t>(button)],
                                         UiTheme::fade(cyan, 0.9f),
                                         lit * 0.7f),
                            panelOpacity));
    GuiKit::drawRoundedGradientRect(
      visual,
      x + 1.0f,
      y + 1.0f,
      width - 2.0f,
      height - 2.0f,
      8.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::cardTop(), UiTheme::selectionTop(), lit),
        panelOpacity),
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::cardBottom(), UiTheme::selectionBottom(), lit),
        panelOpacity));
    GuiKit::drawEmphasizedTextCentered(
      visual,
      kFooterLabels[button],
      x + width * 0.5f,
      y + height * 0.5f,
      15.0f,
      UiTheme::applyOpacity(UiTheme::mix(inks[static_cast<std::size_t>(button)],
                                         UiTheme::textPrimary(),
                                         lit * 0.35f),
                            panelOpacity),
      e);
  }
}

void
ConfigurationMenu::rebuildVisual()
{
  updateLayout();
  visual.clearPrimitives();
  const float virtualWidth = panelFit.virtualWidth;
  const float virtualHeight = panelFit.virtualHeight;
  const ColorRgba cyan = UiTheme::accentCool();

  const float reveal = animator.panelReveal();
  const float animatedPanelY = panelY + animator.panelOffsetY();
  const float animatedFirstRowY = firstRowY + animator.panelOffsetY();
  const float backdrop = animator.openReveal(0.18f);
  const unsigned char panelOpacity =
    static_cast<unsigned char>(std::round(reveal * 255.0f));
  const float breathe =
    0.5f + 0.5f * std::sin(animator.ambientPhase() * 1.04719755f);

  GuiKit::drawVignette(visual,
                       virtualWidth,
                       virtualHeight,
                       UiTheme::fade(UiTheme::scrimCenter(), backdrop),
                       UiTheme::fade(UiTheme::scrimEdge(), backdrop),
                       0.3f);
  GuiGlassStyle glass;
  glass.opacity = panelOpacity;
  glass.ambientPhase = animator.ambientPhase();
  glass.glow = 0.35f + 0.3f * breathe;
  glass.accentReveal = reveal;
  tilt.applyTo(glass);
  GuiKit::drawGlassPanel(visual,
                         panelX + tilt.layerX(GuiPanelTilt::kGlassDepth),
                         animatedPanelY +
                           tilt.layerY(GuiPanelTilt::kGlassDepth),
                         panelWidth,
                         panelHeight,
                         glass);

  drawHeader(reveal, panelOpacity);
  drawTabs(panelOpacity, breathe);

  // A cyan-to-violet hairline under the tabs draws out on open.
  const float ruleWidth = (panelWidth - 40.0f) * reveal;
  const ColorRgba ruleCyan = UiTheme::applyOpacity(cyan, panelOpacity);
  const ColorRgba ruleViolet = UiTheme::applyOpacity(
    UiTheme::fade(UiTheme::accentViolet(), 0.8f), panelOpacity);
  visual.addGradientRect(panelX + 20.0f,
                         animatedFirstRowY - 5.0f,
                         ruleWidth * 0.7f,
                         2.0f,
                         ruleCyan,
                         ruleViolet,
                         ruleViolet,
                         ruleCyan);
  visual.addGradientRect(panelX + 20.0f + ruleWidth * 0.7f,
                         animatedFirstRowY - 5.0f,
                         ruleWidth * 0.3f,
                         2.0f,
                         ruleViolet,
                         UiTheme::transparentOf(ruleViolet),
                         UiTheme::transparentOf(ruleViolet),
                         ruleViolet);

  drawRows(panelOpacity, breathe);
  drawFooter(panelOpacity, breathe);
}

bool
ConfigurationMenu::AppendCommands(Renderer* activeRenderer)
{
  if (!openState || !isVisible()) {
    return true;
  }
  if (activeRenderer == nullptr) {
    return false;
  }
  renderer = activeRenderer;
  rebuildVisual();
  visual.setRenderer(activeRenderer);
  visual.setWindow(window);
  visual.setVisible(true);
  return visual.AppendCommands(activeRenderer);
}
