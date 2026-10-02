#include "RulesetWorkshopMenu.h"
#include "CSimSounds.h"
#include "RuleCatalogLoader.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiPointerHint.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <queue>

namespace {

ColorRgba
stateColor(const RuleFamilyDefinition& definition, unsigned int state)
{
  if (static_cast<std::size_t>(state) >= definition.stateColors.size()) {
    return ColorRgba{ 160, 160, 160, 255 };
  }
  const std::array<unsigned char, 3>& rgb = definition.stateColors[state];
  return ColorRgba{ rgb[0], rgb[1], rgb[2], 255 };
}

// Ink that stays legible on top of `fill`: near black on light colors, near
// white on dark ones.
ColorRgba
inkOn(ColorRgba fill)
{
  const float luminance = 0.299f * static_cast<float>(fill.r) +
                          0.587f * static_cast<float>(fill.g) +
                          0.114f * static_cast<float>(fill.b);
  return luminance > 150.0f ? ColorRgba{ 10, 18, 30, 255 }
                            : ColorRgba{ 236, 244, 252, 255 };
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
    case RuleFamily::VonNeumannTable:
      return "VON NEUMANN TABLE";
    case RuleFamily::Sandpile:
      return "ABELIAN SANDPILE";
    case RuleFamily::Lenia:
      return "LENIA CONTINUOUS";
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

// Families whose transitions read north/east/south/west neighbors rather than
// a neighbor count; their preview assumes empty neighbors.
bool
isDirectionalFamily(RuleFamily family)
{
  return family == RuleFamily::Turmite || family == RuleFamily::LatticeGas ||
         family == RuleFamily::VonNeumannTable ||
         family == RuleFamily::Sandpile;
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

std::string
countDigits(unsigned int mask)
{
  std::string digits;
  for (unsigned int count = 0u; count <= 8u; ++count) {
    if (((mask >> count) & 1u) != 0u) {
      digits.push_back(static_cast<char>('0' + count));
    }
  }
  return digits;
}

// The state a cell in `state` becomes with `count` neighbors, as the preview
// models each family: count-based families count `count` neighbors in the
// state their rule watches (the successor for cyclic rules, the live species
// for colorized Life); Lenia reads `count` as eighths of `fullPotential`;
// directional families see empty neighbors.
unsigned char
previewTransition(const RuleSet& rule,
                  RuleFamily family,
                  unsigned int state,
                  unsigned int count,
                  std::uint64_t fullPotential)
{
  const unsigned char cell = static_cast<unsigned char>(state);
  if (family == RuleFamily::Cyclic) {
    RuleSet::NeighborStateCounts counts{};
    counts[rule.getExtendedCountedState(cell)] =
      static_cast<unsigned char>(count);
    return rule.nextStateFromNeighborhood(cell, counts);
  }
  if (family == RuleFamily::SpeciesLife) {
    RuleSet::NeighborStateCounts counts{};
    counts[state == 1u ? 0u : state] = static_cast<unsigned char>(count);
    return rule.nextStateFromNeighborhood(cell, counts);
  }
  if (family == RuleFamily::LargerThanLife) {
    return rule.nextStateFromExtendedCount(cell, count);
  }
  if (family == RuleFamily::Lenia) {
    return rule.nextStateFromPotential(cell, fullPotential * count / 8u);
  }
  if (family == RuleFamily::Hodgepodge || family == RuleFamily::Dominance) {
    RuleSet::NeighborStateCounts counts{};
    counts[0] = static_cast<unsigned char>(count);
    return rule.nextStateFromNeighborhood(cell, counts);
  }
  if (isDirectionalFamily(family)) {
    RuleSet::DirectionalNeighbors neighbors{};
    neighbors.fill(1u);
    return rule.nextStateFromDirectionalNeighborhood(cell, neighbors);
  }
  return rule.nextState(cell, static_cast<unsigned char>(count));
}

// A color swatch with a hairline rim, so black and navy states still read
// against the dark glass.
void
drawSwatch(GameVisual& visual,
           float x,
           float y,
           float width,
           float height,
           float radius,
           ColorRgba fill,
           unsigned char opacity)
{
  GuiKit::drawRoundedRect(
    visual,
    x - 1.0f,
    y - 1.0f,
    width + 2.0f,
    height + 2.0f,
    radius + 1.0f,
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::panelBorder(),
                                       ColorRgba{ 150, 176, 204, 255 },
                                       0.5f),
                          opacity));
  GuiKit::drawRoundedRect(
    visual, x, y, width, height, radius, UiTheme::applyOpacity(fill, opacity));
}

// An inset value box with drawn chevrons at both ends; the arrow on the side
// the value just moved leans out (`nudge` is signed, -1..1, already scaled by
// the pulse) and the value slides with it.
void
drawValueStepper(GameVisual& visual,
                 float x,
                 float y,
                 float width,
                 float height,
                 const std::string& value,
                 ColorRgba ink,
                 float focus,
                 unsigned char opacity,
                 float nudge)
{
  const ColorRgba cyan = UiTheme::accentCool();
  GuiKit::drawRoundedGradientRect(
    visual,
    x,
    y,
    width,
    height,
    6.0f,
    UiTheme::applyOpacity(ColorRgba{ 5, 11, 22, 230 }, opacity),
    UiTheme::applyOpacity(UiTheme::panelInset(), opacity));
  const float centerY = y + height * 0.5f;
  const ColorRgba arrow = UiTheme::applyOpacity(
    UiTheme::fade(cyan, 0.4f + 0.6f * std::clamp(focus, 0.0f, 1.0f)), opacity);
  const float leftLean = nudge < 0.0f ? nudge * 4.0f : 0.0f;
  const float rightLean = nudge > 0.0f ? nudge * 4.0f : 0.0f;
  GuiKit::drawSideChevron(
    visual, x + 10.0f + leftLean, centerY, 4.5f, -4.0f, 1.8f, arrow);
  GuiKit::drawSideChevron(
    visual, x + width - 10.0f + rightLean, centerY, 4.5f, 4.0f, 1.8f, arrow);
  const float valueFont = std::clamp(height * 0.58f, 12.0f, 14.0f);
  const float valueWidth =
    GuiKit::measureEmphasizedText(value, valueFont, 0.0f);
  visual.addText(value,
                 x + std::max(20.0f, (width - valueWidth) * 0.5f) +
                   nudge * 6.0f,
                 y + std::max(2.0f, (height - valueFont) * 0.5f),
                 valueFont,
                 UiTheme::applyOpacity(ink, opacity));
}

// Glass action button tinted by its outcome; `emphasis` (0..1) floods the
// face with the tint and adds a glow as the button takes focus.
void
drawActionButton(GameVisual& visual,
                 float x,
                 float y,
                 float width,
                 float height,
                 const std::string& label,
                 ColorRgba tint,
                 float emphasis,
                 unsigned char opacity)
{
  const float e = std::clamp(emphasis, 0.0f, 1.0f);
  if (e > 0.01f) {
    GuiKit::drawRoundedBand(
      visual,
      x,
      y,
      width,
      height,
      8.0f,
      0.0f,
      10.0f,
      UiTheme::applyOpacity(UiTheme::fade(tint, 0.3f * e), opacity),
      UiTheme::transparentOf(tint));
  }
  GuiKit::drawRoundedRect(
    visual,
    x,
    y,
    width,
    height,
    8.0f,
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::fade(tint, 0.55f), tint, e),
                          opacity));
  GuiKit::drawRoundedGradientRect(
    visual,
    x + 1.0f,
    y + 1.0f,
    width - 2.0f,
    height - 2.0f,
    7.0f,
    UiTheme::applyOpacity(
      UiTheme::mix(UiTheme::mix(UiTheme::cardTop(), tint, 0.14f),
                   UiTheme::mix(UiTheme::cardTop(), tint, 0.62f),
                   e),
      opacity),
    UiTheme::applyOpacity(
      UiTheme::mix(UiTheme::mix(UiTheme::cardBottom(), tint, 0.08f),
                   UiTheme::mix(UiTheme::cardBottom(), tint, 0.42f),
                   e),
      opacity));
  const float fontSize = std::clamp(height * 0.42f, 12.0f, 15.0f);
  // The label thickens with focus, bolder still through the spring's
  // overshoot.
  const float weight = std::clamp(emphasis, 0.0f, 1.3f);
  const float textWidth =
    GuiKit::measureEmphasizedText(label, fontSize, weight);
  GuiKit::drawEmphasizedText(
    visual,
    label,
    x + std::max(6.0f, (width - textWidth) * 0.5f),
    y + std::max(2.0f, (height - fontSize) * 0.5f),
    fontSize,
    UiTheme::applyOpacity(UiTheme::mix(tint, UiTheme::textPrimary(), e),
                          opacity),
    weight);
}

} // namespace

RulesetWorkshopMenu::RulesetWorkshopMenu(IRenderWindow* targetWindow,
                                         Renderer* targetRenderer)
  : window(targetWindow)
  , renderer(targetRenderer)
{
  for (GameVisual& layer : layers) {
    layer.setSpace(PrimitiveSpace::Pixels);
    layer.setLayerHint(RenderLayerId::UI);
    layer.setWindow(window);
    layer.setRenderer(renderer);
    layer.prepare(renderer);
  }
  setVisible(false);
  rowFocus.configure(GuiMotion::kJelly);
  chipGlow.configure(GuiMotion::kBoing);
  footerFocus.configure(GuiMotion::kJelly);
  scrollThumb.configure(GuiMotion::kGlide);
}

