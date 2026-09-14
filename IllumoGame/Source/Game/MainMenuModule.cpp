#include "MainMenuModule.h"

#include "BuiltinPatterns.h"
#include "CellContext.h"
#include "CellGameModule.h"
#include "PatternCodec.h"
#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
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

MainMenuModule::MainMenuModule()
  : m_menuVisual(2048u)
  , m_selectedItem(kPlayItem)
  , m_animationElapsed(0.0f)
  , m_selectionFromItem(0.0f)
  , m_selectionAnimationElapsed(kSelectionAnimationSeconds)
  , m_bgSimAccum(0.0)
  , m_mouseWasDown(false)
  , m_panelX(0.0f)
  , m_panelY(0.0f)
  , m_panelWidth(440.0f)
  , m_panelHeight(360.0f)
  , m_firstItemY(0.0f)
  , m_itemHeight(44.0f)
  , m_itemWidth(360.0f)
{
}

MainMenuModule::~MainMenuModule()
{
  Exit();
}

bool
MainMenuModule::Start(IllumoContext* context)
{
  if (context == nullptr || context->envVars == nullptr ||
      context->window == nullptr || context->camera == nullptr ||
      context->renderer == nullptr || context->inputManager == nullptr ||
      context->commandRegistry == nullptr || context->scene == nullptr ||
      context->moduleHost == nullptr) {
    Logger::LogError(
      "MainMenuModule::Start: IllumoContext missing required services");
    ic = context;
    return false;
  }
  ic = context;

  // Reset camera for menu presentation
  ic->camera->SetPositionPrecise(0.0, 0.0);
  ic->camera->SetZoom(1.0f);

  m_bgContext = std::make_unique<CellContext>(
    "GAME_OF_LIFE", ic->envVars, ic->window, ic->camera, ic->renderer);
  seedAmbientPattern();

  m_configurationMenu =
    std::make_unique<ConfigurationMenu>(ic->window, ic->renderer);

  m_menuVisual.setSpace(PrimitiveSpace::Pixels);
  m_menuVisual.setLayerHint(RenderLayerId::UI);
  m_menuVisual.setWindow(ic->window);
  m_menuVisual.setRenderer(ic->renderer);
  m_menuVisual.prepare(ic->renderer);

  m_selectedItem = kPlayItem;
  m_selectionFromItem = 0.0f;
  m_selectionAnimationElapsed = kSelectionAnimationSeconds;
  m_animationElapsed = 0.0f;
  m_revealElapsed = 0.0f;
  m_bgSimAccum = 0.0;
  m_mouseWasDown = false;

  registerConsoleCommands();
  updateLayout();
  rebuildVisual();

  return true;
}

void
MainMenuModule::seedAmbientPattern()
{
  if (!m_bgContext || !m_bgContext->getCanvasView()) {
    return;
  }
  CanvasView* canvas = m_bgContext->getCanvasView();
  canvas->clearCanvas();

  // Stamp Pulsar patterns in the background for ambient motion
  CellPattern pulsar;
  if (BuiltinPatterns::find("Pulsar", &pulsar)) {
    for (std::int64_t x = -60; x <= 60; x += 60) {
      for (std::int64_t y = -40; y <= 40; y += 40) {
        for (const CellPatternCell& cell : pulsar.getCells()) {
          canvas->setCanvasPixel(x + cell.dx, y + cell.dy, cell.state);
        }
      }
    }
  } else {
    // Fallback: Acorn pattern
    canvas->setCanvasPixel(-1, 0, 0);
    canvas->setCanvasPixel(1, -1, 0);
    canvas->setCanvasPixel(1, 1, 0);
    canvas->setCanvasPixel(2, 1, 0);
    canvas->setCanvasPixel(3, 1, 0);
    canvas->setCanvasPixel(4, 1, 0);
    canvas->setCanvasPixel(0, 1, 0);
  }
}

void
MainMenuModule::advanceAmbientSimulation(double dt)
{
  if (!m_bgContext || !m_bgContext->getCanvasView() ||
      !m_bgContext->getGrid() || !m_bgContext->getRuleSet()) {
    return;
  }
  m_bgSimAccum += dt;
  const double stepSeconds = 1.0 / 8.0; // 8 TPS ambient rate
  while (m_bgSimAccum >= stepSeconds) {
    m_bgSimAccum -= stepSeconds;
    m_bgContext->getGrid()->advance(*m_bgContext->getRuleSet());
  }
  m_bgContext->getCanvasView()->tickVisual(static_cast<float>(dt));
}

