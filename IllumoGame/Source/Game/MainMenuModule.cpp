#include "MainMenuModule.h"

#include "BuiltinPatterns.h"
#include "CellContext.h"
#include "CellGameModule.h"
#include "PatternCodec.h"
#include <Illumo/Engine/IModuleHost.h>
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
  : m_menuVisual(512u)
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

  m_panelWidth = std::min(480.0f, static_cast<float>(width) - 48.0f);
  m_panelHeight = 400.0f;
  m_panelX = (static_cast<float>(width) - m_panelWidth) * 0.5f;
  m_panelY = (static_cast<float>(height) - m_panelHeight) * 0.5f;

  m_itemWidth = m_panelWidth - 56.0f;
  m_itemHeight = 42.0f;
  m_firstItemY = m_panelY + 140.0f;
}

float
MainMenuModule::itemPosition() const
{
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
  if (config.fadeSpeed <= 0.0) {
    config.fadeSpeed = 8.0;
  }
  config.vsync = ic->envVars->getVar("vsync").valueAsBool;
  config.fullscreen = ic->envVars->getVar("fullscreen").valueAsBool;
  return config;
}

bool
MainMenuModule::applyConfiguration(const SimulatorConfiguration& configuration)
{
  if (ic == nullptr || ic->envVars == nullptr) {
    return false;
  }
  ic->envVars->setVar("ModeString", configuration.ruleSet);
  ic->envVars->setVar("WorldChunksX",
                      static_cast<long>(configuration.worldChunkWidth));
  ic->envVars->setVar("WorldChunksY",
                      static_cast<long>(configuration.worldChunkHeight));
  ic->envVars->setVar("tps", configuration.tps);
  ic->envVars->setVar("speedFactor", configuration.speedFactor);
  ic->envVars->setVar("cellFadeSpeed", configuration.fadeSpeed);
  ic->envVars->setVar("vsync", configuration.vsync);
  ic->envVars->setVar("fullscreen", configuration.fullscreen);
  return true;
}

void
MainMenuModule::Update(double dt)
{
  if (ic == nullptr) {
    return;
  }

  m_animationElapsed += static_cast<float>(dt);
  m_selectionAnimationElapsed =
    std::min(kSelectionAnimationSeconds,
             m_selectionAnimationElapsed + static_cast<float>(dt));

  advanceAmbientSimulation(dt);

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;

  if (m_configurationMenu != nullptr && m_configurationMenu->isOpen()) {
    m_configurationMenu->tick(static_cast<float>(dt));
    if (!consoleOpen) {
      const ConfigurationMenuAction action =
        m_configurationMenu->update(ic->inputManager);
      if (action == ConfigurationMenuAction::Apply) {
        SimulatorConfiguration config;
        std::string error;
        if (m_configurationMenu->readConfiguration(&config, &error)) {
          applyConfiguration(config);
          m_configurationMenu->close();
        } else {
          m_configurationMenu->setError(error);
        }
      } else if (action == ConfigurationMenuAction::Cancel ||
                 action == ConfigurationMenuAction::Exit) {
        m_configurationMenu->close();
      }
    }
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
      if (event.key == KeyCode::Up || event.key == KeyCode::W) {
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

    std::queue<unsigned int>& charQueue = ic->inputManager->getCharQueue();
    while (!charQueue.empty()) {
      charQueue.pop();
    }

    const std::array<double, 2> mouse = ic->inputManager->getMousePosition();
    const float mouseX = static_cast<float>(mouse[0]);
    const float mouseY = static_cast<float>(mouse[1]);
    const bool mouseDown =
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);

    const float itemX = m_panelX + 28.0f;
    const float itemGap = 12.0f;
    for (int i = 0; i < kItemCount; ++i) {
      const float currentItemY =
        m_firstItemY + static_cast<float>(i) * (m_itemHeight + itemGap);
      if (mouseX >= itemX && mouseX <= itemX + m_itemWidth &&
          mouseY >= currentItemY && mouseY <= currentItemY + m_itemHeight) {
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
  m_menuVisual.clearPrimitives();

  int width = 1280;
  int height = 720;
  if (ic != nullptr && ic->window != nullptr) {
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  updateLayout();

  // Dim background over ambient simulation
  m_menuVisual.addFilledRect(0.0f,
                             0.0f,
                             static_cast<float>(width),
                             static_cast<float>(height),
                             ColorRgba{ 10, 14, 23, 180 });

  // Main Card Panel
  m_menuVisual.addFilledRect(
    m_panelX + 6.0f,
    m_panelY + 6.0f,
    m_panelWidth,
    m_panelHeight,
    UiTheme::applyOpacity(UiTheme::panelShadow(), 240));
  m_menuVisual.addFilledRect(
    m_panelX,
    m_panelY,
    m_panelWidth,
    m_panelHeight,
    UiTheme::applyOpacity(UiTheme::panelSurface(), 245));
  m_menuVisual.addOutlineRect(
    m_panelX,
    m_panelY,
    m_panelWidth,
    m_panelHeight,
    UiTheme::applyOpacity(UiTheme::panelBorder(), 255),
    1.0f);
  m_menuVisual.addFilledRect(
    m_panelX, m_panelY, 5.0f, m_panelHeight, UiTheme::accent());

  // Title & Subtitle
  m_menuVisual.addText("ILLUMO",
                       m_panelX + 28.0f,
                       m_panelY + 28.0f,
                       32.0f,
                       UiTheme::textPrimary());
  m_menuVisual.addText("Cellular Automata Engine",
                       m_panelX + 28.0f,
                       m_panelY + 76.0f,
                       15.0f,
                       UiTheme::textMuted());

  // Separator Line
  m_menuVisual.addFilledRect(m_panelX + 28.0f,
                             m_panelY + 110.0f,
                             m_itemWidth,
                             1.0f,
                             UiTheme::panelBorder());

  const char* itemLabels[kItemCount] = { "Play / New Simulation",
                                         "Load Saved Simulation",
                                         "Settings",
                                         "Exit to Desktop" };

  const float itemX = m_panelX + 28.0f;
  const float itemGap = 12.0f;

  for (int i = 0; i < kItemCount; ++i) {
    const float currentItemY =
      m_firstItemY + static_cast<float>(i) * (m_itemHeight + itemGap);
    m_menuVisual.addFilledRect(
      itemX, currentItemY, m_itemWidth, m_itemHeight, UiTheme::panelRaised());
  }

  // Animated Selection indicator
  const float currentSelectionY =
    m_firstItemY + itemPosition() * (m_itemHeight + itemGap);
  m_menuVisual.addFilledRect(
    itemX, currentSelectionY, m_itemWidth, m_itemHeight, UiTheme::selection());
  m_menuVisual.addFilledRect(
    itemX, currentSelectionY, 4.0f, m_itemHeight, UiTheme::accent());

  for (int i = 0; i < kItemCount; ++i) {
    const float currentItemY =
      m_firstItemY + static_cast<float>(i) * (m_itemHeight + itemGap);
    const bool selected = i == m_selectedItem;
    m_menuVisual.addText(itemLabels[i],
                         itemX + 20.0f,
                         currentItemY + 11.0f,
                         17.0f,
                         selected ? UiTheme::textPrimary()
                                  : UiTheme::textMuted());
  }

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
