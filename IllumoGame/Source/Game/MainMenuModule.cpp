#include "MainMenuModule.h"

#include "BuiltinPatterns.h"
#include "CSimPlatform.h"
#include "CanvasCoordinatePolicy.h"
#include "CellContext.h"
#include "CellGameModule.h"
#include "PatternCodec.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

MainMenuModule::MainMenuModule()
  : m_menuVisual(4096u)
  , m_selectedItem(kPlayItem)
  , m_bgSimAccum(0.0)
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

  // Reset camera for menu presentation; the ambient world is drawn at a finer
  // scale than the default canvas so it reads as texture behind the glass.
  ic->camera->SetPositionPrecise(0.0, 0.0);
  ic->camera->SetZoom(kAmbientZoom);

  // Immigration Life: Conway dynamics with cyan and coral species on a dark
  // field, so the live world shares the menu palette. CellContext records its
  // ruleset as the active preference, so keep the player's choice intact.
  const std::string savedFamily = ic->envVars->getVar("FamilyString").value;
  const std::string savedRuleSet = ic->envVars->getVar("RuleSetString").value;
  const std::string savedMode = ic->envVars->getVar("ModeString").value;
  m_bgContext = std::make_unique<CellContext>(
    "IMMIGRATION", ic->envVars, ic->window, ic->camera, ic->renderer);
  ic->envVars->setVar("FamilyString", savedFamily);
  ic->envVars->setVar("RuleSetString", savedRuleSet);
  ic->envVars->setVar("ModeString", savedMode);
  if (m_bgContext->getCanvasView() != nullptr) {
    m_bgContext->getCanvasView()->rebuildPalette(m_bgContext->getRuleSet());
  }
  seedAmbientPattern();

  m_rowEmphasis.configure(2.6f, 0.62f);
  m_recede.configure(2.2f, 0.9f);
  m_parallaxX.configure(0.9f, 1.0f);
  m_parallaxY.configure(0.9f, 1.0f);
  m_spotX.configure(2.4f, 0.85f);
  m_spotY.configure(2.4f, 0.85f);
  m_spotStrength.configure(1.2f, 1.0f);
  m_motif.resetGlider(7, 7, 1, 1);

  m_configurationMenu =
    std::make_unique<ConfigurationMenu>(ic->window, ic->renderer);

  m_newSimulationMenu =
    std::make_unique<NewSimulationMenu>(ic->window, ic->renderer);
  m_menuVisual.setSpace(PrimitiveSpace::Pixels);
  m_menuVisual.setLayerHint(RenderLayerId::UI);
  m_menuVisual.setWindow(ic->window);
  m_menuVisual.setRenderer(ic->renderer);
  m_menuVisual.prepare(ic->renderer);

  m_selectedItem = kPlayItem;
  m_animator.setReducedMotion(reducedMotion());
  m_animator.restart();
  m_rowEmphasis.focusOnly(m_selectedItem, kItemCount);
  m_rowEmphasis.snapAll();
  m_revealElapsed = 0.0f;
  m_bgSimAccum = 0.0;
  // Preserve the press edge so a click that left another screen is not
  // re-delivered to the first menu item.
  m_pointer.reset(ic->inputManager != nullptr &&
                  ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft));

  registerConsoleCommands();
  updateLayout();
  rebuildVisual();

  return true;
}

// Immigration Life states: coral 0, background 1, cyan 2.
static const unsigned char kCoralState = 0;
static const unsigned char kCyanState = 2;

void
MainMenuModule::stampPattern(const char* name,
                             std::int64_t originX,
                             std::int64_t originY,
                             bool flipX,
                             bool flipY,
                             unsigned char state)
{
  CellPattern pattern;
  if (!m_bgContext || !m_bgContext->getCanvasView() ||
      !BuiltinPatterns::find(name, &pattern)) {
    return;
  }
  CanvasView* canvas = m_bgContext->getCanvasView();
  for (const CellPatternCell& cell : pattern.getCells()) {
    // Pattern rows grow downward in RLE but upward in world space, so an
    // unflipped glider travels right and up on screen.
    const std::int64_t dx = flipX ? -cell.dx : cell.dx;
    const std::int64_t dy = flipY ? -cell.dy : cell.dy;
    canvas->setCanvasPixel(originX + dx, originY + dy, state);
  }
}

std::uint32_t
MainMenuModule::nextRandom()
{
  // Deterministic LCG: the menu needs variety, not entropy.
  m_randomState = m_randomState * 1664525u + 1013904223u;
  return m_randomState >> 8u;
}

void
MainMenuModule::seedAmbientPattern()
{
  if (!m_bgContext || !m_bgContext->getCanvasView()) {
    return;
  }
  CanvasView* canvas = m_bgContext->getCanvasView();
  canvas->clearCanvas();
  m_worldElapsed = 0.0;
  m_visitorElapsed = kVisitorSeconds * 0.6f;

  // A Gosper gun in the lower left streams cyan gliders up across the
  // screen; coral methuselahs churn on either side of the panel.
  stampPattern("gosper", -78, -40, false, false, kCyanState);
  stampPattern("glider", 52, -30, true, false, kCyanState);
  const std::int64_t acorns[2][2] = { { 58, 18 }, { -62, 24 } };
  const int acornCells[7][2] = { { 1, 0 }, { 3, 1 }, { 0, 2 }, { 1, 2 },
                                 { 4, 2 }, { 5, 2 }, { 6, 2 } };
  for (const std::int64_t* origin : acorns) {
    for (const int* cell : acornCells) {
      canvas->setCanvasPixel(
        origin[0] + cell[0], origin[1] - cell[1], kCoralState);
    }
  }
}

