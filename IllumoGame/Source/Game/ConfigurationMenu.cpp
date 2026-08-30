#include "ConfigurationMenu.h"
#include "Game/CellContext.h"
#include "Game/SparseCellGrid.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <queue>
#include <sstream>
#include <vector>

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

ConfigurationMenu::ConfigurationMenu(IRenderWindow* targetWindow,
                                     Renderer* targetRenderer)
  : window(targetWindow)
  , renderer(targetRenderer)
  , visual(4096u)
  , openState(false)
  , mouseWasDown(false)
  , replaceFieldOnType(true)
  , selectedRow(0)
  , animationElapsed(0.0f)
  , selectionFromRow(0.0f)
  , selectionAnimationElapsed(kSelectionAnimationSeconds)
  , valuePulseElapsed(kValuePulseSeconds)
  , panelX(0.0f)
  , panelY(0.0f)
  , panelWidth(620.0f)
  , panelHeight(670.0f)
  , firstRowY(0.0f)
  , rowHeight(40.0f)
  , vsync(true)
  , fullscreen(false)
  , uiScale(1)
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
  if (mode == "GAME_OF_LIFE") {
    return "Game of Life";
  }
  if (mode == "BRIANS_BRAIN") {
    return "Brian's Brain";
  }
  if (mode == "DAY_AND_NIGHT") {
    return "Day & Night";
  }
  if (mode == "HIGHLIFE") {
    return "Highlife";
  }
  if (mode == "LIFE_WITHOUT_DEATH") {
    return "Life Without Death";
  }
  if (mode == "SEEDS") {
    return "Seeds";
  }
  if (mode == "WIREWORLD") {
    return "Wireworld";
  }
  if (mode == "RULE_90") {
    return "Rule 90";
  }
  if (mode == "RULE_184") {
    return "Rule 184";
  }
  return mode;
}

void
ConfigurationMenu::open(const SimulatorConfiguration& current)
{
  ruleSet = CellContext::NormalizeModeString(current.ruleSet);
  if (!CellContext::IsKnownModeString(ruleSet)) {
    ruleSet = "GAME_OF_LIFE";
  }
  worldWidthText = topologyText(current.worldChunkWidth);
  worldHeightText = topologyText(current.worldChunkHeight);
  tpsText = std::to_string(current.tps);
  speedText = decimalText(current.speedFactor);
  fadeText = decimalText(current.fadeSpeed);
  vsync = current.vsync;
  fullscreen = current.fullscreen;
  uiScale = current.uiScale > 0 ? current.uiScale : 1;
  msaa = current.msaa;
  errorMessage.clear();
  selectedRow = 0;
  selectionFromRow = 0.0f;
  selectionAnimationElapsed = kSelectionAnimationSeconds;
  valuePulseElapsed = kValuePulseSeconds;
  replaceFieldOnType = true;
  mouseWasDown = false;
  animationElapsed = 0.0f;
  openState = true;
  setVisible(true);
  updateLayout();
}

void
ConfigurationMenu::tick(float deltaSeconds)
{
  if (!openState || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
    return;
  }
  animationElapsed =
    std::min(kOpenAnimationSeconds, animationElapsed + deltaSeconds);
  selectionAnimationElapsed = std::min(
    kSelectionAnimationSeconds, selectionAnimationElapsed + deltaSeconds);
  valuePulseElapsed =
    std::min(kValuePulseSeconds, valuePulseElapsed + deltaSeconds);
}

float
ConfigurationMenu::animationProgress() const
{
  return std::clamp(animationElapsed / kOpenAnimationSeconds, 0.0f, 1.0f);
}

float
ConfigurationMenu::getAnimationProgressForTesting() const
{
  return animationProgress();
}

static float
easeOutCubic(float progress)
{
  const float remaining = 1.0f - std::clamp(progress, 0.0f, 1.0f);
  return 1.0f - remaining * remaining * remaining;
}

float
ConfigurationMenu::panelReveal() const
{
  return easeOutCubic(std::clamp(animationElapsed / 0.24f, 0.0f, 1.0f));
}

float
ConfigurationMenu::panelOffsetY() const
{
  return (1.0f - panelReveal()) * 18.0f;
}

float
ConfigurationMenu::rowReveal(int row) const
{
  const float delay = static_cast<float>(std::max(0, row)) * 0.012f;
  return easeOutCubic(
    std::clamp((animationElapsed - delay) / 0.22f, 0.0f, 1.0f));
}

