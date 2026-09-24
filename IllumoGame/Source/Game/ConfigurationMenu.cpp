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
  , replaceFieldOnType(true)
  , selectedRow(0)
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
  fullscreen = current.fullscreen;
  uiScale = current.uiScale > 0 ? current.uiScale : 1;
  msaa = current.msaa;
  fpsCapText = std::to_string(std::max(0L, current.fpsCap));
  showInspector = current.showInspector;
  reducedUiMotion = current.reducedUiMotion;
  soundVolume = std::clamp(current.soundVolume, 0L, 100L);
  firstVisibleRow = 0;
  errorMessage.clear();
  selectedRow = 0;
  replaceFieldOnType = true;
  animator.setReducedMotion(reducedUiMotion);
  animator.restart();
  // Ignore a held click that opened the modal until its first release.
  pointer.reset(true);
  tilt.level();
  openState = true;
  setVisible(true);
  updateLayout();
  rowFocus.configure(GuiMotion::kJelly);
  rowFocus.focusOnly(selectedRow, kRowCount);
  rowFocus.snapAll();
  toggleKnobs.configure(GuiMotion::kBoing);
  for (int row = 0; row < kRowCount; ++row) {
    toggleKnobs.snap(row, toggleValue(row) ? 1.0f : 0.0f);
  }
  scrollThumb.configure(GuiMotion::kGlide);
  scrollThumb.snapTo(static_cast<float>(firstVisibleRow));
}

bool
ConfigurationMenu::isToggleRow(int row) const
{
  return row == kVsyncRow || row == kFullscreenRow || row == kInspectorRow ||
         row == kReducedMotionRow || row == kEditHintsRow;
}

bool
ConfigurationMenu::toggleValue(int row) const
{
  switch (row) {
    case kVsyncRow:
      return vsync;
    case kFullscreenRow:
      return fullscreen;
    case kInspectorRow:
      return showInspector;
    case kReducedMotionRow:
      return reducedUiMotion;
    case kEditHintsRow:
      return editHints;
    default:
      return false;
  }
}

void
ConfigurationMenu::updateSprings(float deltaSeconds)
{
  const bool still = animator.reducedMotion();
  rowFocus.focusOnly(selectedRow, kRowCount);
  rowFocus.tick(deltaSeconds, still);
  for (int row = 0; row < kRowCount; ++row) {
    if (isToggleRow(row)) {
      toggleKnobs.setTarget(row, toggleValue(row) ? 1.0f : 0.0f);
    }
  }
  toggleKnobs.tick(deltaSeconds, still);
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
  panelWidth = std::min(720.0f, std::max(200.0f, virtualWidth - 32.0f));
  if (panelWidth > virtualWidth) {
    panelWidth = virtualWidth;
  }
  panelHeight = std::min(670.0f, std::max(160.0f, virtualHeight - 24.0f));
  if (panelHeight > virtualHeight) {
    panelHeight = virtualHeight;
  }
  // The layout origin swivels with the body layer, so hit testing and
  // drawing agree while the panel tilts toward the pointer.
  panelX =
    std::max(0.0f, (virtualWidth - panelWidth) * 0.5f) + tilt.bodyShiftX();
  panelY =
    std::max(0.0f, (virtualHeight - panelHeight) * 0.5f) + tilt.bodyShiftY();

  const float headerHeight = panelHeight >= 400.0f
                               ? 104.0f
                               : std::clamp(panelHeight * 0.18f, 36.0f, 104.0f);
  const float footerHeight = panelHeight >= 400.0f
                               ? 52.0f
                               : std::clamp(panelHeight * 0.10f, 28.0f, 52.0f);
  firstRowY = panelY + headerHeight;
  const float availableRowsH = panelHeight - headerHeight - footerHeight;
  visibleRows = GuiPanelLayout::visibleRowCount(availableRowsH, kRowCount);
  rowHeight = availableRowsH / static_cast<float>(visibleRows);
  firstVisibleRow = GuiPanelLayout::clampFirstVisibleRow(
    firstVisibleRow, kRowCount, visibleRows);
}