void
RulesetWorkshopMenu::updateSprings(float deltaSeconds, bool snap)
{
  const int selectedBody = bodyIndexForRow(selectedRow);
  rowFocus.focusOnly(selectedBody,
                     std::min(bodyRowCount, GuiSpringArray::kCapacity));
  for (unsigned int count = 0u; count <= 8u; ++count) {
    chipGlow.setTarget(static_cast<int>(count),
                       ((draft.birthMask >> count) & 1u) != 0u ? 1.0f : 0.0f);
    chipGlow.setTarget(static_cast<int>(count) + 9,
                       ((draft.surviveMask >> count) & 1u) != 0u ? 1.0f : 0.0f);
  }
  const Control focused = controlForRow(selectedRow);
  const std::array<Control, kFooterButtonCount> footerControls = {
    Control::Import, Control::Export, Control::Apply, Control::Discard
  };
  for (int index = 0; index < kFooterButtonCount; ++index) {
    footerFocus.setTarget(
      index,
      focused == footerControls[static_cast<std::size_t>(index)] ? 1.0f : 0.0f);
  }
  scrollThumb.setTarget(static_cast<float>(firstVisibleRow));
  if (snap) {
    rowFocus.snapAll();
    chipGlow.snapAll();
    footerFocus.snapAll();
    scrollThumb.snapTo(scrollThumb.target());
    return;
  }
  const bool still = animator.reducedMotion();
  rowFocus.tick(deltaSeconds, still);
  chipGlow.tick(deltaSeconds, still);
  footerFocus.tick(deltaSeconds, still);
  scrollThumb.tick(deltaSeconds, still);
}

bool
RulesetWorkshopMenu::open(const RuleFamilyDefinition& currentFamily,
                          const RuleSetDefinition& currentRule,
                          bool useReducedMotion)
{
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.open");
  openState = false;
  setVisible(false);
  neighborCount = 3u;
  previewNeighborCount = 3u;
  previewNeighborhood = 2u;
  previewState = 0u;
  paletteState = 0u;
  dragControl = Control::None;
  setDraft(currentFamily, currentRule);
  if (starterRuleIds.empty()) {
    return false;
  }
  selectedRow = rowForControl(Control::StarterRule);
  firstVisibleRow = 0;
  animator.setReducedMotion(useReducedMotion);
  animator.restart();
  // Ignore the held click that opened the workshop until its first release.
  pointer.reset(true);
  tilt.level();
  errorMessage.clear();
  previewDirty = true;
  openState = true;
  setVisible(true);
  updateLayout();
  updateSprings(0.0f, true);
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
  for (GameVisual& layer : layers) {
    layer.clearPrimitives();
  }
  pointer.forgetPosition();
  dragControl = Control::None;
  dropdown.close();
  listControl = Control::None;
  errorMessage.clear();
}

void
RulesetWorkshopMenu::tick(float deltaSeconds)
{
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.tick");
  if (!openState) {
    return;
  }
  animator.tick(deltaSeconds);
  updateSprings(deltaSeconds, false);
  tilt.aim(pointer.x(),
           pointer.y(),
           panelFit.virtualWidth,
           panelFit.virtualHeight,
           openState);
  tilt.tick(deltaSeconds, animator.reducedMotion());
  dropdown.tick(deltaSeconds);
}

float
RulesetWorkshopMenu::getAnimationProgressForTesting() const
{
  return animator.openProgress();
}

float
RulesetWorkshopMenu::rowReveal(int row) const
{
  return animator.rowReveal(row, firstVisibleRow);
}