void
MainMenuModule::launchVisitor()
{
  if (!m_bgContext || !m_bgContext->getCanvasView() || ic == nullptr ||
      ic->window == nullptr) {
    return;
  }
  // Launch just outside the visible world, aimed back into it.
  const std::array<int, 2> window = ic->window->getWindowDimensions();
  const double cellPixels = CanvasCoordinatePolicy::kCellSize * kAmbientZoom;
  const std::int64_t halfWidth = static_cast<std::int64_t>(
    static_cast<double>(std::max(1, window[0])) * 0.5 / cellPixels);
  const std::int64_t halfHeight = static_cast<std::int64_t>(
    static_cast<double>(std::max(1, window[1])) * 0.5 / cellPixels);
  const std::uint32_t roll = nextRandom();
  const bool fromLeft = (roll & 1u) != 0u;
  const bool fromBelow = (roll & 2u) != 0u;
  const std::int64_t along =
    static_cast<std::int64_t>((roll >> 2u) % 1000u) - 500;
  const unsigned char state =
    m_visitorCount % 3 == 2 ? kCyanState : kCoralState;
  ++m_visitorCount;
  if (m_visitorCount % 2 == 0) {
    // Spaceships cruise straight across; flipX sends them right.
    const std::int64_t y = along * halfHeight * 7 / 10 / 500;
    stampPattern("lwss",
                 fromLeft ? -halfWidth - 8 : halfWidth + 3,
                 y,
                 fromLeft,
                 false,
                 state);
    return;
  }
  // Gliders enter diagonally from a corner region.
  const std::int64_t x = fromLeft ? -halfWidth - 4 : halfWidth + 4;
  const std::int64_t y = along * halfHeight / 500;
  stampPattern("glider",
               x,
               fromBelow ? y - halfHeight / 2 : y + halfHeight / 2,
               !fromLeft,
               !fromBelow,
               state);
}

void
MainMenuModule::advanceAmbientSimulation(double dt)
{
  if (!m_bgContext || !m_bgContext->getCanvasView() ||
      !m_bgContext->getGrid() || !m_bgContext->getRuleSet()) {
    return;
  }
  m_worldElapsed += dt;
  if (m_bgContext->getGrid()->getAllocatedChunkCount() > kReseedChunkCount ||
      m_worldElapsed > kReseedSeconds) {
    seedAmbientPattern();
  }
  if (!reducedMotion()) {
    m_visitorElapsed += static_cast<float>(dt);
    if (m_visitorElapsed >= kVisitorSeconds) {
      m_visitorElapsed = 0.0f;
      launchVisitor();
    }
  }
  m_bgSimAccum += dt;
  const double stepSeconds = 1.0 / kAmbientStepsPerSecond;
  while (m_bgSimAccum >= stepSeconds) {
    m_bgSimAccum -= stepSeconds;
    m_bgContext->getGrid()->advance(*m_bgContext->getRuleSet());
  }
  // Resample changed chunks (and stamped visitors) into the view's targets;
  // the view then fades toward them.
  m_bgContext->getCanvasView()->rebuildTargetsFromGrid();
  m_bgContext->getCanvasView()->tickVisual(static_cast<float>(dt));
}

void
MainMenuModule::updateLayout()
{
  m_panelFit = GuiPanelLayout::fit(ic != nullptr ? ic->window : nullptr,
                                   ic != nullptr ? ic->renderer : nullptr,
                                   &m_menuVisual);
  const float virtualWidth = m_panelFit.virtualWidth;
  const float virtualHeight = m_panelFit.virtualHeight;
  m_panelWidth = std::min(680.0f, virtualWidth - 100.0f);
  m_panelHeight = std::min(600.0f, virtualHeight - 24.0f);
  m_panelX = (virtualWidth - m_panelWidth) * 0.5f;
  const float reveal = entranceReveal();
  m_panelY = (virtualHeight - m_panelHeight) * 0.5f + 12.0f * (1.0f - reveal);
  m_itemWidth = m_panelWidth - 56.0f;
  const float headerHeight =
    140.0f + 48.0f * std::clamp((m_panelHeight - 456.0f) / 144.0f, 0.0f, 1.0f);
  m_itemHeight = (m_panelHeight - headerHeight - 56.0f) / 4.0f;
  m_firstItemY = m_panelY + headerHeight;
}

float
MainMenuModule::entranceReveal() const
{
  if (reducedMotion()) {
    return 1.0f;
  }
  return GuiEasing::outCubic(m_revealElapsed / kEntranceSeconds);
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
  return m_animator.selectionPosition(static_cast<float>(m_selectedItem));
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
    m_animator.beginSelectionTravel(itemPosition());
    m_selectedItem = nextItem;
  }
}

void
MainMenuModule::pressItem(float originX, float originY)
{
  m_pressX = originX;
  m_pressY = originY;
  m_animator.triggerPress();
}

