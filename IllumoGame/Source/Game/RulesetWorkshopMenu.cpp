#include "RulesetWorkshopMenu.h"
#include "RuleCatalogLoader.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <queue>

namespace {

float
caretOriginAfterText(const std::string& text, float textX, float fontSize)
{
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (font == nullptr) {
    return textX + GuiKit::estimateTextWidth(text, fontSize);
  }
  const FontMetrics& metrics = font->getMetrics();
  const float scale =
    metrics.pixelSize > 0.0f ? fontSize / metrics.pixelSize : 1.0f;
  float inkRight = textX;
  if (!text.empty()) {
    const TextBounds bounds = font->measureText(text, fontSize);
    const GlyphInfo* lastGlyph =
      font->getGlyph(static_cast<unsigned char>(text.back()));
    inkRight += bounds.width;
    if (lastGlyph != nullptr) {
      inkRight +=
        (lastGlyph->bearingX + lastGlyph->width - lastGlyph->advanceX) * scale;
    }
  }
  const GlyphInfo* caretGlyph = font->getGlyph('|');
  const float caretBearing =
    caretGlyph == nullptr ? 0.0f : caretGlyph->bearingX * scale;
  return inkRight + 1.0f - caretBearing;
}

float
easeOutCubic(float progress)
{
  const float remaining = 1.0f - std::clamp(progress, 0.0f, 1.0f);
  return 1.0f - remaining * remaining * remaining;
}

ColorRgba
stateColor(const RuleFamilyDefinition& definition, unsigned char state)
{
  if (static_cast<std::size_t>(state) >= definition.stateColors.size()) {
    return ColorRgba{ 160, 160, 160, 255 };
  }
  const std::array<unsigned char, 3>& rgb = definition.stateColors[state];
  return ColorRgba{ rgb[0], rgb[1], rgb[2], 255 };
}

std::string
familyLabel(RuleFamily family)
{
  switch (family) {
    case RuleFamily::LifeLike:
      return "LIFE-LIKE";
    case RuleFamily::Generations:
      return "GENERATIONS";
    case RuleFamily::MooreTable:
      return "MOORE TABLE";
    case RuleFamily::Cyclic:
      return "CYCLIC INTERACTION";
    case RuleFamily::SpeciesLife:
      return "COLORIZED LIFE";
    case RuleFamily::LargerThanLife:
      return "LARGER THAN LIFE";
    case RuleFamily::Hodgepodge:
      return "HODGEPODGE CHEMISTRY";
    case RuleFamily::Turmite:
      return "DIRECTIONAL TURMITE";
    case RuleFamily::LatticeGas:
      return "LATTICE GAS";
    case RuleFamily::Dominance:
      return "SPECIES DOMINANCE";
    case RuleFamily::Elementary1D:
      return "ELEMENTARY 1D";
    default:
      return "UNKNOWN FAMILY";
  }
}

std::string
uniqueCustomId(const std::string& prefix,
               const std::string& sourceId,
               bool forFamily)
{
  const RuleSetRegistry& registry = RuleSetRegistry::instance();
  std::string base = RuleSetRegistry::normalizeId(sourceId);
  const std::size_t maximumBaseLength = 64u - prefix.size() - 5u;
  if (base.size() > maximumBaseLength) {
    base.resize(maximumBaseLength);
  }
  for (unsigned int suffix = 1u; suffix < 10000u; ++suffix) {
    const std::string suffixText =
      suffix == 1u ? "" : "_" + std::to_string(suffix);
    std::string candidateBase = base;
    if (candidateBase.size() + suffixText.size() > maximumBaseLength) {
      candidateBase.resize(maximumBaseLength - suffixText.size());
    }
    const std::string candidate = prefix + candidateBase + suffixText;
    const bool exists = forFamily ? registry.isKnownFamily(candidate)
                                  : registry.isKnownRule(candidate);
    if (!exists) {
      return candidate;
    }
  }
  return {};
}

bool
isCountMaskFamily(RuleFamily family)
{
  return family == RuleFamily::LifeLike || family == RuleFamily::Generations ||
         family == RuleFamily::SpeciesLife;
}

std::string
stateLabel(const RuleFamilyDefinition& definition, unsigned int state)
{
  if (state < definition.stateNames.size() &&
      !definition.stateNames[state].empty()) {
    return definition.stateNames[state];
  }
  return "State " + std::to_string(state);
}

void
drawValueStepper(GameVisual& visual,
                 float x,
                 float y,
                 float width,
                 float height,
                 const std::string& value,
                 ColorRgba accent,
                 unsigned char opacity)
{
  GuiKit::drawRoundedRect(
    visual,
    x,
    y,
    width,
    height,
    6.0f,
    UiTheme::applyOpacity(UiTheme::panelInset(), opacity));
  const float sideWidth = std::clamp(width * 0.16f, 20.0f, 30.0f);
  GuiKit::drawRoundedRect(
    visual,
    x + 2.0f,
    y + 2.0f,
    sideWidth,
    std::max(1.0f, height - 4.0f),
    4.0f,
    UiTheme::applyOpacity(UiTheme::panelBorder(), opacity));
  GuiKit::drawRoundedRect(
    visual,
    x + width - sideWidth - 2.0f,
    y + 2.0f,
    sideWidth,
    std::max(1.0f, height - 4.0f),
    4.0f,
    UiTheme::applyOpacity(UiTheme::panelBorder(), opacity));
  const float controlFont = std::clamp(height * 0.58f, 12.0f, 14.0f);
  const float textY = y + std::max(2.0f, (height - controlFont) * 0.5f);
  visual.addText("-",
                 x + sideWidth * 0.5f - controlFont * 0.22f,
                 textY,
                 controlFont,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));
  visual.addText("+",
                 x + width - sideWidth * 0.5f - controlFont * 0.25f,
                 textY,
                 controlFont,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));
  const float valueFont = std::clamp(height * 0.58f, 12.0f, 14.0f);
  const float valueWidth = static_cast<float>(value.size()) * valueFont * 0.58f;
  visual.addText(value,
                 x + std::max(sideWidth + 4.0f, (width - valueWidth) * 0.5f),
                 y + std::max(2.0f, (height - valueFont) * 0.5f),
                 valueFont,
                 UiTheme::applyOpacity(accent, opacity));
}

void
drawActionButton(GameVisual& visual,
                 float x,
                 float y,
                 float width,
                 float height,
                 const std::string& label,
                 ColorRgba tint,
                 bool selected,
                 unsigned char opacity)
{
  GuiKit::drawRoundedRect(
    visual,
    x,
    y,
    width,
    height,
    8.0f,
    UiTheme::applyOpacity(selected ? UiTheme::selection() : UiTheme::menuCard(),
                          opacity));
  GuiKit::drawRoundedRect(
    visual,
    x,
    y,
    width,
    height,
    8.0f,
    UiTheme::applyOpacity(
      tint, static_cast<unsigned char>(selected ? opacity : opacity * 0.55f)));
  const float fontSize = std::clamp(height * 0.42f, 12.0f, 15.0f);
  const float textWidth = static_cast<float>(label.size()) * fontSize * 0.58f;
  visual.addText(
    label,
    x + std::max(8.0f, (width - textWidth) * 0.5f),
    y + std::max(2.0f, (height - fontSize) * 0.5f),
    fontSize,
    UiTheme::applyOpacity(selected ? UiTheme::textPrimary() : tint, opacity));
}

} // namespace

RulesetWorkshopMenu::RulesetWorkshopMenu(IRenderWindow* targetWindow,
                                         Renderer* targetRenderer)
  : window(targetWindow)
  , renderer(targetRenderer)
  , visual(4096u)
{
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  visual.setWindow(window);
  visual.setRenderer(renderer);
  visual.prepare(renderer);
  setVisible(false);
}

bool
RulesetWorkshopMenu::open(const RuleFamilyDefinition& currentFamily,
                          const RuleSetDefinition& currentRule,
                          bool useReducedMotion)
{
  openState = false;
  setVisible(false);
  setDraft(currentFamily, currentRule);
  if (starterRuleIds.empty()) {
    return false;
  }
  selectedRow = rowForControl(Control::StarterRule);
  firstVisibleRow = 0;
  neighborCount = 3u;
  previewNeighborCount = 3u;
  previewNeighborhood = 2u;
  previewState = 0u;
  paletteState = 0u;
  reducedMotion = useReducedMotion;
  animationElapsed = reducedMotion ? kOpenAnimationSeconds : 0.0f;
  selectionFromRow = 0.0f;
  selectionAnimationElapsed = kSelectionAnimationSeconds;
  valuePulseElapsed = kValuePulseSeconds;
  ambientPhase = 0.0f;
  caretBlinkElapsed = 0.0f;
  mouseWasDown = true;
  previousMouseX = -1.0f;
  previousMouseY = -1.0f;
  errorMessage.clear();
  previewDirty = true;
  openState = true;
  setVisible(true);
  updateLayout();
  rebuildVisual();
  return true;
}