float
RulesetWorkshopMenu::selectionRowPosition() const
{
  // Footer actions park the highlight just past the last body row.
  const int selectedBodyRow = bodyIndexForRow(selectedRow);
  if (selectedBodyRow < 0) {
    return static_cast<float>(bodyRowCount);
  }
  return animator.selectionPosition(static_cast<float>(selectedBodyRow));
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
RulesetWorkshopMenu::getValuePulseForTesting() const
{
  return animator.valuePulse();
}

void
RulesetWorkshopMenu::updateLayout()
{
  panelFit = GuiPanelLayout::fit(window, renderer, nullptr);
  visualScale = GuiPanelLayout::visualScale(panelFit, renderer);
  const float virtualWidth = panelFit.virtualWidth;
  const float virtualHeight = panelFit.virtualHeight;
  panelWidth = std::min(820.0f, std::max(200.0f, virtualWidth - 24.0f));
  panelHeight = std::min(700.0f, std::max(160.0f, virtualHeight - 20.0f));
  panelWidth = std::min(panelWidth, virtualWidth);
  panelHeight = std::min(panelHeight, virtualHeight);
  // The layout origin swivels with the body layer, so hit testing and
  // drawing agree while the panel tilts toward the pointer.
  panelX =
    std::max(0.0f, (virtualWidth - panelWidth) * 0.5f) + tilt.bodyShiftX();
  panelY =
    std::max(0.0f, (virtualHeight - panelHeight) * 0.5f) + tilt.bodyShiftY();

  headerHeight = panelHeight >= 500.0f
                   ? 96.0f
                   : std::clamp(panelHeight * 0.19f, 52.0f, 96.0f);
  footerHeight = panelHeight >= 400.0f
                   ? 84.0f
                   : std::clamp(panelHeight * 0.28f, 44.0f, 84.0f);
  firstRowY = panelY + headerHeight;
  const float availableRowsHeight =
    std::max(1.0f, panelHeight - headerHeight - footerHeight - 4.0f);
  visibleRows = GuiPanelLayout::visibleRowCount(availableRowsHeight,
                                                std::max(1, bodyRowCount));
  rowHeight = availableRowsHeight / static_cast<float>(visibleRows);
  firstVisibleRow = GuiPanelLayout::clampFirstVisibleRow(
    firstVisibleRow, bodyRowCount, visibleRows);
}

float
RulesetWorkshopMenu::valueLeft() const
{
  return panelX + panelWidth * 0.53f;
}

float
RulesetWorkshopMenu::valueRight() const
{
  return panelX + panelWidth - 36.0f;
}

float
RulesetWorkshopMenu::sliderTrackLeft() const
{
  return valueLeft() + 8.0f;
}

float
RulesetWorkshopMenu::sliderTrackRight() const
{
  // The channel's number reads in a fixed column right of the track.
  return std::max(sliderTrackLeft() + 8.0f, valueRight() - 48.0f);
}

void
RulesetWorkshopMenu::chipStrip(int count,
                               float* firstX,
                               float* chipWidth,
                               float* gap) const
{
  // Nine-chip strips (B/S counts and the preview) share one width, so their
  // columns line up down the panel; shorter strips may grow wider chips.
  const float spacing = 3.0f;
  const float width = std::max(24.0f, valueRight() - valueLeft());
  const int chips = std::max(1, count);
  const float widest = chips >= 9 ? 25.0f : 36.0f;
  const float chip =
    std::min(widest,
             (width - spacing * static_cast<float>(chips - 1)) /
               static_cast<float>(chips));
  const float total =
    chip * static_cast<float>(chips) + spacing * static_cast<float>(chips - 1);
  *firstX = valueLeft() + (width - total) * 0.5f;
  *chipWidth = chip;
  *gap = spacing;
}

std::array<float, 4>
RulesetWorkshopMenu::footerButtonRect(int index) const
{
  const float animatedPanelY = panelY + animator.panelOffsetY();
  const float footerY = animatedPanelY + panelHeight - footerHeight;
  const bool roomy = footerHeight >= 70.0f;
  const float y = footerY + (roomy ? 40.0f : 9.0f);
  const float height = roomy ? 34.0f : 27.0f;
  const float gap = 8.0f;
  const float left = panelX + 28.0f;
  const float total = panelWidth - 56.0f;
  // Import and Export are narrower secondaries at the left; Save & Apply
  // and Discard split the rest.
  const float secondary = std::clamp(total * 0.16f, 40.0f, 110.0f);
  const float primary =
    std::max(24.0f, (total - secondary * 2.0f - gap * 3.0f) * 0.5f);
  float x = left;
  float width = secondary;
  if (index == 1) {
    x = left + secondary + gap;
  } else if (index == 2) {
    x = left + (secondary + gap) * 2.0f;
    width = primary;
  } else if (index == 3) {
    x = left + (secondary + gap) * 2.0f + primary + gap;
    width = primary;
  }
  return { x, y, width, height };
}

void
RulesetWorkshopMenu::selectRow(int row)
{
  if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
    return;
  }
  const MenuRow& target = rows[static_cast<std::size_t>(row)];
  if (target.kind != RowKind::Control && target.kind != RowKind::FooterAction) {
    return;
  }
  const int previousBody = bodyIndexForRow(selectedRow);
  const int nextBody = bodyIndexForRow(row);
  if (row != selectedRow) {
    if (previousBody >= 0 && nextBody >= 0) {
      animator.beginSelectionTravel(static_cast<float>(previousBody),
                                    static_cast<float>(nextBody));
    } else {
      // Travel between the body list and the footer bar is not a glide.
      animator.settleSelection();
    }
    selectedRow = row;
    animator.resetCaret();
    CSimSounds::play(CSimSound::MenuHover);
  }
  // A preview row makes its state the focused example.
  if (target.control == Control::PreviewStrip && previewState != target.state) {
    previewState = target.state;
    previewDirty = true;
  }
  if (nextBody >= 0) {
    firstVisibleRow =
      GuiPanelLayout::scrollToRow(firstVisibleRow, nextBody, visibleRows);
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

bool
RulesetWorkshopMenu::isListControl(Control control)
{
  return control == Control::Family || control == Control::StarterRule;
}

std::array<float, 4>
RulesetWorkshopMenu::valueField(int row) const
{
  const float rowY = firstRowY + animator.panelOffsetY() +
                     rowHeight * static_cast<float>(row - firstVisibleRow);
  return { valueLeft(),
           rowY + 4.0f,
           std::max(24.0f, valueRight() - valueLeft()),
           rowHeight - 10.0f };
}

void
RulesetWorkshopMenu::openList(Control control)
{
  std::vector<GuiDropdownItem> items;
  listIds.clear();
  int current = -1;
  const RuleSetRegistry& registry = RuleSetRegistry::instance();
  if (control == Control::Family) {
    // Families that have rules to start from, each with how many it offers.
    for (const RuleFamilyDefinition& family : registry.getFamilyDefinitions()) {
      const std::size_t rules = registry.getKnownRules(family.id).size();
      if (rules == 0u) {
        continue;
      }
      if (family.id == familyDraft.id) {
        current = static_cast<int>(items.size());
      }
      items.push_back(
        { family.name,
          std::to_string(rules) + (rules == 1u ? " rule" : " rules") });
      listIds.push_back(family.id);
    }
  } else if (control == Control::StarterRule) {
    for (std::size_t index = 0u; index < starterRuleIds.size(); ++index) {
      const RuleSetDefinition* rule =
        registry.getRuleSetDefinition(starterRuleIds[index]);
      items.push_back({ rule == nullptr ? starterRuleIds[index] : rule->name,
                        std::string() });
      listIds.push_back(starterRuleIds[index]);
    }
    current = starterRuleIndex;
  } else {
    return;
  }
  const int row = rowForControl(control);
  if (items.empty() || row < 0) {
    return;
  }
  const std::array<float, 4> field = valueField(row);
  dropdown.open(std::move(items),
                current,
                field[0],
                field[1],
                field[2],
                field[3],
                8.0f,
                panelFit.virtualHeight - 8.0f,
                animator.reducedMotion());
  if (dropdown.isOpen()) {
    listControl = control;
    errorMessage.clear();
    CSimSounds::play(CSimSound::MenuSelect);
  }
}

void
RulesetWorkshopMenu::chooseFromList(int index)
{
  if (index < 0 || index >= static_cast<int>(listIds.size())) {
    return;
  }
  const std::string& id = listIds[static_cast<std::size_t>(index)];
  if (listControl == Control::Family) {
    selectFamily(id);
  } else if (listControl == Control::StarterRule) {
    if (index != starterRuleIndex) {
      applyStarterRule(index);
    }
  }
  animator.triggerValuePulse(1);
  CSimSounds::play(CSimSound::MenuSelect);
}

RulesetWorkshopAction
RulesetWorkshopMenu::updateList(InputManager* input)
{
  // The open list takes every key, typed letter, the wheel and the pointer;
  // the rows beneath it keep their selection.
  pointer.sample(window, input, panelFit.layoutScale);
  const int before = dropdown.highlightedIndex();
  const GuiDropdownResult result = dropdown.update(input, pointer);
  if (result == GuiDropdownResult::Chosen) {
    chooseFromList(dropdown.chosenIndex());
  } else if (result == GuiDropdownResult::Dismissed) {
    CSimSounds::play(CSimSound::MenuBack);
  } else if (dropdown.highlightedIndex() != before) {
    CSimSounds::play(CSimSound::MenuHover);
  }
  if (!dropdown.isOpen()) {
    listControl = Control::None;
  }
  rebuildVisual();
  return RulesetWorkshopAction::None;
}

bool
RulesetWorkshopMenu::isPreviewByCount() const
{
  return familyDraft.kind != RuleFamily::Elementary1D &&
         !isDirectionalFamily(familyDraft.kind);
}

void
RulesetWorkshopMenu::appendSection(const std::string& label)
{
  rows.push_back(MenuRow{ RowKind::Section, Control::None, label, {}, 0u });
}

void
RulesetWorkshopMenu::appendControl(Control control, const std::string& label)
{
  rows.push_back(MenuRow{ RowKind::Control, control, label, {}, 0u });
}

void
RulesetWorkshopMenu::appendInformation(Control control,
                                       const std::string& label,
                                       const std::string& detail)
{
  rows.push_back(MenuRow{ RowKind::Information, control, label, detail, 0u });
}

void
RulesetWorkshopMenu::rebuildRows()
{
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.rebuildRows");
  const Control previouslySelected = controlForRow(selectedRow);
  const unsigned int previouslySelectedState =
    selectedRow >= 0 && static_cast<std::size_t>(selectedRow) < rows.size()
      ? rows[static_cast<std::size_t>(selectedRow)].state
      : 0u;
  rows.clear();
  appendSection("START FROM");
  // Family first, as in New Canvas: it filters the starter rules.
  appendControl(Control::Family, "Cell family");
  appendControl(Control::StarterRule, "Starter rule");
  appendControl(Control::RulesetName, "Ruleset name");

  appendSection("BEHAVIOR");
  if (isCountMaskFamily(familyDraft.kind)) {
    appendControl(Control::BirthCounts, "Born with");
    appendControl(Control::SurvivalCounts, "Survives with");
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
  } else if (familyDraft.kind == RuleFamily::Lenia) {
    appendInformation(Control::TableInformation,
                      "Kernel and growth",
                      draft.rule.empty()
                        ? "Edit mu, sigma and kernel in JSON, then import."
                        : draft.rule);
  } else {
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
    appendControl(Control::PreviewNeighborhood, "Neighborhood");
  } else {
    // Background first, so the rows read in the order of Born with and
    // Survives with above them; then the rest in state order.
    const unsigned int shown =
      std::min(std::max(1u, familyDraft.stateCount), kPreviewRowLimit);
    for (unsigned int index = 0u; index < shown; ++index) {
      const unsigned int state = shown >= 2u && index < 2u ? 1u - index : index;
      rows.push_back(MenuRow{ RowKind::Control,
                              Control::PreviewStrip,
                              stateLabel(familyDraft, state),
                              {},
                              state });
    }
  }

  appendSection("CELL STATES");
  appendControl(Control::FamilyName, "Family name");
  appendControl(Control::PaletteState, "State to edit");
  appendControl(Control::StateName, "State label");
  appendControl(Control::Red, "Red channel");
  appendControl(Control::Green, "Green channel");
  appendControl(Control::Blue, "Blue channel");
  bodyRowCount = static_cast<int>(rows.size());
  rows.push_back(MenuRow{
    RowKind::FooterAction, Control::Import, "Import rules from JSON", {}, 0u });
  rows.push_back(MenuRow{
    RowKind::FooterAction, Control::Export, "Export this rule", {}, 0u });
  rows.push_back(
    MenuRow{ RowKind::FooterAction, Control::Apply, "Save & Apply", {}, 0u });
  rows.push_back(
    MenuRow{ RowKind::FooterAction, Control::Discard, "Discard", {}, 0u });

  int nextSelection = -1;
  for (std::size_t index = 0u; index < rows.size(); ++index) {
    if (rows[index].control == previouslySelected &&
        (previouslySelected != Control::PreviewStrip ||
         rows[index].state == previouslySelectedState)) {
      nextSelection = static_cast<int>(index);
      break;
    }
  }
  if (nextSelection < 0 && previouslySelected == Control::PreviewStrip) {
    nextSelection = rowForControl(Control::PreviewStrip);
  }
  if (nextSelection < 0) {
    nextSelection = rowForControl(Control::StarterRule);
  }
  if (nextSelection < 0) {
    nextSelection = rowForControl(Control::Apply);
  }
  selectedRow = nextSelection;
  updateLayout();
}

void
RulesetWorkshopMenu::setError(const std::string& error)
{
  errorMessage = error;
  if (!error.empty()) {
    CSimSounds::play(CSimSound::MenuError);
  }
  rebuildVisual();
}

RulesetWorkshopAction
RulesetWorkshopMenu::update(InputManager* input)
{
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.update");
  if (!openState || input == nullptr) {
    return RulesetWorkshopAction::None;
  }
  updateLayout();
  if (dropdown.isOpen()) {
    return updateList(input);
  }
  RulesetWorkshopAction action = RulesetWorkshopAction::None;
  std::queue<InputManager::KeyPressEvent> remaining;
  std::queue<InputManager::KeyPressEvent>& keys = input->getKeyQueue();
  fineAdjust = input->isShiftPressed();
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
        animator.resetCaret();
        animator.triggerValuePulse();
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
        animator.resetCaret();
        animator.triggerValuePulse();
      }
    }
  } else {
    while (!characters.empty()) {
      characters.pop();
    }
  }

  bool wheelScrolled = false;
  firstVisibleRow = GuiPanelLayout::applyWheelScroll(
    input, firstVisibleRow, bodyRowCount, visibleRows, &wheelScrolled);

  updateLayout();
  pointer.sample(window, input, panelFit.layoutScale);
  const float mouseX = pointer.x();
  const float mouseY = pointer.y();
  const bool mouseMoved = pointer.moved();
  const bool clicked = pointer.clicked();

  // A held press on a color slider keeps steering it wherever the pointer
  // goes, and owns the pointer until it is released.
  if (dragControl != Control::None) {
    if (pointer.pressed()) {
      const float trackLeft = sliderTrackLeft();
      const float trackWidth = sliderTrackRight() - trackLeft;
      const float fraction = std::clamp(
        (mouseX - trackLeft) / std::max(1.0f, trackWidth), 0.0f, 1.0f);
      setColorChannel(dragControl,
                      static_cast<int>(std::lround(fraction * 255.0f)));
      GuiPointerHint::markInteractive();
      rebuildVisual();
      return action;
    }
    dragControl = Control::None;
  }

  const std::array<Control, kFooterButtonCount> footerControls = {
    Control::Import, Control::Export, Control::Apply, Control::Discard
  };
  int footerHover = -1;
  for (int index = 0; index < kFooterButtonCount; ++index) {
    const std::array<float, 4> rect = footerButtonRect(index);
    if (GuiKit::isPointInRect(
          mouseX, mouseY, rect[0], rect[1], rect[2], rect[3])) {
      footerHover = index;
      break;
    }
  }
  if (footerHover >= 0) {
    GuiPointerHint::markInteractive();
    const Control control =
      footerControls[static_cast<std::size_t>(footerHover)];
    if (mouseMoved || clicked) {
      selectRow(rowForControl(control));
    }
    if (clicked && action == RulesetWorkshopAction::None) {
      action = activateControl(control);
    }
  } else {
    const int row = bodyRowForPoint(mouseX, mouseY);
    const Control control = controlForRow(row);
    if (row >= 0 && control != Control::None &&
        rows[static_cast<std::size_t>(row)].kind == RowKind::Control) {
      GuiPointerHint::markInteractive();
      if (mouseMoved || clicked) {
        selectRow(row);
      }
      if (clicked && action == RulesetWorkshopAction::None) {
        float chipX = 0.0f;
        float chipWidth = 0.0f;
        float gap = 0.0f;
        // The chip under the pointer in an `count`-chip strip, or -1.
        const auto chipAt = [&](int count) {
          chipStrip(count, &chipX, &chipWidth, &gap);
          const float total =
            (chipWidth + gap) * static_cast<float>(count) - gap;
          if (mouseX < chipX || mouseX > chipX + total) {
            return -1;
          }
          return std::clamp(
            static_cast<int>((mouseX - chipX) / (chipWidth + gap)),
            0,
            count - 1);
        };
        if (control == Control::BirthCounts ||
            control == Control::SurvivalCounts) {
          const int count = chipAt(9);
          if (count >= 0 &&
              toggleNeighborCount(control, static_cast<unsigned int>(count))) {
            animator.triggerValuePulse();
          }
        } else if (control == Control::PreviewStrip) {
          const int count = isPreviewByCount() ? chipAt(9) : -1;
          if (count >= 0) {
            previewNeighborCount = static_cast<unsigned int>(count);
            previewDirty = true;
            animator.triggerValuePulse();
            CSimSounds::play(CSimSound::MenuSelect);
          }
        } else if (control == Control::PreviewNeighborhood) {
          const int pattern = chipAt(8);
          if (pattern >= 0) {
            previewNeighborhood = static_cast<unsigned int>(pattern);
            previewDirty = true;
            animator.triggerValuePulse();
            CSimSounds::play(CSimSound::MenuSelect);
          }
        } else if (control == Control::PaletteState &&
                   familyDraft.stateCount <= kSwatchPickerLimit) {
          const int state = chipAt(static_cast<int>(familyDraft.stateCount));
          if (state >= 0 && static_cast<unsigned int>(state) != paletteState) {
            paletteState = static_cast<unsigned int>(state);
            animator.triggerValuePulse();
            CSimSounds::play(CSimSound::MenuSelect);
          }
        } else if (control == Control::Red || control == Control::Green ||
                   control == Control::Blue) {
          if (mouseX >= sliderTrackLeft() - 10.0f &&
              mouseX <= sliderTrackRight() + 10.0f) {
            dragControl = control;
            const float fraction = std::clamp(
              (mouseX - sliderTrackLeft()) /
                std::max(1.0f, sliderTrackRight() - sliderTrackLeft()),
              0.0f,
              1.0f);
            setColorChannel(control,
                            static_cast<int>(std::lround(fraction * 255.0f)));
          }
        } else if (isListControl(control)) {
          // A list field opens from anywhere on its row, as in New Canvas.
          openList(control);
        } else if (!isTextControl(row) && mouseX >= valueLeft() &&
                   mouseX <= valueRight()) {
          // A stepper: its left half steps back, its right half forward.
          changeControl(control,
                        mouseX < (valueLeft() + valueRight()) * 0.5f ? -1 : 1);
        }
      }
    }
  }
  rebuildVisual();
  // Apply, import and export are voiced by the canvas once they succeed or
  // fail; discarding is always a step back.
  if (action == RulesetWorkshopAction::Cancel) {
    CSimSounds::play(CSimSound::MenuBack);
  }
  return action;
}