// Springs that make the title screen respond: row emphasis follows the
// selection, the panel recedes behind overlays, and the background leans
// toward the pointer while a soft spotlight follows it.
void
MainMenuModule::updateMotion(float dt)
{
  const bool still = reducedMotion();
  m_rowEmphasis.focusOnly(m_selectedItem, kItemCount);
  m_rowEmphasis.tick(dt, still);

  const bool overlayOpen =
    (m_configurationMenu != nullptr && m_configurationMenu->isOpen()) ||
    (m_newSimulationMenu != nullptr && m_newSimulationMenu->isOpen());
  m_recede.setTarget(overlayOpen ? 1.0f : 0.0f);
  m_recede.tick(dt, still);

  m_motif.tick(dt, still);

  const float width = m_panelFit.virtualWidth;
  const float height = m_panelFit.virtualHeight;
  const float pointerX = m_pointer.x();
  const float pointerY = m_pointer.y();
  const bool pointerInside = !overlayOpen && !still && pointerX >= 0.0f &&
                             pointerY >= 0.0f && pointerX <= width &&
                             pointerY <= height;
  if (pointerInside) {
    m_parallaxX.setTarget(
      std::clamp(pointerX / width * 2.0f - 1.0f, -1.0f, 1.0f));
    m_parallaxY.setTarget(
      std::clamp(pointerY / height * 2.0f - 1.0f, -1.0f, 1.0f));
    if (m_spotStrength.value() <= 0.0f) {
      // The spotlight appears where the pointer is rather than sweeping in.
      m_spotX.snapTo(pointerX);
      m_spotY.snapTo(pointerY);
    }
    m_spotX.setTarget(pointerX);
    m_spotY.setTarget(pointerY);
  } else if (still) {
    m_parallaxX.setTarget(0.0f);
    m_parallaxY.setTarget(0.0f);
  }
  m_spotStrength.setTarget(pointerInside ? 1.0f : 0.0f);
  m_parallaxX.tick(dt, still);
  m_parallaxY.tick(dt, still);
  m_spotX.tick(dt, still);
  m_spotY.tick(dt, still);
  m_spotStrength.tick(dt, still);
}

void
MainMenuModule::openCanvasSetup()
{
  NewSimulationConfiguration initial;
  initial.ruleSet = ic->envVars->getVar("RuleSetString").value;
  if (initial.ruleSet.empty()) {
    initial.ruleSet = ic->envVars->getVar("ModeString").value;
  }
  initial.family = ic->envVars->getVar("FamilyString").value;
  const RuleSetDefinition* initialRule =
    RuleSetRegistry::instance().getRuleSetDefinition(initial.ruleSet);
  if (initialRule != nullptr && initial.family != initialRule->familyId) {
    initial.family = initialRule->familyId;
  }
  initial.worldChunkWidth = ic->envVars->getVar("WorldChunksX").valueAsLong;
  initial.worldChunkHeight = ic->envVars->getVar("WorldChunksY").valueAsLong;
  m_configurationMenu->close();
  m_newSimulationMenu->open(initial, reducedMotion());
}