void
RulesetWorkshopMenu::setDraft(const RuleFamilyDefinition& family,
                              const RuleSetDefinition& rule)
{
  starterRuleIds.clear();
  const std::vector<RuleSetDefinition>& definitions =
    RuleSetRegistry::instance().getDefinitions();
  for (const RuleSetDefinition& registeredDefinition : definitions) {
    if (registeredDefinition.familyId == family.id) {
      starterRuleIds.push_back(registeredDefinition.id);
    }
  }
  starterRuleIndex = 0;
  for (std::size_t index = 0u; index < starterRuleIds.size(); ++index) {
    if (starterRuleIds[index] == rule.id) {
      starterRuleIndex = static_cast<int>(index);
      break;
    }
  }
  familyDraft = family;
  draft = rule;
  if (draft.builtIn) {
    draft.id = uniqueCustomId("CUSTOM_", rule.id, false);
    draft.builtIn = false;
    draft.name = "Custom " + rule.name;
  }
  draft.familyId = familyDraft.id;
  familyChanged = false;
  const unsigned int validStateCount = std::max(1u, familyDraft.stateCount);
  paletteState = std::min(paletteState, validStateCount - 1u);
  previewState = std::min(previewState, validStateCount - 1u);
  errorMessage.clear();
  previewDirty = true;
  rebuildRows();
  rebuildVisual();
}

void
RulesetWorkshopMenu::close()
{
  openState = false;
  setVisible(false);
  visual.clearPrimitives();
  previousMouseX = -1.0f;
  previousMouseY = -1.0f;
  errorMessage.clear();
}

void
RulesetWorkshopMenu::tick(float deltaSeconds)
{
  if (!openState || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
    return;
  }
  if (reducedMotion) {
    animationElapsed = kOpenAnimationSeconds;
    selectionAnimationElapsed = kSelectionAnimationSeconds;
    valuePulseElapsed = kValuePulseSeconds;
    ambientPhase = 0.0f;
    caretBlinkElapsed = 0.0f;
    return;
  }
  animationElapsed =
    std::min(kOpenAnimationSeconds, animationElapsed + deltaSeconds);
  selectionAnimationElapsed = std::min(
    kSelectionAnimationSeconds, selectionAnimationElapsed + deltaSeconds);
  valuePulseElapsed =
    std::min(kValuePulseSeconds, valuePulseElapsed + deltaSeconds);
  ambientPhase = std::fmod(ambientPhase + std::min(deltaSeconds, 0.1f), 12.0f);
  caretBlinkElapsed =
    std::fmod(caretBlinkElapsed + deltaSeconds, kCaretBlinkPeriodSeconds);
}

float
RulesetWorkshopMenu::animationProgress() const
{
  if (reducedMotion) {
    return 1.0f;
  }
  return std::clamp(animationElapsed / kOpenAnimationSeconds, 0.0f, 1.0f);
}

float
RulesetWorkshopMenu::getAnimationProgressForTesting() const
{
  return animationProgress();
}

float
RulesetWorkshopMenu::panelReveal() const
{
  if (reducedMotion) {
    return 1.0f;
  }
  return easeOutCubic(std::clamp(animationElapsed / 0.24f, 0.0f, 1.0f));
}

float
RulesetWorkshopMenu::panelOffsetY() const
{
  return (1.0f - panelReveal()) * 18.0f;
}

float
RulesetWorkshopMenu::rowReveal(int row) const
{
  if (reducedMotion) {
    return 1.0f;
  }
  const float delay =
    static_cast<float>(std::max(0, row - firstVisibleRow)) * 0.012f;
  return easeOutCubic(
    std::clamp((animationElapsed - delay) / 0.22f, 0.0f, 1.0f));
}

float
RulesetWorkshopMenu::selectionRowPosition() const
{
  const int selectedBodyRow = bodyIndexForRow(selectedRow);
  if (selectedBodyRow < 0) {
    return static_cast<float>(bodyRowCount);
  }
  if (reducedMotion) {
    return static_cast<float>(selectedBodyRow);
  }
  const float progress = easeOutCubic(std::clamp(
    selectionAnimationElapsed / kSelectionAnimationSeconds, 0.0f, 1.0f));
  return selectionFromRow +
         (static_cast<float>(selectedBodyRow) - selectionFromRow) * progress;
}

float
RulesetWorkshopMenu::getSelectionPositionForTesting() const
{
  return selectionRowPosition();
}

int
RulesetWorkshopMenu::getControlIndexForTesting(const std::string& label) const
{
  for (std::size_t index = 0u; index < rows.size(); ++index) {
    if (rows[index].label == label && rows[index].control != Control::None) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool
RulesetWorkshopMenu::hasControlForTesting(const std::string& label) const
{
  return getControlIndexForTesting(label) >= 0;
}

std::string
RulesetWorkshopMenu::getSelectedControlForTesting() const
{
  if (selectedRow < 0 || static_cast<std::size_t>(selectedRow) >= rows.size()) {
    return {};
  }
  return rows[static_cast<std::size_t>(selectedRow)].label;
}

float
RulesetWorkshopMenu::valuePulse() const
{
  if (reducedMotion) {
    return 0.0f;
  }
  const float progress =
    std::clamp(valuePulseElapsed / kValuePulseSeconds, 0.0f, 1.0f);
  return 1.0f - progress;
}

float
RulesetWorkshopMenu::getValuePulseForTesting() const
{
  return valuePulse();
}

void
RulesetWorkshopMenu::triggerValuePulse()
{
  valuePulseElapsed = 0.0f;
}

void
RulesetWorkshopMenu::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float preferredScale =
    renderer != nullptr ? std::max(1.0f, renderer->getUiScale()) : 1.0f;
  layoutScale =
    std::min(preferredScale,
             std::max(0.25f,
                      std::min(static_cast<float>(width) / 640.0f,
                               static_cast<float>(height) / 480.0f)));
  const float fitScale = layoutScale / preferredScale;
  Transform2D transform;
  transform.scaleX = fitScale;
  transform.scaleY = fitScale;
  visual.setTransform(transform);

  const float virtualWidth = static_cast<float>(width) / layoutScale;
  const float virtualHeight = static_cast<float>(height) / layoutScale;
  panelWidth = std::min(820.0f, std::max(200.0f, virtualWidth - 24.0f));
  panelHeight = std::min(700.0f, std::max(160.0f, virtualHeight - 20.0f));
  panelWidth = std::min(panelWidth, virtualWidth);
  panelHeight = std::min(panelHeight, virtualHeight);
  panelX = std::max(0.0f, (virtualWidth - panelWidth) * 0.5f);
  panelY = std::max(0.0f, (virtualHeight - panelHeight) * 0.5f);

  headerHeight = panelHeight >= 500.0f
                   ? 102.0f
                   : std::clamp(panelHeight * 0.19f, 52.0f, 102.0f);
  footerHeight = panelHeight >= 400.0f
                   ? 78.0f
                   : std::clamp(panelHeight * 0.28f, 44.0f, 78.0f);
  firstRowY = panelY + headerHeight;
  const float availableRowsHeight =
    std::max(1.0f, panelHeight - headerHeight - footerHeight - 4.0f);
  visibleRows = std::clamp(static_cast<int>(availableRowsHeight / 34.0f),
                           1,
                           std::max(1, bodyRowCount));
  rowHeight = availableRowsHeight / static_cast<float>(visibleRows);
  firstVisibleRow =
    std::clamp(firstVisibleRow, 0, std::max(0, bodyRowCount - visibleRows));
}

void
RulesetWorkshopMenu::selectRow(int row)
{
  if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
    return;
  }
  const RowKind kind = rows[static_cast<std::size_t>(row)].kind;
  if (kind != RowKind::Control && kind != RowKind::FooterAction) {
    return;
  }
  const int previousBody = bodyIndexForRow(selectedRow);
  const int nextBody = bodyIndexForRow(row);
  if (row != selectedRow) {
    if (previousBody >= 0 && nextBody >= 0) {
      selectionFromRow = selectionRowPosition();
      selectionAnimationElapsed = 0.0f;
    } else {
      selectionAnimationElapsed = kSelectionAnimationSeconds;
    }
    selectedRow = row;
    caretBlinkElapsed = 0.0f;
  }
  if (nextBody >= 0) {
    if (nextBody < firstVisibleRow) {
      firstVisibleRow = nextBody;
    }
    if (nextBody >= firstVisibleRow + visibleRows) {
      firstVisibleRow = nextBody - visibleRows + 1;
    }
  }
  errorMessage.clear();
}