int
RulesetWorkshopMenu::bodyRowForPoint(float x, float y) const
{
  const float rowAreaTop = firstRowY + animator.panelOffsetY();
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
    animator.triggerValuePulse();
    rebuildVisual();
    return RulesetWorkshopAction::None;
  }
  if (isListControl(control)) {
    openList(control);
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
  CSimSounds::play(CSimSound::MenuSelect);
  return true;
}

bool
RulesetWorkshopMenu::setColorChannel(Control control, int value)
{
  if ((control != Control::Red && control != Control::Green &&
       control != Control::Blue) ||
      paletteState >= familyDraft.stateColors.size()) {
    return false;
  }
  const std::size_t channel = control == Control::Red     ? 0u
                              : control == Control::Green ? 1u
                                                          : 2u;
  unsigned char& target = familyDraft.stateColors[paletteState][channel];
  const unsigned char next =
    static_cast<unsigned char>(std::clamp(value, 0, 255));
  if (target == next) {
    return false;
  }
  target = next;
  familyChanged = true;
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
    // Long-range cyclic rules may require more than eight successors.
    int maximum = 0;
    const int radius = static_cast<int>(draft.neighborhoodRadius);
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if ((x != 0 || y != 0) &&
            RuleSet::extendedNeighborhoodContains(
              draft.extendedNeighborhoodShape, radius, x, y)) {
          maximum += 1;
        }
      }
    }
    maximum = std::max(1, maximum);
    const int next = static_cast<int>(draft.cyclicThreshold) + direction - 1;
    const unsigned int wrapped =
      static_cast<unsigned int>((next % maximum + maximum) % maximum + 1);
    changed = wrapped != draft.cyclicThreshold;
    draft.cyclicThreshold = wrapped;
    previewDirty = true;
  } else if (control == Control::CyclicStep &&
             familyDraft.kind == RuleFamily::Cyclic) {
    // An inert background is not a phase, so the cycle is one state shorter.
    const unsigned int stateCount =
      std::max(2u,
               draft.inertBackground ? familyDraft.stateCount - 1u
                                     : familyDraft.stateCount);
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
  } else if (control == Control::PreviewStrip && isPreviewByCount()) {
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
    // Shift steps a single unit for fine tuning.
    const int step = fineAdjust ? 1 : 8;
    changed = setColorChannel(
      control,
      static_cast<int>(familyDraft.stateColors[paletteState][channel]) +
        direction * step);
  }
  if (changed) {
    if (control == Control::BirthCounts || control == Control::SurvivalCounts) {
      previewDirty = true;
    }
    animator.triggerValuePulse(direction);
    CSimSounds::play(CSimSound::MenuSelect);
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
  applyStarterRule((starterRuleIndex + direction + count) % count);
}

void
RulesetWorkshopMenu::applyStarterRule(int index)
{
  if (index < 0 || static_cast<std::size_t>(index) >= starterRuleIds.size()) {
    return;
  }
  starterRuleIndex = index;
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
  // The preview shows one row per state up to its limit.
  rebuildRows();
}

void
RulesetWorkshopMenu::refreshPreview()
{
  if (!previewDirty) {
    return;
  }
  previewDirty = false;
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.refreshPreview");
  previewValid = false;
  RuleSetRegistry previewRegistry;
  if (!previewRegistry.registerFamily(familyDraft) ||
      !previewRegistry.registerRule(draft)) {
    previewText = "invalid draft";
    return;
  }
  std::unique_ptr<RuleSet> rule = previewRegistry.createRuleSet(draft.id);
  if (rule == nullptr) {
    previewText = "preview unavailable";
    return;
  }
  const RuleFamily kind = familyDraft.kind;
  if (kind == RuleFamily::Elementary1D) {
    for (unsigned int pattern = 0u; pattern < 8u; ++pattern) {
      previewElementaryNext[pattern] =
        rule->nextElementary(static_cast<unsigned char>((pattern >> 2u) & 1u),
                             static_cast<unsigned char>((pattern >> 1u) & 1u),
                             static_cast<unsigned char>(pattern & 1u));
    }
    previewValid = true;
    const unsigned int pattern = previewNeighborhood & 7u;
    previewText = std::to_string((pattern >> 2u) & 1u) +
                  std::to_string((pattern >> 1u) & 1u) +
                  std::to_string(pattern & 1u) + " -> " +
                  stateLabel(familyDraft, previewElementaryNext[pattern]);
    return;
  }

  std::uint64_t fullPotential = 0u;
  if (kind == RuleFamily::Lenia) {
    // The neighbor control reads as the kernel potential in eighths.
    for (const RuleSet::KernelTap& tap : rule->getKernelTaps()) {
      fullPotential += tap.weight;
    }
    fullPotential *= familyDraft.stateCount - 1u;
  }
  const unsigned int stateCount = std::max(1u, familyDraft.stateCount);
  const unsigned int shown = std::min(stateCount, kPreviewRowLimit);
  previewNext.assign(static_cast<std::size_t>(shown) * 9u, 1u);
  for (unsigned int state = 0u; state < shown; ++state) {
    for (unsigned int count = 0u; count <= 8u; ++count) {
      previewNext[state * 9u + count] =
        previewTransition(*rule, kind, state, count, fullPotential);
    }
  }
  previewValid = true;

  previewState = std::min(previewState, stateCount - 1u);
  const unsigned char next = previewTransition(
    *rule, kind, previewState, previewNeighborCount, fullPotential);
  const std::string from = stateLabel(familyDraft, previewState);
  const std::string to = stateLabel(familyDraft, next);
  if (kind == RuleFamily::Lenia) {
    previewText = from + " at potential " +
                  std::to_string(previewNeighborCount) + "/8 -> " + to;
  } else if (isDirectionalFamily(kind)) {
    previewText = from + " with empty neighbors -> " + to;
  } else {
    previewText = from + " + " + std::to_string(previewNeighborCount) +
                  (kind == RuleFamily::Cyclic ? " successor neighbors -> "
                                              : " live neighbors -> ") +
                  to;
  }
}

