#include "NewSimulationMenu.h"
#include "CSimSounds.h"
#include "CellContext.h"
#include "Rulesets/RuleSetRegistry.h"
#include "SparseCellGrid.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiPointerHint.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <queue>

bool
NewSimulationConfiguration::isValid() const
{
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(ruleSet);
  return rule != nullptr && rule->familyId == family &&
         SparseCellGrid::isValidTopology(worldChunkWidth, worldChunkHeight);
}

NewSimulationMenu::NewSimulationMenu(IRenderWindow* target, Renderer* owner)
  : window(target)
  , renderer(owner)
{
  for (GameVisual& layer : layers) {
    layer.setWindow(window);
    layer.setRenderer(renderer);
    layer.setSpace(PrimitiveSpace::Pixels);
    layer.setLayerHint(RenderLayerId::UI);
    layer.prepare(renderer);
  }
}

void
NewSimulationMenu::open(const NewSimulationConfiguration& initial,
                        bool reducedMotion)
{
  draft = initial.isValid() ? initial : NewSimulationConfiguration{};
  finite = draft.worldChunkWidth > 0;
  if (!finite) {
    draft.worldChunkWidth = 32;
    draft.worldChunkHeight = 24;
  }
  animator.setReducedMotion(reducedMotion);
  animator.restart();
  selected = 0;
  focus.configure(GuiMotion::kJelly);
  focus.focusOnly(selected, kRowCount);
  focus.snapAll();
  modeBlend.configure(GuiMotion::kSwell);
  modeBlend.snapTo(finite ? 1.0f : 0.0f);
  motif.resetGlider(6, 6, 1, 1);
  // Ignore the held click that opened the menu until its first release.
  pointer.reset(true);
  tilt.level();
  dropdown.close();
  listRow = -1;
  openState = true;
  rebuild();
}

NewSimulationConfiguration
NewSimulationMenu::configuration() const
{
  NewSimulationConfiguration result = draft;
  if (!finite) {
    result.worldChunkWidth = 0;
    result.worldChunkHeight = 0;
  }
  return result;
}

void
NewSimulationMenu::tick(float dt)
{
  ILLUMO_PROFILE_ZONE("NewSimulationMenu.tick");
  if (std::isfinite(dt)) {
    const float step = std::clamp(dt, 0.0f, 0.1f);
    const bool still = animator.reducedMotion();
    animator.tick(step);
    focus.focusOnly(selected, kRowCount);
    focus.tick(step, still);
    modeBlend.setTarget(finite ? 1.0f : 0.0f);
    modeBlend.tick(step, still);
    motif.tick(step, still);
    tilt.aim(pointer.x(),
             pointer.y(),
             panelFit.virtualWidth,
             panelFit.virtualHeight,
             openState);
    tilt.tick(step, still);
    dropdown.tick(step);
  }
  rebuild();
}

void
NewSimulationMenu::valueBox(int row,
                            float* boxX,
                            float* boxY,
                            float* boxWidth,
                            float* boxHeight) const
{
  const float ry = y + 110 + static_cast<float>(row) * rowHeight +
                   animator.rowDrop(row, 0) * 10.0f;
  *boxX = x + width * 0.45f;
  *boxY = ry + 5;
  *boxWidth = width * 0.55f - 37;
  *boxHeight = rowHeight - 15;
}