void
MainMenuModule::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (ic != nullptr && ic->window != nullptr) {
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }

  const float scale = ic != nullptr && ic->renderer != nullptr
                        ? std::max(1.0f, ic->renderer->getUiScale())
                        : 1.0f;
  m_layoutScale =
    std::min(scale,
             std::max(0.25f,
                      std::min(static_cast<float>(width) / 640.0f,
                               static_cast<float>(height) / 480.0f)));
  Transform2D fit;
  fit.scaleX = m_layoutScale / scale;
  fit.scaleY = fit.scaleX;
  m_menuVisual.setTransform(fit);
  const float virtualWidth = static_cast<float>(width) / m_layoutScale;
  const float virtualHeight = static_cast<float>(height) / m_layoutScale;
  m_panelWidth = std::min(540.0f, virtualWidth - 48.0f);
  m_panelHeight = std::min(488.0f, virtualHeight - 24.0f);
  m_panelX = (virtualWidth - m_panelWidth) * 0.5f;
  const float reveal =
    reducedMotion() ? 1.0f : easeOutCubic(m_revealElapsed / 0.45f);
  m_panelY = (virtualHeight - m_panelHeight) * 0.5f + 12.0f * (1.0f - reveal);
  m_itemWidth = m_panelWidth - 56.0f;
  m_itemHeight = (m_panelHeight - 196.0f) / 4.0f;
  m_firstItemY = m_panelY + 140.0f;
}

bool
MainMenuModule::reducedMotion() const
{
  return ic != nullptr && ic->envVars != nullptr &&
         ic->envVars->getVar("reducedUiMotion").valueAsBool;
}

float
MainMenuModule::itemPosition() const
{
  if (reducedMotion()) {
    return static_cast<float>(m_selectedItem);
  }
  const float progress = easeOutCubic(std::clamp(
    m_selectionAnimationElapsed / kSelectionAnimationSeconds, 0.0f, 1.0f));
  return m_selectionFromItem +
         (static_cast<float>(m_selectedItem) - m_selectionFromItem) * progress;
}

void
MainMenuModule::selectItem(int item)
{
  int nextItem = item;
  if (nextItem < 0) {
    nextItem = kItemCount - 1;
  } else if (nextItem >= kItemCount) {
    nextItem = 0;
  }
  if (nextItem != m_selectedItem) {
    m_selectionFromItem = itemPosition();
    m_selectedItem = nextItem;
    m_selectionAnimationElapsed = 0.0f;
  }
}

void
MainMenuModule::activateSelectedItem()
{
  if (ic == nullptr || ic->moduleHost == nullptr) {
    return;
  }

  switch (m_selectedItem) {
    case kPlayItem: {
      ic->moduleHost->RequestTransition(std::make_unique<CellGameModule>());
      break;
    }
    case kLoadItem: {
      SaveLoadDialogSpec spec;
      spec.fileDescription = "Illumo Simulations";
      spec.extensionPattern = "*.illumo";
      const std::string path = SaveLoad::GetLoadLocation(spec);
      if (!path.empty()) {
        ic->moduleHost->RequestTransition(
          std::make_unique<CellGameModule>(path));
      }
      break;
    }
    case kSettingsItem: {
      if (m_configurationMenu != nullptr) {
        m_configurationMenu->open(currentConfiguration());
      }
      break;
    }
    case kExitItem: {
      if (ic->window != nullptr) {
        ic->window->requestClose();
      }
      break;
    }
    default:
      break;
  }
}

void
MainMenuModule::selectItemForTesting(int item)
{
  selectItem(item);
}

void
MainMenuModule::activateSelectedItemForTesting()
{
  activateSelectedItem();
}

bool
MainMenuModule::isSettingsOpenForTesting() const
{
  return m_configurationMenu != nullptr && m_configurationMenu->isOpen();
}