std::string
RulesetWorkshopMenu::ruleNotation() const
{
  if (isCountMaskFamily(familyDraft.kind)) {
    std::string notation = "B" + countDigits(draft.birthMask) + "/S" +
                           countDigits(draft.surviveMask);
    if (familyDraft.kind == RuleFamily::Generations) {
      notation += "/C" + std::to_string(familyDraft.stateCount);
    }
    return notation;
  }
  if (familyDraft.kind == RuleFamily::Elementary1D) {
    return "W" + std::to_string(draft.ruleNumber);
  }
  if (familyDraft.kind == RuleFamily::Cyclic) {
    return "T" + std::to_string(draft.cyclicThreshold) + " +" +
           std::to_string(draft.cyclicStep);
  }
  if (!draft.rule.empty() && draft.rule.size() <= 14u) {
    return draft.rule;
  }
  return "CUSTOM";
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
    return familyDraft.name;
  }
  if (control == Control::FamilyName) {
    return familyDraft.name.empty() ? "Type a family name" : familyDraft.name;
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
  return {};
}

std::string
RulesetWorkshopMenu::helpForControl(Control control) const
{
  if (control == Control::StarterRule) {
    return "Enter or click opens the rules to start from; Left/Right steps. "
           "Nothing changes until you apply.";
  }
  if (control == Control::Family) {
    return "Enter or click opens the cell families; Left/Right steps. The "
           "family sets the states.";
  }
  if (control == Control::FamilyName) {
    return "Name this cell schema; custom families shared by several rules "
           "change together.";
  }
  if (control == Control::RulesetName) {
    return "Type to rename this draft; Backspace removes a character.";
  }
  if (control == Control::BirthCounts) {
    return "An empty cell comes alive with any lit count of neighbors. Click "
           "a count to flip it.";
  }
  if (control == Control::SurvivalCounts) {
    return "A live cell survives with any lit count of neighbors. Click a "
           "count to flip it.";
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
  if (control == Control::TableInformation) {
    return "This family's parameters are edited in JSON; use Import below.";
  }
  if (control == Control::PreviewStrip) {
    std::string help = previewText + ".";
    if (isPreviewByCount()) {
      help += " Each chip is the next state for that many neighbors.";
    }
    if (familyDraft.stateCount > kPreviewRowLimit) {
      help += " First " + std::to_string(kPreviewRowLimit) + " of " +
              std::to_string(familyDraft.stateCount) + " states shown.";
    }
    return help;
  }
  if (control == Control::PreviewNeighborhood) {
    return previewText +
           ". Each chip is the next centre cell for its left-centre-right "
           "pattern.";
  }
  if (control == Control::PaletteState) {
    return "Choose which state's label and color to edit.";
  }
  if (control == Control::StateName) {
    return "Type a short label for the selected state.";
  }
  if (control == Control::Red || control == Control::Green ||
      control == Control::Blue) {
    return "Drag to mix the selected state's color; Left/Right step by 8, "
           "with Shift by 1.";
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
RulesetWorkshopMenu::drawHeader(float reveal, unsigned char panelOpacity)
{
  GameVisual& visual = layers[kContentLayer];
  const ColorRgba cyan = UiTheme::accentCool();
  const float animatedPanelY = panelY + animator.panelOffsetY();
  // The header floats nearer than the rows and swings further with the tilt.
  const float headerX = panelX + tilt.layerX(GuiPanelTilt::kHeaderDepth);
  const float headerY =
    animatedPanelY + tilt.layerY(GuiPanelTilt::kHeaderDepth);
  const float headerSlide = (1.0f - reveal) * 10.0f;
  const bool roomy = headerHeight >= 86.0f;
  visual.addText("B E N D   T H E   R U L E S",
                 headerX + 28.0f - headerSlide,
                 headerY + (roomy ? 14.0f : 8.0f),
                 10.0f,
                 UiTheme::applyOpacity(cyan, panelOpacity));
  const float titleFont = std::clamp(panelWidth * 0.038f, 16.0f, 24.0f);
  visual.addText("RULESET WORKSHOP",
                 headerX + 28.0f - headerSlide * 0.6f,
                 headerY + (roomy ? 28.0f : 21.0f),
                 titleFont,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), panelOpacity));

  // The draft at a glance: its notation, lit, over its family and states.
  const std::string notation = ruleNotation();
  const std::string meta = familyLabel(familyDraft.kind) + "  /  " +
                           std::to_string(familyDraft.stateCount) + " STATES";
  const float notationFont = roomy ? 18.0f : 14.0f;
  const float metaFont = 9.5f;
  const float cardWidth =
    std::max(GuiKit::measureEmphasizedText(notation, notationFont, 0.6f),
             GuiKit::measureEmphasizedText(meta, metaFont, 0.0f)) +
    28.0f;
  const float cardHeight = roomy ? 50.0f : 36.0f;
  const float cardX = headerX + panelWidth - 24.0f - cardWidth;
  const float cardY = headerY + (roomy ? 16.0f : 8.0f);
  const float titleRight =
    headerX + 28.0f +
    GuiKit::measureEmphasizedText("RULESET WORKSHOP", titleFont, 0.0f);
  if (cardX > titleRight + 16.0f) {
    const float pulse = animator.valuePulse();
    GuiKit::drawRoundedGradientRect(
      visual,
      cardX,
      cardY,
      cardWidth,
      cardHeight,
      10.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::fade(cyan, 0.55f), cyan, 0.6f * pulse),
        panelOpacity),
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::accentViolet(), 0.55f),
                            panelOpacity));
    GuiKit::drawRoundedGradientRect(
      visual,
      cardX + 1.0f,
      cardY + 1.0f,
      cardWidth - 2.0f,
      cardHeight - 2.0f,
      9.0f,
      UiTheme::applyOpacity(ColorRgba{ 14, 26, 44, 255 }, panelOpacity),
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    GuiKit::drawEmphasizedText(
      visual,
      notation,
      cardX + 14.0f,
      cardY + (roomy ? 8.0f : 5.0f),
      notationFont,
      UiTheme::applyOpacity(UiTheme::mix(cyan, UiTheme::textPrimary(), 0.25f),
                            panelOpacity),
      0.6f);
    visual.addText(meta,
                   cardX + 14.0f,
                   cardY + cardHeight - metaFont - (roomy ? 8.0f : 5.0f),
                   metaFont,
                   UiTheme::applyOpacity(UiTheme::textMuted(), panelOpacity));
  }

  if (!roomy) {
    return;
  }
  // Keycap hints, as many as fit on one line.
  const std::array<const char*, 4> keys = {
    "UPDOWN", "LEFTRIGHT", "ENTER", "ESC"
  };
  const std::array<const char*, 4> actions = {
    "select", "adjust", "activate", "discard"
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
                                 headerY + 66.0f,
                                 keys[index],
                                 actions[index],
                                 hintSize,
                                 panelOpacity);
  }
}

void
RulesetWorkshopMenu::drawFooter(unsigned char panelOpacity)
{
  GameVisual& visual = layers[kContentLayer];
  const float animatedPanelY = panelY + animator.panelOffsetY();
  const float footerY = animatedPanelY + panelHeight - footerHeight;
  const ColorRgba footerRule =
    UiTheme::applyOpacity(UiTheme::panelBorder(), panelOpacity);
  visual.addGradientRect(panelX + 20.0f,
                         footerY,
                         (panelWidth - 40.0f) * 0.5f,
                         1.0f,
                         UiTheme::transparentOf(footerRule),
                         footerRule,
                         footerRule,
                         UiTheme::transparentOf(footerRule));
  visual.addGradientRect(panelX + 20.0f + (panelWidth - 40.0f) * 0.5f,
                         footerY,
                         (panelWidth - 40.0f) * 0.5f,
                         1.0f,
                         footerRule,
                         UiTheme::transparentOf(footerRule),
                         UiTheme::transparentOf(footerRule),
                         footerRule);
  const std::string footerMessage =
    !errorMessage.empty() ? errorMessage
    : dropdown.isOpen()
      ? "Up/Down to browse, type to jump, Enter to choose, Esc to close the "
        "list."
      : helpForControl(controlForRow(selectedRow));
  const ColorRgba footerColor =
    errorMessage.empty() ? UiTheme::textSecondary() : UiTheme::error();
  if (footerHeight >= 70.0f) {
    visual.addText(footerMessage.substr(0u, 110u),
                   panelX + 28.0f,
                   footerY + 12.0f,
                   11.5f,
                   UiTheme::applyOpacity(footerColor, panelOpacity));
  }
  const std::array<const char*, kFooterButtonCount> labels = {
    "IMPORT", "EXPORT", "SAVE & APPLY", "DISCARD"
  };
  const std::array<ColorRgba, kFooterButtonCount> tints = {
    UiTheme::accentCool(),
    UiTheme::accentCool(),
    UiTheme::success(),
    UiTheme::warning()
  };
  for (int index = 0; index < kFooterButtonCount; ++index) {
    const std::array<float, 4> rect = footerButtonRect(index);
    // The file actions are quieter secondaries until focused.
    drawActionButton(visual,
                     rect[0],
                     rect[1],
                     rect[2],
                     rect[3],
                     labels[static_cast<std::size_t>(index)],
                     tints[static_cast<std::size_t>(index)],
                     footerFocus.value(index),
                     index < 2
                       ? static_cast<unsigned char>(panelOpacity * 0.85f)
                       : panelOpacity);
  }
}