void
NewSimulationMenu::openList(int row)
{
  std::vector<GuiDropdownItem> items;
  listIds.clear();
  int current = -1;
  if (row == 0) {
    // Families that have rules, each with how many it offers.
    for (const std::string& id : CellContext::GetKnownFamilyStrings()) {
      const std::size_t rules = CellContext::GetKnownRuleStrings(id).size();
      if (rules == 0u) {
        continue;
      }
      const RuleFamilyDefinition* family =
        RuleSetRegistry::instance().getFamilyDefinition(id);
      if (id == draft.family) {
        current = static_cast<int>(items.size());
      }
      items.push_back({ family == nullptr ? id : family->name,
                        std::to_string(rules) +
                          (rules == 1u ? " rule" : " rules") });
      listIds.push_back(id);
    }
  } else {
    for (const std::string& id :
         RuleSetRegistry::instance().getKnownRules(draft.family)) {
      const RuleSetDefinition* rule =
        RuleSetRegistry::instance().getRuleSetDefinition(id);
      if (id == draft.ruleSet) {
        current = static_cast<int>(items.size());
      }
      items.push_back({ rule == nullptr ? id : rule->name, "" });
      listIds.push_back(id);
    }
  }
  float boxX = 0;
  float boxY = 0;
  float boxWidth = 0;
  float boxHeight = 0;
  valueBox(row, &boxX, &boxY, &boxWidth, &boxHeight);
  dropdown.open(std::move(items),
                current,
                boxX,
                boxY,
                boxWidth,
                boxHeight,
                8.0f,
                panelFit.virtualHeight - 8.0f,
                animator.reducedMotion());
  if (dropdown.isOpen()) {
    listRow = row;
    CSimSounds::play(CSimSound::MenuSelect);
  }
}

void
NewSimulationMenu::chooseFromList(int index)
{
  if (index < 0 || index >= static_cast<int>(listIds.size())) {
    return;
  }
  const std::string& id = listIds[static_cast<std::size_t>(index)];
  if (listRow == 0) {
    if (id != draft.family) {
      // A new family keeps the ruleset only if it belongs to that family.
      draft.family = id;
      const std::vector<std::string> rules =
        CellContext::GetKnownRuleStrings(id);
      if (!rules.empty() &&
          std::find(rules.begin(), rules.end(), draft.ruleSet) == rules.end()) {
        draft.ruleSet = rules.front();
      }
    }
  } else {
    draft.ruleSet = id;
  }
  animator.triggerValuePulse(1);
  CSimSounds::play(CSimSound::MenuSelect);
}

NewSimulationAction
NewSimulationMenu::updateList(InputManager* input)
{
  // The open list takes every key, typed letter, the wheel and the pointer;
  // the rows beneath it keep their selection.
  rebuild();
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
    listRow = -1;
  }
  rebuild();
  return NewSimulationAction::None;
}

void
NewSimulationMenu::selectRow(int row)
{
  if (row == selected) {
    return;
  }
  animator.beginSelectionTravel(static_cast<float>(selected),
                                static_cast<float>(row));
  selected = row;
  CSimSounds::play(CSimSound::MenuHover);
}

void
NewSimulationMenu::select(int direction)
{
  int row = selected;
  do {
    row = (row + direction + kRowCount) % kRowCount;
  } while (!finite && (row == 3 || row == 4));
  selectRow(row);
}

void
NewSimulationMenu::change(int direction)
{
  animator.triggerValuePulse(direction);
  bool changed = true;
  if (selected == 0) {
    const std::vector<std::string> families =
      CellContext::GetKnownFamilyStrings();
    if (!families.empty()) {
      std::size_t index = 0u;
      const std::vector<std::string>::const_iterator found =
        std::find(families.begin(), families.end(), draft.family);
      if (found != families.end()) {
        index = static_cast<std::size_t>(found - families.begin());
      }
      for (std::size_t attempt = 0u; attempt < families.size(); ++attempt) {
        index = direction > 0
                  ? (index + 1u) % families.size()
                  : (index + families.size() - 1u) % families.size();
        const std::vector<std::string> rules =
          CellContext::GetKnownRuleStrings(families[index]);
        if (!rules.empty()) {
          draft.family = families[index];
          draft.ruleSet = rules.front();
          break;
        }
      }
    }
  } else if (selected == 1) {
    const std::vector<std::string> rules =
      RuleSetRegistry::instance().getKnownRules(draft.family);
    if (!rules.empty()) {
      const int count = static_cast<int>(rules.size());
      const int index = static_cast<int>(
        std::find(rules.begin(), rules.end(), draft.ruleSet) - rules.begin());
      draft.ruleSet =
        rules[static_cast<std::size_t>((index + direction + count) % count)];
    }
  } else if (selected == 2) {
    finite = !finite;
  } else if (finite && (selected == 3 || selected == 4)) {
    std::int64_t& dimension =
      selected == 3 ? draft.worldChunkWidth : draft.worldChunkHeight;
    const std::int64_t before = dimension;
    dimension = std::clamp(dimension + direction,
                           std::int64_t{ 1 },
                           SparseCellGrid::kMaximumWorldChunksPerAxis);
    // A size already at its limit does not move.
    changed = dimension != before;
  } else if (selected == 5) {
    draft.starterPattern = !draft.starterPattern;
  } else {
    return; // the action rows have no value
  }
  CSimSounds::play(changed ? CSimSound::MenuSelect : CSimSound::MenuError);
}