void
MainMenuModule::activateSelectedItem()
{
  if (ic == nullptr || ic->moduleHost == nullptr) {
    return;
  }

  switch (m_selectedItem) {
    case kPlayItem: {
      openCanvasSetup();
      break;
    }
    case kLoadItem: {
      SaveLoadDialogSpec spec;
      spec.fileDescription = "CSim Simulations";
      spec.defaultFilename = "MyCanvas.csim";
      spec.extensionPattern = "*.CSIM;*.ILLUMO";
      const std::weak_ptr<bool> alive = m_lifetime;
      CSimPlatform::current().chooseLoadLocation(
        spec, [this, alive](const std::string& location) {
          if (!alive.expired() && !location.empty() && ic != nullptr &&
              ic->moduleHost != nullptr) {
            ic->moduleHost->RequestTransition(
              std::make_unique<CellGameModule>(location));
          }
        });
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
  config.ruleSet = ic->envVars->getVar("RuleSetString").value;
  if (config.ruleSet.empty()) {
    config.ruleSet = ic->envVars->getVar("ModeString").value;
  }
  if (config.ruleSet.empty()) {
    config.ruleSet = "GAME_OF_LIFE";
  }
  config.family = ic->envVars->getVar("FamilyString").value;
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(config.ruleSet);
  if (rule != nullptr && config.family != rule->familyId) {
    config.family = rule->familyId;
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
  const EnvVar& hintsVar = ic->envVars->getVar("editHints");
  config.editHints = hintsVar.value.empty() || hintsVar.valueAsBool;
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
  const RuleSetDefinition* rule =
    RuleSetRegistry::instance().getRuleSetDefinition(configuration.ruleSet);
  if (rule == nullptr || rule->familyId != configuration.family) {
    return false;
  }
  const bool fullscreenChanged =
    configuration.fullscreen != ic->envVars->getVar("fullscreen").valueAsBool;
  ic->envVars->setVar("FamilyString", configuration.family);
  ic->envVars->setVar("RuleSetString", configuration.ruleSet);
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
  ic->envVars->setVar("editHints", configuration.editHints);
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
  m_revealElapsed =
    std::min(kEntranceCeilingSeconds, m_revealElapsed + static_cast<float>(dt));
  // Reduced motion is a live preference; the shared clocks follow it.
  m_animator.setReducedMotion(reducedMotion());
  m_animator.tick(static_cast<float>(dt));
  updateMotion(static_cast<float>(dt));

  advanceAmbientSimulation(std::min(dt, 0.25));

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;

  if (m_newSimulationMenu->isOpen()) {
    m_pointer.adoptPressed(
      ic->inputManager != nullptr &&
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft));
    m_newSimulationMenu->tick(static_cast<float>(dt));
    if (!consoleOpen) {
      const NewSimulationAction action =
        m_newSimulationMenu->update(ic->inputManager);
      if (action == NewSimulationAction::Create) {
        ic->moduleHost->RequestTransition(std::make_unique<CellGameModule>(
          m_newSimulationMenu->configuration()));
        m_newSimulationMenu->close();
      } else if (action == NewSimulationAction::Back) {
        m_newSimulationMenu->close();
      }
    }
    rebuildVisual();
    return;
  }

  if (m_configurationMenu != nullptr && m_configurationMenu->isOpen()) {
    // Preserve the button edge across modal close; Apply must not click the
    // main-menu action underneath it on the following frame.
    m_pointer.adoptPressed(
      ic->inputManager != nullptr &&
      ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft));
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
    bool activated = false;
    while (!keyQueue.empty()) {
      const InputManager::KeyPressEvent event = keyQueue.front();
      keyQueue.pop();
      if (event.key == KeyCode::Grave) {
        remainingKeys.push(event);
        continue;
      }
      if (activated || (event.action != InputAction::Press &&
                        event.action != InputAction::Hold)) {
        continue;
      }
      if (event.key == KeyCode::F1) {
        m_configurationMenu->open(currentConfiguration());
        activated = true;
      } else if (event.key == KeyCode::Up || event.key == KeyCode::W) {
        selectItem(m_selectedItem - 1);
      } else if (event.key == KeyCode::Down || event.key == KeyCode::S ||
                 event.key == KeyCode::Tab) {
        selectItem(m_selectedItem + 1);
      } else if (event.key == KeyCode::Enter || event.key == KeyCode::Space) {
        const float rowY = m_firstItemY + static_cast<float>(m_selectedItem) *
                                            (m_itemHeight + 8.0f);
        pressItem(m_panelX + 28.0f + m_itemWidth * 0.5f,
                  rowY + m_itemHeight * 0.5f);
        activateSelectedItem();
        activated = true;
      } else if (event.key == KeyCode::Escape) {
        if (ic->window != nullptr) {
          ic->window->requestClose();
        }
      }
    }
    keyQueue.swap(remainingKeys);
    if (m_configurationMenu->isOpen() || m_newSimulationMenu->isOpen()) {
      rebuildVisual();
      return;
    }

    std::queue<unsigned int>& charQueue = ic->inputManager->getCharQueue();
    while (!charQueue.empty()) {
      charQueue.pop();
    }

    m_pointer.sample(ic->window, ic->inputManager, m_panelFit.layoutScale);
    const float mouseX = m_pointer.x();
    const float mouseY = m_pointer.y();

    const float itemX = m_panelX + 28.0f;
    const float itemGap = 8.0f;
    for (int i = 0; i < kItemCount; ++i) {
      const float currentItemY =
        m_firstItemY + static_cast<float>(i) * (m_itemHeight + itemGap);
      if ((m_pointer.moved() || m_pointer.clicked()) && mouseX >= itemX &&
          mouseX <= itemX + m_itemWidth && mouseY >= currentItemY &&
          mouseY <= currentItemY + m_itemHeight) {
        if (m_selectedItem != i) {
          selectItem(i);
        }
        if (m_pointer.clicked()) {
          pressItem(mouseX, mouseY);
          activateSelectedItem();
        }
        break;
      }
    }
  }

  rebuildVisual();
}

// Line icons that come alive with row emphasis `e` (0 idle, 1 focused): the
// play arrow nudges forward with an echo, the folder lid lifts over a page,
// the sliders' knobs glide, and the exit arrow pushes through the door.
static void
drawMenuIcon(GameVisual& visual,
             int item,
             float x,
             float y,
             ColorRgba color,
             float e)
{
  const float emphasis = std::clamp(e, 0.0f, 1.2f);
  if (item == 0) {
    const float nudge = 3.0f * emphasis;
    if (emphasis > 0.02f) {
      visual.addFilledTriangle(x + 3.0f + nudge - 6.0f,
                               y + 2.0f,
                               x + 3.0f + nudge - 6.0f,
                               y + 20.0f,
                               x + 18.0f + nudge - 6.0f,
                               y + 11.0f,
                               UiTheme::fade(color, 0.3f * emphasis));
    }
    visual.addFilledTriangle(x + 3.0f + nudge,
                             y,
                             x + 3.0f + nudge,
                             y + 22.0f,
                             x + 21.0f + nudge,
                             y + 11.0f,
                             color);
  } else if (item == 1) {
    const float lift = 3.5f * emphasis;
    if (emphasis > 0.02f) {
      // A page slides up out of the folder.
      visual.addFilledRect(x + 5.0f,
                           y + 9.0f - 4.0f * emphasis,
                           14.0f,
                           8.0f,
                           UiTheme::fade(color, 0.45f * emphasis));
    }
    visual.addOutlineRect(x, y + 6.0f, 24.0f, 17.0f, color, 2.0f);
    visual.addLine(x, y + 5.0f - lift, x, y + 1.0f - lift, color, 2.0f);
    visual.addLine(x, y + 1.0f - lift, x + 10.0f, y + 1.0f - lift, color, 2.0f);
    visual.addLine(x + 10.0f,
                   y + 1.0f - lift,
                   x + 15.0f,
                   y + 6.0f - lift * 0.4f,
                   color,
                   2.0f);
  } else if (item == 2) {
    const float restKnob[3] = { 6.0f, 16.0f, 9.0f };
    const float focusKnob[3] = { 15.0f, 5.0f, 17.0f };
    for (int line = 0; line < 3; ++line) {
      const float ly = y + 3.0f + static_cast<float>(line) * 8.0f;
      const float knob =
        restKnob[line] + (focusKnob[line] - restKnob[line]) * emphasis;
      visual.addLine(x, ly, x + 24.0f, ly, UiTheme::fade(color, 0.55f), 2.0f);
      visual.addLine(x, ly, x + knob + 2.0f, ly, color, 2.0f);
      visual.addFilledRect(x + knob, ly - 3.0f, 4.0f, 6.0f, color);
    }
  } else {
    const float push = 4.0f * emphasis;
    visual.addLine(x + 2.0f, y, x + 2.0f, y + 24.0f, color, 2.0f);
    visual.addLine(x + 2.0f, y, x + 10.0f, y, color, 2.0f);
    visual.addLine(x + 2.0f, y + 24.0f, x + 10.0f, y + 24.0f, color, 2.0f);
    visual.addLine(
      x + 8.0f + push, y + 12.0f, x + 25.0f + push, y + 12.0f, color, 2.0f);
    visual.addLine(
      x + 19.0f + push, y + 6.0f, x + 25.0f + push, y + 12.0f, color, 2.0f);
    visual.addLine(
      x + 19.0f + push, y + 18.0f, x + 25.0f + push, y + 12.0f, color, 2.0f);
  }
}

void
MainMenuModule::drawBackground(float width, float height, float reveal)
{
  const float ambient = m_animator.ambientPhase();
  const float phase = ambient * 0.52359877f;
  const float leanX = m_parallaxX.value();
  const float leanY = m_parallaxY.value();

  // The live Immigration world shows through a translucent tint; a radial
  // scrim darkens the corners so the panel sits in a pool of light.
  m_menuVisual.addFilledRect(
    0.0f, 0.0f, width, height, ColorRgba{ 5, 10, 22, 118 });
  GuiKit::drawVignette(m_menuVisual,
                       width,
                       height,
                       ColorRgba{ 4, 8, 18, 0 },
                       ColorRgba{ 2, 4, 11, 232 },
                       0.34f);

  // Three slow aurora glows drift on the ambient cycle and lean against the
  // pointer at different depths.
  const ColorRgba tints[3] = { UiTheme::accentCool(),
                               UiTheme::accentViolet(),
                               ColorRgba{ 40, 120, 210, 255 } };
  const float depths[3] = { 26.0f, 44.0f, 16.0f };
  for (int light = 0; light < 3; ++light) {
    const float seed = static_cast<float>(light);
    const float cx = width * (0.16f + 0.34f * seed) +
                     std::sin(phase + seed * 2.1f) * width * 0.06f -
                     leanX * depths[light];
    const float cy = height * (0.28f + 0.2f * static_cast<float>(light % 2)) +
                     std::cos(phase * 0.8f + seed * 1.3f) * height * 0.08f -
                     leanY * depths[light];
    const float swell = 1.0f + 0.08f * std::sin(phase * 1.7f + seed);
    GuiKit::drawSoftGlow(
      m_menuVisual,
      cx,
      cy,
      width * 0.34f * swell,
      height * 0.52f * swell,
      UiTheme::fade(tints[light], (light == 2 ? 0.12f : 0.16f) * reveal),
      20);
  }

  const float spot = m_spotStrength.value();
  if (spot > 0.01f) {
    GuiKit::drawSoftGlow(m_menuVisual,
                         m_spotX.value(),
                         m_spotY.value(),
                         150.0f,
                         150.0f,
                         UiTheme::fade(UiTheme::accentCool(), 0.1f * spot),
                         20);
  }
}

void
MainMenuModule::drawTitle(float room, unsigned char opacity)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const bool still = reducedMotion();
  const float ambient = m_animator.ambientPhase();
  const float eyebrowReveal =
    still ? 1.0f : GuiEasing::outCubic(m_revealElapsed / 0.5f);
  m_menuVisual.addText(
    "C E L L U L A R   P L A Y G R O U N D",
    m_panelX + 30.0f - 8.0f * (1.0f - eyebrowReveal),
    m_panelY + 23.0f,
    9.0f + room * 2.0f,
    UiTheme::applyOpacity(UiTheme::fade(cyan, eyebrowReveal), opacity));

  const float titleSize = 40.0f + room * 18.0f;
  // Letters overshoot while landing; the raster covers that peak so glyphs
  // are never magnified.
  const float peakTitleSize = titleSize * 1.08f;
  const float displayedTitleSize = peakTitleSize * m_panelFit.layoutScale;
  // Three cached resolutions cover the supported 1x-4x UI scales without
  // enlarging the default 32px glyphs or caching an atlas for every resize.
  const int rasterSize = displayedTitleSize <= 64.0f    ? 64
                         : displayedTitleSize <= 128.0f ? 128
                                                        : 256;
  if (m_titleRasterSize != rasterSize) {
    m_titleFont = Font::getDefaultFont();
    if (m_titleFont != nullptr && !m_titleFont->getPath().empty() &&
        m_titleFont->getPath() != "<memory>") {
      m_titleFont = Font::loadFromFile(m_titleFont->getPath(),
                                       static_cast<float>(rasterSize));
    }
    m_titleRasterSize = rasterSize;
  }
  // Each letter drops in on its own overshooting curve, then breathes with
  // a gentle bob that ripples across the word.
  const char* letters[4] = { "C", "S", "I", "M" };
  const float titleX = m_panelX + 28.0f;
  const float titleY = m_panelY + 43.0f;
  float advance = 0.0f;
  for (int letter = 0; letter < 4; ++letter) {
    const float start = kTitleLetterDelaySeconds +
                        static_cast<float>(letter) * kTitleLetterStaggerSeconds;
    const float progress =
      still ? 1.0f : (m_revealElapsed - start) / kTitleLetterSeconds;
    const float landed = still ? 1.0f : GuiEasing::outBack(progress, 1.6f);
    const float fade = std::clamp(progress * 2.2f, 0.0f, 1.0f);
    const float bob =
      still
        ? 0.0f
        : std::sin(ambient * 2.0943951f - static_cast<float>(letter) * 0.9f) *
            1.5f * std::clamp(progress - 1.0f, 0.0f, 1.0f);
    const float size = titleSize * (0.82f + 0.18f * landed);
    const float letterWidth =
      m_titleFont != nullptr
        ? m_titleFont->measureText(letters[letter], titleSize).width
        : titleSize * 0.6f;
    const float drawnWidth =
      m_titleFont != nullptr
        ? m_titleFont->measureText(letters[letter], size).width
        : size * 0.6f;
    const size_t index = m_menuVisual.addText(
      letters[letter],
      titleX + advance + (letterWidth - drawnWidth) * 0.5f,
      titleY + (1.0f - landed) * 22.0f + (titleSize - size) * 0.5f + bob,
      size,
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::textPrimary(), fade),
                            opacity));
    m_menuVisual.getText(index)->font = m_titleFont;
    advance += letterWidth;
  }
  // A cyan-to-violet underline draws out beneath the word.
  const float underline =
    still
      ? 1.0f
      : GuiEasing::outCubic((m_revealElapsed - 0.42f) / kTitleLetterSeconds);
  if (underline > 0.0f) {
    const float lineY = titleY + titleSize * 0.98f + 4.0f;
    m_menuVisual.addGradientRect(
      titleX + 2.0f,
      lineY,
      std::max(0.0f, advance - 4.0f) * underline,
      3.0f,
      UiTheme::applyOpacity(cyan, opacity),
      UiTheme::applyOpacity(UiTheme::accentViolet(), opacity),
      UiTheme::applyOpacity(UiTheme::accentViolet(), opacity),
      UiTheme::applyOpacity(cyan, opacity));
  }

  const float taglineReveal =
    still ? 1.0f : GuiEasing::outCubic((m_revealElapsed - 0.3f) / 0.45f);
  m_menuVisual.addText(
    "Small rules. Endless possibilities.",
    m_panelX + 30.0f,
    m_panelY + 98.0f + room * 20.0f + 6.0f * (1.0f - taglineReveal),
    13.0f + room * 3.0f,
    UiTheme::applyOpacity(
      UiTheme::fade(UiTheme::textSecondary(), taglineReveal), opacity));
  if (room > 0.5f) {
    const float secondReveal =
      still ? 1.0f : GuiEasing::outCubic((m_revealElapsed - 0.4f) / 0.45f);
    m_menuVisual.addText(
      "Build a world. See what emerges.",
      m_panelX + 30.0f,
      m_panelY + 145.0f + 6.0f * (1.0f - secondReveal),
      12.0f,
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::textMuted(), secondReveal),
                            opacity));
  }

  // The motif is a real glider walking a 7x7 torus, leaning with the
  // pointer like the background but less, so it floats between the layers.
  const float cellSize = 7.0f + room * 5.0f;
  const float gap = 3.0f;
  const float motifWidth = m_motif.width(cellSize, gap);
  const float motifX =
    m_panelX + m_panelWidth - motifWidth - 33.0f + m_parallaxX.value() * 3.0f;
  const float motifY = m_panelY + 35.0f + m_parallaxY.value() * 3.0f;
  m_motif.draw(m_menuVisual,
               motifX,
               motifY,
               cellSize,
               gap,
               ColorRgba{ 104, 231, 241, 255 },
               ColorRgba{ 52, 80, 116, 120 },
               0.8f,
               opacity);
}

