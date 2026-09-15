#include "NewSimulationMenu.h"
#include "Rulesets/RuleSetRegistry.h"
#include "SparseCellGrid.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <queue>

bool
NewSimulationConfiguration::isValid() const
{
  return RuleSetRegistry::instance().isKnownRule(ruleSet) &&
         SparseCellGrid::isValidTopology(worldChunkWidth, worldChunkHeight);
}

NewSimulationMenu::NewSimulationMenu(IRenderWindow* target, Renderer* owner)
  : window(target)
  , renderer(owner)
{
  visual.setWindow(window);
  visual.setRenderer(renderer);
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  visual.prepare(renderer);
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
  reduced = reducedMotion;
  elapsed = 0;
  selected = 0;
  ambientPhase = 0;
  valuePulse = 0;
  focus.fill(0);
  focus[0] = 1;
  mouseWasDown = true;
  previousX = -1;
  previousY = -1;
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
  if (std::isfinite(dt)) {
    const float step = std::clamp(dt, 0.0f, 0.1f);
    elapsed = std::min(0.35f, elapsed + step);
    ambientPhase = reduced ? 0 : std::fmod(ambientPhase + step, 12.0f);
    valuePulse = reduced ? 0 : std::max(0.0f, valuePulse - step * 4);
    for (int row = 0; row < 7; ++row) {
      const float target = row == selected ? 1.0f : 0.0f;
      focus[static_cast<std::size_t>(row)] +=
        (target - focus[static_cast<std::size_t>(row)]) *
        (reduced ? 1.0f : 1 - std::exp(-18 * step));
    }
  }
  rebuild();
}

void
NewSimulationMenu::select(int direction)
{
  do {
    selected = (selected + direction + 7) % 7;
  } while (!finite && (selected == 2 || selected == 3));
}

void
NewSimulationMenu::change(int direction)
{
  valuePulse = reduced ? 0.0f : 1.0f;
  if (selected == 0) {
    const std::vector<std::string> rules =
      RuleSetRegistry::instance().getKnownRules();
    if (!rules.empty()) {
      const int count = static_cast<int>(rules.size());
      const int index = static_cast<int>(
        std::find(rules.begin(), rules.end(), draft.ruleSet) - rules.begin());
      draft.ruleSet =
        rules[static_cast<std::size_t>((index + direction + count) % count)];
    }
  } else if (selected == 1) {
    finite = !finite;
  } else if (finite && (selected == 2 || selected == 3)) {
    std::int64_t& dimension =
      selected == 2 ? draft.worldChunkWidth : draft.worldChunkHeight;
    dimension = std::clamp(dimension + direction,
                           std::int64_t{ 1 },
                           SparseCellGrid::kMaximumWorldChunksPerAxis);
  } else if (selected == 4) {
    draft.starterPattern = !draft.starterPattern;
  }
}

NewSimulationAction
NewSimulationMenu::activate()
{
  if (selected == 5) {
    return configuration().isValid() ? NewSimulationAction::Create
                                     : NewSimulationAction::None;
  }
  if (selected == 6) {
    return NewSimulationAction::Back;
  }
  change(1);
  return NewSimulationAction::None;
}