NewSimulationAction
NewSimulationMenu::activate()
{
  if (selected == 6) {
    if (!configuration().isValid()) {
      CSimSounds::play(CSimSound::MenuError);
      return NewSimulationAction::None;
    }
    return NewSimulationAction::Create;
  }
  if (selected == 7) {
    return NewSimulationAction::Back;
  }
  if (isListRow(selected)) {
    openList(selected);
    return NewSimulationAction::None;
  }
  change(1);
  return NewSimulationAction::None;
}

NewSimulationAction
NewSimulationMenu::update(InputManager* input)
{
  ILLUMO_PROFILE_ZONE("NewSimulationMenu.update");
  if (!openState || input == nullptr) {
    return NewSimulationAction::None;
  }
  if (dropdown.isOpen()) {
    return updateList(input);
  }
  NewSimulationAction result = NewSimulationAction::None;
  std::queue<InputManager::KeyPressEvent> remaining;
  std::queue<InputManager::KeyPressEvent>& keys = input->getKeyQueue();
  while (!keys.empty()) {
    const InputManager::KeyPressEvent event = keys.front();
    keys.pop();
    if (event.key == KeyCode::Grave) {
      remaining.push(event);
      continue;
    }
    if (result != NewSimulationAction::None ||
        (event.action != InputAction::Press &&
         event.action != InputAction::Hold)) {
      continue;
    }
    if (event.key == KeyCode::Escape)
      result = NewSimulationAction::Back;
    else if (event.key == KeyCode::Down || event.key == KeyCode::Tab)
      select(1);
    else if (event.key == KeyCode::Up)
      select(-1);
    else if (event.key == KeyCode::Left)
      change(-1);
    else if (event.key == KeyCode::Right)
      change(1);
    else if (event.key == KeyCode::Home)
      selectRow(0);
    else if (event.key == KeyCode::End)
      selectRow(kRowCount - 1);
    else if (event.key == KeyCode::Enter && event.action == InputAction::Press)
      result = activate();
  }
  keys.swap(remaining);
  while (!input->getCharQueue().empty())
    input->getCharQueue().pop();
  // All rows fit; the wheel must never change a field or keyboard selection.
  double* wheel = input->getMouseScrollOffset();
  if (wheel != nullptr)
    *wheel = 0;
  rebuild();
  pointer.sample(window, input, panelFit.layoutScale);
  const float mx = pointer.x();
  const float my = pointer.y();
  if (mx >= x + 24 && mx < x + width - 24 && my >= y + 110 &&
      my < y + 110 + rowHeight * kRowCount) {
    // The disabled world-size rows are not pressable.
    const int hoverRow = static_cast<int>((my - y - 110) / rowHeight);
    if (finite || (hoverRow != 3 && hoverRow != 4)) {
      GuiPointerHint::markInteractive();
    }
  }
  if (result == NewSimulationAction::None &&
      (pointer.moved() || pointer.clicked()) && mx >= x + 24 &&
      mx < x + width - 24 && my >= y + 110 &&
      my < y + 110 + rowHeight * kRowCount) {
    const int row = static_cast<int>((my - y - 110) / rowHeight);
    if (finite || (row != 3 && row != 4)) {
      selectRow(row);
      if (pointer.clicked()) {
        // The family and ruleset rows open their list anywhere; the other
        // value rows step back on their left half.
        if (!isListRow(selected) && selected < 6 && mx < x + width * 0.55f)
          change(-1);
        else
          result = activate();
      }
    } else if (pointer.clicked()) {
      // World size rows are disabled while the world is infinite.
      CSimSounds::play(CSimSound::MenuError);
    }
  }
  rebuild();
  return result;
}