SimulatorConfiguration
MainMenuModule::currentConfiguration() const
{
  SimulatorConfiguration config;
  if (ic == nullptr || ic->envVars == nullptr) {
    return config;
  }
  config.ruleSet = ic->envVars->getVar("ModeString").value;
  if (config.ruleSet.empty()) {
    config.ruleSet = "GAME_OF_LIFE";
  }
  config.worldChunkWidth = ic->envVars->getVar("WorldChunksX").valueAsLong;
  config.worldChunkHeight = ic->envVars->getVar("WorldChunksY").valueAsLong;
  config.tps = ic->envVars->getVar("tps").valueAsLong;
  if (config.tps <= 0) {
    config.tps = 30;
  }
  config.speedFactor = ic->envVars->getVar("speedFactor").valueAsDouble;
  if (config.speedFactor <= 0.0) {
    config.speedFactor = 1.0;
  }
  config.fadeSpeed = ic->envVars->getVar("cellFadeSpeed").valueAsDouble;
  if (config.fadeSpeed < 0.0) {
    config.fadeSpeed = 8.0;
  }
  config.vsync = ic->envVars->getVar("vsync").valueAsBool;
  config.fullscreen = ic->envVars->getVar("fullscreen").valueAsBool;
  const EnvVar& scaleVar = ic->envVars->getVar("uiScale");
  config.uiScale = scaleVar.value.empty() ? 1 : scaleVar.valueAsLong;
  const EnvVar& msaaVar = ic->envVars->getVar("msaa");
  config.msaa = msaaVar.value.empty() ? 4 : msaaVar.valueAsLong;
  config.fpsCap = getTargetFps(ic->envVars);
  config.showInspector = ic->envVars->getVar("showInspector").valueAsBool;
  config.reducedUiMotion = ic->envVars->getVar("reducedUiMotion").valueAsBool;
  return config;
}

bool
MainMenuModule::applyConfiguration(const SimulatorConfiguration& configuration)
{
  if (ic == nullptr || ic->envVars == nullptr) {
    return false;
  }
  if (configuration.fpsCap < 0 || configuration.fpsCap > 1000) {
    return false;
  }
  const bool fullscreenChanged =
    configuration.fullscreen != ic->envVars->getVar("fullscreen").valueAsBool;
  ic->envVars->setVar("ModeString", configuration.ruleSet);
  ic->envVars->setVar("WorldChunksX",
                      static_cast<long>(configuration.worldChunkWidth));
  ic->envVars->setVar("WorldChunksY",
                      static_cast<long>(configuration.worldChunkHeight));
  ic->envVars->setVar("tps", configuration.tps);
  ic->envVars->setVar("speedFactor", configuration.speedFactor);
  ic->envVars->setVar("cellFadeSpeed", configuration.fadeSpeed);
  ic->envVars->setVar("fps", configuration.fpsCap);
  ic->envVars->setVar("showInspector", configuration.showInspector);
  ic->envVars->setVar("reducedUiMotion", configuration.reducedUiMotion);
  ic->envVars->setVar("vsync", configuration.vsync);
  ic->envVars->setVar("fullscreen", configuration.fullscreen);
  ic->envVars->setVar("uiScale", configuration.uiScale);
  ic->envVars->setVar("msaa", configuration.msaa);
  if (fullscreenChanged && ic->window != nullptr) {
    ic->window->toggleFullscreen();
  }
  ic->envVars->save();
  return true;
}