NewSimulationAction
NewSimulationMenu::update(InputManager* input)
{
  if (!openState || input == nullptr) {
    return NewSimulationAction::None;
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
      selected = 0;
    else if (event.key == KeyCode::End)
      selected = 6;
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
  const std::array<double, 2> mouse = window->getMouseCoords();
  const float mx = static_cast<float>(mouse[0]) / scale;
  const float my = static_cast<float>(mouse[1]) / scale;
  const bool down = input->isMouseButtonPressed(KeyCode::MouseLeft);
  if (result == NewSimulationAction::None &&
      ((mx != previousX || my != previousY) || (down && !mouseWasDown)) &&
      mx >= x + 24 && mx < x + width - 24 && my >= y + 110 &&
      my < y + 110 + rowHeight * 7) {
    const int row = static_cast<int>((my - y - 110) / rowHeight);
    if (finite || (row != 2 && row != 3)) {
      selected = row;
      if (down && !mouseWasDown) {
        if (selected < 5 && mx < x + width * 0.55f)
          change(-1);
        else
          result = activate();
      }
    }
  }
  previousX = mx;
  previousY = my;
  mouseWasDown = down;
  rebuild();
  return result;
}

void
NewSimulationMenu::rebuild()
{
  const std::array<int, 2> dimensions = window->getWindowDimensions();
  const float preferred = std::max(1.0f, renderer->getUiScale());
  scale = std::min(preferred,
                   std::max(0.01f,
                            std::min(static_cast<float>(dimensions[0]) / 640,
                                     static_cast<float>(dimensions[1]) / 480)));
  Transform2D fit;
  fit.scaleX = scale / preferred;
  fit.scaleY = fit.scaleX;
  visual.setTransform(fit);
  const float screenWidth = static_cast<float>(dimensions[0]) / scale;
  const float screenHeight = static_cast<float>(dimensions[1]) / scale;
  width = std::min(720.0f, screenWidth - 32);
  const float height = std::min(610.0f, screenHeight - 24);
  const float progress =
    reduced ? 1.0f : std::clamp(elapsed / 0.35f, 0.0f, 1.0f);
  const float reveal = 1 - std::pow(1 - progress, 3.0f);
  x = (screenWidth - width) / 2;
  y = (screenHeight - height) / 2 + 16 * (1 - reveal);
  rowHeight = (height - 164) / 7;
  visual.clearPrimitives();
  GuiKit::drawBackdrop(visual, screenWidth, screenHeight, 160);
  GuiKit::drawRoundedPanel(visual, x, y, width, height);
  visual.addText("A FRESH WORLD", x + 28, y + 20, 11, UiTheme::accentCool());
  visual.addText("NEW CANVAS", x + 28, y + 41, 28, UiTheme::textPrimary());
  visual.addText("Choose the space. Then see what emerges.",
                 x + 28,
                 y + 80,
                 13,
                 UiTheme::textMuted());
  // A bounded decorative cell colony echoes the main menu without simulating.
  for (int cy = 0; cy < 6; ++cy) {
    for (int cx = 0; cx < 6; ++cx) {
      const float wave =
        0.5f + 0.5f * std::sin(ambientPhase * 1.5f +
                               static_cast<float>(cx - cy) * 0.8f);
      const bool live = (cx + cy * 3) % 5 < 2;
      const float size = live ? 7 + wave : 7;
      GuiKit::drawRoundedRect(
        visual,
        x + width - 90 + static_cast<float>(cx) * 10,
        y + 15 + static_cast<float>(cy) * 10,
        size,
        size,
        2,
        live
          ? UiTheme::applyOpacity(UiTheme::accentCool(),
                                  static_cast<unsigned char>(90 + wave * 140))
          : UiTheme::menuCard());
    }
  }
  const float modeBadgeX = x + width - 164;
  const float modeBadgeY = y + 79;
  GuiKit::drawRoundedRect(visual,
                          modeBadgeX,
                          modeBadgeY,
                          140,
                          22,
                          8,
                          UiTheme::applyOpacity(UiTheme::accentCool(), 100));
  GuiKit::drawRoundedRect(
    visual, modeBadgeX + 1, modeBadgeY + 1, 138, 20, 7, UiTheme::panelInset());
  visual.addFilledEllipse(modeBadgeX + 10,
                          modeBadgeY + 8,
                          6,
                          6,
                          finite ? UiTheme::accentViolet()
                                 : UiTheme::accentCool());
  visual.addText(finite ? "FINITE TORUS" : "INFINITE FIELD",
                 modeBadgeX + 22,
                 modeBadgeY + 6,
                 10,
                 UiTheme::textPrimary());
  visual.addFilledRect(x + 24,
                       y + 103,
                       width - 48,
                       1,
                       UiTheme::applyOpacity(UiTheme::accentCool(), 80));

  const RuleDefinition* rule =
    RuleSetRegistry::instance().getRuleDefinition(draft.ruleSet);
  std::string ruleName = rule == nullptr ? draft.ruleSet : rule->name;
  if (ruleName.size() > 30)
    ruleName = ruleName.substr(0, 27) + "...";
  const std::string labels[] = { "Ruleset", "Canvas boundary", "Width",
                                 "Height",  "Starting cells",  "Create canvas",
                                 "Back" };
  const std::string values[] = {
    ruleName,
    finite ? "Wrap opposite edges" : "Infinite",
    finite ? std::to_string(draft.worldChunkWidth * 16) + " cells"
           : "Unbounded",
    finite ? std::to_string(draft.worldChunkHeight * 16) + " cells"
           : "Unbounded",
    draft.starterPattern ? "Starter pattern" : "Empty canvas",
    "Enter to begin  >",
    "Discard choices"
  };
  for (int row = 0; row < 7; ++row) {
    const bool disabled = !finite && (row == 2 || row == 3);
    const float ry = y + 110 + static_cast<float>(row) * rowHeight;
    const float emphasis = reduced ? (row == selected ? 1.0f : 0.0f)
                                   : focus[static_cast<std::size_t>(row)];
    const float lift = emphasis * 2;
    GuiKit::drawRoundedRect(visual,
                            x + 24,
                            ry + 3,
                            width - 48,
                            rowHeight - 5,
                            10,
                            ColorRgba{ 0, 0, 0, 160 });
    GuiKit::drawRoundedRect(visual,
                            x + 24,
                            ry - lift,
                            width - 48,
                            rowHeight - 5,
                            10,
                            row == 5 ? ColorRgba{ 43, 111, 133, 255 }
                                     : UiTheme::menuBorder());
    GuiKit::drawRoundedRect(visual,
                            x + 25,
                            ry + 1 - lift,
                            width - 50,
                            rowHeight - 7,
                            9,
                            row == 5 ? ColorRgba{ 24, 66, 86, 255 }
                                     : UiTheme::menuCard());
    GuiKit::drawRoundedRect(
      visual,
      x + 25,
      ry + 1 - lift,
      width - 50,
      rowHeight - 7,
      9,
      UiTheme::applyOpacity(UiTheme::selection(),
                            static_cast<unsigned char>(emphasis * 220)));
    GuiKit::drawRoundedRect(
      visual,
      x + 30,
      ry + 10 - lift,
      3,
      rowHeight - 23,
      1.5f,
      UiTheme::applyOpacity(UiTheme::accentCool(),
                            static_cast<unsigned char>(emphasis * 230)));
    if (!disabled && row < 5) {
      GuiKit::drawRoundedRect(visual,
                              x + width * 0.45f,
                              ry + 5 - lift,
                              width * 0.55f - 37,
                              rowHeight - 15,
                              7,
                              ColorRgba{ 10, 20, 35, 145 });
      visual.addText("<",
                     x + width * 0.45f + 8,
                     ry + (rowHeight - 18) / 2 - lift,
                     12,
                     UiTheme::accentCool());
      visual.addText(">",
                     x + width - 50,
                     ry + (rowHeight - 18) / 2 - lift,
                     12,
                     UiTheme::accentCool());
      if (row == selected && valuePulse > 0) {
        GuiKit::drawRoundedRect(
          visual,
          x + width * 0.45f,
          ry + 5 - lift,
          width * 0.55f - 37,
          rowHeight - 15,
          7,
          UiTheme::applyOpacity(UiTheme::accentCool(),
                                static_cast<unsigned char>(valuePulse * 45)));
      }
    }
    const ColorRgba color = disabled ? UiTheme::textMuted()
                            : row == selected || row == 5
                              ? UiTheme::accentCool()
                              : UiTheme::textPrimary();
    const float labelY = ry + std::max(2.0f, (rowHeight - 16.0f) * 0.5f) - lift;
    const float valueY = ry + std::max(2.0f, (rowHeight - 14.0f) * 0.5f) - lift;
    visual.addText(labels[row], x + 40, labelY, 16, color);
    visual.addText(values[row], x + width * 0.45f + 27, valueY, 14, color);
  }
  visual.addText(selected == 2 || selected == 3
                   ? "LEFT / RIGHT or click either half: resize by 16 cells"
                   : "UP / DOWN: select   LEFT / RIGHT: change   ENTER: choose",
                 x + 28,
                 y + height - 43,
                 11,
                 UiTheme::textMuted());
  visual.addText("ESC: back     Display and performance settings remain in F1.",
                 x + 28,
                 y + height - 25,
                 11,
                 UiTheme::textMuted());
}
