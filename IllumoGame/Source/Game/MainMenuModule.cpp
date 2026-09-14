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
  : m_menuVisual(4096u)
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
  m_panelWidth = std::min(680.0f, virtualWidth - 100.0f);
  m_panelHeight = std::min(600.0f, virtualHeight - 24.0f);
  m_panelX = (virtualWidth - m_panelWidth) * 0.5f;
  const float reveal =
    reducedMotion() ? 1.0f : easeOutCubic(m_revealElapsed / 0.45f);
  m_panelY = (virtualHeight - m_panelHeight) * 0.5f + 12.0f * (1.0f - reveal);
  m_itemWidth = m_panelWidth - 56.0f;
  const float headerHeight =
    140.0f + 48.0f * std::clamp((m_panelHeight - 456.0f) / 144.0f, 0.0f, 1.0f);
  m_itemHeight = (m_panelHeight - headerHeight - 56.0f) / 4.0f;
  m_firstItemY = m_panelY + headerHeight;
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

// Non-overlapping quarter fans keep translucent rounded surfaces evenly tinted.
static void
drawMenuSurface(GameVisual& visual,
                float x,
                float y,
                float width,
                float height,
                float radius,
                ColorRgba color)
{
  radius = std::min(radius, std::min(width, height) * 0.5f);
  visual.addFilledRect(x + radius, y, width - 2.0f * radius, height, color);
  visual.addFilledRect(x, y + radius, radius, height - 2.0f * radius, color);
  visual.addFilledRect(
    x + width - radius, y + radius, radius, height - 2.0f * radius, color);
  const float centersX[4] = {
    x + radius, x + width - radius, x + width - radius, x + radius
  };
  const float centersY[4] = {
    y + radius, y + radius, y + height - radius, y + height - radius
  };
  const float starts[4] = { 3.14159265f, 4.71238898f, 0.0f, 1.57079633f };
  for (int corner = 0; corner < 4; ++corner) {
    for (int segment = 0; segment < 6; ++segment) {
      const float a =
        starts[corner] + static_cast<float>(segment) * 0.26179939f;
      const float b = a + 0.26179939f;
      visual.addFilledTriangle(centersX[corner],
                               centersY[corner],
                               centersX[corner] + std::cos(a) * radius,
                               centersY[corner] + std::sin(a) * radius,
                               centersX[corner] + std::cos(b) * radius,
                               centersY[corner] + std::sin(b) * radius,
                               color);
    }
  }
}

