#include "ExitConfirmDialog.h"
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

static float
easeOutCubic(float progress)
{
  const float remaining = 1.0f - std::clamp(progress, 0.0f, 1.0f);
  return 1.0f - remaining * remaining * remaining;
}

ExitConfirmDialog::ExitConfirmDialog(IRenderWindow* targetWindow,
                                     Renderer* targetRenderer)
  : window(targetWindow)
  , renderer(targetRenderer)
  , visual(512u)
  , openState(false)
  , mouseWasDown(false)
  , selectedButton(kCancelButton)
  , animationElapsed(0.0f)
  , selectionFromButton(0.0f)
  , selectionAnimationElapsed(kSelectionAnimationSeconds)
  , panelX(0.0f)
  , panelY(0.0f)
  , panelWidth(420.0f)
  , panelHeight(196.0f)
  , buttonY(0.0f)
  , buttonWidth(132.0f)
  , buttonHeight(36.0f)
  , cancelX(0.0f)
  , exitX(0.0f)
{
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  visual.setWindow(window);
  visual.setRenderer(renderer);
  visual.prepare(renderer);
  setVisible(false);
}

void
ExitConfirmDialog::open()
{
  selectedButton = kCancelButton;
  selectionFromButton = 0.0f;
  selectionAnimationElapsed = kSelectionAnimationSeconds;
  animationElapsed = 0.0f;
  mouseWasDown = false;
  openState = true;
  setVisible(true);
  updateLayout();
}

void
ExitConfirmDialog::close()
{
  openState = false;
  mouseWasDown = false;
  setVisible(false);
}

void
ExitConfirmDialog::tick(float deltaSeconds)
{
  if (!openState || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
    return;
  }
  animationElapsed =
    std::min(kOpenAnimationSeconds, animationElapsed + deltaSeconds);
  selectionAnimationElapsed = std::min(
    kSelectionAnimationSeconds, selectionAnimationElapsed + deltaSeconds);
}

float
ExitConfirmDialog::animationProgress() const
{
  return std::clamp(animationElapsed / kOpenAnimationSeconds, 0.0f, 1.0f);
}

float
ExitConfirmDialog::getAnimationProgressForTesting() const
{
  return animationProgress();
}

float
ExitConfirmDialog::panelReveal() const
{
  return easeOutCubic(std::clamp(animationElapsed / 0.24f, 0.0f, 1.0f));
}

float
ExitConfirmDialog::panelOffsetY() const
{
  return (1.0f - panelReveal()) * 18.0f;
}

float
ExitConfirmDialog::itemReveal(int item) const
{
  const float delay = static_cast<float>(std::max(0, item)) * 0.018f;
  return easeOutCubic(
    std::clamp((animationElapsed - delay) / 0.22f, 0.0f, 1.0f));
}

float
ExitConfirmDialog::selectionButtonPosition() const
{
  const float progress = easeOutCubic(std::clamp(
    selectionAnimationElapsed / kSelectionAnimationSeconds, 0.0f, 1.0f));
  return selectionFromButton +
         (static_cast<float>(selectedButton) - selectionFromButton) * progress;
}

float
ExitConfirmDialog::getSelectionPositionForTesting() const
{
  return selectionButtonPosition();
}

void
ExitConfirmDialog::updateLayout()
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

  panelWidth = std::min(480.0f, std::max(200.0f, virtualWidth - 32.0f));
  if (panelWidth > virtualWidth) {
    panelWidth = virtualWidth;
  }
  panelHeight = std::min(196.0f, std::max(120.0f, virtualHeight - 24.0f));
  if (panelHeight > virtualHeight) {
    panelHeight = virtualHeight;
  }
  panelX = std::max(0.0f, (virtualWidth - panelWidth) * 0.5f);
  panelY = std::max(0.0f, (virtualHeight - panelHeight) * 0.5f);
  buttonHeight = std::clamp(panelHeight * 0.20f, 24.0f, 36.0f);
  buttonY = panelY + panelHeight - buttonHeight - 16.0f;
  const float buttonGap = std::clamp(panelWidth * 0.025f, 6.0f, 12.0f);
  buttonWidth =
    std::clamp((panelWidth - 48.0f - buttonGap * 2.0f) / 3.0f, 40.0f, 124.0f);
  const float buttonsWidth = buttonWidth * 3.0f + buttonGap * 2.0f;
  cancelX = panelX + (panelWidth - buttonsWidth) * 0.5f;
  menuX = cancelX + buttonWidth + buttonGap;
  exitX = menuX + buttonWidth + buttonGap;
}