void
NewSimulationMenu::addDrawables(DrawList& scene)
{
  for (GameVisual& layer : layers) {
    scene.AddDrawable(&layer, RenderLayerId::UI);
  }
}

void
NewSimulationMenu::rebuild()
{
  ILLUMO_PROFILE_ZONE("NewSimulationMenu.rebuild");
  panelFit = GuiPanelLayout::fit(window, renderer, nullptr);
  const float screenWidth = panelFit.virtualWidth;
  const float screenHeight = panelFit.virtualHeight;
  width = std::min(720.0f, screenWidth - 32);
  const float height = std::min(610.0f, screenHeight - 24);
  // The layout origin swivels with the body layer, so hit testing and
  // drawing agree while the panel tilts toward the pointer.
  x = (screenWidth - width) / 2 + tilt.bodyShiftX();
  y = (screenHeight - height) / 2 + animator.panelOffsetY() + tilt.bodyShiftY();
  rowHeight = (height - 164) / static_cast<float>(kRowCount);
  for (GameVisual& layer : layers) {
    layer.clearPrimitives();
  }
  GameVisual& glassLayer = layers[kGlassLayer];
  GameVisual& visual = layers[kHeaderLayer];
  GameVisual& accent = layers[kAccentLayer];

  const float scrim = animator.openReveal(0.18f);
  const float reveal = animator.panelReveal();
  const unsigned char opacity = static_cast<unsigned char>(255.0f * reveal);
  const float ambient = animator.ambientPhase();
  const float breathe = 0.5f + 0.5f * std::sin(ambient * 1.04719755f);
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();

  GuiKit::drawVignette(glassLayer,
                       screenWidth,
                       screenHeight,
                       UiTheme::fade(UiTheme::scrimCenter(), scrim),
                       UiTheme::fade(UiTheme::scrimEdge(), scrim),
                       0.3f);
  GuiGlassStyle glass;
  glass.opacity = opacity;
  glass.ambientPhase = ambient;
  glass.glow = 0.4f + 0.3f * breathe;
  glass.accentReveal = reveal;
  tilt.applyTo(glass);
  GuiKit::drawGlassPanel(glassLayer,
                         x + tilt.layerX(GuiPanelTilt::kGlassDepth),
                         y + tilt.layerY(GuiPanelTilt::kGlassDepth),
                         width,
                         height,
                         glass);

  // The header floats nearer than the rows and swings further with the tilt.
  const float headerX = x + tilt.layerX(GuiPanelTilt::kHeaderDepth);
  const float headerY = y + tilt.layerY(GuiPanelTilt::kHeaderDepth);
  const float headerSlide = (1.0f - reveal) * 10.0f;
  visual.addText("A FRESH WORLD",
                 headerX + 28 - headerSlide,
                 headerY + 20,
                 11,
                 UiTheme::applyOpacity(cyan, opacity));
  visual.addText("NEW CANVAS",
                 headerX + 28 - headerSlide * 0.6f,
                 headerY + 41,
                 28,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));
  visual.addText("Choose the space. Then see what emerges.",
                 headerX + 28 - headerSlide * 0.3f,
                 headerY + 80,
                 13,
                 UiTheme::applyOpacity(UiTheme::textMuted(), opacity));

  // A live glider walks a 6x6 torus, echoing the title screen's motif; it is
  // the nearest layer and swings furthest.
  motif.draw(accent,
             x + width - 90 + tilt.layerX(GuiPanelTilt::kAccentDepth),
             y + 15 + tilt.layerY(GuiPanelTilt::kAccentDepth),
             7.0f,
             3.0f,
             ColorRgba{ 104, 231, 241, 255 },
             ColorRgba{ 44, 68, 100, 150 },
             0.7f,
             opacity);

  // The boundary badge crossfades between infinite (cyan) and torus (violet).
  const float mode = std::clamp(modeBlend.value(), 0.0f, 1.0f);
  const ColorRgba modeColor = UiTheme::mix(cyan, violet, mode);
  const float modeBadgeX = headerX + width - 164;
  const float modeBadgeY = headerY + 79;
  GuiKit::drawRoundedRect(
    visual,
    modeBadgeX,
    modeBadgeY,
    140,
    22,
    11,
    UiTheme::applyOpacity(UiTheme::fade(modeColor, 0.45f), opacity));
  GuiKit::drawRoundedGradientRect(
    visual,
    modeBadgeX + 1,
    modeBadgeY + 1,
    138,
    20,
    10,
    UiTheme::applyOpacity(ColorRgba{ 16, 26, 44, 255 }, opacity),
    UiTheme::applyOpacity(UiTheme::panelInset(), opacity));
  GuiKit::drawSoftGlow(
    accent,
    modeBadgeX + 13,
    modeBadgeY + 11,
    12,
    12,
    UiTheme::applyOpacity(UiTheme::fade(modeColor, 0.35f + 0.25f * breathe),
                          opacity),
    12);
  accent.addFilledEllipse(modeBadgeX + 10,
                          modeBadgeY + 8,
                          6,
                          6,
                          UiTheme::applyOpacity(modeColor, opacity));
  accent.addText(finite ? "FINITE TORUS" : "INFINITE FIELD",
                 modeBadgeX + 22,
                 modeBadgeY + 6,
                 10,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));

  // Divider: a cyan-to-violet hairline that fades at both ends.
  const float dividerWidth = width - 48;
  const ColorRgba dividerCyan =
    UiTheme::applyOpacity(UiTheme::fade(cyan, 0.45f), opacity);
  const ColorRgba dividerViolet =
    UiTheme::applyOpacity(UiTheme::fade(violet, 0.45f), opacity);
  visual.addGradientRect(x + 24,
                         y + 103,
                         dividerWidth * 0.25f,
                         1,
                         UiTheme::transparentOf(cyan),
                         dividerCyan,
                         dividerCyan,
                         UiTheme::transparentOf(cyan));
  visual.addGradientRect(x + 24 + dividerWidth * 0.25f,
                         y + 103,
                         dividerWidth * 0.5f,
                         1,
                         dividerCyan,
                         dividerViolet,
                         dividerViolet,
                         dividerCyan);
  visual.addGradientRect(x + 24 + dividerWidth * 0.75f,
                         y + 103,
                         dividerWidth * 0.25f,
                         1,
                         dividerViolet,
                         UiTheme::transparentOf(violet),
                         UiTheme::transparentOf(violet),
                         dividerViolet);

  drawRows(opacity, breathe);
  drawFooter(height, opacity);
  if (dropdown.isOpen() && listRow >= 0) {
    // The list rides its field as the panel tilts, above everything else.
    float boxX = 0;
    float boxY = 0;
    float boxWidth = 0;
    float boxHeight = 0;
    valueBox(listRow, &boxX, &boxY, &boxWidth, &boxHeight);
    dropdown.follow(boxX, boxY);
    dropdown.draw(layers[kListCardLayer],
                  layers[kListDropLayer],
                  layers[kListTextLayer],
                  opacity);
  }
  const float scale = GuiPanelLayout::visualScale(panelFit, renderer);
  for (GameVisual& layer : layers) {
    GuiPanelLayout::scaleFromScreenOrigin(layer, scale);
  }
}