int
RulesetWorkshopMenu::bodyIndexForRow(int row) const
{
  if (row < 0 || row >= bodyRowCount) {
    return -1;
  }
  return row;
}

int
RulesetWorkshopMenu::rowForControl(Control control) const
{
  for (std::size_t index = 0u; index < rows.size(); ++index) {
    if (rows[index].control == control) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

RulesetWorkshopMenu::Control
RulesetWorkshopMenu::controlForRow(int row) const
{
  if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
    return Control::None;
  }
  return rows[static_cast<std::size_t>(row)].control;
}

bool
RulesetWorkshopMenu::isTextControl(int row) const
{
  const Control control = controlForRow(row);
  return control == Control::RulesetName || control == Control::FamilyName ||
         control == Control::StateName;
}

void
RulesetWorkshopMenu::appendSection(const std::string& label)
{
  rows.push_back(MenuRow{ RowKind::Section, Control::None, label, {} });
}

void
RulesetWorkshopMenu::appendControl(Control control, const std::string& label)
{
  rows.push_back(MenuRow{ RowKind::Control, control, label, {} });
}

void
RulesetWorkshopMenu::appendInformation(Control control,
                                       const std::string& label,
                                       const std::string& detail)
{
  rows.push_back(MenuRow{ RowKind::Information, control, label, detail });
}

void
RulesetWorkshopMenu::rebuildRows()
{
  const Control previouslySelected = controlForRow(selectedRow);
  rows.clear();
  appendSection("RULE");
  appendControl(Control::StarterRule, "Starter rule");
  appendControl(Control::Family, "Cell family");
  appendControl(Control::FamilyName, "Family name");
  appendInformation(
    Control::ModelKind, "Simulation model", familyLabel(familyDraft.kind));
  appendControl(Control::RulesetName, "Ruleset name");
  if (isCountMaskFamily(familyDraft.kind)) {
    appendControl(Control::BirthCounts, "Birth counts");
    appendControl(Control::SurvivalCounts, "Survival counts");
  } else if (familyDraft.kind == RuleFamily::Elementary1D) {
    appendControl(Control::WolframNumber, "Wolfram rule number");
  } else if (familyDraft.kind == RuleFamily::MooreTable) {
    appendInformation(Control::TableInformation,
                      "Transition table",
                      "Edit transitions in JSON, then import the rule.");
  } else if (familyDraft.kind == RuleFamily::Cyclic) {
    appendControl(Control::CyclicThreshold, "Successor threshold");
    appendControl(Control::CyclicStep, "Cycle step");
  } else if (familyDraft.kind == RuleFamily::LargerThanLife) {
    appendInformation(Control::TableInformation,
                      "Extended neighborhood",
                      draft.rule.empty()
                        ? "Edit range and thresholds in JSON, then import."
                        : draft.rule);
  } else if (familyDraft.kind == RuleFamily::Hodgepodge ||
             familyDraft.kind == RuleFamily::Turmite ||
             familyDraft.kind == RuleFamily::LatticeGas ||
             familyDraft.kind == RuleFamily::Dominance) {
    appendInformation(Control::TableInformation,
                      "Interaction contract",
                      draft.rule.empty()
                        ? "Edit family parameters in JSON, then import."
                        : draft.rule);
  }

  if (familyDraft.kind == RuleFamily::Generations) {
    appendControl(Control::GenerationStates, "Number of states");
  }

  appendSection("PREVIEW");
  if (familyDraft.kind == RuleFamily::Elementary1D) {
    appendControl(Control::PreviewNeighborhood, "Example neighborhood");
  } else {
    appendControl(Control::PreviewState, "Example cell state");
    appendControl(Control::PreviewNeighbors, "Live neighbors (0-8)");
  }
  appendInformation(Control::PreviewResult, "Next state", {});

  appendSection("APPEARANCE");
  appendControl(Control::PaletteState, "State to edit");
  appendControl(Control::StateName, "State label");
  appendControl(Control::Red, "Red channel");
  appendControl(Control::Green, "Green channel");
  appendControl(Control::Blue, "Blue channel");

  appendSection("FILES");
  appendControl(Control::Import, "Import rules from JSON");
  appendControl(Control::Export, "Export this rule");
  bodyRowCount = static_cast<int>(rows.size());
  rows.push_back(
    MenuRow{ RowKind::FooterAction, Control::Apply, "Save & Apply", {} });
  rows.push_back(
    MenuRow{ RowKind::FooterAction, Control::Discard, "Discard", {} });

  int nextSelection = rowForControl(previouslySelected);
  if (nextSelection < 0) {
    nextSelection = rowForControl(Control::StarterRule);
  }
  if (nextSelection < 0) {
    nextSelection = rowForControl(Control::Apply);
  }
  selectedRow = nextSelection;
  firstVisibleRow = 0;
  updateLayout();
}

void
RulesetWorkshopMenu::setError(const std::string& error)
{
  errorMessage = error;
  rebuildVisual();
}

RulesetWorkshopAction
RulesetWorkshopMenu::update(InputManager* input)
{
  if (!openState || input == nullptr) {
    return RulesetWorkshopAction::None;
  }
  updateLayout();
  RulesetWorkshopAction action = RulesetWorkshopAction::None;
  std::queue<InputManager::KeyPressEvent> remaining;
  std::queue<InputManager::KeyPressEvent>& keys = input->getKeyQueue();
  while (!keys.empty()) {
    const InputManager::KeyPressEvent event = keys.front();
    keys.pop();
    if (event.key == KeyCode::Grave) {
      remaining.push(event);
      continue;
    }
    if (event.action != InputAction::Press &&
        event.action != InputAction::Hold) {
      continue;
    }
    const bool textFieldSelected = isTextControl(selectedRow);
    if (event.key == KeyCode::Escape && event.action == InputAction::Press) {
      action = RulesetWorkshopAction::Cancel;
    } else if (event.key == KeyCode::Up ||
               (!textFieldSelected && event.key == KeyCode::W)) {
      moveSelection(-1);
    } else if (event.key == KeyCode::Down ||
               (!textFieldSelected && event.key == KeyCode::S)) {
      moveSelection(1);
    } else if (event.key == KeyCode::Tab) {
      const int direction = input->isShiftPressed() ? -1 : 1;
      moveSelection(direction);
    } else if (event.key == KeyCode::PageDown) {
      for (int step = 0; step < visibleRows; ++step) {
        moveSelection(1);
      }
    } else if (event.key == KeyCode::PageUp) {
      for (int step = 0; step < visibleRows; ++step) {
        moveSelection(-1);
      }
    } else if (event.key == KeyCode::Home) {
      for (std::size_t index = 0u; index < rows.size(); ++index) {
        if (rows[index].kind == RowKind::Control) {
          selectRow(static_cast<int>(index));
          break;
        }
      }
    } else if (event.key == KeyCode::End) {
      for (std::size_t index = rows.size(); index > 0u; --index) {
        if (rows[index - 1u].kind == RowKind::FooterAction) {
          selectRow(static_cast<int>(index - 1u));
          break;
        }
      }
    } else if (event.key == KeyCode::Left) {
      changeSelected(-1);
    } else if (event.key == KeyCode::Right) {
      changeSelected(1);
    } else if ((event.key == KeyCode::Enter ||
                (event.key == KeyCode::Space && !textFieldSelected)) &&
               event.action == InputAction::Press) {
      action = activateSelected();
    } else if (event.key == KeyCode::Backspace && textFieldSelected) {
      const Control control = controlForRow(selectedRow);
      std::string* value = control == Control::RulesetName ? &draft.name
                           : control == Control::FamilyName
                             ? &familyDraft.name
                             : &familyDraft.stateNames[paletteState];
      if (!value->empty()) {
        value->pop_back();
        familyChanged = familyChanged || control == Control::FamilyName ||
                        control == Control::StateName;
        previewDirty = true;
        caretBlinkElapsed = 0.0f;
        triggerValuePulse();
      }
    }
    if (action != RulesetWorkshopAction::None) {
      while (!keys.empty()) {
        keys.pop();
      }
      break;
    }
  }
  keys.swap(remaining);

  std::queue<unsigned int>& characters = input->getCharQueue();
  if (isTextControl(selectedRow)) {
    const Control control = controlForRow(selectedRow);
    std::string* value = control == Control::RulesetName ? &draft.name
                         : control == Control::FamilyName
                           ? &familyDraft.name
                           : &familyDraft.stateNames[paletteState];
    while (!characters.empty()) {
      const unsigned int codepoint = characters.front();
      characters.pop();
      if (codepoint >= 32u && codepoint <= 126u && value->size() < 36u) {
        value->push_back(static_cast<char>(codepoint));
        familyChanged = familyChanged || control == Control::FamilyName ||
                        control == Control::StateName;
        previewDirty = true;
        caretBlinkElapsed = 0.0f;
        triggerValuePulse();
      }
    }
  } else {
    while (!characters.empty()) {
      characters.pop();
    }
  }

  double* scroll = input->getMouseScrollOffset();
  const bool wheelScrolled =
    scroll != nullptr && std::isfinite(*scroll) && *scroll != 0.0;
  if (wheelScrolled) {
    const int rowsToScroll = static_cast<int>(std::ceil(std::min(
      std::abs(*scroll), static_cast<double>(std::max(1, bodyRowCount)))));
    firstVisibleRow = std::clamp(
      firstVisibleRow + (*scroll > 0.0 ? -rowsToScroll : rowsToScroll),
      0,
      std::max(0, bodyRowCount - visibleRows));
    *scroll = 0.0;
  }

  updateLayout();
  const std::array<double, 2> mouse =
    window != nullptr ? window->getMouseCoords() : input->getMousePosition();
  const float mouseX = static_cast<float>(mouse[0]) / layoutScale;
  const float mouseY = static_cast<float>(mouse[1]) / layoutScale;
  const bool mouseMoved = mouseX != previousMouseX || mouseY != previousMouseY;
  previousMouseX = mouseX;
  previousMouseY = mouseY;
  const bool down = input->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool clicked = down && !mouseWasDown;
  const float animatedPanelY = panelY + panelOffsetY();
  const float footerY = animatedPanelY + panelHeight - footerHeight;
  const float buttonY = footerY + (footerHeight >= 65.0f ? 34.0f : 10.0f);
  const float buttonHeight = footerHeight >= 65.0f ? 34.0f : 27.0f;
  const float buttonGap = 8.0f;
  const float buttonX = panelX + 28.0f;
  const float buttonWidth = (panelWidth - 56.0f - buttonGap) * 0.5f;
  const bool overApply = GuiKit::isPointInRect(
    mouseX, mouseY, buttonX, buttonY, buttonWidth, buttonHeight);
  const bool overDiscard =
    GuiKit::isPointInRect(mouseX,
                          mouseY,
                          buttonX + buttonWidth + buttonGap,
                          buttonY,
                          buttonWidth,
                          buttonHeight);
  if ((mouseMoved || clicked) && overApply) {
    const int applyRow = rowForControl(Control::Apply);
    selectRow(applyRow);
    if (clicked && action == RulesetWorkshopAction::None) {
      action = activateControl(Control::Apply);
    }
  } else if ((mouseMoved || clicked) && overDiscard) {
    const int discardRow = rowForControl(Control::Discard);
    selectRow(discardRow);
    if (clicked && action == RulesetWorkshopAction::None) {
      action = activateControl(Control::Discard);
    }
  } else {
    const int bodyIndex = bodyRowForPoint(mouseX, mouseY);
    if (bodyIndex >= 0) {
      const int row = bodyIndex;
      const Control control = controlForRow(row);
      if ((mouseMoved || clicked) && control != Control::None) {
        selectRow(row);
      }
      if (clicked && action == RulesetWorkshopAction::None &&
          control != Control::None) {
        const float valueLeft = panelX + panelWidth * 0.53f;
        const float valueRight = panelX + panelWidth - 36.0f;
        if (control == Control::BirthCounts ||
            control == Control::SurvivalCounts) {
          const float gap = 3.0f;
          const float chipWidth =
            std::min(25.0f, (valueRight - valueLeft - gap * 8.0f) / 9.0f);
          const float chipX =
            valueLeft +
            (valueRight - valueLeft - (chipWidth * 9.0f + gap * 8.0f)) * 0.5f;
          if (mouseX >= chipX &&
              mouseX <= chipX + (chipWidth + gap) * 9.0f - gap) {
            const unsigned int count = static_cast<unsigned int>(std::clamp(
              static_cast<int>((mouseX - chipX) / (chipWidth + gap)), 0, 8));
            if (toggleNeighborCount(control, count)) {
              triggerValuePulse();
            }
          }
        } else if (control == Control::Import || control == Control::Export) {
          action = activateControl(control);
        } else if (control != Control::RulesetName &&
                   control != Control::FamilyName &&
                   control != Control::StateName &&
                   control != Control::TableInformation) {
          if (mouseX >= valueLeft && mouseX <= valueRight) {
            const float sideWidth =
              std::clamp((valueRight - valueLeft) * 0.16f, 20.0f, 30.0f);
            if (mouseX < valueLeft + sideWidth) {
              changeControl(control, -1);
            } else if (mouseX > valueRight - sideWidth) {
              changeControl(control, 1);
            }
          }
        }
      }
    }
  }
  mouseWasDown = down;
  rebuildVisual();
  return action;
}

int
RulesetWorkshopMenu::bodyRowForPoint(float x, float y) const
{
  const float rowAreaTop = firstRowY + panelOffsetY();
  const float rowAreaHeight = rowHeight * static_cast<float>(visibleRows);
  if (!GuiKit::isPointInRect(
        x, y, panelX + 20.0f, rowAreaTop, panelWidth - 40.0f, rowAreaHeight)) {
    return -1;
  }
  const int bodyRow =
    firstVisibleRow + static_cast<int>((y - rowAreaTop) / rowHeight);
  if (bodyRow < 0 || bodyRow >= bodyRowCount) {
    return -1;
  }
  return bodyRow;
}

void
RulesetWorkshopMenu::moveSelection(int direction)
{
  if (rows.empty() || direction == 0) {
    return;
  }
  int candidate = selectedRow;
  for (std::size_t attempt = 0u; attempt < rows.size(); ++attempt) {
    candidate = (candidate + direction + static_cast<int>(rows.size())) %
                static_cast<int>(rows.size());
    const RowKind kind = rows[static_cast<std::size_t>(candidate)].kind;
    if (kind == RowKind::Control || kind == RowKind::FooterAction) {
      selectRow(candidate);
      return;
    }
  }
}

RulesetWorkshopAction
RulesetWorkshopMenu::activateSelected()
{
  return activateControl(controlForRow(selectedRow));
}

RulesetWorkshopAction
RulesetWorkshopMenu::activateControl(Control control)
{
  if (control == Control::Apply) {
    return RulesetWorkshopAction::Apply;
  }
  if (control == Control::Discard) {
    return RulesetWorkshopAction::Cancel;
  }
  if (control == Control::Import) {
    return RulesetWorkshopAction::Import;
  }
  if (control == Control::Export) {
    return RulesetWorkshopAction::Export;
  }
  if (control == Control::BirthCounts || control == Control::SurvivalCounts) {
    toggleNeighborCount(control, neighborCount);
    triggerValuePulse();
    rebuildVisual();
    return RulesetWorkshopAction::None;
  }
  changeControl(control, 1);
  rebuildVisual();
  return RulesetWorkshopAction::None;
}

bool
RulesetWorkshopMenu::toggleNeighborCount(Control control, unsigned int count)
{
  if (count > 8u ||
      (control != Control::BirthCounts && control != Control::SurvivalCounts)) {
    return false;
  }
  neighborCount = count;
  unsigned int& mask =
    control == Control::BirthCounts ? draft.birthMask : draft.surviveMask;
  mask ^= 1u << count;
  draft.rule.clear();
  previewDirty = true;
  errorMessage.clear();
  return true;
}

bool
RulesetWorkshopMenu::changeControl(Control control, int direction)
{
  if (direction == 0) {
    return false;
  }
  errorMessage.clear();
  bool changed = false;
  if (control == Control::StarterRule) {
    const int previousStarterRule = starterRuleIndex;
    cycleStarterRule(direction);
    changed = starterRuleIndex != previousStarterRule;
  } else if (control == Control::Family) {
    const std::vector<RuleFamilyDefinition>& families =
      RuleSetRegistry::instance().getFamilyDefinitions();
    if (families.empty()) {
      return false;
    }
    int currentFamily = 0;
    for (std::size_t index = 0u; index < families.size(); ++index) {
      if (families[index].id == familyDraft.id) {
        currentFamily = static_cast<int>(index);
        break;
      }
    }
    const int nextFamily =
      (currentFamily + direction + static_cast<int>(families.size())) %
      static_cast<int>(families.size());
    changed = selectFamily(families[static_cast<std::size_t>(nextFamily)].id);
  } else if (control == Control::BirthCounts ||
             control == Control::SurvivalCounts) {
    const unsigned int previousCount = neighborCount;
    neighborCount = static_cast<unsigned int>(
      (static_cast<int>(neighborCount) + direction + 9) % 9);
    changed = neighborCount != previousCount;
  } else if (control == Control::GenerationStates &&
             familyDraft.kind == RuleFamily::Generations) {
    const int count =
      std::clamp(static_cast<int>(familyDraft.stateCount) + direction, 3, 256);
    changed = static_cast<unsigned int>(count) != familyDraft.stateCount;
    if (changed) {
      resizeGenerationStates(static_cast<unsigned int>(count));
    }
  } else if (control == Control::CyclicThreshold &&
             familyDraft.kind == RuleFamily::Cyclic) {
    const int next = static_cast<int>(draft.cyclicThreshold) + direction - 1;
    const unsigned int wrapped =
      static_cast<unsigned int>((next % 8 + 8) % 8 + 1);
    changed = wrapped != draft.cyclicThreshold;
    draft.cyclicThreshold = wrapped;
    previewDirty = true;
  } else if (control == Control::CyclicStep &&
             familyDraft.kind == RuleFamily::Cyclic) {
    const unsigned int stateCount = std::max(2u, familyDraft.stateCount);
    unsigned int next = draft.cyclicStep;
    for (unsigned int attempt = 0u; attempt < stateCount; ++attempt) {
      const int candidate = static_cast<int>(next) + direction - 1;
      next = static_cast<unsigned int>(
        (candidate % static_cast<int>(stateCount - 1u) +
         static_cast<int>(stateCount - 1u)) %
          static_cast<int>(stateCount - 1u) +
        1);
      if (std::gcd(next, stateCount) == 1u) {
        break;
      }
    }
    changed = next != draft.cyclicStep;
    draft.cyclicStep = next;
    previewDirty = true;
  } else if (control == Control::WolframNumber &&
             familyDraft.kind == RuleFamily::Elementary1D) {
    const int next = static_cast<int>(draft.ruleNumber) + 2 * direction;
    const int wrapped = (next % 256 + 256) % 256;
    changed = static_cast<unsigned int>(wrapped) != draft.ruleNumber;
    draft.ruleNumber = static_cast<unsigned int>(wrapped);
    previewDirty = true;
  } else if (control == Control::PreviewState) {
    const unsigned int count = std::max(1u, familyDraft.stateCount);
    const unsigned int previousState = previewState;
    previewState = static_cast<unsigned int>(
      (static_cast<int>(previewState) + direction + static_cast<int>(count)) %
      static_cast<int>(count));
    changed = previewState != previousState;
    previewDirty = true;
  } else if (control == Control::PreviewNeighbors) {
    const unsigned int previousCount = previewNeighborCount;
    previewNeighborCount = static_cast<unsigned int>(
      (static_cast<int>(previewNeighborCount) + direction + 9) % 9);
    changed = previewNeighborCount != previousCount;
    previewDirty = true;
  } else if (control == Control::PreviewNeighborhood) {
    const unsigned int previousPattern = previewNeighborhood;
    previewNeighborhood = static_cast<unsigned int>(
      (static_cast<int>(previewNeighborhood) + direction + 8) % 8);
    changed = previewNeighborhood != previousPattern;
    previewDirty = true;
  } else if (control == Control::PaletteState) {
    const unsigned int count =
      std::max(1u, static_cast<unsigned int>(familyDraft.stateNames.size()));
    const unsigned int previousState = paletteState;
    paletteState = static_cast<unsigned int>(
      (static_cast<int>(paletteState) + direction + static_cast<int>(count)) %
      static_cast<int>(count));
    changed = paletteState != previousState;
  } else if ((control == Control::Red || control == Control::Green ||
              control == Control::Blue) &&
             paletteState < familyDraft.stateColors.size()) {
    const std::size_t channel = control == Control::Red     ? 0u
                                : control == Control::Green ? 1u
                                                            : 2u;
    const unsigned char previousColor =
      familyDraft.stateColors[paletteState][channel];
    const int color =
      std::clamp(static_cast<int>(previousColor) + direction * 8, 0, 255);
    familyDraft.stateColors[paletteState][channel] =
      static_cast<unsigned char>(color);
    changed = familyDraft.stateColors[paletteState][channel] != previousColor;
    familyChanged = familyChanged || changed;
  }
  if (changed) {
    if (control == Control::BirthCounts || control == Control::SurvivalCounts) {
      previewDirty = true;
    }
    triggerValuePulse();
  }
  return changed;
}

void
RulesetWorkshopMenu::changeSelected(int direction)
{
  changeControl(controlForRow(selectedRow), direction);
  rebuildVisual();
}
void
RulesetWorkshopMenu::cycleStarterRule(int direction)
{
  if (starterRuleIds.empty()) {
    return;
  }
  const int count = static_cast<int>(starterRuleIds.size());
  starterRuleIndex = (starterRuleIndex + direction + count) % count;
  const RuleSetDefinition* definition =
    RuleSetRegistry::instance().getRuleSetDefinition(
      starterRuleIds[static_cast<std::size_t>(starterRuleIndex)]);
  if (definition == nullptr) {
    return;
  }
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(definition->familyId);
  if (family == nullptr) {
    return;
  }
  familyDraft = *family;
  familyChanged = false;
  draft = *definition;
  draft.id = uniqueCustomId("CUSTOM_", definition->id, false);
  draft.builtIn = false;
  draft.name = "Custom " + definition->name;
  draft.familyId = familyDraft.id;
  const unsigned int validStateCount = std::max(1u, familyDraft.stateCount);
  paletteState = std::min(paletteState, validStateCount - 1u);
  previewState = std::min(previewState, validStateCount - 1u);
  previewDirty = true;
  rebuildRows();
}

bool
RulesetWorkshopMenu::selectFamily(const std::string& familyId)
{
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(familyId);
  if (family == nullptr || familyDraft.id == family->id) {
    return false;
  }

  const std::vector<RuleSetDefinition>& definitions =
    RuleSetRegistry::instance().getDefinitions();
  const RuleSetDefinition* starter = nullptr;
  for (std::size_t index = 0u; index < definitions.size(); ++index) {
    if (definitions[index].familyId == family->id) {
      starter = &definitions[index];
      break;
    }
  }
  if (starter == nullptr) {
    return false;
  }

  const std::string rulesetId = draft.id;
  const std::string rulesetName = draft.name;
  familyDraft = *family;
  familyChanged = false;
  starterRuleIds.clear();
  for (const RuleSetDefinition& definition : definitions) {
    if (definition.familyId == familyDraft.id) {
      starterRuleIds.push_back(definition.id);
    }
  }
  draft = *starter;
  draft.id = rulesetId;
  draft.name = rulesetName;
  draft.familyId = familyDraft.id;
  draft.builtIn = false;
  starterRuleIndex = 0;
  for (std::size_t index = 0u; index < starterRuleIds.size(); ++index) {
    if (starterRuleIds[index] == starter->id) {
      starterRuleIndex = static_cast<int>(index);
      break;
    }
  }
  const unsigned int validStateCount = std::max(1u, familyDraft.stateCount);
  paletteState = std::min(paletteState, validStateCount - 1u);
  previewState = std::min(previewState, validStateCount - 1u);
  previewDirty = true;
  rebuildRows();
  return true;
}

void
RulesetWorkshopMenu::resizeGenerationStates(unsigned int count)
{
  familyDraft.stateNames.resize(count);
  familyDraft.stateColors.resize(count);
  for (unsigned int state = 0u; state < count; ++state) {
    if (familyDraft.stateNames[state].empty()) {
      familyDraft.stateNames[state] = "State " + std::to_string(state);
    }
    if (state >= familyDraft.stateCount) {
      familyDraft.stateColors[state] = { 180u, 180u, 180u };
    }
  }
  if (count >= 2u) {
    familyDraft.stateNames[1] = "Background";
    familyDraft.stateColors[1] = { 255u, 255u, 255u };
  }
  familyDraft.stateCount = count;
  familyChanged = true;
  paletteState = std::min(paletteState, count - 1u);
  previewState = std::min(previewState, count - 1u);
  previewDirty = true;
}

void
RulesetWorkshopMenu::refreshPreview()
{
  if (!previewDirty) {
    return;
  }
  previewDirty = false;
  RuleSetRegistry previewRegistry;
  if (!previewRegistry.registerFamily(familyDraft) ||
      !previewRegistry.registerRule(draft)) {
    previewText = "invalid draft";
    return;
  }
  std::unique_ptr<RuleSet> rule =
    previewRegistry.createRuleSet(draft.id, nullptr);
  if (rule == nullptr) {
    previewText = "preview unavailable";
    return;
  }
  if (familyDraft.kind == RuleFamily::Elementary1D) {
    const unsigned char left =
      static_cast<unsigned char>((previewNeighborhood >> 2u) & 1u);
    const unsigned char current =
      static_cast<unsigned char>((previewNeighborhood >> 1u) & 1u);
    const unsigned char right =
      static_cast<unsigned char>(previewNeighborhood & 1u);
    previewInputState = current;
    const unsigned char next = rule->nextElementary(left, current, right);
    previewOutputState = next;
    previewText = std::to_string(left) + std::to_string(current) +
                  std::to_string(right) + " -> " +
                  stateLabel(familyDraft, next);
    return;
  }
  previewState =
    std::min(previewState, std::max(1u, familyDraft.stateCount) - 1u);
  previewInputState = static_cast<unsigned char>(previewState);
  unsigned char next = 1u;
  if (familyDraft.kind == RuleFamily::Cyclic) {
    RuleSet::NeighborStateCounts counts{};
    const unsigned int successor =
      (previewState + draft.cyclicStep) % familyDraft.stateCount;
    counts[successor] = static_cast<unsigned char>(previewNeighborCount);
    next = rule->nextStateFromNeighborhood(
      static_cast<unsigned char>(previewState), counts);
  } else if (familyDraft.kind == RuleFamily::SpeciesLife) {
    RuleSet::NeighborStateCounts counts{};
    unsigned int speciesState = previewState;
    if (speciesState == 1u) {
      speciesState = 0u;
    }
    counts[speciesState] = static_cast<unsigned char>(previewNeighborCount);
    next = rule->nextStateFromNeighborhood(
      static_cast<unsigned char>(previewState), counts);
  } else if (familyDraft.kind == RuleFamily::LargerThanLife) {
    next = rule->nextStateFromExtendedCount(
      static_cast<unsigned char>(previewState), previewNeighborCount);
  } else if (familyDraft.kind == RuleFamily::Hodgepodge ||
             familyDraft.kind == RuleFamily::Dominance) {
    RuleSet::NeighborStateCounts counts{};
    counts[0] = static_cast<unsigned char>(previewNeighborCount);
    next = rule->nextStateFromNeighborhood(
      static_cast<unsigned char>(previewState), counts);
  } else if (familyDraft.kind == RuleFamily::Turmite ||
             familyDraft.kind == RuleFamily::LatticeGas) {
    RuleSet::DirectionalNeighbors neighbors{};
    neighbors.fill(1u);
    next = rule->nextStateFromDirectionalNeighborhood(
      static_cast<unsigned char>(previewState), neighbors);
  } else {
    next = rule->nextState(static_cast<unsigned char>(previewState),
                           static_cast<unsigned char>(previewNeighborCount));
  }
  previewOutputState = next;
  previewText =
    stateLabel(familyDraft, previewState) + " + " +
    std::to_string(previewNeighborCount) +
    (familyDraft.kind == RuleFamily::Cyclic ? " successor neighbors -> "
                                            : " live neighbors -> ") +
    stateLabel(familyDraft, next);
}

std::string
RulesetWorkshopMenu::valueForControl(Control control) const
{
  if (control == Control::StarterRule) {
    if (starterRuleIndex >= 0 &&
        static_cast<std::size_t>(starterRuleIndex) < starterRuleIds.size()) {
      const RuleSetDefinition* definition =
        RuleSetRegistry::instance().getRuleSetDefinition(
          starterRuleIds[static_cast<std::size_t>(starterRuleIndex)]);
      if (definition != nullptr) {
        return definition->name;
      }
    }
    return draft.name;
  }
  if (control == Control::Family) {
    return familyDraft.name + "  /  " + familyLabel(familyDraft.kind);
  }
  if (control == Control::FamilyName) {
    return familyDraft.name.empty() ? "Type a family name" : familyDraft.name;
  }
  if (control == Control::ModelKind) {
    return familyLabel(familyDraft.kind);
  }
  if (control == Control::RulesetName) {
    return draft.name.empty() ? "Type a rule name" : draft.name;
  }
  if (control == Control::GenerationStates) {
    return std::to_string(familyDraft.stateCount) + " states";
  }
  if (control == Control::CyclicThreshold) {
    return std::to_string(draft.cyclicThreshold) + " neighbors";
  }
  if (control == Control::CyclicStep) {
    return "+" + std::to_string(draft.cyclicStep) + " state";
  }
  if (control == Control::WolframNumber) {
    return std::to_string(draft.ruleNumber);
  }
  if (control == Control::PreviewState) {
    return stateLabel(familyDraft, previewState);
  }
  if (control == Control::PreviewNeighbors) {
    return std::to_string(previewNeighborCount) + " of 8";
  }
  if (control == Control::PreviewNeighborhood) {
    return std::to_string((previewNeighborhood >> 2u) & 1u) +
           std::to_string((previewNeighborhood >> 1u) & 1u) +
           std::to_string(previewNeighborhood & 1u);
  }
  if (control == Control::PaletteState) {
    return stateLabel(familyDraft, paletteState);
  }
  if (control == Control::StateName &&
      paletteState < familyDraft.stateNames.size()) {
    return familyDraft.stateNames[paletteState].empty()
             ? "Type a state label"
             : familyDraft.stateNames[paletteState];
  }
  if (control == Control::Red || control == Control::Green ||
      control == Control::Blue) {
    if (paletteState < familyDraft.stateColors.size()) {
      const std::size_t channel = control == Control::Red     ? 0u
                                  : control == Control::Green ? 1u
                                                              : 2u;
      return std::to_string(familyDraft.stateColors[paletteState][channel]);
    }
  }
  if (control == Control::Import) {
    return "Choose a JSON file";
  }
  if (control == Control::Export) {
    return "Save this rule as JSON";
  }
  return {};
}

std::string
RulesetWorkshopMenu::helpForControl(Control control) const
{
  if (control == Control::StarterRule) {
    return "Choose a starting rule. The active rule stays unchanged.";
  }
  if (control == Control::Family) {
    return "Choose a compatible cell schema. Rules stay bound to one family.";
  }
  if (control == Control::FamilyName) {
    return "Name this cell schema; shared custom families update their rules "
           "together.";
  }
  if (control == Control::ModelKind) {
    return "The model chooses how this family interprets transitions.";
  }
  if (control == Control::RulesetName) {
    return "Type to rename this draft; Backspace removes a character.";
  }
  if (control == Control::BirthCounts || control == Control::SurvivalCounts) {
    return "Select a count chip, then press Enter to turn it on or off.";
  }
  if (control == Control::GenerationStates) {
    return "Change the length of this rule's decay sequence.";
  }
  if (control == Control::CyclicThreshold) {
    return "Advance when this many neighbors hold the successor state.";
  }
  if (control == Control::CyclicStep) {
    return "Choose which state each cell chases around the full cycle.";
  }
  if (control == Control::WolframNumber) {
    return "Adjust the elementary rule number in steps of two.";
  }
  if (control == Control::PreviewState) {
    return "Choose the cell state used in the example transition.";
  }
  if (control == Control::PreviewNeighbors) {
    return familyDraft.kind == RuleFamily::Cyclic
             ? "Choose how many neighbors hold the successor state."
             : "Choose how many of the eight neighbors are active.";
  }
  if (control == Control::PreviewNeighborhood) {
    return "Choose a left-center-right pattern from 000 to 111.";
  }
  if (control == Control::PaletteState) {
    return "Choose which state's label and color to edit.";
  }
  if (control == Control::StateName) {
    return "Type a short label for the selected state.";
  }
  if (control == Control::Red || control == Control::Green ||
      control == Control::Blue) {
    return "Adjust the selected state's color channel.";
  }
  if (control == Control::Import) {
    return "Load a rule from JSON into this draft.";
  }
  if (control == Control::Export) {
    return "Export the staged rule as a JSON file.";
  }
  if (control == Control::Apply) {
    return "Validate, save, then activate this custom rule.";
  }
  if (control == Control::Discard) {
    return "Close the workshop and keep the active rule unchanged.";
  }
  return {};
}

void
RulesetWorkshopMenu::rebuildVisual()
{
  updateLayout();
  visual.clearPrimitives();
  if (!openState) {
    return;
  }
  refreshPreview();
  int width = 1280;
  int height = 720;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float virtualWidth = static_cast<float>(width) / layoutScale;
  const float virtualHeight = static_cast<float>(height) / layoutScale;
  const float reveal = panelReveal();
  const float animatedPanelY = panelY + panelOffsetY();
  const float animatedFirstRowY = firstRowY + panelOffsetY();
  const unsigned char backdropOpacity = static_cast<unsigned char>(std::round(
    (reducedMotion ? 1.0f : std::clamp(animationElapsed / 0.18f, 0.0f, 1.0f)) *
    255.0f));
  const unsigned char panelOpacity =
    static_cast<unsigned char>(std::round(reveal * 255.0f));
  const float breathe =
    reducedMotion ? 0.5f : 0.5f + 0.5f * std::sin(ambientPhase * 1.04719755f);

  visual.addFilledRect(
    0.0f,
    0.0f,
    virtualWidth,
    virtualHeight,
    UiTheme::applyOpacity(UiTheme::canvasShade(), backdropOpacity));
  GuiKit::drawRoundedPanel(
    visual, panelX, animatedPanelY, panelWidth, panelHeight, panelOpacity);
  GuiKit::drawRoundedRect(
    visual,
    panelX + 20.0f,
    animatedFirstRowY - 5.0f,
    (panelWidth - 40.0f) * reveal,
    2.0f,
    1.0f,
    UiTheme::applyOpacity(UiTheme::accentCool(), panelOpacity));

  const float titleFont = std::clamp(panelWidth * 0.038f, 16.0f, 26.0f);
  const float smallFont = panelHeight >= 400.0f ? 12.0f : 10.0f;
  visual.addText("F2  /  RULESET WORKSHOP",
                 panelX + 28.0f,
                 animatedPanelY + 14.0f,
                 11.0f,
                 UiTheme::applyOpacity(UiTheme::accentCool(), panelOpacity));
  visual.addText("RULESET WORKSHOP",
                 panelX + 28.0f,
                 animatedPanelY + 31.0f,
                 titleFont,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), panelOpacity));
  visual.addText(draft.name,
                 panelX + 30.0f,
                 animatedPanelY + (headerHeight >= 90.0f ? 63.0f : 52.0f),
                 14.0f,
                 UiTheme::applyOpacity(UiTheme::textMuted(), panelOpacity));

  const std::string family = familyLabel(familyDraft.kind);
  const float familyChipWidth =
    std::max(92.0f, static_cast<float>(family.size()) * 7.0f + 18.0f);
  const float stateChipWidth = 88.0f;
  const float chipGap = 7.0f;
  const float chipGroupWidth = familyChipWidth + stateChipWidth + chipGap;
  const float familyChipX = panelX + panelWidth - chipGroupWidth - 24.0f;
  const float chipY = animatedPanelY + 18.0f;
  if (panelWidth >= chipGroupWidth + 56.0f) {
    GuiKit::drawRoundedRect(
      visual,
      familyChipX,
      chipY,
      familyChipWidth,
      25.0f,
      7.0f,
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    visual.addText(family,
                   familyChipX + 8.0f,
                   chipY + 7.0f,
                   11.0f,
                   UiTheme::applyOpacity(UiTheme::accentCool(), panelOpacity));
    const float stateChipX = familyChipX + familyChipWidth + chipGap;
    GuiKit::drawRoundedRect(
      visual,
      stateChipX,
      chipY,
      stateChipWidth,
      25.0f,
      7.0f,
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    visual.addText(std::to_string(familyDraft.stateCount) + " STATES",
                   stateChipX + 8.0f,
                   chipY + 7.0f,
                   11.0f,
                   UiTheme::applyOpacity(UiTheme::textPrimary(), panelOpacity));
  }
  if (headerHeight >= 78.0f) {
    visual.addText(
      "ARROWS / W,S MOVE   LEFT / RIGHT CHANGE   ENTER SELECT   ESC CLOSE",
      panelX + 30.0f,
      animatedPanelY + headerHeight - 15.0f,
      smallFont,
      UiTheme::applyOpacity(UiTheme::textMuted(), panelOpacity));
  }

  const float rowFontSize = std::clamp(rowHeight * 0.48f, 15.0f, 17.0f);
  const float valueLeft = panelX + panelWidth * 0.53f;
  const float valueRight = panelX + panelWidth - 36.0f;
  const float valueWidth = std::max(24.0f, valueRight - valueLeft);
  for (int bodyRow = firstVisibleRow;
       bodyRow < firstVisibleRow + visibleRows && bodyRow < bodyRowCount;
       ++bodyRow) {
    const MenuRow& row = rows[static_cast<std::size_t>(bodyRow)];
    const float rowY =
      animatedFirstRowY +
      rowHeight * static_cast<float>(bodyRow - firstVisibleRow);
    const unsigned char rowOpacity = static_cast<unsigned char>(
      std::round(rowReveal(bodyRow) * static_cast<float>(panelOpacity)));
    const float textY = rowY + std::max(2.0f, (rowHeight - rowFontSize) * 0.5f);
    const bool selected = bodyRow == selectedRow;
    if (row.kind == RowKind::Section) {
      visual.addText(row.label,
                     panelX + 28.0f,
                     textY,
                     std::clamp(rowFontSize * 0.78f, 11.0f, 13.0f),
                     UiTheme::applyOpacity(UiTheme::accentCool(), rowOpacity));
      const float dividerX =
        panelX + 34.0f + static_cast<float>(row.label.size()) * 7.2f;
      visual.addFilledRect(
        dividerX,
        rowY + rowHeight * 0.55f,
        std::max(0.0f, panelX + panelWidth - 56.0f - dividerX),
        1.0f,
        UiTheme::applyOpacity(UiTheme::panelBorder(), rowOpacity));
      continue;
    }

    if (row.kind == RowKind::Information) {
      GuiKit::drawRoundedRect(
        visual,
        panelX + 22.0f,
        rowY + 1.0f,
        panelWidth - 44.0f,
        rowHeight - 3.0f,
        7.0f,
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
      if (row.control == Control::PreviewResult) {
        const float swatchSize = std::clamp(rowHeight * 0.42f, 11.0f, 16.0f);
        const float swatchY = rowY + (rowHeight - swatchSize) * 0.5f;
        GuiKit::drawRoundedRect(
          visual,
          valueLeft,
          swatchY,
          swatchSize,
          swatchSize,
          4.0f,
          UiTheme::applyOpacity(stateColor(familyDraft, previewInputState),
                                rowOpacity));
        GuiKit::drawRoundedRect(
          visual,
          valueLeft + swatchSize + 5.0f,
          swatchY,
          swatchSize,
          swatchSize,
          4.0f,
          UiTheme::applyOpacity(stateColor(familyDraft, previewOutputState),
                                rowOpacity));
        visual.addText(row.label,
                       panelX + 34.0f,
                       textY,
                       rowFontSize,
                       UiTheme::applyOpacity(UiTheme::textMuted(), rowOpacity));
        const float previewFont = std::clamp(rowFontSize * 0.78f, 11.0f, 13.0f);
        visual.addText(
          previewText,
          valueLeft + swatchSize * 2.0f + 14.0f,
          rowY + std::max(2.0f, (rowHeight - previewFont) * 0.5f),
          previewFont,
          UiTheme::applyOpacity(UiTheme::textPrimary(), rowOpacity));
      } else {
        visual.addText(
          row.label,
          panelX + 34.0f,
          textY,
          rowFontSize,
          UiTheme::applyOpacity(UiTheme::textPrimary(), rowOpacity));
        visual.addText(row.detail,
                       valueLeft,
                       textY,
                       std::clamp(rowFontSize * 0.78f, 11.0f, 13.0f),
                       UiTheme::applyOpacity(UiTheme::textMuted(), rowOpacity));
      }
      continue;
    }

    GuiKit::drawRoundedRect(
      visual,
      panelX + 20.0f,
      rowY + 2.0f,
      panelWidth - 40.0f,
      rowHeight - 4.0f,
      8.0f,
      UiTheme::applyOpacity(
        selected ? UiTheme::selection() : UiTheme::menuCard(), rowOpacity));
    GuiKit::drawRoundedRect(
      visual,
      panelX + 20.0f,
      rowY + 1.0f,
      panelWidth - 40.0f,
      rowHeight - 5.0f,
      8.0f,
      UiTheme::applyOpacity(
        selected ? UiTheme::accentCool() : UiTheme::panelBorder(),
        static_cast<unsigned char>(
          selected ? rowOpacity * (0.62f + 0.18f * breathe) : rowOpacity)));
    if (selected && valuePulse() > 0.0f) {
      GuiKit::drawRoundedRect(
        visual,
        panelX + 21.0f,
        rowY + 2.0f,
        panelWidth - 42.0f,
        rowHeight - 7.0f,
        7.0f,
        UiTheme::applyOpacity(
          UiTheme::accentCool(),
          static_cast<unsigned char>(rowOpacity * valuePulse() * 0.16f)));
    }
    visual.addText(row.label,
                   panelX + 34.0f,
                   textY,
                   rowFontSize,
                   UiTheme::applyOpacity(selected ? UiTheme::accentCool()
                                                  : UiTheme::textPrimary(),
                                         rowOpacity));

    const Control control = row.control;
    if (control == Control::BirthCounts || control == Control::SurvivalCounts) {
      const unsigned int mask =
        control == Control::BirthCounts ? draft.birthMask : draft.surviveMask;
      const float gap = 3.0f;
      const float chipWidth = std::min(25.0f, (valueWidth - gap * 8.0f) / 9.0f);
      const float chipsWidth = chipWidth * 9.0f + gap * 8.0f;
      const float countChipX = valueLeft + (valueWidth - chipsWidth) * 0.5f;
      const bool countRowFocused = selected;
      for (unsigned int count = 0u; count <= 8u; ++count) {
        const bool enabled = ((mask >> count) & 1u) != 0u;
        const bool cursor = countRowFocused && count == neighborCount;
        const ColorRgba chipColor =
          enabled ? UiTheme::accentCool() : UiTheme::panelBorder();
        const float currentX = countChipX + (chipWidth + gap) * count;
        GuiKit::drawRoundedRect(
          visual,
          currentX,
          rowY + 4.0f,
          chipWidth,
          rowHeight - 10.0f,
          5.0f,
          UiTheme::applyOpacity(cursor ? UiTheme::selection() : chipColor,
                                rowOpacity));
        visual.addText(std::to_string(count),
                       currentX + std::max(3.0f, (chipWidth - 8.0f) * 0.5f),
                       textY,
                       rowFontSize,
                       UiTheme::applyOpacity(enabled ? UiTheme::textPrimary()
                                                     : UiTheme::textMuted(),
                                             rowOpacity));
      }
    } else if (control == Control::RulesetName ||
               control == Control::FamilyName ||
               control == Control::StateName) {
      GuiKit::drawRoundedRect(
        visual,
        valueLeft,
        rowY + 4.0f,
        valueWidth,
        rowHeight - 10.0f,
        5.0f,
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
      const std::string value = valueForControl(control);
      const std::string editHint =
        control == Control::StateName ? "EDIT" : "TYPE";
      const bool placeholder =
        (control == Control::RulesetName && draft.name.empty()) ||
        (control == Control::FamilyName && familyDraft.name.empty()) ||
        (control == Control::StateName &&
         paletteState < familyDraft.stateNames.size() &&
         familyDraft.stateNames[paletteState].empty());
      const std::string displayValue = value.substr(0u, 32u);
      const float fieldFont = std::clamp(rowFontSize * 0.86f, 13.0f, 15.0f);
      visual.addText(displayValue,
                     valueLeft + 8.0f,
                     textY,
                     fieldFont,
                     UiTheme::applyOpacity(placeholder ? UiTheme::textMuted()
                                                       : UiTheme::textPrimary(),
                                           rowOpacity));
      const bool caretVisible =
        reducedMotion || caretBlinkElapsed < kCaretBlinkPeriodSeconds * 0.55f;
      if (selected && caretVisible) {
        const float caretX = std::min(
          caretOriginAfterText(displayValue, valueLeft + 8.0f, fieldFont),
          valueRight - 44.0f);
        visual.addText(
          "|",
          caretX,
          textY,
          fieldFont,
          UiTheme::applyOpacity(UiTheme::accentCool(), rowOpacity));
      }
      visual.addText(editHint,
                     valueRight - 30.0f,
                     textY,
                     10.0f,
                     UiTheme::applyOpacity(UiTheme::accentCool(), rowOpacity));
    } else if (control == Control::Import || control == Control::Export) {
      const float actionWidth = std::min(valueWidth, 210.0f);
      drawActionButton(visual,
                       valueRight - actionWidth,
                       rowY + 4.0f,
                       actionWidth,
                       rowHeight - 10.0f,
                       control == Control::Import ? "IMPORT JSON"
                                                  : "EXPORT JSON",
                       UiTheme::accentCool(),
                       false,
                       rowOpacity);
    } else if (control == Control::Red || control == Control::Green ||
               control == Control::Blue) {
      const std::size_t channel = control == Control::Red     ? 0u
                                  : control == Control::Green ? 1u
                                                              : 2u;
      const unsigned char amount =
        paletteState < familyDraft.stateColors.size()
          ? familyDraft.stateColors[paletteState][channel]
          : 0u;
      const ColorRgba tint =
        control == Control::Red     ? ColorRgba{ 255, 88, 96, 255 }
        : control == Control::Green ? ColorRgba{ 76, 220, 158, 255 }
                                    : ColorRgba{ 88, 156, 255, 255 };
      const float stepperHeight = rowHeight - 9.0f;
      drawValueStepper(visual,
                       valueLeft,
                       rowY + 3.0f,
                       valueWidth,
                       stepperHeight,
                       valueForControl(control),
                       UiTheme::textPrimary(),
                       rowOpacity);
      const float barHeight = 3.0f;
      const float barY = rowY + rowHeight - barHeight - 2.0f;
      GuiKit::drawRoundedRect(
        visual,
        valueLeft + 3.0f,
        barY,
        valueWidth - 6.0f,
        barHeight,
        2.0f,
        UiTheme::applyOpacity(UiTheme::panelBorder(), rowOpacity));
      visual.addFilledRect(
        valueLeft + 3.0f,
        barY,
        std::max(0.0f, (valueWidth - 6.0f) * amount / 255.0f),
        barHeight,
        UiTheme::applyOpacity(tint, rowOpacity));
    } else {
      drawValueStepper(visual,
                       valueLeft,
                       rowY + 4.0f,
                       valueWidth,
                       rowHeight - 10.0f,
                       valueForControl(control),
                       selected ? UiTheme::accentCool()
                                : UiTheme::textPrimary(),
                       rowOpacity);
      if (control == Control::PaletteState &&
          paletteState < familyDraft.stateColors.size()) {
        const float swatchSize = std::clamp(rowHeight * 0.42f, 11.0f, 16.0f);
        GuiKit::drawRoundedRect(
          visual,
          valueLeft - swatchSize - 8.0f,
          rowY + (rowHeight - swatchSize) * 0.5f,
          swatchSize,
          swatchSize,
          4.0f,
          UiTheme::applyOpacity(
            stateColor(familyDraft, static_cast<unsigned char>(paletteState)),
            rowOpacity));
      }
    }
  }

  if (bodyRowCount > visibleRows) {
    const float trackTop = animatedFirstRowY;
    const float trackHeight = rowHeight * static_cast<float>(visibleRows);
    const float thumbHeight =
      std::max(12.0f,
               trackHeight * static_cast<float>(visibleRows) /
                 static_cast<float>(bodyRowCount));
    const float trackTravel = std::max(0.0f, trackHeight - thumbHeight);
    const float maxOffset =
      static_cast<float>(std::max(1, bodyRowCount - visibleRows));
    const float thumbY =
      trackTop + trackTravel * static_cast<float>(firstVisibleRow) / maxOffset;
    visual.addFilledRect(
      panelX + panelWidth - 12.0f,
      trackTop,
      3.0f,
      trackHeight,
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    visual.addFilledRect(
      panelX + panelWidth - 12.0f,
      thumbY,
      3.0f,
      thumbHeight,
      UiTheme::applyOpacity(UiTheme::accent(), panelOpacity));
  }

  const float footerY = animatedPanelY + panelHeight - footerHeight;
  visual.addFilledRect(
    panelX + 20.0f,
    footerY,
    panelWidth - 40.0f,
    1.0f,
    UiTheme::applyOpacity(UiTheme::panelBorder(), panelOpacity));
  const Control focusedControl = controlForRow(selectedRow);
  const std::string footerMessage =
    errorMessage.empty() ? helpForControl(focusedControl) : errorMessage;
  const ColorRgba footerColor =
    errorMessage.empty() ? UiTheme::textMuted() : UiTheme::error();
  if (footerHeight >= 65.0f) {
    visual.addText(footerMessage.substr(0u, 100u),
                   panelX + 28.0f,
                   footerY + 7.0f,
                   std::clamp(smallFont * 0.95f, 10.0f, 12.0f),
                   UiTheme::applyOpacity(footerColor, panelOpacity));
  }
  const float buttonGap = 8.0f;
  const float buttonX = panelX + 28.0f;
  const float buttonY = footerY + (footerHeight >= 65.0f ? 34.0f : 10.0f);
  const float buttonHeight = footerHeight >= 65.0f ? 34.0f : 27.0f;
  const float buttonWidth = (panelWidth - 56.0f - buttonGap) * 0.5f;
  drawActionButton(visual,
                   buttonX,
                   buttonY,
                   buttonWidth,
                   buttonHeight,
                   "SAVE & APPLY",
                   UiTheme::success(),
                   focusedControl == Control::Apply,
                   panelOpacity);
  drawActionButton(visual,
                   buttonX + buttonWidth + buttonGap,
                   buttonY,
                   buttonWidth,
                   buttonHeight,
                   "DISCARD",
                   UiTheme::warning(),
                   focusedControl == Control::Discard,
                   panelOpacity);
  setVisible(true);
}
bool
RulesetWorkshopMenu::AppendCommands(Renderer* targetRenderer)
{
  if (!isVisible()) {
    return true;
  }
  return visual.AppendCommands(targetRenderer);
}