void
MainMenuModule::Update(double dt)
{
  if (ic == nullptr) {
    return;
  }

  if (!std::isfinite(dt) || dt < 0.0) {
    dt = 0.0;
  }
  m_revealElapsed = std::min(0.6f, m_revealElapsed + static_cast<float>(dt));
  m_animationElapsed =
    reducedMotion()
      ? 0.0f
      : std::fmod(m_animationElapsed + static_cast<float>(std::min(dt, 0.1)),
                  12.0f);
  m_selectionAnimationElapsed =
    std::min(kSelectionAnimationSeconds,
             m_selectionAnimationElapsed + static_cast<float>(dt));

  advanceAmbientSimulation(std::min(dt, 0.25));

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;

  if (m_configurationMenu != nullptr && m_configurationMenu->isOpen()) {
    // Preserve the button edge across modal close; Apply must not click the
    // main-menu action underneath it on the following frame.
    m_mouseWasDown = ic->inputManager != nullptr &&
                     ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    m_configurationMenu->tick(static_cast<float>(dt));
    if (!consoleOpen) {
      const ConfigurationMenuAction action =
        m_configurationMenu->update(ic->inputManager);
      if (action == ConfigurationMenuAction::Apply) {
        SimulatorConfiguration config;
        std::string error;
        if (m_configurationMenu->readConfiguration(&config, &error)) {
          if (applyConfiguration(config)) {
            m_configurationMenu->close();
          } else {
            m_configurationMenu->setError("Unable to apply settings.");
          }
        } else {
          m_configurationMenu->setError(error);
        }
      } else if (action == ConfigurationMenuAction::Cancel ||
                 action == ConfigurationMenuAction::Exit) {
        m_configurationMenu->close();
        if (action == ConfigurationMenuAction::Exit && ic->window != nullptr) {
          ic->window->requestClose();
        }
      }
    }
    rebuildVisual();
    return;
  }

  updateLayout();

  if (!consoleOpen && ic->inputManager != nullptr) {
    std::queue<InputManager::KeyPressEvent>& keyQueue =
      ic->inputManager->getKeyQueue();
    std::queue<InputManager::KeyPressEvent> remainingKeys;
    while (!keyQueue.empty()) {
      const InputManager::KeyPressEvent event = keyQueue.front();
      keyQueue.pop();
      if (event.key == KeyCode::Grave) {
        remainingKeys.push(event);
        continue;
      }
      if (event.action != InputAction::Press &&
          event.action != InputAction::Hold) {
        continue;
      }
      if (event.key == KeyCode::F1) {
        m_configurationMenu->open(currentConfiguration());
        break;
      } else if (event.key == KeyCode::Up || event.key == KeyCode::W) {
        selectItem(m_selectedItem - 1);
      } else if (event.key == KeyCode::Down || event.key == KeyCode::S ||
                 event.key == KeyCode::Tab) {
        selectItem(m_selectedItem + 1);
      } else if (event.key == KeyCode::Enter || event.key == KeyCode::Space) {
        activateSelectedItem();
      } else if (event.key == KeyCode::Escape) {
        if (ic->window != nullptr) {
          ic->window->requestClose();
        }
      }
    }
    keyQueue.swap(remainingKeys);
    if (m_configurationMenu->isOpen()) {
      rebuildVisual();
      return;
    }

    std::queue<unsigned int>& charQueue = ic->inputManager->getCharQueue();
    while (!charQueue.empty()) {
      charQueue.pop();
    }

    const std::array<double, 2> mouse = ic->window->getMouseCoords();
    const float mouseX = static_cast<float>(mouse[0]) / m_layoutScale;
    const float mouseY = static_cast<float>(mouse[1]) / m_layoutScale;
    const bool moved = mouseX != m_previousMouseX || mouseY != m_previousMouseY;
    m_previousMouseX = mouseX;
    m_previousMouseY = mouseY;
    const bool mouseDown =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);

    const float itemX = m_panelX + 28.0f;
    const float itemGap = 8.0f;
    for (int i = 0; i < kItemCount; ++i) {
      const float currentItemY =
        m_firstItemY + static_cast<float>(i) * (m_itemHeight + itemGap);
      if ((moved || (mouseDown && !m_mouseWasDown)) && mouseX >= itemX &&
          mouseX <= itemX + m_itemWidth && mouseY >= currentItemY &&
          mouseY <= currentItemY + m_itemHeight) {
        if (m_selectedItem != i) {
          selectItem(i);
        }
        if (mouseDown && !m_mouseWasDown) {
          activateSelectedItem();
        }
        break;
      }
    }
    m_mouseWasDown = mouseDown;
  }

  rebuildVisual();
}