void
NewSimulationMenu::drawRows(unsigned char opacity, float breathe)
{
  ILLUMO_PROFILE_ZONE("NewSimulationMenu.drawRows");
  GameVisual& cards = layers[kCardLayer];
  GameVisual& dropLayer = layers[kDropLayer];
  GameVisual& visual = layers[kContentLayer];
  const ColorRgba cyan = UiTheme::accentCool();
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(draft.ruleSet);
  std::string ruleName = rule == nullptr ? draft.ruleSet : rule->name;
  if (ruleName.size() > 30)
    ruleName = ruleName.substr(0, 27) + "...";
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(draft.family);
  const std::string familyName =
    family == nullptr ? draft.family : family->name;
  const std::string labels[] = { "Cell family",   "Ruleset", "Canvas boundary",
                                 "Width",         "Height",  "Starting cells",
                                 "Create canvas", "Back" };
  const std::string values[] = {
    familyName,
    ruleName,
    finite ? "Wrap opposite edges" : "Infinite",
    finite ? std::to_string(draft.worldChunkWidth * 16) + " cells"
           : "Unbounded",
    finite ? std::to_string(draft.worldChunkHeight * 16) + " cells"
           : "Unbounded",
    draft.starterPattern ? "Starter pattern" : "Empty canvas",
    // Its arrow is drawn after the text (drawRows).
    "Enter to begin",
    "Discard choices"
  };
  const float cardX = x + 24;
  const float cardWidth = width - 48;
  const float cardHeight = rowHeight - 5;
  const float radius = 10;
  const float firstRowY = y + 110;

  // Cards: soft shadow, a rim that warms with emphasis, a top-lit face. The
  // Create row carries a teal face so the primary action reads at a glance.
  for (int row = 0; row < kRowCount; ++row) {
    const float rowReveal = animator.rowReveal(row, 0);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(static_cast<float>(opacity) * rowReveal);
    const float ry = firstRowY + static_cast<float>(row) * rowHeight +
                     animator.rowDrop(row, 0) * 10.0f;
    const float e = std::clamp(focus.value(row), 0.0f, 1.0f);
    const bool primary = row == 6;
    GuiKit::drawSoftShadow(
      cards,
      cardX,
      ry,
      cardWidth,
      cardHeight,
      radius,
      9,
      3,
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::glowShadow(), 0.7f),
                            rowOpacity));
    GuiKit::drawRoundedRect(
      cards,
      cardX,
      ry,
      cardWidth,
      cardHeight,
      radius,
      UiTheme::applyOpacity(UiTheme::mix(primary
                                           ? ColorRgba{ 52, 128, 150, 255 }
                                           : UiTheme::cardRim(),
                                         ColorRgba{ 80, 160, 190, 255 },
                                         e * 0.6f),
                            rowOpacity));
    GuiKit::drawRoundedGradientRect(
      cards,
      cardX + 1,
      ry + 1,
      cardWidth - 2,
      cardHeight - 2,
      radius - 1,
      UiTheme::applyOpacity(primary ? ColorRgba{ 30, 88, 110, 255 }
                                    : UiTheme::cardTop(),
                            rowOpacity),
      UiTheme::applyOpacity(primary ? ColorRgba{ 18, 54, 74, 255 }
                                    : UiTheme::cardBottom(),
                            rowOpacity));
  }

  // The selection pours between rows like a drop of liquid, with a
  // breathing glow and an arrival sheen.
  const GuiSelectionSpan span =
    animator.selectionSpan(static_cast<float>(selected));
  const float selectedDrop = animator.rowDrop(selected, 0) * 10.0f;
  GuiLiquidSelection drop;
  drop.crossStart = cardX;
  drop.crossSize = cardWidth;
  drop.headStart = firstRowY + span.leading * rowHeight + selectedDrop;
  drop.tailStart = firstRowY + span.trailing * rowHeight + selectedDrop;
  drop.cellLength = cardHeight;
  drop.radius = radius;
  drop.squash = span.squash;
  drop.glowSpread = 12 + 3 * breathe;
  drop.glow =
    UiTheme::applyOpacity(UiTheme::fade(cyan, 0.16f + 0.1f * breathe), opacity);
  drop.rim = UiTheme::applyOpacity(UiTheme::fade(cyan, 0.85f), opacity);
  drop.rimBottom = UiTheme::accentBlendOf(drop.rim);
  drop.glowBottom = UiTheme::accentBlendOf(drop.glow);
  drop.faceTop = UiTheme::applyOpacity(UiTheme::selectionTop(), opacity);
  drop.faceBottom = UiTheme::applyOpacity(UiTheme::selectionBottom(), opacity);
  drop.sheen = animator.selectionSheen();
  drop.sheenColor =
    UiTheme::applyOpacity(ColorRgba{ 210, 250, 255, 40 }, opacity);
  GuiKit::drawLiquidSelection(dropLayer, drop);
  // The accent bar rides the head of the drop.
  GuiKit::drawRoundedRect(dropLayer,
                          x + 31,
                          drop.headStart + 9,
                          3,
                          std::max(4.0f, cardHeight - 18),
                          1.5f,
                          UiTheme::applyOpacity(cyan, opacity));

  const float pulse = animator.valuePulse();
  const float direction = static_cast<float>(animator.valuePulseDirection());
  for (int row = 0; row < kRowCount; ++row) {
    const bool disabled = !finite && (row == 3 || row == 4);
    const float rowReveal = animator.rowReveal(row, 0);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(static_cast<float>(opacity) * rowReveal);
    const float ry = firstRowY + static_cast<float>(row) * rowHeight +
                     animator.rowDrop(row, 0) * 10.0f;
    const float e = std::clamp(focus.value(row), 0.0f, 1.2f);
    const float eClamped = std::min(1.0f, e);
    const bool active = row == selected;
    if (!disabled && row < 6) {
      const float boxX = x + width * 0.45f;
      const float boxWidth = width * 0.55f - 37;
      GuiKit::drawRoundedGradientRect(
        visual,
        boxX,
        ry + 5,
        boxWidth,
        rowHeight - 15,
        7,
        UiTheme::applyOpacity(ColorRgba{ 6, 13, 26, 170 }, rowOpacity),
        UiTheme::applyOpacity(ColorRgba{ 12, 22, 38, 150 }, rowOpacity));
      if (active && pulse > 0.0f) {
        GuiKit::drawRoundedBand(visual,
                                boxX,
                                ry + 5,
                                boxWidth,
                                rowHeight - 15,
                                7,
                                -1,
                                8,
                                UiTheme::fade(cyan, 0.5f * pulse),
                                UiTheme::transparentOf(cyan));
      }
      // The arrow on the side the value moved springs out and bounces back.
      const float swing = direction * animator.valueWobble();
      const float leftNudge = active && direction < 0 ? 5.0f * swing : 0.0f;
      const float rightNudge = active && direction > 0 ? 5.0f * swing : 0.0f;
      const ColorRgba arrow = UiTheme::applyOpacity(
        UiTheme::fade(cyan, 0.45f + 0.55f * eClamped), rowOpacity);
      // Drawn chevrons rather than the font's "<" and ">", so they stay crisp;
      // centered on the value box.
      const float arrowY = ry + 5 + (rowHeight - 15) * 0.5f;
      if (isListRow(row)) {
        // A list field shows a caret at its right end: down while closed,
        // up while its list is open.
        const bool listOpen = dropdown.isOpen() && listRow == row;
        GuiKit::drawChevron(visual,
                            x + width - 46,
                            listOpen ? arrowY - 2.0f : arrowY + 2.5f,
                            4.5f,
                            listOpen ? 4.0f : -4.0f,
                            1.8f,
                            arrow);
      } else {
        GuiKit::drawSideChevron(
          visual, boxX + 9 - leftNudge, arrowY, 4.5f, -4.0f, 1.8f, arrow);
        GuiKit::drawSideChevron(visual,
                                x + width - 43 + rightNudge,
                                arrowY,
                                4.5f,
                                4.0f,
                                1.8f,
                                arrow);
      }
    }
    const ColorRgba labelColor =
      disabled   ? UiTheme::fade(UiTheme::textMuted(), 0.7f)
      : row == 6 ? cyan
                 : UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped);
    const float labelY = ry + std::max(2.0f, (rowHeight - 16.0f) * 0.5f);
    const float valueY = ry + std::max(2.0f, (rowHeight - 14.0f) * 0.5f);
    GuiKit::drawEmphasizedText(visual,
                               labels[row],
                               x + 40 + 4.0f * e,
                               labelY,
                               16,
                               UiTheme::applyOpacity(labelColor, rowOpacity),
                               disabled ? 0.0f : e);
    // A changed value lurches the way it moved and wobbles back into place.
    const float slide = active ? animator.valueWobble() * 8.0f : 0.0f;
    const ColorRgba valueColor =
      disabled ? UiTheme::fade(UiTheme::textMuted(), 0.7f)
      : row == 6
        ? UiTheme::mix(cyan, ColorRgba{ 210, 250, 255, 255 }, 0.3f * breathe)
        : UiTheme::mix(
            UiTheme::textSecondary(), UiTheme::textPrimary(), eClamped);
    // The create row's value breathes, so it sits alone in the last layer.
    GameVisual& valueLayer = row == 6 ? layers[kPulseLayer] : visual;
    const float valueX = x + width * 0.45f + 27 + slide;
    const ColorRgba shownValueColor = UiTheme::applyOpacity(
      UiTheme::fade(valueColor, 1.0f - 0.45f * pulse * (active ? 1.0f : 0.0f)),
      rowOpacity);
    valueLayer.addText(values[row], valueX, valueY, 14, shownValueColor);
    if (row == 6) {
      // "Enter to begin", then a drawn chevron that leans toward the action.
      const float textWidth =
        GuiKit::measureEmphasizedText(values[row], 14, 0.0f);
      GuiKit::drawSideChevron(valueLayer,
                              valueX + textWidth + 12.0f + 3.0f * eClamped,
                              valueY + 7.0f,
                              4.5f,
                              4.0f,
                              1.8f,
                              shownValueColor);
    }
  }
}