static void
drawMenuIcon(GameVisual& visual, int item, float x, float y, ColorRgba color)
{
  if (item == 0) {
    visual.addFilledTriangle(
      x + 3.0f, y, x + 3.0f, y + 22.0f, x + 21.0f, y + 11.0f, color);
  } else if (item == 1) {
    visual.addOutlineRect(x, y + 6.0f, 24.0f, 17.0f, color, 2.0f);
    visual.addLine(x, y + 5.0f, x, y + 1.0f, color, 2.0f);
    visual.addLine(x, y + 1.0f, x + 10.0f, y + 1.0f, color, 2.0f);
    visual.addLine(x + 10.0f, y + 1.0f, x + 15.0f, y + 6.0f, color, 2.0f);
  } else if (item == 2) {
    for (int line = 0; line < 3; ++line) {
      const float ly = y + 3.0f + static_cast<float>(line) * 8.0f;
      const float knob = line == 1 ? 16.0f : 6.0f;
      visual.addLine(x, ly, x + 24.0f, ly, color, 2.0f);
      visual.addFilledRect(x + knob, ly - 3.0f, 4.0f, 6.0f, color);
    }
  } else {
    visual.addLine(x + 2.0f, y, x + 2.0f, y + 24.0f, color, 2.0f);
    visual.addLine(x + 2.0f, y, x + 10.0f, y, color, 2.0f);
    visual.addLine(x + 2.0f, y + 24.0f, x + 10.0f, y + 24.0f, color, 2.0f);
    visual.addLine(x + 8.0f, y + 12.0f, x + 25.0f, y + 12.0f, color, 2.0f);
    visual.addLine(x + 19.0f, y + 6.0f, x + 25.0f, y + 12.0f, color, 2.0f);
    visual.addLine(x + 19.0f, y + 18.0f, x + 25.0f, y + 12.0f, color, 2.0f);
  }
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

  const float room = std::clamp((m_panelHeight - 456.0f) / 144.0f, 0.0f, 1.0f);
  // Wide, low-opacity halos give the card depth without a postprocessing pass.
  for (int layer = 6; layer > 0; --layer) {
    const float spread = static_cast<float>(layer) * 5.0f;
    drawMenuSurface(
      m_menuVisual,
      m_panelX - spread,
      m_panelY - spread * 0.4f,
      m_panelWidth + spread * 2.0f,
      m_panelHeight + spread * 0.8f,
      22.0f + spread,
      ColorRgba{ 39, 116, 150, static_cast<unsigned char>(3.0f * reveal) });
  }
  drawMenuSurface(m_menuVisual,
                  m_panelX + 2.0f,
                  m_panelY + 10.0f,
                  m_panelWidth,
                  m_panelHeight,
                  22.0f,
                  UiTheme::applyOpacity(ColorRgba{ 2, 7, 17, 150 }, opacity));
  drawMenuSurface(
    m_menuVisual,
    m_panelX,
    m_panelY,
    m_panelWidth,
    m_panelHeight,
    22.0f,
    UiTheme::applyOpacity(ColorRgba{ 69, 110, 142, 255 }, opacity));
  drawMenuSurface(m_menuVisual,
                  m_panelX + 1.0f,
                  m_panelY + 1.0f,
                  m_panelWidth - 2.0f,
                  m_panelHeight - 2.0f,
                  21.0f,
                  UiTheme::applyOpacity(ColorRgba{ 14, 23, 40, 255 }, opacity));
  for (int strip = 0; strip < 14; ++strip) {
    const float sx = m_panelX + 24.0f +
                     static_cast<float>(strip) * (m_panelWidth - 48.0f) / 14.0f;
    const ColorRgba tint = strip < 8 ? cyan : violet;
    m_menuVisual.addFilledRect(sx,
                               m_panelY,
                               (m_panelWidth - 48.0f) / 14.0f + 0.1f,
                               2.0f,
                               UiTheme::applyOpacity(tint, opacity));
  }
  m_menuVisual.addText("C E L L U L A R   P L A Y G R O U N D",
                       m_panelX + 30.0f,
                       m_panelY + 23.0f,
                       9.0f + room * 2.0f,
                       UiTheme::applyOpacity(cyan, opacity));
  m_menuVisual.addText("ILLUMO",
                       m_panelX + 28.0f,
                       m_panelY + 43.0f,
                       40.0f + room * 18.0f,
                       UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));
  m_menuVisual.addText(
    "Small rules. Endless possibilities.",
    m_panelX + 30.0f,
    m_panelY + 98.0f + room * 20.0f,
    13.0f + room * 3.0f,
    UiTheme::applyOpacity(ColorRgba{ 182, 207, 226, 255 }, opacity));
  if (room > 0.5f) {
    m_menuVisual.addText("Build a world. See what emerges.",
                         m_panelX + 30.0f,
                         m_panelY + 145.0f,
                         12.0f,
                         UiTheme::applyOpacity(UiTheme::textMuted(), opacity));
  }

  const float cellSize = 7.0f + room * 5.0f;
  const float cellStep = cellSize + 3.0f;
  const float motifWidth = cellStep * 7.0f;
  const float motifX = m_panelX + m_panelWidth - motifWidth - 30.0f;
  const float motifY = m_panelY + 35.0f;
  const unsigned int motifRows[7] = { 0u, 8u, 4u, 28u, 0u, 0u, 0u };
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 7; ++col) {
      const bool lit = (motifRows[row] & (1u << col)) != 0u;
      const float shimmer =
        reducedMotion()
          ? 0.5f
          : 0.5f + 0.5f * std::sin(m_animationElapsed * 1.57079633f -
                                   static_cast<float>(row + col) * 0.65f);
      const float x = motifX + static_cast<float>(col) * cellStep;
      const float y = motifY + static_cast<float>(row) * cellStep;
      if (lit) {
        drawMenuSurface(
          m_menuVisual,
          x - 3.0f,
          y - 3.0f,
          cellSize + 6.0f,
          cellSize + 6.0f,
          4.0f,
          UiTheme::applyOpacity(
            ColorRgba{ 75,
                       220,
                       238,
                       static_cast<unsigned char>(25.0f + 25.0f * shimmer) },
            opacity));
      }
      drawMenuSurface(
        m_menuVisual,
        x,
        y,
        cellSize,
        cellSize,
        2.0f,
        UiTheme::applyOpacity(lit ? ColorRgba{ 104, 231, 241, 255 }
                                  : ColorRgba{ 60,
                                               90,
                                               128,
                                               static_cast<unsigned char>(
                                                 65.0f + 65.0f * shimmer) },
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
    drawMenuSurface(
      m_menuVisual,
      itemX,
      y + 3.0f,
      m_itemWidth,
      m_itemHeight,
      13.0f,
      UiTheme::applyOpacity(ColorRgba{ 4, 10, 21, 220 }, opacity));
    drawMenuSurface(
      m_menuVisual,
      itemX,
      y,
      m_itemWidth,
      m_itemHeight,
      13.0f,
      UiTheme::applyOpacity(ColorRgba{ 43, 61, 85, 255 }, opacity));
    drawMenuSurface(
      m_menuVisual,
      itemX + 1.0f,
      y + 1.0f,
      m_itemWidth - 2.0f,
      m_itemHeight - 2.0f,
      12.0f,
      UiTheme::applyOpacity(ColorRgba{ 23, 36, 56, 255 }, opacity));
  }
  const float selectionY = m_firstItemY + itemPosition() * stride;
  drawMenuSurface(
    m_menuVisual,
    itemX - 3.0f,
    selectionY - 3.0f,
    m_itemWidth + 6.0f,
    m_itemHeight + 6.0f,
    16.0f,
    UiTheme::applyOpacity(
      ColorRgba{
        68, 204, 229, static_cast<unsigned char>(22.0f + 12.0f * breathe) },
      opacity));
  drawMenuSurface(m_menuVisual,
                  itemX,
                  selectionY,
                  m_itemWidth,
                  m_itemHeight,
                  13.0f,
                  UiTheme::applyOpacity(cyan, opacity));
  drawMenuSurface(
    m_menuVisual,
    itemX + 1.0f,
    selectionY + 1.0f,
    m_itemWidth - 2.0f,
    m_itemHeight - 2.0f,
    12.0f,
    UiTheme::applyOpacity(ColorRgba{ 28, 78, 102, 255 }, opacity));
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
      itemX + 80.0f + slide,
      y + 12.0f + room * 7.0f,
      19.0f + room * 2.0f,
      UiTheme::applyOpacity(
        item == m_selectedItem ? cyan : UiTheme::textPrimary(), rowOpacity));
    m_menuVisual.addText(
      descriptions[item],
      itemX + 80.0f + slide,
      y + 37.0f + room * 8.0f,
      10.0f + room * 2.0f,
      UiTheme::applyOpacity(UiTheme::textMuted(), rowOpacity));
    const float iconY = y + (m_itemHeight - 42.0f) * 0.5f;
    drawMenuSurface(m_menuVisual,
                    itemX + 16.0f,
                    iconY,
                    44.0f,
                    42.0f,
                    10.0f,
                    UiTheme::applyOpacity(item == m_selectedItem
                                            ? ColorRgba{ 61, 139, 162, 170 }
                                            : ColorRgba{ 46, 65, 92, 200 },
                                          rowOpacity));
    drawMenuIcon(m_menuVisual,
                 item,
                 itemX + 26.0f,
                 iconY + 9.0f,
                 UiTheme::applyOpacity(item == m_selectedItem
                                         ? cyan
                                         : ColorRgba{ 166, 186, 213, 255 },
                                       rowOpacity));
    m_menuVisual.addText(">",
                         itemX + m_itemWidth - 24.0f,
                         y + m_itemHeight * 0.5f - 9.0f,
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