void
MainMenuModule::drawRows(float room, unsigned char opacity, float breathe)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const char* labels[kItemCount] = {
    "New simulation", "Load simulation", "Settings", "Exit to desktop"
  };
  const char* descriptions[kItemCount] = {
    "Create, experiment, and watch patterns evolve",
    "Continue exploring a saved world",
    "Tune simulation, display, and motion",
    "Close CSim"
  };
  const bool still = reducedMotion();
  const float itemX = m_panelX + 28.0f;
  const float stride = m_itemHeight + 8.0f;
  const float radius = 13.0f;

  float rowReveal[kItemCount];
  for (int item = 0; item < kItemCount; ++item) {
    rowReveal[item] = still
                        ? 1.0f
                        : GuiEasing::outCubic(
                            (m_revealElapsed - static_cast<float>(item) *
                                                 kItemEntranceStaggerSeconds) /
                            kItemEntranceSeconds);
  }

  // Cards: soft drop shadow, a rim that warms toward the accent with
  // emphasis, and a top-lit gradient face.
  for (int item = 0; item < kItemCount; ++item) {
    const float y = m_firstItemY + static_cast<float>(item) * stride +
                    (1.0f - rowReveal[item]) * 8.0f;
    const unsigned char rowOpacity =
      static_cast<unsigned char>(static_cast<float>(opacity) * rowReveal[item]);
    const float e = std::clamp(m_rowEmphasis.value(item), 0.0f, 1.0f);
    GuiKit::drawSoftShadow(
      m_menuVisual,
      itemX,
      y,
      m_itemWidth,
      m_itemHeight,
      radius,
      12.0f,
      4.0f,
      UiTheme::applyOpacity(UiTheme::glowShadow(), rowOpacity));
    GuiKit::drawRoundedRect(
      m_menuVisual,
      itemX,
      y,
      m_itemWidth,
      m_itemHeight,
      radius,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::cardRim(), ColorRgba{ 70, 140, 170, 255 }, e),
        rowOpacity));
    GuiKit::drawRoundedGradientRect(
      m_menuVisual,
      itemX + 1.0f,
      y + 1.0f,
      m_itemWidth - 2.0f,
      m_itemHeight - 2.0f,
      radius - 1.0f,
      UiTheme::applyOpacity(UiTheme::cardTop(), rowOpacity),
      UiTheme::applyOpacity(UiTheme::cardBottom(), rowOpacity));
  }

  // The selection pill stretches between rows while it travels, breathes a
  // soft glow while it rests, and a sheen sweeps across when it lands.
  const GuiSelectionSpan span =
    m_animator.selectionSpan(static_cast<float>(m_selectedItem));
  const float spanTop = std::min(span.leading, span.trailing);
  const float spanBottom = std::max(span.leading, span.trailing);
  const float press = m_animator.pressPulse();
  const float squish = 3.0f * press * press;
  const float pillX = itemX + squish;
  const float pillY = m_firstItemY + spanTop * stride + squish * 0.5f;
  const float pillWidth = m_itemWidth - squish * 2.0f;
  const float pillHeight =
    (spanBottom - spanTop) * stride + m_itemHeight - squish;
  const unsigned char pillOpacity = static_cast<unsigned char>(
    static_cast<float>(opacity) * rowReveal[m_selectedItem]);
  GuiKit::drawRoundedBand(
    m_menuVisual,
    pillX,
    pillY,
    pillWidth,
    pillHeight,
    radius,
    0.0f,
    16.0f + 4.0f * breathe,
    UiTheme::applyOpacity(UiTheme::fade(cyan, 0.2f + 0.12f * breathe),
                          pillOpacity),
    UiTheme::transparentOf(cyan));
  GuiKit::drawRoundedRect(m_menuVisual,
                          pillX,
                          pillY,
                          pillWidth,
                          pillHeight,
                          radius,
                          UiTheme::applyOpacity(cyan, pillOpacity));
  GuiKit::drawRoundedGradientRect(
    m_menuVisual,
    pillX + 1.0f,
    pillY + 1.0f,
    pillWidth - 2.0f,
    pillHeight - 2.0f,
    radius - 1.0f,
    UiTheme::applyOpacity(UiTheme::selectionTop(), pillOpacity),
    UiTheme::applyOpacity(UiTheme::selectionBottom(), pillOpacity));
  GuiKit::drawSheen(
    m_menuVisual,
    pillX,
    pillY,
    pillWidth,
    pillHeight,
    radius,
    m_animator.selectionSheen(),
    UiTheme::applyOpacity(ColorRgba{ 210, 250, 255, 46 }, pillOpacity));
  const float pressProgress = m_animator.pressProgress();
  if (pressProgress >= 0.0f) {
    // A ring ripples out of the pressed card and a flash blooms at the
    // press point.
    const float ring = GuiEasing::outCubic(pressProgress);
    const float fadeOut = 1.0f - pressProgress;
    GuiKit::drawRoundedBand(m_menuVisual,
                            pillX,
                            pillY,
                            pillWidth,
                            pillHeight,
                            radius,
                            2.0f + 18.0f * ring,
                            5.0f + 26.0f * ring,
                            UiTheme::fade(cyan, 0.7f * fadeOut),
                            UiTheme::transparentOf(cyan));
    GuiKit::drawSoftGlow(
      m_menuVisual,
      m_pressX,
      m_pressY,
      40.0f + 140.0f * ring,
      30.0f + 60.0f * ring,
      UiTheme::fade(ColorRgba{ 190, 245, 255, 255 }, 0.35f * fadeOut),
      16);
  }

  for (int item = 0; item < kItemCount; ++item) {
    const unsigned char rowOpacity =
      static_cast<unsigned char>(static_cast<float>(opacity) * rowReveal[item]);
    const float y = m_firstItemY + static_cast<float>(item) * stride +
                    (1.0f - rowReveal[item]) * 8.0f;
    const float e = std::max(0.0f, m_rowEmphasis.value(item));
    const float eClamped = std::min(1.0f, e);
    const float slide = (1.0f - rowReveal[item]) * 10.0f + 6.0f * e;

    // Icon tile grows and glows with emphasis.
    const float tileScale = 1.0f + 0.06f * e;
    const float tileWidth = 44.0f * tileScale;
    const float tileHeight = 42.0f * tileScale;
    const float tileCenterX = itemX + 38.0f;
    const float tileCenterY = y + m_itemHeight * 0.5f;
    if (eClamped > 0.02f) {
      GuiKit::drawSoftGlow(
        m_menuVisual,
        tileCenterX,
        tileCenterY,
        40.0f,
        38.0f,
        UiTheme::applyOpacity(UiTheme::fade(cyan, 0.3f * eClamped), rowOpacity),
        16);
    }
    GuiKit::drawRoundedGradientRect(
      m_menuVisual,
      tileCenterX - tileWidth * 0.5f,
      tileCenterY - tileHeight * 0.5f,
      tileWidth,
      tileHeight,
      10.0f,
      UiTheme::applyOpacity(UiTheme::mix(ColorRgba{ 52, 72, 101, 220 },
                                         ColorRgba{ 66, 150, 172, 200 },
                                         eClamped),
                            rowOpacity),
      UiTheme::applyOpacity(UiTheme::mix(ColorRgba{ 38, 55, 80, 220 },
                                         ColorRgba{ 42, 104, 128, 200 },
                                         eClamped),
                            rowOpacity));
    drawMenuIcon(
      m_menuVisual,
      item,
      tileCenterX - 12.0f,
      tileCenterY - 12.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(ColorRgba{ 166, 186, 213, 255 }, cyan, eClamped),
        rowOpacity),
      e);

    m_menuVisual.addText(
      labels[item],
      itemX + 80.0f + slide,
      y + 12.0f + room * 7.0f,
      19.0f + room * 2.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped), rowOpacity));
    m_menuVisual.addText(
      descriptions[item],
      itemX + 80.0f + slide,
      y + 37.0f + room * 8.0f,
      10.0f + room * 2.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textMuted(), UiTheme::textSecondary(), eClamped),
        rowOpacity));
    // The chevron wakes up and leans toward the action.
    const float chevronBob =
      still ? 0.0f
            : std::sin(m_animator.ambientPhase() * 6.2831853f / 1.6f) * 1.5f *
                eClamped;
    m_menuVisual.addText(
      ">",
      itemX + m_itemWidth - 26.0f + 6.0f * e + chevronBob,
      y + m_itemHeight * 0.5f - 9.0f,
      18.0f,
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.35f + 0.65f * eClamped),
                            rowOpacity));
  }
}