void
MainMenuModule::rebuildVisual()
{
  updateLayout();
  m_menuVisual.clearPrimitives();
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const float width = static_cast<float>(dimensions[0]) / m_layoutScale;
  const float height = static_cast<float>(dimensions[1]) / m_layoutScale;
  const float reveal =
    reducedMotion() ? 1.0f : easeOutCubic(m_revealElapsed / 0.45f);
  const unsigned char opacity = static_cast<unsigned char>(255.0f * reveal);
  const float breathe =
    reducedMotion() ? 0.5f
                    : 0.5f + 0.5f * std::sin(m_animationElapsed * 1.04719755f);
  const ColorRgba cyan{ 87, 221, 242, 255 };
  const ColorRgba violet{ 151, 128, 245, 255 };
  m_menuVisual.addFilledRect(
    0.0f, 0.0f, width, height, ColorRgba{ 7, 12, 24, 225 });

  // Bounded layers of translucent color create ambient light without textures.
  for (int band = 0; band < 12; ++band) {
    const float inset = static_cast<float>(band) * 18.0f;
    const unsigned char alpha =
      static_cast<unsigned char>(2.0f + breathe * 2.0f);
    m_menuVisual.addFilledRect(inset,
                               0.0f,
                               std::max(0.0f, width * 0.45f - inset),
                               height,
                               ColorRgba{ 30, 110, 155, alpha });
    m_menuVisual.addFilledRect(width * 0.65f + inset,
                               0.0f,
                               std::max(0.0f, width * 0.35f - inset),
                               height,
                               ColorRgba{ 91, 55, 155, alpha });
  }
  // A slow cell constellation echoes the simulator without random per-frame
  // work.
  for (int cell = 0; cell < 28; ++cell) {
    const float phase = static_cast<float>(cell) * 0.73f;
    const float drift =
      reducedMotion()
        ? 0.0f
        : std::sin(m_animationElapsed * 0.52359877f + phase) * 9.0f;
    const float x = std::fmod(static_cast<float>(cell * 137 + 31), width);
    const float y = std::fmod(static_cast<float>(cell * 79 + 17), height);
    m_menuVisual.addOutlineRect(
      x,
      std::clamp(y + drift, 0.0f, height - 8.0f),
      8.0f,
      8.0f,
      ColorRgba{
        85, 180, 225, static_cast<unsigned char>(25.0f + 25.0f * breathe) },
      1.0f);
  }

  GuiPanelChrome chrome;
  chrome.background =
    UiTheme::applyOpacity(ColorRgba{ 14, 23, 39, 247 }, opacity);
  chrome.border =
    UiTheme::applyOpacity(ColorRgba{ 65, 101, 133, 255 }, opacity);
  chrome.shadow = UiTheme::applyOpacity(UiTheme::panelShadow(), opacity);
  chrome.shadowOffset = 8.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = false;
  GuiKit::drawPanel(
    m_menuVisual, m_panelX, m_panelY, m_panelWidth, m_panelHeight, chrome);
  m_menuVisual.addFilledRect(m_panelX,
                             m_panelY,
                             m_panelWidth * 0.65f * reveal,
                             3.0f,
                             UiTheme::applyOpacity(cyan, opacity));
  m_menuVisual.addFilledRect(m_panelX + m_panelWidth * 0.65f,
                             m_panelY,
                             m_panelWidth * 0.35f * reveal,
                             3.0f,
                             UiTheme::applyOpacity(violet, opacity));
  m_menuVisual.addText("I L L U M O",
                       m_panelX + 28.0f,
                       m_panelY + 28.0f,
                       34.0f,
                       UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));
  m_menuVisual.addText("Small rules. Endless possibilities.",
                       m_panelX + 28.0f,
                       m_panelY + 78.0f,
                       15.0f,
                       UiTheme::applyOpacity(cyan, opacity));
  m_menuVisual.addText("CELLULAR AUTOMATA SANDBOX",
                       m_panelX + 28.0f,
                       m_panelY + 108.0f,
                       11.0f,
                       UiTheme::applyOpacity(UiTheme::textMuted(), opacity));

  const unsigned int logoRows[5] = { 4u, 2u, 14u, 0u, 0u };
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 4; ++col) {
      const bool lit = (logoRows[row] & (1u << col)) != 0u;
      m_menuVisual.addFilledRect(
        m_panelX + m_panelWidth - 91.0f + static_cast<float>(col) * 14.0f,
        m_panelY + 34.0f + static_cast<float>(row) * 14.0f,
        10.0f,
        10.0f,
        UiTheme::applyOpacity(lit ? cyan : ColorRgba{ 34, 51, 73, 255 },
                              opacity));
    }
  }

  const char* labels[kItemCount] = {
    "New simulation", "Load simulation", "Settings", "Exit to desktop"
  };
  const char* descriptions[kItemCount] = {
    "Create, experiment, and watch patterns evolve",
    "Continue exploring a saved world",
    "Tune simulation, display, and motion",
    "Close IllumoGame"
  };
  const float itemX = m_panelX + 28.0f;
  const float stride = m_itemHeight + 8.0f;
  for (int item = 0; item < kItemCount; ++item) {
    const float y = m_firstItemY + static_cast<float>(item) * stride;
    m_menuVisual.addFilledRect(
      itemX,
      y,
      m_itemWidth,
      m_itemHeight,
      UiTheme::applyOpacity(UiTheme::panelRaised(), opacity));
  }
  const float selectionY = m_firstItemY + itemPosition() * stride;
  m_menuVisual.addFilledRect(
    itemX,
    selectionY,
    m_itemWidth,
    m_itemHeight,
    UiTheme::applyOpacity(ColorRgba{ 32, 74, 99, 230 }, opacity));
  m_menuVisual.addOutlineRect(
    itemX,
    selectionY,
    m_itemWidth,
    m_itemHeight,
    UiTheme::applyOpacity(
      ColorRgba{
        87, 221, 242, static_cast<unsigned char>(100.0f + 70.0f * breathe) },
      opacity),
    1.0f);
  m_menuVisual.addFilledRect(itemX,
                             selectionY,
                             3.0f,
                             m_itemHeight,
                             UiTheme::applyOpacity(cyan, opacity));
  for (int item = 0; item < kItemCount; ++item) {
    const float rowReveal =
      reducedMotion()
        ? 1.0f
        : easeOutCubic((m_revealElapsed - static_cast<float>(item) * 0.035f) /
                       0.32f);
    const unsigned char rowOpacity =
      static_cast<unsigned char>(255.0f * rowReveal);
    const float y = m_firstItemY + static_cast<float>(item) * stride;
    const float slide = (1.0f - rowReveal) * 10.0f;
    m_menuVisual.addText(
      labels[item],
      itemX + 18.0f + slide,
      y + 9.0f,
      18.0f,
      UiTheme::applyOpacity(
        item == m_selectedItem ? cyan : UiTheme::textPrimary(), rowOpacity));
    m_menuVisual.addText(
      descriptions[item],
      itemX + 18.0f + slide,
      y + 34.0f,
      11.0f,
      UiTheme::applyOpacity(UiTheme::textMuted(), rowOpacity));
    m_menuVisual.addText(item == m_selectedItem ? ">" : ".",
                         itemX + m_itemWidth - 24.0f,
                         y + 16.0f,
                         18.0f,
                         UiTheme::applyOpacity(cyan, rowOpacity));
  }
  m_menuVisual.addText(
    "ARROWS / MOUSE   Select     ENTER   Open     F1   Settings",
    m_panelX + 28.0f,
    m_panelY + m_panelHeight - 23.0f,
    10.0f,
    UiTheme::applyOpacity(UiTheme::textMuted(), opacity));
  m_menuVisual.setVisible(true);
}