void
ConfigurationMenu::selectRow(int row)
{
  const int nextRow = std::clamp(row, 0, kRowCount - 1);
  if (nextRow != selectedRow) {
    animator.beginSelectionTravel(static_cast<float>(selectedRow),
                                  static_cast<float>(nextRow));
    selectedRow = nextRow;
    animator.resetCaret();
    CSimSounds::play(CSimSound::MenuHover);
  }
  firstVisibleRow =
    GuiPanelLayout::scrollToRow(firstVisibleRow, selectedRow, visibleRows);
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
    case kFpsCapRow:
      return &fpsCapText;
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
  bool changed = false;
  if (selectedRow == kFamilyRow) {
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
  } else if (selectedRow == kRulesetRow) {
    const std::vector<std::string> rulesets =
      CellContext::GetKnownRuleStrings(family);
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
  } else if (selectedRow == kFpsCapRow) {
    const std::array<long, 9> presets = {
      0, 30, 60, 90, 120, 144, 165, 240, 360
    };
    long cap = 0;
    parseLongText(fpsCapText, 0, 1000, &cap);
    long next = direction > 0 ? presets.front() : presets.back();
    if (direction > 0) {
      for (long preset : presets) {
        if (preset > cap) {
          next = preset;
          break;
        }
      }
    } else {
      for (long preset : presets) {
        if (preset < cap) {
          next = preset;
        }
      }
    }
    fpsCapText = std::to_string(next);
    changed = true;
  } else if (selectedRow == kInspectorRow) {
    showInspector = !showInspector;
    changed = true;
  } else if (selectedRow == kReducedMotionRow) {
    reducedUiMotion = !reducedUiMotion;
    // The draft value also governs this overlay's own motion immediately.
    animator.setReducedMotion(reducedUiMotion);
    changed = true;
  } else if (selectedRow == kEditHintsRow) {
    editHints = !editHints;
    changed = true;
  } else if (selectedRow == kSoundVolumeRow) {
    const long next = std::clamp(soundVolume + direction * 10L, 0L, 100L);
    if (next == soundVolume) {
      // Already at the end of the range.
      CSimSounds::play(CSimSound::MenuError);
    } else {
      soundVolume = next;
      // Previewed at the level just chosen, before it is applied.
      CSimSounds::playAt(CSimSound::MenuSelect, static_cast<int>(soundVolume));
      animator.triggerValuePulse(direction);
    }
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
    animator.triggerValuePulse(direction);
    CSimSounds::play(CSimSound::MenuSelect);
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
  if (selectedRow == kSoundVolumeRow) {
    // ENTER steps up and wraps from 100% back to off.
    if (soundVolume >= 100) {
      soundVolume = 0;
      animator.triggerValuePulse(-1);
    } else {
      cycleSelected(1);
    }
    return ConfigurationMenuAction::None;
  }
  if (selectedRow == kFamilyRow || selectedRow == kRulesetRow ||
      selectedRow == kVsyncRow || selectedRow == kFullscreenRow ||
      selectedRow == kUiScaleRow || selectedRow == kMsaaRow ||
      selectedRow == kFpsCapRow || selectedRow == kInspectorRow ||
      selectedRow == kReducedMotionRow || selectedRow == kEditHintsRow) {
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
    } else if (event.key == KeyCode::PageDown) {
      selectRow(selectedRow + visibleRows);
    } else if (event.key == KeyCode::PageUp) {
      selectRow(selectedRow - visibleRows);
    } else if (event.key == KeyCode::Home) {
      selectRow(0);
    } else if (event.key == KeyCode::End) {
      selectRow(kRowCount - 1);
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
    inputManager, firstVisibleRow, kRowCount, visibleRows, &wheelScrolled);
  updateLayout();
  pointer.sample(window, inputManager, panelFit.layoutScale);
  const float mouseX = pointer.x();
  const float mouseY = pointer.y();
  const float animatedFirstRowY = firstRowY + animator.panelOffsetY();
  if (((pointer.moved() && !wheelScrolled) || pointer.clicked()) &&
      mouseX >= panelX + 20.0f && mouseX <= panelX + panelWidth - 20.0f &&
      mouseY >= animatedFirstRowY &&
      mouseY <
        animatedFirstRowY + rowHeight * static_cast<float>(visibleRows)) {
    const int row = firstVisibleRow +
                    static_cast<int>((mouseY - animatedFirstRowY) / rowHeight);
    if (row != selectedRow) {
      selectRow(row);
    }
    if (pointer.clicked()) {
      if (row < kApplyRow && mouseX < panelX + panelWidth * 0.54f) {
        cycleSelected(-1);
      } else {
        action = activateSelected();
      }
    }
  }
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
  parsed.fullscreen = fullscreen;
  parsed.uiScale = uiScale > 0 ? uiScale : 1;
  parsed.msaa = msaa;
  parsed.soundVolume = soundVolume;
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

  // Header rule: a cyan-to-violet hairline that draws out on open.
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
  if (visibleRows < kRowCount) {
    // Rounded scrollbar whose thumb glides to the scroll position.
    const float trackHeight = rowHeight * static_cast<float>(visibleRows);
    const float trackX = panelX + panelWidth - 14.0f;
    GuiKit::drawRoundedRect(
      visual,
      trackX,
      animatedFirstRowY,
      4.0f,
      trackHeight,
      2.0f,
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    const float thumbRow = std::clamp(
      scrollThumb.value(), 0.0f, static_cast<float>(kRowCount - visibleRows));
    GuiKit::drawRoundedRect(
      visual,
      trackX,
      animatedFirstRowY + trackHeight * thumbRow / kRowCount,
      4.0f,
      trackHeight * static_cast<float>(visibleRows) / kRowCount,
      2.0f,
      UiTheme::applyOpacity(UiTheme::accent(), panelOpacity));
  }

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
                              ? 11.0f
                              : std::clamp(headerHeight * 0.18f, 9.0f, 13.0f);
  const float noteFontSize = panelHeight >= 400.0f
                               ? 11.0f
                               : std::clamp(footerHeight * 0.25f, 8.0f, 11.0f);
  const float helpFontSize = panelHeight >= 400.0f
                               ? 13.0f
                               : std::clamp(footerHeight * 0.30f, 9.0f, 13.0f);

  const float headerSlide = (1.0f - reveal) * 10.0f;
  // The header floats nearer than the rows and swings further with the tilt.
  const float headerX = panelX + tilt.layerX(GuiPanelTilt::kHeaderDepth);
  const float headerY =
    animatedPanelY + tilt.layerY(GuiPanelTilt::kHeaderDepth);
  if (headerHeight >= 90.0f) {
    visual.addText("M A K E   I T   Y O U R S",
                   headerX + 28.0f - headerSlide,
                   headerY + 16.0f,
                   10.0f,
                   UiTheme::applyOpacity(cyan, panelOpacity));
  }
  visual.addText("SIMULATOR SETTINGS",
                 headerX + 28.0f - headerSlide * 0.6f,
                 headerY + (panelHeight >= 400.0f
                              ? 34.0f
                              : std::max(4.0f, headerHeight * 0.10f)),
                 titleFontSize,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), panelOpacity));
  const ColorRgba secondaryText = UiTheme::textSecondary();
  if (headerHeight >= 60.0f) {
    visual.addText("UP/DOWN: select   LEFT/RIGHT: change   TYPE: edit",
                   headerX + 28.0f,
                   headerY +
                     (panelHeight >= 400.0f ? 65.0f : headerHeight * 0.48f),
                   subFontSize,
                   UiTheme::applyOpacity(secondaryText, panelOpacity));
    visual.addText("SCROLL / PGDN: more   ENTER: activate   ESC: discard",
                   headerX + 28.0f,
                   headerY +
                     (panelHeight >= 400.0f ? 82.0f : headerHeight * 0.72f),
                   subFontSize,
                   UiTheme::applyOpacity(UiTheme::textMuted(), panelOpacity));
  }

  const std::string labels[kRowCount] = { "Family",
                                          "Ruleset",
                                          "Width (chunks)",
                                          "Height (chunks)",
                                          "Simulation rate (TPS)",
                                          "Speed multiplier",
                                          "Fade speed",
                                          "Vertical sync",
                                          "Fullscreen",
                                          "UI scale",
                                          "Anti-aliasing*",
                                          "FPS cap",
                                          "Simulation inspector",
                                          "Reduced menu motion",
                                          "Edit control hints",
                                          "Sound volume",
                                          "Apply changes",
                                          "Discard changes",
                                          "Exit simulator" };
  const std::string values[kRowCount] = {
    RuleSetRegistry::instance().getFamilyDefinition(family) == nullptr
      ? family
      : RuleSetRegistry::instance().getFamilyDefinition(family)->name,
    displayRuleSetName(ruleSet),
    worldWidthText,
    worldHeightText,
    tpsText,
    speedText,
    fadeText,
    vsync ? "On" : "Off",
    fullscreen ? "On" : "Off",
    std::to_string(uiScale) + "x",
    msaa == 0 ? "Off" : std::to_string(msaa) + "x",
    fpsCapText == "0" ? "Uncapped" : fpsCapText + " FPS",
    showInspector ? "On" : "Off",
    reducedUiMotion ? "On" : "Off",
    editHints ? "On" : "Off",
    soundVolume == 0 ? "Off" : std::to_string(soundVolume) + "%",
    "ENTER",
    "ENTER",
    "ENTER"
  };
  const std::string help[kRowCount] = {
    "Choose the cell family; the ruleset list is filtered to this family.",
    "Choose transition behavior that belongs to the selected family.",
    "Enter a positive chunk count, or inf on both world axes.",
    "Enter a positive chunk count, or inf on both world axes.",
    "Target simulation ticks per second: 1 to 1000.",
    "Simulation-rate multiplier: greater than 0, up to 100.",
    "Color transition speed: 0 snaps immediately; maximum 100.",
    "Synchronize frame presentation to the monitor.",
    "Use the entire display.",
    "Scale interface size: 1x, 2x, 3x, 4x.",
    "Multisample anti-aliasing: Off, 2x, 4x, 8x. (*Requires restart)",
    "0 = uncapped; 1-1000 FPS. VSync still limits to monitor refresh.",
    "Show generation, cell coordinates, and population in the simulation.",
    "Disable decorative motion and snap menu transitions.",
    "Show input hints at the bottom while editing.",
    "Sound effect volume, off to 100%; each step previews the new level.",
    "Validate, save, and apply the displayed settings.",
    "Close the menu without changing any settings.",
    "Leave CSim (confirmation appears during a simulation)."
  };
  const float valueColumnX = panelX + panelWidth * 0.54f;
  const float rowFontSize = panelHeight >= 360.0f
                              ? std::clamp(rowHeight * 0.56f, 16.0f, 18.0f)
                              : std::clamp(rowHeight * 0.52f, 9.0f, 18.0f);
  const float cardX = panelX + 20.0f;
  const float cardWidth = panelWidth - 40.0f;
  const float cardHeight = rowHeight - 4.0f;
  // Action rows carry a tint of their outcome on the card rim.
  for (int row = firstVisibleRow; row < firstVisibleRow + visibleRows; ++row) {
    const float y =
      animatedFirstRowY + rowHeight * static_cast<float>(row - firstVisibleRow);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(std::round(rowReveal(row) * 255.0f));
    const float e = std::clamp(rowFocus.value(row), 0.0f, 1.0f);
    const ColorRgba rim = row == kApplyRow    ? ColorRgba{ 46, 110, 86, 255 }
                          : row == kCancelRow ? ColorRgba{ 112, 92, 50, 255 }
                          : row == kExitRow   ? ColorRgba{ 118, 58, 66, 255 }
                                              : UiTheme::cardRim();
    GuiKit::drawRoundedRect(
      visual,
      cardX,
      y,
      cardWidth,
      cardHeight,
      8.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(rim, ColorRgba{ 80, 160, 190, 255 }, e * 0.5f),
        rowOpacity));
    GuiKit::drawRoundedGradientRect(
      visual,
      cardX + 1.0f,
      y + 1.0f,
      cardWidth - 2.0f,
      cardHeight - 2.0f,
      7.0f,
      UiTheme::applyOpacity(UiTheme::cardTop(), rowOpacity),
      UiTheme::applyOpacity(UiTheme::cardBottom(), rowOpacity));
  }

  // The selection pours between rows like a drop of liquid, clamped to the
  // visible window, and sweeps a sheen when it lands.
  const float windowTop = static_cast<float>(firstVisibleRow);
  const float windowBottom =
    static_cast<float>(firstVisibleRow + visibleRows - 1);
  const GuiSelectionSpan span =
    animator.selectionSpan(static_cast<float>(selectedRow));
  const bool selectionVisible = selectedRow >= firstVisibleRow &&
                                selectedRow < firstVisibleRow + visibleRows;
  const unsigned char selectionOpacity =
    selectionVisible
      ? static_cast<unsigned char>(std::round(rowReveal(selectedRow) * 255.0f))
      : 0;
  GuiLiquidSelection drop;
  drop.crossStart = cardX;
  drop.crossSize = cardWidth;
  drop.headStart =
    animatedFirstRowY +
    rowHeight * (std::clamp(span.leading, windowTop, windowBottom) - windowTop);
  drop.tailStart =
    animatedFirstRowY +
    rowHeight *
      (std::clamp(span.trailing, windowTop, windowBottom) - windowTop);
  drop.cellLength = cardHeight;
  drop.radius = 8.0f;
  drop.squash = span.squash;
  drop.glowSpread = 9.0f + 3.0f * breathe;
  drop.glow = UiTheme::applyOpacity(UiTheme::fade(cyan, 0.18f + 0.1f * breathe),
                                    selectionOpacity);
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

  for (int row = firstVisibleRow; row < firstVisibleRow + visibleRows; ++row) {
    const float y =
      animatedFirstRowY + rowHeight * static_cast<float>(row - firstVisibleRow);
    const bool selected = row == selectedRow;
    const bool textField = row == kWorldWidthRow || row == kWorldHeightRow ||
                           row == kTpsRow || row == kSpeedRow ||
                           row == kFadeRow || row == kFpsCapRow;
    const float textY = y + std::max(2.0f, (rowHeight - rowFontSize) * 0.5f);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(std::round(rowReveal(row) * 255.0f));
    const float e = std::clamp(rowFocus.value(row), 0.0f, 1.2f);
    const float eClamped = std::min(1.0f, e);
    if (row < kApplyRow) {
      const float fieldWidth =
        panelX + panelWidth - 25.0f - valueColumnX + 12.0f;
      GuiKit::drawRoundedGradientRect(
        visual,
        valueColumnX - 12.0f,
        y + 3.0f,
        fieldWidth,
        rowHeight - 10.0f,
        6.0f,
        UiTheme::applyOpacity(ColorRgba{ 5, 11, 22, 225 }, rowOpacity),
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
      if (selected && animator.valuePulse() > 0.0f) {
        // A changed value blooms: a soft ring that fades out.
        GuiKit::drawRoundedBand(
          visual,
          valueColumnX - 12.0f,
          y + 3.0f,
          fieldWidth,
          rowHeight - 10.0f,
          6.0f,
          -1.0f,
          7.0f,
          UiTheme::applyOpacity(
            UiTheme::fade(cyan, 0.55f * animator.valuePulse()), rowOpacity),
          UiTheme::transparentOf(cyan));
      }
      if (isToggleRow(row)) {
        // The track keeps its 36x16 pill; the knob boings across on a
        // spring, stretching like a droplet while it moves, and the track
        // warms toward the accent as it travels.
        const float knob = std::clamp(toggleKnobs.value(row), -0.2f, 1.2f);
        const float lit = std::clamp(knob, 0.0f, 1.0f);
        const float knobStretch =
          std::min(7.0f, std::abs(toggleKnobs.velocity(row)) * 0.6f);
        const float toggleX = panelX + panelWidth - 75.0f;
        const float toggleY = y + (rowHeight - 20.0f) * 0.5f;
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
      }
    }
    // Labels lean in horizontally only; their row baseline stays fixed. They
    // thicken with focus.
    GuiKit::drawEmphasizedText(
      visual,
      labels[row],
      panelX + 36.0f + 4.0f * e,
      textY,
      rowFontSize,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped), rowOpacity),
      e);
    const ColorRgba valueColor =
      row == kApplyRow  ? UiTheme::success()
      : row == kExitRow ? UiTheme::error()
      : row == kCancelRow
        ? UiTheme::warning()
        : UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped);
    // A stepped value springs a few pixels the way it moved and bounces back.
    const float valueX =
      valueColumnX + (selected && row < kApplyRow && !isToggleRow(row)
                        ? 5.0f * animator.valueWobble()
                        : 0.0f);
    visual.addText(values[row],
                   valueX,
                   textY,
                   rowFontSize,
                   UiTheme::applyOpacity(valueColor, rowOpacity));
    const bool caretVisible = animator.caretVisible();
    if (selected && textField && caretVisible) {
      const float caretX =
        std::min(GuiKit::caretOriginAfterText(values[row], valueX, rowFontSize),
                 panelX + panelWidth - 31.0f);
      visual.addText("|",
                     caretX,
                     textY,
                     rowFontSize,
                     UiTheme::applyOpacity(cyan, rowOpacity));
    }
  }
  const float noteY = panelHeight >= 400.0f
                        ? animatedPanelY + panelHeight - 44.0f
                        : animatedPanelY + panelHeight - footerHeight + 2.0f;
  const float helpY =
    panelHeight >= 400.0f
      ? animatedPanelY + panelHeight - 24.0f
      : animatedPanelY + panelHeight - footerHeight * 0.5f + 2.0f;
  visual.addText("* Marked settings require restarting CSim to take effect.",
                 panelX + 28.0f,
                 noteY,
                 noteFontSize,
                 UiTheme::applyOpacity(UiTheme::warning(), panelOpacity));
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
