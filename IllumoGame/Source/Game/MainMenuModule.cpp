#include "MainMenuModule.h"

#include "BuiltinPatterns.h"
#include "CSimPlatform.h"
#include "CSimSounds.h"
#include "CSimTypeface.h"
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
#include <cstdlib>
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

  m_rowEmphasis.configure(GuiMotion::kJelly);
  m_recede.configure(GuiMotion::kSwell);
  // The world and the spotlight follow the pointer like a current: lazily,
  // with a little slosh when it stops.
  m_parallaxX.configure(GuiMotion::kDrift);
  m_parallaxY.configure(GuiMotion::kDrift);
  m_spotX.configure(GuiMotion::kDrift);
  m_spotY.configure(GuiMotion::kDrift);
  m_spotStrength.configure(GuiMotion::kSwell);
  m_motif.resetGlider(7, 7, 1, 1);
  // Poses spring in and out with a jelly squash; hops boing.
  for (TitleLetterPose& pose : m_letterPoses) {
    pose.weight.configure(GuiMotion::kBoing);
    pose.squash.configure(GuiMotion::kJelly);
    pose.hop.configure(GuiMotion::kBoing);
    pose.weight.snapTo(0.0f);
    pose.squash.snapTo(0.0f);
    pose.hop.snapTo(0.0f);
    pose.hold = 0.0f;
    pose.hopDelay = -1.0f;
  }
  m_poseCountdown = kTitlePoseStartSeconds + kTitlePoseMinPauseSeconds;
  m_lastPosedLetter = -1;

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
  // The panel drops in on a spring, dipping just past center before it
  // floats back up.
  m_panelY =
    (virtualHeight - m_panelHeight) * 0.5f + m_animator.panelOffsetY() * 0.6f;
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
    m_animator.beginSelectionTravel(static_cast<float>(m_selectedItem),
                                    static_cast<float>(nextItem));
    m_selectedItem = nextItem;
    CSimSounds::play(CSimSound::MenuHover);
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
  // The panel swivels toward the pointer and swings back level when the
  // pointer leaves or an overlay takes over.
  m_tilt.aim(pointerX, pointerY, width, height, pointerInside);
  m_tilt.tick(dt, still);
  m_parallaxX.tick(dt, still);
  m_parallaxY.tick(dt, still);
  m_spotX.tick(dt, still);
  m_spotY.tick(dt, still);
  m_spotStrength.tick(dt, still);
  updateTitlePoses(dt);
}

// A hop launches the letter up on a boingy spring: it stretches thin as it
// flies, then dips past the line on the way back, which reads as a heavy
// squash, and bounces once more.
void
MainMenuModule::hopTitleLetter(int letter)
{
  TitleLetterPose& pose = m_letterPoses[static_cast<std::size_t>(letter)];
  // High enough to read as a jump, low enough to clear the eyebrow label.
  pose.hop.kick(400.0f);
  pose.squash.kick(-8.0f);
}

void
MainMenuModule::strikeTitlePose(int letter)
{
  TitleLetterPose& pose = m_letterPoses[static_cast<std::size_t>(letter)];
  const std::uint32_t roll = nextRandom();
  const float hold = 0.3f + static_cast<float>((roll >> 4u) % 400u) / 1000.0f;
  // Flex and slim hold a shape; a hop is a jump; now and then a wave of hops
  // ripples out from the letter across the whole word.
  const std::uint32_t kind = roll % 7u;
  if (kind == 6u) {
    for (int other = 0; other < kTitleLetterCount; ++other) {
      m_letterPoses[static_cast<std::size_t>(other)].hopDelay =
        static_cast<float>(std::abs(other - letter)) * kTitleHopStaggerSeconds;
    }
    return;
  }
  if (kind >= 4u) {
    hopTitleLetter(letter);
    return;
  }
  // Flex: black and squat. Slim: hairline and tall.
  const bool flex = kind < 2u;
  pose.weight.setTarget(flex ? 320.0f : -460.0f);
  pose.squash.setTarget(flex ? 1.0f : -1.0f);
  pose.hold = hold;
  // Neighbours get jostled the other way.
  for (int side = -1; side <= 1; side += 2) {
    const int neighbour = letter + side;
    if (neighbour >= 0 && neighbour < kTitleLetterCount) {
      m_letterPoses[static_cast<std::size_t>(neighbour)].squash.kick(
        flex ? -3.5f : 3.5f);
    }
  }
}