void
ExitConfirmDialog::selectButton(int button)
{
  int nextButton = button;
  if (nextButton < 0) {
    nextButton = kButtonCount - 1;
  } else if (nextButton >= kButtonCount) {
    nextButton = 0;
  }
  if (nextButton != selectedButton) {
    selectionFromButton = selectionButtonPosition();
    selectedButton = nextButton;
    selectionAnimationElapsed = 0.0f;
  }
}

ExitConfirmAction
ExitConfirmDialog::activateSelected() const
{
  if (selectedButton == kExitButton) {
    return ExitConfirmAction::Confirm;
  }
  if (selectedButton == kMenuButton) {
    return ExitConfirmAction::MainMenu;
  }
  return ExitConfirmAction::Cancel;
}

ExitConfirmAction
ExitConfirmDialog::clickAt(float x, float y)
{
  const float animatedButtonY = buttonY + panelOffsetY();
  if (y < animatedButtonY || y > animatedButtonY + buttonHeight) {
    return ExitConfirmAction::None;
  }
  if (x >= cancelX && x <= cancelX + buttonWidth) {
    selectButton(kCancelButton);
    return ExitConfirmAction::Cancel;
  }
  if (x >= menuX && x <= menuX + buttonWidth) {
    selectButton(kMenuButton);
    return ExitConfirmAction::MainMenu;
  }
  if (x >= exitX && x <= exitX + buttonWidth) {
    selectButton(kExitButton);
    return ExitConfirmAction::Confirm;
  }
  return ExitConfirmAction::None;
}

ExitConfirmAction
ExitConfirmDialog::update(InputManager* inputManager)
{
  if (!openState || inputManager == nullptr) {
    return ExitConfirmAction::None;
  }
  updateLayout();

  ExitConfirmAction action = ExitConfirmAction::None;
  std::queue<InputManager::KeyPressEvent>& keyQueue =
    inputManager->getKeyQueue();
  while (!keyQueue.empty()) {
    const InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();
    if (event.action != InputAction::Press &&
        event.action != InputAction::Hold) {
      continue;
    }
    if (event.key == KeyCode::Escape || event.key == KeyCode::N) {
      action = ExitConfirmAction::Cancel;
    } else if (event.key == KeyCode::M) {
      action = ExitConfirmAction::MainMenu;
    } else if (event.key == KeyCode::Y) {
      action = ExitConfirmAction::Confirm;
    } else if (event.key == KeyCode::Left) {
      selectButton(selectedButton - 1);
    } else if (event.key == KeyCode::Right || event.key == KeyCode::Tab) {
      selectButton(selectedButton + 1);
    } else if (event.key == KeyCode::Enter) {
      action = activateSelected();
    }
  }

  std::queue<unsigned int>& charQueue = inputManager->getCharQueue();
  while (!charQueue.empty()) {
    charQueue.pop();
  }

  const bool mouseDown = inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  if (mouseDown && !mouseWasDown) {
    const std::array<double, 2> mouse = inputManager->getMousePosition();
    const float scale = renderer != nullptr ? renderer->getUiScale() : 1.0f;
    const ExitConfirmAction clicked =
      clickAt(static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f),
              static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f));
    if (clicked != ExitConfirmAction::None) {
      action = clicked;
    }
  }
  mouseWasDown = mouseDown;
  return action;
}