void
RulesetWorkshopMenu::rebuildVisual()
{
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.rebuildVisual");
  updateLayout();
  for (GameVisual& layer : layers) {
    layer.clearPrimitives();
  }
  GameVisual& glassLayer = layers[kGlassLayer];
  GameVisual& surfaces = layers[kSurfaceLayer];
  GameVisual& visual = layers[kContentLayer];
  GameVisual& cursorLayer = layers[kCursorLayer];
  if (!openState) {
    return;
  }
  refreshPreview();
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

  GuiKit::drawVignette(glassLayer,
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
  GuiKit::drawGlassPanel(glassLayer,
                         panelX + tilt.layerX(GuiPanelTilt::kGlassDepth),
                         animatedPanelY +
                           tilt.layerY(GuiPanelTilt::kGlassDepth),
                         panelWidth,
                         panelHeight,
                         glass);
  const float ruleWidth = (panelWidth - 40.0f) * reveal;
  const ColorRgba ruleCyan = UiTheme::applyOpacity(cyan, panelOpacity);
  const ColorRgba ruleViolet = UiTheme::applyOpacity(
    UiTheme::fade(UiTheme::accentViolet(), 0.8f), panelOpacity);
  surfaces.addGradientRect(panelX + 20.0f,
                           animatedFirstRowY - 5.0f,
                           ruleWidth * 0.7f,
                           2.0f,
                           ruleCyan,
                           ruleViolet,
                           ruleViolet,
                           ruleCyan);
  surfaces.addGradientRect(panelX + 20.0f + ruleWidth * 0.7f,
                           animatedFirstRowY - 5.0f,
                           ruleWidth * 0.3f,
                           2.0f,
                           ruleViolet,
                           UiTheme::transparentOf(ruleViolet),
                           UiTheme::transparentOf(ruleViolet),
                           ruleViolet);

  drawHeader(reveal, panelOpacity);

  const float rowFontSize = std::clamp(rowHeight * 0.48f, 15.0f, 17.0f);
  const float valueX = valueLeft();
  const float valueEnd = valueRight();
  const float valueWidth = std::max(24.0f, valueEnd - valueX);
  const int lastVisibleRow =
    std::min(firstVisibleRow + visibleRows, bodyRowCount);
  const float cardX = panelX + 20.0f;
  const float cardWidth = panelWidth - 40.0f;

  // Pass one: row surfaces (section rules, inset information rows, cards).
  for (int bodyRow = firstVisibleRow; bodyRow < lastVisibleRow; ++bodyRow) {
    const MenuRow& row = rows[static_cast<std::size_t>(bodyRow)];
    const float rowY =
      animatedFirstRowY +
      rowHeight * static_cast<float>(bodyRow - firstVisibleRow);
    const unsigned char rowOpacity = static_cast<unsigned char>(
      std::round(rowReveal(bodyRow) * static_cast<float>(panelOpacity)));
    if (row.kind == RowKind::Section) {
      const float sectionFont = std::clamp(rowFontSize * 0.7f, 10.5f, 12.0f);
      const float dividerX =
        panelX + 40.0f +
        GuiKit::measureEmphasizedText(row.label, sectionFont, 0.0f);
      const float dividerWidth =
        std::max(0.0f, panelX + panelWidth - 40.0f - dividerX);
      const ColorRgba ruleColor = UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::panelBorder(), cyan, 0.35f), rowOpacity);
      surfaces.addGradientRect(dividerX,
                               rowY + rowHeight * 0.6f,
                               dividerWidth,
                               1.0f,
                               ruleColor,
                               UiTheme::transparentOf(ruleColor),
                               UiTheme::transparentOf(ruleColor),
                               ruleColor);
      continue;
    }
    if (row.kind == RowKind::Information) {
      GuiKit::drawRoundedGradientRect(
        surfaces,
        panelX + 22.0f,
        rowY + 1.0f,
        panelWidth - 44.0f,
        rowHeight - 3.0f,
        7.0f,
        UiTheme::applyOpacity(ColorRgba{ 8, 14, 26, 220 }, rowOpacity),
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
      continue;
    }
    const float e = std::clamp(rowFocus.value(bodyRow), 0.0f, 1.0f);
    GuiKit::drawRoundedRect(
      surfaces,
      cardX,
      rowY + 1.0f,
      cardWidth,
      rowHeight - 5.0f,
      8.0f,
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::cardRim(),
                                         ColorRgba{ 80, 160, 190, 255 },
                                         e * 0.5f),
                            rowOpacity));
    GuiKit::drawRoundedGradientRect(
      surfaces,
      cardX + 1.0f,
      rowY + 2.0f,
      cardWidth - 2.0f,
      rowHeight - 7.0f,
      7.0f,
      UiTheme::applyOpacity(UiTheme::cardTop(), rowOpacity),
      UiTheme::applyOpacity(UiTheme::cardBottom(), rowOpacity));
  }

  // The selection pours between rows like a drop of liquid (body rows only;
  // footer actions light their own buttons).
  const int selectedBody = bodyIndexForRow(selectedRow);
  if (selectedBody >= firstVisibleRow && selectedBody < lastVisibleRow) {
    const float windowTop = static_cast<float>(firstVisibleRow);
    const float windowBottom = static_cast<float>(lastVisibleRow - 1);
    const GuiSelectionSpan span =
      animator.selectionSpan(static_cast<float>(selectedBody));
    const unsigned char pillOpacity = static_cast<unsigned char>(
      std::round(rowReveal(selectedBody) * static_cast<float>(panelOpacity)));
    GuiLiquidSelection drop;
    drop.crossStart = cardX;
    drop.crossSize = cardWidth;
    drop.headStart =
      animatedFirstRowY +
      rowHeight *
        (std::clamp(span.leading, windowTop, windowBottom) - windowTop) +
      1.0f;
    drop.tailStart =
      animatedFirstRowY +
      rowHeight *
        (std::clamp(span.trailing, windowTop, windowBottom) - windowTop) +
      1.0f;
    drop.cellLength = rowHeight - 5.0f;
    drop.radius = 8.0f;
    drop.squash = span.squash;
    drop.glowSpread = 9.0f + 3.0f * breathe;
    drop.glow = UiTheme::applyOpacity(
      UiTheme::fade(cyan, 0.18f + 0.1f * breathe), pillOpacity);
    drop.rim = UiTheme::applyOpacity(UiTheme::fade(cyan, 0.85f), pillOpacity);
    drop.rimBottom = UiTheme::accentBlendOf(drop.rim);
    drop.glowBottom = UiTheme::accentBlendOf(drop.glow);
    drop.faceTop = UiTheme::applyOpacity(UiTheme::selectionTop(), pillOpacity);
    drop.faceBottom =
      UiTheme::applyOpacity(UiTheme::selectionBottom(), pillOpacity);
    drop.sheen = animator.selectionSheen();
    drop.sheenColor =
      UiTheme::applyOpacity(ColorRgba{ 210, 250, 255, 38 }, pillOpacity);
    GuiKit::drawLiquidSelection(layers[kDropLayer], drop);
  }

  // Pass two: row contents.
  const float pulse = animator.valuePulse();
  for (int bodyRow = firstVisibleRow; bodyRow < lastVisibleRow; ++bodyRow) {
    const MenuRow& row = rows[static_cast<std::size_t>(bodyRow)];
    const float rowY =
      animatedFirstRowY +
      rowHeight * static_cast<float>(bodyRow - firstVisibleRow);
    const unsigned char rowOpacity = static_cast<unsigned char>(
      std::round(rowReveal(bodyRow) * static_cast<float>(panelOpacity)));
    const float textY = rowY + std::max(2.0f, (rowHeight - rowFontSize) * 0.5f);
    const bool selected = bodyRow == selectedRow;
    if (row.kind == RowKind::Section) {
      const float sectionFont = std::clamp(rowFontSize * 0.7f, 10.5f, 12.0f);
      visual.addText(row.label,
                     panelX + 30.0f,
                     rowY + rowHeight * 0.6f - sectionFont * 0.55f,
                     sectionFont,
                     UiTheme::applyOpacity(cyan, rowOpacity));
      continue;
    }

    if (row.kind == RowKind::Information) {
      visual.addText(row.label,
                     panelX + 34.0f,
                     textY,
                     rowFontSize,
                     UiTheme::applyOpacity(UiTheme::textPrimary(), rowOpacity));
      visual.addText(row.detail,
                     valueX,
                     rowY + (rowHeight - 12.0f) * 0.5f,
                     12.0f,
                     UiTheme::applyOpacity(UiTheme::textMuted(), rowOpacity));
      continue;
    }

    const Control control = row.control;
    const float e = std::clamp(rowFocus.value(bodyRow), 0.0f, 1.2f);
    const float eClamped = std::min(1.0f, e);
    if (selected && pulse > 0.0f) {
      GuiKit::drawRoundedBand(
        visual,
        cardX,
        rowY + 1.0f,
        cardWidth,
        rowHeight - 5.0f,
        8.0f,
        -1.0f,
        6.0f,
        UiTheme::applyOpacity(UiTheme::fade(cyan, 0.45f * pulse), rowOpacity),
        UiTheme::transparentOf(cyan));
    }
    // A preview row leads with its state's swatch, named from the live draft
    // so a renamed state reads at once.
    float labelX = panelX + 34.0f + 4.0f * e;
    std::string label = row.label;
    if (control == Control::PreviewStrip) {
      const float swatch = std::clamp(rowHeight * 0.4f, 10.0f, 15.0f);
      drawSwatch(visual,
                 labelX,
                 rowY + (rowHeight - 4.0f - swatch) * 0.5f,
                 swatch,
                 swatch,
                 3.0f,
                 stateColor(familyDraft, row.state),
                 rowOpacity);
      labelX += swatch + 10.0f;
      label = stateLabel(familyDraft, row.state);
    }
    GuiKit::drawEmphasizedText(
      visual,
      label,
      labelX,
      textY,
      rowFontSize,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped), rowOpacity),
      e);

    // Stepped values spring the way they moved and wobble back.
    const float nudge =
      selected ? std::clamp(animator.valueWobble(), -1.0f, 1.0f) : 0.0f;
    const float boxY = rowY + 4.0f;
    const float boxHeight = rowHeight - 10.0f;
    float chipX = 0.0f;
    float chipWidth = 0.0f;
    float gap = 0.0f;
    // The keyboard cursor rides a chip in its own breathing layer.
    const auto drawChipCursor = [&](float x) {
      GuiKit::drawRoundedOutline(
        cursorLayer,
        x - 2.0f,
        boxY - 2.0f,
        chipWidth + 4.0f,
        boxHeight + 4.0f,
        6.0f,
        1.5f,
        UiTheme::applyOpacity(
          UiTheme::fade(UiTheme::textPrimary(), 0.6f + 0.4f * breathe),
          rowOpacity));
    };

    if (control == Control::BirthCounts || control == Control::SurvivalCounts) {
      const int glowBase = control == Control::BirthCounts ? 0 : 9;
      chipStrip(9, &chipX, &chipWidth, &gap);
      for (unsigned int count = 0u; count <= 8u; ++count) {
        const float lit = std::clamp(
          chipGlow.value(glowBase + static_cast<int>(count)), 0.0f, 1.1f);
        const float litClamped = std::min(1.0f, lit);
        const float currentX = chipX + (chipWidth + gap) * count;
        // Enabled counts light up on a spring and glow softly.
        if (litClamped > 0.02f) {
          GuiKit::drawRoundedBand(
            visual,
            currentX,
            boxY,
            chipWidth,
            boxHeight,
            5.0f,
            0.0f,
            5.0f,
            UiTheme::applyOpacity(UiTheme::fade(cyan, 0.4f * litClamped),
                                  rowOpacity),
            UiTheme::transparentOf(cyan));
        }
        GuiKit::drawRoundedGradientRect(
          visual,
          currentX,
          boxY,
          chipWidth,
          boxHeight,
          5.0f,
          UiTheme::applyOpacity(UiTheme::mix(ColorRgba{ 40, 58, 82, 255 },
                                             ColorRgba{ 120, 236, 250, 255 },
                                             litClamped),
                                rowOpacity),
          UiTheme::applyOpacity(
            UiTheme::mix(UiTheme::panelInset(), cyan, litClamped), rowOpacity));
        if (selected && count == neighborCount) {
          drawChipCursor(currentX);
        }
        GuiKit::drawTextCentered(
          visual,
          std::to_string(count),
          currentX + chipWidth * 0.5f,
          boxY + boxHeight * 0.5f,
          std::clamp(boxHeight * 0.6f, 11.0f, 15.0f),
          UiTheme::applyOpacity(UiTheme::mix(UiTheme::textMuted(),
                                             ColorRgba{ 6, 18, 30, 255 },
                                             litClamped),
                                rowOpacity));
      }
    } else if (control == Control::PreviewStrip) {
      if (!previewValid) {
        visual.addText(previewText,
                       valueX,
                       rowY + (rowHeight - 12.0f) * 0.5f,
                       12.0f,
                       UiTheme::applyOpacity(UiTheme::error(), rowOpacity));
      } else if (isPreviewByCount()) {
        // Every neighbor count, painted in the state it leads to; a count
        // that changes the cell gets a lit rim.
        chipStrip(9, &chipX, &chipWidth, &gap);
        for (unsigned int count = 0u; count <= 8u; ++count) {
          const std::size_t index =
            static_cast<std::size_t>(row.state) * 9u + count;
          const unsigned char next =
            index < previewNext.size() ? previewNext[index] : 1u;
          const ColorRgba fill = stateColor(familyDraft, next);
          const float currentX = chipX + (chipWidth + gap) * count;
          const bool changes = next != row.state;
          GuiKit::drawRoundedRect(
            visual,
            currentX - 1.0f,
            boxY - 1.0f,
            chipWidth + 2.0f,
            boxHeight + 2.0f,
            6.0f,
            UiTheme::applyOpacity(
              changes ? UiTheme::fade(cyan, 0.85f)
                      : UiTheme::mix(UiTheme::panelBorder(),
                                     ColorRgba{ 150, 176, 204, 255 },
                                     0.35f),
              rowOpacity));
          GuiKit::drawRoundedRect(visual,
                                  currentX,
                                  boxY,
                                  chipWidth,
                                  boxHeight,
                                  5.0f,
                                  UiTheme::applyOpacity(fill, rowOpacity));
          if (selected && count == previewNeighborCount) {
            drawChipCursor(currentX);
          }
          GuiKit::drawTextCentered(
            visual,
            std::to_string(count),
            currentX + chipWidth * 0.5f,
            boxY + boxHeight * 0.5f,
            std::clamp(boxHeight * 0.55f, 10.0f, 14.0f),
            UiTheme::applyOpacity(UiTheme::fade(inkOn(fill), 0.8f),
                                  rowOpacity));
        }
      } else {
        // Directional rules: what the cell becomes among empty neighbors.
        const unsigned char next =
          static_cast<std::size_t>(row.state) * 9u < previewNext.size()
            ? previewNext[static_cast<std::size_t>(row.state) * 9u]
            : 1u;
        const float swatch = std::clamp(boxHeight * 0.7f, 10.0f, 16.0f);
        const float swatchY = boxY + (boxHeight - swatch) * 0.5f;
        drawSwatch(visual,
                   valueX,
                   swatchY,
                   swatch,
                   swatch,
                   3.0f,
                   stateColor(familyDraft, row.state),
                   rowOpacity);
        GuiKit::drawSideChevron(
          visual,
          valueX + swatch + 18.0f,
          swatchY + swatch * 0.5f,
          4.5f,
          4.0f,
          1.8f,
          UiTheme::applyOpacity(UiTheme::fade(cyan, 0.8f), rowOpacity));
        drawSwatch(visual,
                   valueX + swatch + 28.0f,
                   swatchY,
                   swatch,
                   swatch,
                   3.0f,
                   stateColor(familyDraft, next),
                   rowOpacity);
        visual.addText(
          stateLabel(familyDraft, next) + "  (empty neighbors)",
          valueX + swatch * 2.0f + 38.0f,
          boxY + (boxHeight - 12.0f) * 0.5f,
          12.0f,
          UiTheme::applyOpacity(UiTheme::textSecondary(), rowOpacity));
      }
    } else if (control == Control::PreviewNeighborhood) {
      // Each left-centre-right pattern, painted in the centre cell's next
      // state.
      chipStrip(8, &chipX, &chipWidth, &gap);
      for (unsigned int pattern = 0u; pattern < 8u; ++pattern) {
        const ColorRgba fill = stateColor(
          familyDraft, previewValid ? previewElementaryNext[pattern] : 1u);
        const float currentX = chipX + (chipWidth + gap) * pattern;
        GuiKit::drawRoundedRect(
          visual,
          currentX - 1.0f,
          boxY - 1.0f,
          chipWidth + 2.0f,
          boxHeight + 2.0f,
          6.0f,
          UiTheme::applyOpacity(UiTheme::mix(UiTheme::panelBorder(),
                                             ColorRgba{ 150, 176, 204, 255 },
                                             0.35f),
                                rowOpacity));
        GuiKit::drawRoundedRect(visual,
                                currentX,
                                boxY,
                                chipWidth,
                                boxHeight,
                                5.0f,
                                UiTheme::applyOpacity(fill, rowOpacity));
        if (selected && pattern == previewNeighborhood) {
          drawChipCursor(currentX);
        }
        GuiKit::drawTextCentered(
          visual,
          std::to_string((pattern >> 2u) & 1u) +
            std::to_string((pattern >> 1u) & 1u) + std::to_string(pattern & 1u),
          currentX + chipWidth * 0.5f,
          boxY + boxHeight * 0.5f,
          std::clamp(boxHeight * 0.45f, 9.0f, 12.0f),
          UiTheme::applyOpacity(UiTheme::fade(inkOn(fill), 0.85f), rowOpacity));
      }
    } else if (control == Control::RulesetName ||
               control == Control::FamilyName ||
               control == Control::StateName) {
      // An inset field that lights its rim while it takes typing.
      GuiKit::drawRoundedRect(
        visual,
        valueX - 1.0f,
        boxY - 1.0f,
        valueWidth + 2.0f,
        boxHeight + 2.0f,
        6.0f,
        UiTheme::applyOpacity(
          UiTheme::mix(UiTheme::panelBorder(), cyan, selected ? 0.8f : 0.0f),
          rowOpacity));
      GuiKit::drawRoundedGradientRect(
        visual,
        valueX,
        boxY,
        valueWidth,
        boxHeight,
        5.0f,
        UiTheme::applyOpacity(ColorRgba{ 5, 11, 22, 240 }, rowOpacity),
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
      const std::string value = valueForControl(control);
      const bool placeholder =
        (control == Control::RulesetName && draft.name.empty()) ||
        (control == Control::FamilyName && familyDraft.name.empty()) ||
        (control == Control::StateName &&
         paletteState < familyDraft.stateNames.size() &&
         familyDraft.stateNames[paletteState].empty());
      const std::string displayValue = value.substr(0u, 32u);
      const float fieldFont = std::clamp(rowFontSize * 0.86f, 13.0f, 15.0f);
      visual.addText(displayValue,
                     valueX + 10.0f,
                     textY,
                     fieldFont,
                     UiTheme::applyOpacity(placeholder ? UiTheme::textMuted()
                                                       : UiTheme::textPrimary(),
                                           rowOpacity));
      if (selected && animator.caretVisible()) {
        const float caretX = std::min(
          GuiKit::caretOriginAfterText(displayValue, valueX + 10.0f, fieldFont),
          valueEnd - 8.0f);
        visual.addText("|",
                       caretX,
                       textY,
                       fieldFont,
                       UiTheme::applyOpacity(cyan, rowOpacity));
      }
      if (!selected) {
        visual.addText(
          "EDIT",
          valueEnd - 32.0f,
          boxY + (boxHeight - 9.5f) * 0.5f,
          9.5f,
          UiTheme::applyOpacity(UiTheme::fade(cyan, 0.5f), rowOpacity));
      }
    } else if (control == Control::Red || control == Control::Green ||
               control == Control::Blue) {
      // A track painted with the color this channel would mix, a knob at its
      // value, and the number beside it.
      const std::size_t channel = control == Control::Red     ? 0u
                                  : control == Control::Green ? 1u
                                                              : 2u;
      const ColorRgba current = stateColor(familyDraft, paletteState);
      ColorRgba low = current;
      ColorRgba high = current;
      unsigned char* lowChannel =
        channel == 0u ? &low.r : (channel == 1u ? &low.g : &low.b);
      unsigned char* highChannel =
        channel == 0u ? &high.r : (channel == 1u ? &high.g : &high.b);
      *lowChannel = 0u;
      *highChannel = 255u;
      const unsigned char amount =
        channel == 0u ? current.r : (channel == 1u ? current.g : current.b);
      const float trackLeft = sliderTrackLeft();
      const float trackWidth = sliderTrackRight() - trackLeft;
      const float centerY = rowY + (rowHeight - 4.0f) * 0.5f;
      GuiKit::drawRoundedRect(
        visual,
        trackLeft - 1.0f,
        centerY - 5.0f,
        trackWidth + 2.0f,
        10.0f,
        5.0f,
        UiTheme::applyOpacity(UiTheme::panelBorder(), rowOpacity));
      GuiKit::drawRoundedSideGradientRect(
        visual,
        trackLeft,
        centerY - 4.0f,
        trackWidth,
        8.0f,
        4.0f,
        UiTheme::applyOpacity(low, rowOpacity),
        UiTheme::applyOpacity(high, rowOpacity));
      const float knobX =
        trackLeft + trackWidth * static_cast<float>(amount) / 255.0f;
      const bool dragging = dragControl == control;
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
        knobX - knobSize * 0.5f - 1.0f,
        centerY - knobSize * 0.5f - 1.0f,
        knobSize + 2.0f,
        knobSize + 2.0f,
        knobSize * 0.5f + 1.0f,
        UiTheme::applyOpacity(ColorRgba{ 8, 14, 26, 255 }, rowOpacity));
      GuiKit::drawRoundedRect(
        visual,
        knobX - knobSize * 0.5f,
        centerY - knobSize * 0.5f,
        knobSize,
        knobSize,
        knobSize * 0.5f,
        UiTheme::applyOpacity(UiTheme::textPrimary(), rowOpacity));
      const float readoutFont = std::clamp(rowFontSize * 0.86f, 13.0f, 15.0f);
      visual.addText(
        valueForControl(control),
        sliderTrackRight() + 14.0f + nudge * 4.0f,
        centerY - readoutFont * 0.5f,
        readoutFont,
        UiTheme::applyOpacity(UiTheme::mix(UiTheme::textSecondary(),
                                           UiTheme::textPrimary(),
                                           eClamped),
                              rowOpacity));
    } else if (control == Control::PaletteState &&
               familyDraft.stateCount <= kSwatchPickerLimit) {
      // Pick a state by its swatch; the chosen one wears a ring.
      const int count = static_cast<int>(familyDraft.stateCount);
      chipStrip(count, &chipX, &chipWidth, &gap);
      for (int state = 0; state < count; ++state) {
        const float currentX =
          chipX + (chipWidth + gap) * static_cast<float>(state);
        const bool chosen = static_cast<unsigned int>(state) == paletteState;
        drawSwatch(visual,
                   currentX,
                   boxY,
                   chipWidth,
                   boxHeight,
                   5.0f,
                   stateColor(familyDraft, static_cast<unsigned int>(state)),
                   rowOpacity);
        if (chosen) {
          if (selected) {
            drawChipCursor(currentX);
          } else {
            GuiKit::drawRoundedOutline(
              visual,
              currentX - 2.0f,
              boxY - 2.0f,
              chipWidth + 4.0f,
              boxHeight + 4.0f,
              6.0f,
              1.5f,
              UiTheme::applyOpacity(UiTheme::fade(cyan, 0.8f), rowOpacity));
          }
        }
      }
    } else if (isListControl(control)) {
      // A drop-down field, as in New Canvas: the value at its left and a
      // caret at its right, pointing up while its list is open. The value
      // leans the way Left/Right stepped it.
      const bool listOpen = dropdown.isOpen() && listControl == control;
      GuiKit::drawRoundedRect(
        visual,
        valueX - 1.0f,
        boxY - 1.0f,
        valueWidth + 2.0f,
        boxHeight + 2.0f,
        6.0f,
        UiTheme::applyOpacity(
          UiTheme::mix(UiTheme::panelBorder(), cyan, listOpen ? 0.8f : 0.0f),
          rowOpacity));
      GuiKit::drawRoundedGradientRect(
        visual,
        valueX,
        boxY,
        valueWidth,
        boxHeight,
        5.0f,
        UiTheme::applyOpacity(ColorRgba{ 5, 11, 22, 230 }, rowOpacity),
        UiTheme::applyOpacity(UiTheme::panelInset(), rowOpacity));
      const float valueFont = std::clamp(boxHeight * 0.58f, 12.0f, 14.0f);
      visual.addText(
        valueForControl(control),
        valueX + 12.0f + nudge * 6.0f,
        boxY + std::max(2.0f, (boxHeight - valueFont) * 0.5f),
        valueFont,
        UiTheme::applyOpacity(
          UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped), rowOpacity));
      const float caretY = boxY + boxHeight * 0.5f;
      GuiKit::drawChevron(
        visual,
        valueX + valueWidth - 14.0f,
        listOpen ? caretY - 2.0f : caretY + 2.5f,
        4.5f,
        listOpen ? 4.0f : -4.0f,
        1.8f,
        UiTheme::applyOpacity(UiTheme::fade(cyan, 0.45f + 0.55f * eClamped),
                              rowOpacity));
    } else {
      drawValueStepper(visual,
                       valueX,
                       boxY,
                       valueWidth,
                       boxHeight,
                       valueForControl(control),
                       UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped),
                       eClamped,
                       rowOpacity,
                       nudge);
      if (control == Control::PaletteState) {
        const float swatch = std::clamp(rowHeight * 0.42f, 11.0f, 16.0f);
        drawSwatch(visual,
                   valueX - swatch - 10.0f,
                   rowY + (rowHeight - 4.0f - swatch) * 0.5f,
                   swatch,
                   swatch,
                   4.0f,
                   stateColor(familyDraft, paletteState),
                   rowOpacity);
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
    const float thumbRow = std::clamp(scrollThumb.value(), 0.0f, maxOffset);
    const float thumbY = trackTop + trackTravel * thumbRow / maxOffset;
    GuiKit::drawRoundedRect(
      visual,
      panelX + panelWidth - 13.0f,
      trackTop,
      4.0f,
      trackHeight,
      2.0f,
      UiTheme::applyOpacity(UiTheme::panelInset(), panelOpacity));
    GuiKit::drawRoundedRect(
      visual,
      panelX + panelWidth - 13.0f,
      thumbY,
      4.0f,
      thumbHeight,
      2.0f,
      UiTheme::applyOpacity(UiTheme::accent(), panelOpacity));
  }

  drawFooter(panelOpacity);
  const int listRow = rowForControl(listControl);
  if (dropdown.isOpen() && listRow >= 0) {
    // The list rides its field as the panel tilts, above everything else.
    const std::array<float, 4> field = valueField(listRow);
    dropdown.follow(field[0], field[1]);
    dropdown.draw(layers[kListCardLayer],
                  layers[kListDropLayer],
                  layers[kListTextLayer],
                  panelOpacity);
  }
  for (GameVisual& layer : layers) {
    GuiPanelLayout::scaleFromScreenOrigin(layer, visualScale);
  }
  setVisible(true);
}

bool
RulesetWorkshopMenu::AppendCommands(Renderer* targetRenderer)
{
  ILLUMO_PROFILE_ZONE("RulesetWorkshopMenu.AppendCommands");
  if (!isVisible()) {
    return true;
  }
  bool appended = true;
  for (GameVisual& layer : layers) {
    appended = layer.AppendCommands(targetRenderer) && appended;
  }
  return appended;
}