void
MainMenuModule::updateTitlePoses(float dt)
{
  const bool still = reducedMotion();
  for (int letter = 0; letter < kTitleLetterCount; ++letter) {
    TitleLetterPose& pose = m_letterPoses[static_cast<std::size_t>(letter)];
    if (still) {
      pose.hold = 0.0f;
      pose.hopDelay = -1.0f;
    }
    if (pose.hopDelay >= 0.0f) {
      pose.hopDelay -= dt;
      if (pose.hopDelay < 0.0f) {
        pose.hopDelay = -1.0f;
        hopTitleLetter(letter);
      }
    }
    if (pose.hold > 0.0f) {
      pose.hold -= dt;
    }
    if (pose.hold <= 0.0f) {
      pose.hold = 0.0f;
      pose.weight.setTarget(0.0f);
      pose.squash.setTarget(0.0f);
    }
    pose.weight.tick(dt, still);
    pose.squash.tick(dt, still);
    pose.hop.tick(dt, still);
  }
  // Poses wait for the word to land and rest while an overlay is up.
  if (still || m_revealElapsed < kTitlePoseStartSeconds ||
      m_recede.target() > 0.5f) {
    return;
  }
  m_poseCountdown -= dt;
  if (m_poseCountdown > 0.0f) {
    return;
  }
  int letter = static_cast<int>(nextRandom() % kTitleLetterCount);
  if (letter == m_lastPosedLetter) {
    letter =
      (letter + 1 + static_cast<int>(nextRandom() % (kTitleLetterCount - 1))) %
      kTitleLetterCount;
  }
  strikeTitlePose(letter);
  m_lastPosedLetter = letter;
  m_poseCountdown = kTitlePoseMinPauseSeconds +
                    kTitlePosePauseRangeSeconds *
                      static_cast<float>(nextRandom() % 1000u) / 1000.0f;
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

  CSimSounds::play(CSimSound::MenuSelect);
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

std::array<float, 4>
MainMenuModule::itemHitBoundsForTesting(int item) const
{
  return std::array<float, 4>{
    m_panelX + m_tilt.shiftX(GuiPanelTilt::kBodyDepth) + 28.0f,
    m_firstItemY + m_tilt.shiftY(GuiPanelTilt::kBodyDepth) +
      static_cast<float>(item) * (m_itemHeight + 8.0f),
    m_itemWidth,
    m_itemHeight
  };
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
  config.soundVolume = CSimSounds::volumeSetting(ic->envVars);
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
  ic->envVars->setVar("soundVolume", configuration.soundVolume);
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
        CSimSounds::play(CSimSound::MenuBack);
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
            CSimSounds::play(CSimSound::MenuSelect);
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
        const float rowY =
          m_firstItemY + m_tilt.shiftY(GuiPanelTilt::kBodyDepth) +
          static_cast<float>(m_selectedItem) * (m_itemHeight + 8.0f);
        pressItem(m_panelX + m_tilt.shiftX(GuiPanelTilt::kBodyDepth) + 28.0f +
                    m_itemWidth * 0.5f,
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

    // Poking the word sends a hop rippling through it.
    if (m_pointer.clicked() && !reducedMotion() &&
        GuiKit::isPointInRect(mouseX,
                              mouseY,
                              m_titleBounds[0],
                              m_titleBounds[1],
                              m_titleBounds[2],
                              m_titleBounds[3])) {
      for (int letter = 0; letter < kTitleLetterCount; ++letter) {
        m_letterPoses[static_cast<std::size_t>(letter)].hopDelay =
          static_cast<float>(letter) * kTitleHopStaggerSeconds;
      }
    }

    // Rows are hit where they are drawn, tilt included.
    const float itemX =
      m_panelX + m_tilt.shiftX(GuiPanelTilt::kBodyDepth) + 28.0f;
    const float itemGap = 8.0f;
    for (int i = 0; i < kItemCount; ++i) {
      const float currentItemY =
        m_firstItemY + m_tilt.shiftY(GuiPanelTilt::kBodyDepth) +
        static_cast<float>(i) * (m_itemHeight + itemGap);
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
  const float leanX = std::clamp(m_parallaxX.value(), -1.3f, 1.3f);
  const float leanY = std::clamp(m_parallaxY.value(), -1.3f, 1.3f);

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

  // Three slow aurora glows drift like lava-lamp blobs: each squeezes along
  // one axis while it swells along the other, and they lean against the
  // pointer at different depths. Every rate is a whole number of cycles per
  // ambient period, so nothing jumps when the cycle wraps.
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
                     std::cos(phase + seed * 1.3f) * height * 0.08f -
                     leanY * depths[light];
    const float squeeze = std::sin(phase * 2.0f + seed * 1.9f);
    const float swell = 1.0f + 0.06f * std::sin(phase * 3.0f + seed);
    GuiKit::drawSoftGlow(
      m_menuVisual,
      cx,
      cy,
      width * 0.34f * swell * (1.0f + 0.12f * squeeze),
      height * 0.52f * swell * (1.0f - 0.1f * squeeze),
      UiTheme::fade(tints[light], (light == 2 ? 0.12f : 0.16f) * reveal),
      20);
  }

  const float spot = std::clamp(m_spotStrength.value(), 0.0f, 1.0f);
  if (spot > 0.01f) {
    // The spotlight is a blob of light: it smears along its motion and
    // thins across it, then rounds out again when the pointer rests.
    const float smearX =
      std::min(0.45f, std::abs(m_spotX.velocity()) / 1400.0f);
    const float smearY =
      std::min(0.45f, std::abs(m_spotY.velocity()) / 1400.0f);
    GuiKit::drawSoftGlow(m_menuVisual,
                         m_spotX.value(),
                         m_spotY.value(),
                         150.0f * (1.0f + smearX - 0.4f * smearY),
                         150.0f * (1.0f + smearY - 0.4f * smearX),
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
  // The title floats nearer than the glass, so it swings further as the
  // panel tilts toward the pointer.
  const float panelX = m_panelX + m_tilt.shiftX(GuiPanelTilt::kHeaderDepth);
  const float panelY = m_panelY + m_tilt.shiftY(GuiPanelTilt::kHeaderDepth);
  const float eyebrowReveal =
    still ? 1.0f : GuiEasing::outCubic(m_revealElapsed / 0.5f);
  m_menuVisual.addText(
    "C E L L U L A R   P L A Y G R O U N D",
    panelX + 30.0f - 8.0f * (1.0f - eyebrowReveal),
    // Headroom for the title's hops.
    panelY + 18.0f,
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
    // A variable typeface rasterizes just the word, at each title weight;
    // otherwise the default face gets one atlas at this size.
    m_titleRamp = CSimTypeface::titleRamp(rasterSize, "CSIM");
    m_titleFont.reset();
    if (m_titleRamp.empty()) {
      m_titleFont = Font::getDefaultFont();
      if (m_titleFont != nullptr && !m_titleFont->getPath().empty() &&
          m_titleFont->getPath() != "<memory>") {
        m_titleFont = Font::loadFromFile(m_titleFont->getPath(),
                                         static_cast<float>(rasterSize));
      }
    }
    m_titleRasterSize = rasterSize;
  }
  // Each letter falls in like a drop, stretched tall and thin, and splats
  // squat and heavy as it lands before bouncing back; then the word floats on
  // a slow swell that ripples weight from letter to letter, letters near the
  // pointer pool heavier and puff up, and one letter at a time strikes a pose
  // (updateTitlePoses). Squash keeps each letter's feet on the line, and
  // wider, heavier letters shoulder their neighbours aside.
  const char* letters[kTitleLetterCount] = { "C", "S", "I", "M" };
  const float titleX = panelX + 28.0f;
  const float titleY = panelY + 43.0f;
  const float pointerPull =
    still ? 0.0f : std::clamp(m_spotStrength.value(), 0.0f, 1.0f);
  const float pointerReach = titleSize * 1.1f;
  const float lineY = titleY + titleSize * 0.98f + 4.0f;
  float advance = 0.0f;
  for (int letter = 0; letter < kTitleLetterCount; ++letter) {
    const TitleLetterPose& pose =
      m_letterPoses[static_cast<std::size_t>(letter)];
    const float start = kTitleLetterDelaySeconds +
                        static_cast<float>(letter) * kTitleLetterStaggerSeconds;
    const float progress =
      still ? 1.0f : (m_revealElapsed - start) / kTitleLetterSeconds;
    const float landed = still
                           ? 1.0f
                           : GuiEasing::springStep(m_revealElapsed - start,
                                                   kTitleLetterBounceHz,
                                                   kTitleLetterBounceDamping);
    const float fade = std::clamp(progress * 2.2f, 0.0f, 1.0f);
    const float settled = std::clamp(progress - 1.0f, 0.0f, 1.0f);
    const float swell =
      std::sin(ambient * 2.0943951f - static_cast<float>(letter) * 0.9f);
    const float bob = still ? 0.0f : swell * 2.2f * settled;
    const float size = titleSize * (0.82f + 0.18f * landed);

    // A hop is height above the line; its dip below the line on the way back
    // becomes squash, so feet never leave the baseline downward.
    const float hop = pose.hop.value();
    const float lift = std::max(0.0f, hop);
    const float flight = std::min(1.0f, std::abs(pose.hop.velocity()) / 400.0f);
    const float impact = std::clamp(-hop / 5.0f, 0.0f, 1.0f);

    float weight = CSimTypeface::kTitleRestWeight;
    // Squash: + squat and wide, - tall and narrow.
    float squash = 0.0f;
    if (!still) {
      // `landed` overshoots 1 at impact, so the letter lands heavier than it
      // rests; sinking letters are heavy and rising ones light.
      weight = kTitleFallWeight +
               (CSimTypeface::kTitleRestWeight - kTitleFallWeight) * landed +
               kTitleBreathWeight * swell * settled + pose.weight.value() -
               260.0f * flight + 280.0f * impact;
      squash = pose.squash.value() - 0.7f * flight + 1.1f * impact;
      if (progress > 0.0f) {
        // Tall while falling, splatted on impact.
        squash += -0.8f * std::clamp(1.0f - landed, 0.0f, 1.0f) +
                  3.0f * std::max(0.0f, landed - 1.0f);
      }
      if (pointerPull > 0.0f) {
        const float restWidth =
          m_titleRamp.empty()
            ? 0.0f
            : m_titleRamp.measure(
                letters[letter], titleSize, CSimTypeface::kTitleRestWeight);
        const float dx = m_spotX.value() -
                         (titleX + advance + std::max(0.0f, restWidth) * 0.5f);
        const float dy = m_spotY.value() - (titleY + titleSize * 0.5f);
        const float closeness =
          pointerPull *
          std::exp(-(dx * dx + dy * dy) / (2.0f * pointerReach * pointerReach));
        weight += kTitlePointerWeight * closeness;
        squash += 0.3f * closeness;
      }
    }
    squash = std::clamp(squash, -1.4f, 1.4f);
    const float stretchX = 1.0f + 0.2f * squash;
    const float stretchY = 1.0f - 0.17f * squash;

    float letterWidth =
      m_titleRamp.empty()
        ? -1.0f
        : m_titleRamp.measure(letters[letter], titleSize, weight);
    float drawnWidth = m_titleRamp.empty()
                         ? -1.0f
                         : m_titleRamp.measure(letters[letter], size, weight);
    if (letterWidth < 0.0f || drawnWidth < 0.0f) {
      letterWidth =
        m_titleFont != nullptr
          ? m_titleFont->measureText(letters[letter], titleSize).width
          : titleSize * 0.6f;
      drawnWidth = m_titleFont != nullptr
                     ? m_titleFont->measureText(letters[letter], size).width
                     : size * 0.6f;
    }
    const float cellWidth = letterWidth * stretchX;
    if (lift > 1.0f) {
      // A glow pools on the line under a hopping letter and shrinks as it
      // rises.
      const float away = std::clamp(lift / 18.0f, 0.0f, 1.0f);
      GuiKit::drawSoftGlow(
        m_menuVisual,
        titleX + advance + cellWidth * 0.5f,
        lineY + 1.5f,
        cellWidth * (0.7f - 0.3f * away),
        5.0f,
        UiTheme::applyOpacity(UiTheme::fade(cyan, 0.5f * (1.0f - away)),
                              opacity),
        12);
    }
    const size_t index = m_menuVisual.addText(
      letters[letter],
      titleX + advance + (letterWidth - drawnWidth) * stretchX * 0.5f,
      titleY + (1.0f - landed) * 22.0f + (titleSize - size) * 0.5f + bob - lift,
      size,
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::textPrimary(), fade),
                            opacity));
    TextPrimitive* text = m_menuVisual.getText(index);
    text->font = m_titleFont;
    text->stretchX = stretchX;
    text->stretchY = stretchY;
    m_titleRamp.apply(*text, weight);
    advance += cellWidth;
  }
  m_titleBounds = { titleX, titleY, advance, titleSize };
  // A cyan-to-violet underline draws out beneath the word.
  const float underline =
    still
      ? 1.0f
      : GuiEasing::outCubic((m_revealElapsed - 0.42f) / kTitleLetterSeconds);
  if (underline > 0.0f) {
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
    panelX + 30.0f,
    panelY + 98.0f + room * 20.0f + 6.0f * (1.0f - taglineReveal),
    13.0f + room * 3.0f,
    UiTheme::applyOpacity(
      UiTheme::fade(UiTheme::textSecondary(), taglineReveal), opacity));
  if (room > 0.5f) {
    const float secondReveal =
      still ? 1.0f : GuiEasing::outCubic((m_revealElapsed - 0.4f) / 0.45f);
    m_menuVisual.addText(
      "Build a world. See what emerges.",
      panelX + 30.0f,
      panelY + 145.0f + 6.0f * (1.0f - secondReveal),
      12.0f,
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::textMuted(), secondReveal),
                            opacity));
  }

  // The motif is a real glider walking a 7x7 torus. It is the nearest layer,
  // so it swings furthest as the panel tilts and seems to hover over the
  // glass.
  const float cellSize = 7.0f + room * 5.0f;
  const float gap = 3.0f;
  const float motifWidth = m_motif.width(cellSize, gap);
  const float motifX = m_panelX + m_panelWidth - motifWidth - 33.0f +
                       m_tilt.shiftX(GuiPanelTilt::kAccentDepth);
  const float motifY =
    m_panelY + 35.0f + m_tilt.shiftY(GuiPanelTilt::kAccentDepth);
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
  // Rows sit a little nearer than the glass and swing with the tilt; hit
  // testing in Update applies the same shift.
  const float itemX =
    m_panelX + m_tilt.shiftX(GuiPanelTilt::kBodyDepth) + 28.0f;
  const float firstItemY =
    m_firstItemY + m_tilt.shiftY(GuiPanelTilt::kBodyDepth);
  const float stride = m_itemHeight + 8.0f;
  const float radius = 13.0f;

  // Rows fade in on a stagger and drop into place on a spring, bouncing just
  // past their slot before they settle.
  float rowReveal[kItemCount];
  float rowY[kItemCount];
  for (int item = 0; item < kItemCount; ++item) {
    rowReveal[item] = still
                        ? 1.0f
                        : GuiEasing::outCubic(
                            (m_revealElapsed - static_cast<float>(item) *
                                                 kItemEntranceStaggerSeconds) /
                            kItemEntranceSeconds);
    rowY[item] = firstItemY + static_cast<float>(item) * stride +
                 m_animator.rowDrop(item, 0) * 12.0f;
  }

  // Cards: soft drop shadow, a rim that warms toward the accent with
  // emphasis, and a top-lit gradient face.
  for (int item = 0; item < kItemCount; ++item) {
    const float y = rowY[item];
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

  // The selection is a drop of liquid: its head pours toward the new row and
  // overshoots, its tail stretches, necks and snaps in behind it, and it
  // wobbles like jelly when pressed. It breathes a soft glow while it rests
  // and a sheen sweeps across when it lands.
  const GuiSelectionSpan span =
    m_animator.selectionSpan(static_cast<float>(m_selectedItem));
  const float selectedDrop = rowY[m_selectedItem] - firstItemY -
                             static_cast<float>(m_selectedItem) * stride;
  const unsigned char pillOpacity = static_cast<unsigned char>(
    static_cast<float>(opacity) * rowReveal[m_selectedItem]);
  GuiLiquidSelection drop;
  drop.crossStart = itemX;
  drop.crossSize = m_itemWidth;
  drop.headStart = firstItemY + span.leading * stride + selectedDrop;
  drop.tailStart = firstItemY + span.trailing * stride + selectedDrop;
  drop.cellLength = m_itemHeight;
  drop.radius = radius;
  drop.squash =
    std::clamp(span.squash + 0.9f * m_animator.pressWobble(), -1.0f, 1.0f);
  drop.glowSpread = 16.0f + 4.0f * breathe;
  drop.glow = UiTheme::applyOpacity(UiTheme::fade(cyan, 0.2f + 0.12f * breathe),
                                    pillOpacity);
  drop.rim = UiTheme::applyOpacity(cyan, pillOpacity);
  drop.faceTop = UiTheme::applyOpacity(UiTheme::selectionTop(), pillOpacity);
  drop.faceBottom =
    UiTheme::applyOpacity(UiTheme::selectionBottom(), pillOpacity);
  drop.sheen = m_animator.selectionSheen();
  drop.sheenColor =
    UiTheme::applyOpacity(ColorRgba{ 210, 250, 255, 46 }, pillOpacity);
  GuiKit::drawLiquidSelection(m_menuVisual, drop);
  const float pressProgress = m_animator.pressProgress();
  if (pressProgress >= 0.0f) {
    // A flash blooms at the press point and a splash leaps out of it:
    // a ripple ring and teardrop droplets that arc back down.
    const float ring = GuiEasing::outCubic(pressProgress);
    const float fadeOut = 1.0f - pressProgress;
    GuiKit::drawSoftGlow(
      m_menuVisual,
      m_pressX,
      m_pressY,
      40.0f + 140.0f * ring,
      30.0f + 60.0f * ring,
      UiTheme::fade(ColorRgba{ 190, 245, 255, 255 }, 0.35f * fadeOut),
      16);
    GuiKit::drawSplash(m_menuVisual,
                       m_pressX,
                       m_pressY,
                       pressProgress,
                       1.0f,
                       ColorRgba{ 170, 240, 255, 255 });
  }

  for (int item = 0; item < kItemCount; ++item) {
    const unsigned char rowOpacity =
      static_cast<unsigned char>(static_cast<float>(opacity) * rowReveal[item]);
    const float y = rowY[item];
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

    // The label thickens as the row takes focus, swelling past bold on the
    // jelly spring's overshoot before it settles.
    GuiKit::drawEmphasizedText(
      m_menuVisual,
      labels[item],
      itemX + 80.0f + slide,
      y + 12.0f + room * 7.0f,
      19.0f + room * 2.0f,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textPrimary(), cyan, eClamped), rowOpacity),
      std::min(e, 1.3f));
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
            : std::sin(m_animator.ambientPhase() * 6.2831853f / 1.5f) * 1.5f *
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
  float x = m_panelX + m_tilt.shiftX(GuiPanelTilt::kFooterDepth) +
            (m_panelWidth - total) * 0.5f;
  const float y = m_panelY + m_tilt.shiftY(GuiPanelTilt::kFooterDepth) +
                  m_panelHeight - 25.0f;
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
  // The glass is the back layer of the swivel: it shifts least, its shadow
  // slides away from the pointer and a glare follows it.
  m_tilt.applyTo(glass);
  GuiKit::drawGlassPanel(m_menuVisual,
                         m_panelX + m_tilt.shiftX(GuiPanelTilt::kGlassDepth),
                         m_panelY + m_tilt.shiftY(GuiPanelTilt::kGlassDepth),
                         m_panelWidth,
                         m_panelHeight,
                         glass);

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