float
ConfigurationMenu::selectionRowPosition() const
{
  const float progress = easeOutCubic(std::clamp(
    selectionAnimationElapsed / kSelectionAnimationSeconds, 0.0f, 1.0f));
  return selectionFromRow +
         (static_cast<float>(selectedRow) - selectionFromRow) * progress;
}

float
ConfigurationMenu::getSelectionPositionForTesting() const
{
  return selectionRowPosition();
}

float
ConfigurationMenu::valuePulse() const
{
  const float progress =
    std::clamp(valuePulseElapsed / kValuePulseSeconds, 0.0f, 1.0f);
  return 1.0f - progress;
}

float
ConfigurationMenu::getValuePulseForTesting() const
{
  return valuePulse();
}

void
ConfigurationMenu::triggerValuePulse()
{
  valuePulseElapsed = 0.0f;
}

void
ConfigurationMenu::close()
{
  openState = false;
  mouseWasDown = false;
  setVisible(false);
}

void
ConfigurationMenu::setError(const std::string& message)
{
  errorMessage = message;
}

void
ConfigurationMenu::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = renderer != nullptr ? renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);
  panelWidth = std::min(720.0f, std::max(200.0f, virtualWidth - 32.0f));
  if (panelWidth > virtualWidth) {
    panelWidth = virtualWidth;
  }
  panelHeight = std::min(670.0f, std::max(160.0f, virtualHeight - 24.0f));
  if (panelHeight > virtualHeight) {
    panelHeight = virtualHeight;
  }
  panelX = std::max(0.0f, (virtualWidth - panelWidth) * 0.5f);
  panelY = std::max(0.0f, (virtualHeight - panelHeight) * 0.5f);

  const float headerHeight = panelHeight >= 400.0f
                               ? 104.0f
                               : std::clamp(panelHeight * 0.18f, 36.0f, 104.0f);
  const float footerHeight = panelHeight >= 400.0f
                               ? 52.0f
                               : std::clamp(panelHeight * 0.10f, 28.0f, 52.0f);
  firstRowY = panelY + headerHeight;
  const float availableRowsH =
    std::max(12.0f * static_cast<float>(kRowCount),
             panelHeight - headerHeight - footerHeight);
  rowHeight = availableRowsH / static_cast<float>(kRowCount);
}

void
ConfigurationMenu::selectRow(int row)
{
  const int nextRow = std::clamp(row, 0, kRowCount - 1);
  if (nextRow != selectedRow) {
    selectionFromRow = selectionRowPosition();
    selectedRow = nextRow;
    selectionAnimationElapsed = 0.0f;
  }
  replaceFieldOnType = true;
  errorMessage.clear();
}

std::string*
ConfigurationMenu::editableField()
{
  switch (selectedRow) {
    case kWorldWidthRow:
      return &worldWidthText;
    case kWorldHeightRow:
      return &worldHeightText;
    case kTpsRow:
      return &tpsText;
    case kSpeedRow:
      return &speedText;
    case kFadeRow:
      return &fadeText;
    default:
      return nullptr;
  }
}

void
ConfigurationMenu::addCharacter(unsigned int codepoint)
{
  std::string* field = editableField();
  if (field == nullptr || codepoint > 127u) {
    return;
  }
  const char character = static_cast<char>(codepoint);
  bool accepted = std::isdigit(static_cast<unsigned char>(character)) != 0;
  if (selectedRow == kWorldWidthRow || selectedRow == kWorldHeightRow) {
    const char lower =
      static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    accepted = accepted || lower == 'i' || lower == 'n' || lower == 'f';
  } else if (selectedRow == kSpeedRow || selectedRow == kFadeRow) {
    accepted = accepted || character == '.';
  }
  if (!accepted || field->size() >= 16u) {
    return;
  }
  if (replaceFieldOnType) {
    field->clear();
    replaceFieldOnType = false;
  }
  field->push_back(character);
  triggerValuePulse();
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
  triggerValuePulse();
  errorMessage.clear();
}