void
MainMenuModule::drawFooter(unsigned char opacity)
{
  const float size = 9.0f;
  struct Hint
  {
    const char* key;
    const char* action;
  };
  const Hint hints[4] = { { "UPDOWN", "Select" },
                          { "ENTER", "Open" },
                          { "F1", "Settings" },
                          { "ESC", "Quit" } };
  float total = 0.0f;
  for (const Hint& hint : hints) {
    total += GuiKit::measureKeyHint(hint.key, hint.action, size);
  }
  total -= size * 1.6f;
  float x = m_panelX + (m_panelWidth - total) * 0.5f;
  const float y = m_panelY + m_panelHeight - 25.0f;
  for (const Hint& hint : hints) {
    x += GuiKit::drawKeyHint(
      m_menuVisual, x, y, hint.key, hint.action, size, opacity);
  }
}

void
MainMenuModule::rebuildVisual()
{
  updateLayout();
  m_menuVisual.clearPrimitives();
  const float width = m_panelFit.virtualWidth;
  const float height = m_panelFit.virtualHeight;
  const float reveal = entranceReveal();
  // The panel dims as it recedes behind an open overlay.
  const float presence =
    1.0f - 0.45f * std::clamp(m_recede.value(), 0.0f, 1.0f);
  const unsigned char opacity =
    static_cast<unsigned char>(255.0f * reveal * presence);
  const float ambient = m_animator.ambientPhase();
  const float breathe = 0.5f + 0.5f * std::sin(ambient * 1.04719755f);

  drawBackground(width, height, reveal);

  const float room = std::clamp((m_panelHeight - 456.0f) / 144.0f, 0.0f, 1.0f);
  GuiGlassStyle glass;
  glass.opacity = opacity;
  glass.ambientPhase = ambient;
  glass.glow = 0.45f + 0.35f * breathe;
  glass.accentReveal =
    reducedMotion() ? 1.0f : GuiEasing::outCubic(m_revealElapsed / 0.8f);
  GuiKit::drawGlassPanel(
    m_menuVisual, m_panelX, m_panelY, m_panelWidth, m_panelHeight, glass);

  drawTitle(room, opacity);
  drawRows(room, opacity, breathe);
  drawFooter(opacity);
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
  if (m_newSimulationMenu != nullptr && m_newSimulationMenu->isOpen()) {
    scene->AddDrawable(&m_newSimulationMenu->getVisual(), RenderLayerId::UI);
  }
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
        openCanvasSetup();
      }
    },
    "play",
    "Open new canvas setup");
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
  m_lifetime.reset();
  unregisterConsoleCommands();
  m_newSimulationMenu.reset();
  m_configurationMenu.reset();
  m_bgContext.reset();
}