void
NewSimulationMenu::drawFooter(float height, unsigned char opacity)
{
  GameVisual& visual = layers[kContentLayer];
  if (dropdown.isOpen()) {
    float hintX = x + 28;
    const float size = 9.0f;
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "UPDOWN", "Browse", size, opacity);
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "A-Z", "Jump", size, opacity);
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "ENTER", "Choose", size, opacity);
    GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "ESC", "Close list", size, opacity);
  } else if (isListRow(selected)) {
    float hintX = x + 28;
    const float size = 9.0f;
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "UPDOWN", "Select", size, opacity);
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "ENTER", "Open list", size, opacity);
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "LEFTRIGHT", "Step", size, opacity);
    GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "ESC", "Back", size, opacity);
  } else if (selected == 3 || selected == 4) {
    visual.addText("LEFT / RIGHT or click either half: resize by 16 cells",
                   x + 28,
                   y + height - 45,
                   11,
                   UiTheme::applyOpacity(UiTheme::textSecondary(), opacity));
  } else {
    float hintX = x + 28;
    const float size = 9.0f;
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "UPDOWN", "Select", size, opacity);
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "LEFTRIGHT", "Change", size, opacity);
    hintX += GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "ENTER", "Choose", size, opacity);
    GuiKit::drawKeyHint(
      visual, hintX, y + height - 48, "ESC", "Back", size, opacity);
  }
  visual.addText("Display and performance settings remain in F1.",
                 x + 28,
                 y + height - 24,
                 10,
                 UiTheme::applyOpacity(UiTheme::textMuted(), opacity));
}