void
MainMenuModule::DispatchDrawables(Scene* scene)
{
  if (scene == nullptr) {
    return;
  }
  if (m_bgContext != nullptr && m_bgContext->getCanvasView() != nullptr) {
    scene->AddDrawable(m_bgContext->getCanvasView(), RenderLayerId::World);
  }
  scene->AddDrawable(&m_menuVisual, RenderLayerId::UI);
  if (m_configurationMenu != nullptr && m_configurationMenu->isOpen()) {
    scene->AddDrawable(m_configurationMenu.get(), RenderLayerId::UI);
  }
}

void
MainMenuModule::registerConsoleCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  ic->commandRegistry->RegisterCommand(
    "play",
    [this](const std::vector<std::string>& args) {
      if (!args.empty()) {
        if (ic->commandLine != nullptr) {
          ic->commandLine->logError("Usage: play");
        }
        return;
      }
      if (ic->moduleHost != nullptr) {
        ic->moduleHost->RequestTransition(std::make_unique<CellGameModule>());
      }
    },
    "play",
    "Start simulation game module");
}

void
MainMenuModule::unregisterConsoleCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  ic->commandRegistry->UnregisterCommand("play");
}

void
MainMenuModule::Exit()
{
  unregisterConsoleCommands();
  m_configurationMenu.reset();
  m_bgContext.reset();
}