void
ConfigurationMenu::cycleSelected(int direction)
{
  if (direction == 0) {
    return;
  }
  bool changed = false;
  if (selectedRow == kRulesetRow) {
    const std::vector<std::string> rulesets =
      CellContext::GetKnownModeStrings();
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
  } else if (selectedRow == kVsyncRow) {
    vsync = !vsync;
    changed = true;
  } else if (selectedRow == kFullscreenRow) {
    fullscreen = !fullscreen;
    changed = true;
  } else if (selectedRow == kUiScaleRow) {
    const std::array<long, 4> scaleOptions = { 1, 2, 3, 4 };
    std::size_t index = 0u;
    for (std::size_t i = 0; i < scaleOptions.size(); ++i) {
      if (scaleOptions[i] == uiScale) {
        index = i;
        break;
      }
    }
    if (direction > 0) {
      index = (index + 1u) % scaleOptions.size();
    } else {
      index = (index + scaleOptions.size() - 1u) % scaleOptions.size();
    }
    uiScale = scaleOptions[index];
    changed = true;
  } else if (selectedRow == kMsaaRow) {
    const std::array<long, 4> msaaOptions = { 0, 2, 4, 8 };
    std::size_t index = 2u;
    for (std::size_t i = 0; i < msaaOptions.size(); ++i) {
      if (msaaOptions[i] == msaa) {
        index = i;
        break;
      }
    }
    if (direction > 0) {
      index = (index + 1u) % msaaOptions.size();
    } else {
      index = (index + msaaOptions.size() - 1u) % msaaOptions.size();
    }
    msaa = msaaOptions[index];
    changed = true;
  }
  if (changed) {
    triggerValuePulse();
  }
  replaceFieldOnType = true;
  errorMessage.clear();
}