void
ExitConfirmDialog::rebuildVisual()
{
  visual.clearPrimitives();
  if (!openState) {
    visual.setVisible(false);
    return;
  }

  int width = 1280;
  int height = 720;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  updateLayout();

  const float scale = renderer != nullptr ? renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const float reveal = panelReveal();
  const float animatedPanelY = panelY + panelOffsetY();
  const float animatedButtonY = buttonY + panelOffsetY();
  const unsigned char backdropOpacity = static_cast<unsigned char>(
    std::round(std::clamp(animationElapsed / 0.18f, 0.0f, 1.0f) * 255.0f));
  const unsigned char panelOpacity =
    static_cast<unsigned char>(std::round(reveal * 255.0f));
  const unsigned char titleOpacity =
    static_cast<unsigned char>(std::round(itemReveal(0) * 255.0f));
  const unsigned char bodyOpacity =
    static_cast<unsigned char>(std::round(itemReveal(1) * 255.0f));
  const unsigned char helpOpacity =
    static_cast<unsigned char>(std::round(itemReveal(2) * 255.0f));

  visual.addFilledRect(
    0.0f,
    0.0f,
    virtualWidth,
    virtualHeight,
    UiTheme::applyOpacity(UiTheme::canvasShade(), backdropOpacity));
  visual.addFilledRect(
    panelX + 5.0f,
    animatedPanelY + 5.0f,
    panelWidth,
    panelHeight,
    UiTheme::applyOpacity(UiTheme::panelShadow(), panelOpacity));
  visual.addFilledRect(
    panelX,
    animatedPanelY,
    panelWidth,
    panelHeight,
    UiTheme::applyOpacity(UiTheme::panelSurface(), panelOpacity));
  visual.addOutlineRect(
    panelX,
    animatedPanelY,
    panelWidth,
    panelHeight,
    UiTheme::applyOpacity(UiTheme::panelBorder(), panelOpacity),
    1.0f);
  visual.addFilledRect(panelX,
                       animatedPanelY,
                       5.0f,
                       panelHeight * reveal,
                       UiTheme::applyOpacity(UiTheme::accent(), panelOpacity));
  visual.addText("SIMULATION PAUSED",
                 panelX + 28.0f,
                 animatedPanelY + 24.0f,
                 22.0f,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), titleOpacity));
  visual.addText("Choose an action below to continue:",
                 panelX + 28.0f,
                 animatedPanelY + 64.0f,
                 16.0f,
                 UiTheme::applyOpacity(UiTheme::textMuted(), bodyOpacity));
  visual.addText("ENTER: select    M: menu    Y: exit desktop    ESC: resume",
                 panelX + 28.0f,
                 animatedPanelY + 90.0f,
                 13.0f,
                 UiTheme::applyOpacity(UiTheme::textMuted(), helpOpacity));

  const float buttonXs[kButtonCount] = { cancelX, menuX, exitX };
  const char* buttonLabels[kButtonCount] = { "Resume",
                                             "Main Menu",
                                             "Exit App" };
  const ColorRgba buttonColors[kButtonCount] = { UiTheme::warning(),
                                                 UiTheme::accent(),
                                                 UiTheme::error() };
  for (int button = 0; button < kButtonCount; ++button) {
    const unsigned char buttonOpacity =
      static_cast<unsigned char>(std::round(itemReveal(3 + button) * 255.0f));
    visual.addFilledRect(
      buttonXs[button],
      animatedButtonY,
      buttonWidth,
      buttonHeight,
      UiTheme::applyOpacity(UiTheme::panelRaised(), buttonOpacity));
  }

  const float selectionPos = selectionButtonPosition();
  float selectionX = cancelX;
  if (selectionPos <= 1.0f) {
    selectionX = cancelX + (menuX - cancelX) * selectionPos;
  } else {
    selectionX = menuX + (exitX - menuX) * (selectionPos - 1.0f);
  }

  const unsigned char selectionOpacity = static_cast<unsigned char>(
    std::round(itemReveal(3 + selectedButton) * 255.0f));
  visual.addFilledRect(
    selectionX,
    animatedButtonY,
    buttonWidth,
    buttonHeight,
    UiTheme::applyOpacity(UiTheme::selection(), selectionOpacity));
  visual.addFilledRect(
    selectionX,
    animatedButtonY,
    4.0f,
    buttonHeight * itemReveal(3 + selectedButton),
    UiTheme::applyOpacity(buttonColors[selectedButton], selectionOpacity));

  for (int button = 0; button < kButtonCount; ++button) {
    const bool selected = button == selectedButton;
    const unsigned char buttonOpacity =
      static_cast<unsigned char>(std::round(itemReveal(3 + button) * 255.0f));
    visual.addText(buttonLabels[button],
                   buttonXs[button] + 12.0f,
                   animatedButtonY + 8.0f,
                   15.0f,
                   UiTheme::applyOpacity(selected ? buttonColors[button]
                                                  : UiTheme::textPrimary(),
                                         buttonOpacity));
  }
  visual.setVisible(true);
}

bool
ExitConfirmDialog::AppendCommands(Renderer* activeRenderer)
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