ConfigurationMenuAction
ConfigurationMenu::activateSelected()
{
  if (selectedRow == kApplyRow) {
    return ConfigurationMenuAction::Apply;
  }
  if (selectedRow == kCancelRow) {
    return ConfigurationMenuAction::Cancel;
  }
  if (selectedRow == kExitRow) {
    return ConfigurationMenuAction::Exit;
  }
  if (selectedRow == kRulesetRow || selectedRow == kVsyncRow ||
      selectedRow == kFullscreenRow || selectedRow == kUiScaleRow ||
      selectedRow == kMsaaRow) {
    cycleSelected(1);
  }
  return ConfigurationMenuAction::None;
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
    } else if (event.key == KeyCode::Up) {
      selectRow(selectedRow - 1);
    } else if (event.key == KeyCode::Down || event.key == KeyCode::Tab) {
      selectRow((selectedRow + 1) % kRowCount);
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

  const bool mouseDown = inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  if (mouseDown && !mouseWasDown) {
    const std::array<double, 2> mouse = inputManager->getMousePosition();
    const float scale = renderer != nullptr ? renderer->getUiScale() : 1.0f;
    const float mouseX =
      static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f);
    const float mouseY =
      static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f);
    const float animatedFirstRowY = firstRowY + panelOffsetY();
    if (mouseX >= panelX + 20.0f && mouseX <= panelX + panelWidth - 20.0f &&
        mouseY >= animatedFirstRowY &&
        mouseY <
          animatedFirstRowY + rowHeight * static_cast<float>(kRowCount)) {
      const int row =
        static_cast<int>((mouseY - animatedFirstRowY) / rowHeight);
      selectRow(row);
      if (row == kApplyRow || row == kCancelRow || row == kExitRow ||
          row == kRulesetRow || row == kVsyncRow || row == kFullscreenRow ||
          row == kUiScaleRow || row == kMsaaRow) {
        action = activateSelected();
      }
    }
  }
  mouseWasDown = mouseDown;
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
  parsed.ruleSet = CellContext::NormalizeModeString(ruleSet);
  if (!CellContext::IsKnownModeString(parsed.ruleSet)) {
    if (error != nullptr) {
      *error = "Select a supported ruleset.";
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
  parsed.vsync = vsync;
  parsed.fullscreen = fullscreen;
  parsed.uiScale = uiScale > 0 ? uiScale : 1;
  parsed.msaa = msaa;
  *configuration = parsed;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void
ConfigurationMenu::rebuildVisual()
{
  updateLayout();
  visual.clearPrimitives();
  int width = 1280;
  int height = 720;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }

  const float scale = renderer != nullptr ? renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const float reveal = panelReveal();
  const float animatedPanelY = panelY + panelOffsetY();
  const float animatedFirstRowY = firstRowY + panelOffsetY();
  const unsigned char backdropOpacity = static_cast<unsigned char>(
    std::round(std::clamp(animationElapsed / 0.18f, 0.0f, 1.0f) * 255.0f));
  const unsigned char panelOpacity =
    static_cast<unsigned char>(std::round(reveal * 255.0f));

  visual.addFilledRect(
    0.0f,
    0.0f,
    virtualWidth,
    virtualHeight,
    UiTheme::applyOpacity(UiTheme::canvasShade(), backdropOpacity));
  GuiPanelChrome chrome;
  chrome.background =
    UiTheme::applyOpacity(UiTheme::panelSurface(), panelOpacity);
  chrome.border = UiTheme::applyOpacity(UiTheme::panelBorder(), panelOpacity);
  chrome.shadow =
    ColorRgba{ 0, 0, 0, static_cast<unsigned char>(180.0f * reveal) };
  chrome.shadowOffset = 6.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = true;
  chrome.accent = UiTheme::applyOpacity(UiTheme::accent(), panelOpacity);
  chrome.accentWidth = 5.0f;
  GuiKit::drawPanel(
    visual, panelX, animatedPanelY, panelWidth, panelHeight, chrome);

  const float headerHeight = panelHeight >= 400.0f
                               ? 104.0f
                               : std::clamp(panelHeight * 0.18f, 36.0f, 104.0f);
  const float footerHeight = panelHeight >= 400.0f
                               ? 52.0f
                               : std::clamp(panelHeight * 0.10f, 28.0f, 52.0f);
  const float titleFontSize =
    panelHeight >= 360.0f ? 24.0f
                          : std::clamp(headerHeight * 0.35f, 12.0f, 24.0f);
  const float subFontSize = panelHeight >= 400.0f
                              ? 13.0f
                              : std::clamp(headerHeight * 0.18f, 9.0f, 13.0f);
  const float noteFontSize = panelHeight >= 400.0f
                               ? 11.0f
                               : std::clamp(footerHeight * 0.25f, 8.0f, 11.0f);
  const float helpFontSize = panelHeight >= 400.0f
                               ? 13.0f
                               : std::clamp(footerHeight * 0.30f, 9.0f, 13.0f);

  visual.addText("SIMULATOR SETTINGS",
                 panelX + 28.0f,
                 animatedPanelY + (panelHeight >= 400.0f
                                     ? 22.0f
                                     : std::max(4.0f, headerHeight * 0.10f)),
                 titleFontSize,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), panelOpacity));
  const ColorRgba secondaryText{ 190, 207, 222, 255 };
  if (headerHeight >= 60.0f) {
    visual.addText("UP/DOWN: select    LEFT/RIGHT: change    TYPE: edit value",
                   panelX + 28.0f,
                   animatedPanelY +
                     (panelHeight >= 400.0f ? 58.0f : headerHeight * 0.48f),
                   subFontSize,
                   UiTheme::applyOpacity(secondaryText, panelOpacity));
    visual.addText("ENTER: activate    F1 or ESC: close without applying",
                   panelX + 28.0f,
                   animatedPanelY +
                     (panelHeight >= 400.0f ? 77.0f : headerHeight * 0.72f),
                   subFontSize,
                   UiTheme::applyOpacity(secondaryText, panelOpacity));
  }

  const std::string labels[kRowCount] = { "Ruleset",
                                          "World width (chunks)",
                                          "World height (chunks)",
                                          "Simulation rate (TPS)",
                                          "Speed multiplier",
                                          "Fade speed",
                                          "Vertical sync",
                                          "Fullscreen",
                                          "UI scale",
                                          "Anti-aliasing (MSAA)*",
                                          "Apply changes",
                                          "Discard changes",
                                          "Exit simulator" };
  const std::string values[kRowCount] = { displayRuleSetName(ruleSet),
                                          worldWidthText,
                                          worldHeightText,
                                          tpsText,
                                          speedText,
                                          fadeText,
                                          vsync ? "On" : "Off",
                                          fullscreen ? "On" : "Off",
                                          std::to_string(uiScale) + "x",
                                          msaa == 0
                                            ? "Off"
                                            : std::to_string(msaa) + "x",
                                          "ENTER",
                                          "ENTER",
                                          "ENTER" };
  const std::string help[kRowCount] = {
    "Choose the cellular-automaton rules.",
    "Enter a positive chunk count, or inf on both world axes.",
    "Enter a positive chunk count, or inf on both world axes.",
    "Target simulation ticks per second: 1 to 1000.",
    "Simulation-rate multiplier: greater than 0, up to 100.",
    "Color transition speed: 0 snaps immediately; maximum 100.",
    "Synchronize frame presentation to the monitor.",
    "Use the entire display.",
    "Scale interface size: 1x, 2x, 3x, 4x.",
    "Multisample anti-aliasing: Off, 2x, 4x, 8x. (*Requires restart)",
    "Validate, save, and apply the displayed settings.",
    "Close the menu without changing any settings.",
    "Ask for confirmation, then exit IllumoGame through the normal "
    "shutdown path."
  };
  const float valueColumnX = panelX + panelWidth * 0.54f;
  const float rowFontSize = panelHeight >= 360.0f
                              ? std::clamp(rowHeight * 0.56f, 16.0f, 18.0f)
                              : std::clamp(rowHeight * 0.52f, 9.0f, 18.0f);
  for (int row = 0; row < kRowCount; ++row) {
    const float y = animatedFirstRowY + rowHeight * static_cast<float>(row);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(std::round(rowReveal(row) * 255.0f));
    visual.addFilledRect(
      panelX + 20.0f,
      y,
      panelWidth - 40.0f,
      rowHeight - 4.0f,
      UiTheme::applyOpacity(UiTheme::panelRaised(), rowOpacity));
    if (row < kApplyRow) {
      visual.addFilledRect(
        valueColumnX - 12.0f,
        y + 2.0f,
        panelX + panelWidth - 20.0f - valueColumnX + 12.0f,
        rowHeight - 8.0f,
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
    }
  }

  const float selectionY =
    animatedFirstRowY + rowHeight * selectionRowPosition();
  const unsigned char selectionOpacity =
    static_cast<unsigned char>(std::round(rowReveal(selectedRow) * 255.0f));
  visual.addFilledRect(
    panelX + 20.0f,
    selectionY,
    panelWidth - 40.0f,
    rowHeight - 4.0f,
    UiTheme::applyOpacity(UiTheme::selection(), selectionOpacity));
  visual.addFilledRect(
    panelX + 20.0f,
    selectionY,
    4.0f,
    (rowHeight - 4.0f) * rowReveal(selectedRow),
    UiTheme::applyOpacity(UiTheme::accent(), selectionOpacity));
  for (int row = 0; row < kRowCount; ++row) {
    const float y = animatedFirstRowY + rowHeight * static_cast<float>(row);
    const bool selected = row == selectedRow;
    const float textY = y + std::max(2.0f, (rowHeight - rowFontSize) * 0.5f);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(std::round(rowReveal(row) * 255.0f));
    if (row == selectedRow && row < kApplyRow && valuePulse() > 0.0f) {
      const unsigned char pulseOpacity = static_cast<unsigned char>(
        std::round(valuePulse() * static_cast<float>(rowOpacity)));
      visual.addOutlineRect(
        valueColumnX - 12.0f,
        y + 2.0f,
        panelX + panelWidth - 20.0f - valueColumnX + 12.0f,
        rowHeight - 8.0f,
        UiTheme::applyOpacity(UiTheme::accent(), pulseOpacity),
        2.0f);
    }
    visual.addText(labels[row],
                   panelX + 36.0f,
                   textY,
                   rowFontSize,
                   UiTheme::applyOpacity(selected ? UiTheme::accent()
                                                  : UiTheme::textPrimary(),
                                         rowOpacity));
    visual.addText(values[row],
                   valueColumnX,
                   textY,
                   rowFontSize,
                   UiTheme::applyOpacity(row == kApplyRow  ? UiTheme::success()
                                         : row == kExitRow ? UiTheme::error()
                                         : row == kCancelRow
                                           ? UiTheme::warning()
                                         : selected ? UiTheme::accent()
                                                    : UiTheme::textPrimary(),
                                         rowOpacity));
  }
  const ColorRgba restartNoteColor{ 255, 200, 100, 255 };
  const float noteY = panelHeight >= 400.0f
                        ? animatedPanelY + panelHeight - 44.0f
                        : animatedPanelY + panelHeight - footerHeight + 2.0f;
  const float helpY =
    panelHeight >= 400.0f
      ? animatedPanelY + panelHeight - 24.0f
      : animatedPanelY + panelHeight - footerHeight * 0.5f + 2.0f;
  visual.addText(
    "* Marked settings require restarting IllumoGame to take effect.",
    panelX + 28.0f,
    noteY,
    noteFontSize,
    UiTheme::applyOpacity(restartNoteColor, panelOpacity));
  if (!errorMessage.empty()) {
    visual.addText(errorMessage,
                   panelX + 28.0f,
                   helpY,
                   helpFontSize,
                   UiTheme::applyOpacity(UiTheme::error(), panelOpacity));
  } else {
    visual.addText(help[selectedRow],
                   panelX + 28.0f,
                   helpY,
                   helpFontSize,
                   UiTheme::applyOpacity(secondaryText, panelOpacity));
  }
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
