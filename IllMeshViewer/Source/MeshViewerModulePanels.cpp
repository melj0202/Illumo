#include "MeshViewerModule.h"

#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>

// The viewer's dock panels, both in the right column. Surface ids are the
// panels' window ids while detached.
struct MeshViewerPanelInfo
{
  const char* id;
  const char* title;
  std::uint32_t surface;
  float weight;
  float detachedWidth;
  float detachedHeight;
  MeshViewerAction toggle;
  MeshViewerAction popOut;
};

static const std::array<MeshViewerPanelInfo, 2> kPanels = { {
  { "info",
    "Info",
    201u,
    0.7f,
    280.0f,
    220.0f,
    MeshViewerAction::ToggleInfoPanel,
    MeshViewerAction::PopOutInfoPanel },
  { "display",
    "Display",
    202u,
    1.6f,
    300.0f,
    520.0f,
    MeshViewerAction::ToggleDisplayPanel,
    MeshViewerAction::PopOutDisplayPanel },
} };

static std::unique_ptr<GameVisual>
makeChromeVisual(IRenderWindow* window, Renderer* renderer)
{
  std::unique_ptr<GameVisual> visual = std::make_unique<GameVisual>(256u);
  visual->setSpace(PrimitiveSpace::Pixels);
  visual->setLayerHint(RenderLayerId::UI);
  visual->setWindow(window);
  visual->setRenderer(renderer);
  visual->prepare(renderer);
  return visual;
}

void
MeshViewerModule::setupDock()
{
  m_dock = GuiPanelDock{};
  for (const MeshViewerPanelInfo& info : kPanels) {
    GuiDockPanelSpec spec;
    spec.id = info.id;
    spec.title = info.title;
    spec.side = GuiDockSide::Right;
    spec.surface = info.surface;
    spec.weight = info.weight;
    spec.detachedWidth = info.detachedWidth;
    spec.detachedHeight = info.detachedHeight;
    m_dock.addPanel(spec);
  }
  m_dockVisual = makeChromeVisual(ic->window, ic->renderer);
  for (std::unique_ptr<GameVisual>& chrome : m_detachedChrome) {
    chrome = makeChromeVisual(ic->window, ic->renderer);
  }
  m_dockLeftWasDown = false;
  m_dockPressHeld = false;
  m_mainPanelsBlocked = false;
  m_layoutLoaded = false;
  m_savedLayout.clear();
  syncLayout();
  layoutDock();
}

void
MeshViewerModule::layoutDock()
{
  if (ic == nullptr) {
    return;
  }
  m_dock.setSurfaces(ic->panelSurfaces);
  const GuiPanelFit view = GuiPanelLayout::viewport(ic->window, ic->renderer);
  m_dock.layout(view.virtualWidth,
                view.virtualHeight,
                MeshViewerUi::kHeaderHeight,
                MeshViewerUi::kStatusHeight,
                view.layoutScale);
}

GuiPanelPlacement
MeshViewerModule::placementFor(const std::string& id) const
{
  const GuiDockView& view = m_dock.view(id);
  GuiPanelPlacement placement;
  placement.area = view.content;
  placement.visible =
    view.visible && view.content.w > 0.0f && view.content.h > 0.0f;
  placement.surfaces = ic != nullptr ? ic->panelSurfaces : nullptr;
  placement.surface = view.surface;
  placement.inputBlocked = view.surface == IPanelSurfaces::kMainSurface
                             ? m_mainPanelsBlocked
                             : m_dock.consumedPress(view.surface);
  return placement;
}

void
MeshViewerModule::updateDock(bool modal)
{
  if (ic == nullptr || ic->window == nullptr || !m_ui) {
    return;
  }
  layoutDock();
  const float scale =
    GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const float x = static_cast<float>(mouse[0]) / scale;
  const float y = static_cast<float>(mouse[1]) / scale;
  const bool left = ic->inputManager != nullptr &&
                    ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool pressEdge = left && !m_dockLeftWasDown;
  m_dockLeftWasDown = left;
  if (!left) {
    m_dockPressHeld = false;
  }
  // The menu bar and its dropdowns draw over the dock.
  const bool mainBlocked =
    modal || m_ui->isMenuOpen() || y < MeshViewerUi::kHeaderHeight;
  if (pressEdge && mainBlocked) {
    m_dockPressHeld = true;
  }
  GuiDockPointer pointer;
  pointer.x = x;
  pointer.y = y;
  pointer.down = left && !m_dockPressHeld;
  m_dock.update(pointer);
  layoutDock();
  m_mainPanelsBlocked = m_dockPressHeld ||
                        m_dock.consumedPress(IPanelSurfaces::kMainSurface) ||
                        m_dock.dragging();
  if (m_info) {
    m_info->setPlacement(placementFor("info"));
  }
  if (m_display) {
    m_display->setPlacement(placementFor("display"));
  }
  m_ui->setViewport(m_dock.center());
  m_ui->setPanels(panelMenu(), m_dock.canDetach());
  if (m_dockVisual) {
    m_dockVisual->clearPrimitives();
    m_dock.drawDocked(*m_dockVisual);
  }
  for (std::size_t index = 0; index < kPanels.size(); ++index) {
    if (m_detachedChrome[index]) {
      m_detachedChrome[index]->clearPrimitives();
      m_dock.drawDetached(kPanels[index].id, *m_detachedChrome[index]);
    }
  }
}

std::vector<MeshViewerPanelMenuEntry>
MeshViewerModule::panelMenu() const
{
  std::vector<MeshViewerPanelMenuEntry> entries;
  for (const MeshViewerPanelInfo& info : kPanels) {
    const GuiDockMode mode = m_dock.mode(info.id);
    MeshViewerPanelMenuEntry entry;
    entry.title = info.title;
    entry.toggle = info.toggle;
    entry.popOut = info.popOut;
    entry.visible = mode != GuiDockMode::Hidden;
    entry.detached =
      mode == GuiDockMode::Detached || mode == GuiDockMode::Opening;
    entries.push_back(entry);
  }
  return entries;
}

bool
MeshViewerModule::handlePanelAction(MeshViewerAction action)
{
  if (action == MeshViewerAction::ResetLayout) {
    m_dock.reset();
    layoutDock();
    if (m_ui) {
      m_ui->showToast("Panel layout reset", GuiToolPalette::accent);
    }
    return true;
  }
  for (const MeshViewerPanelInfo& info : kPanels) {
    const GuiDockMode mode = m_dock.mode(info.id);
    if (action == info.toggle) {
      m_dock.setHidden(info.id, mode != GuiDockMode::Hidden);
      layoutDock();
      return true;
    }
    if (action == info.popOut) {
      if (mode == GuiDockMode::Detached || mode == GuiDockMode::Opening) {
        m_dock.dock(info.id);
      } else if (!m_dock.detach(info.id)) {
        Logger::LogWarning(std::string("Panel windows are unavailable; the ") +
                           info.title + " panel stays docked");
        if (m_ui) {
          m_ui->showToast("Separate windows are not available here",
                          GuiToolPalette::warning);
        }
      }
      layoutDock();
      return true;
    }
  }
  return false;
}

static std::string
encodeLayout(const std::string& text)
{
  std::string value = text;
  std::replace(value.begin(), value.end(), '\n', ';');
  return value;
}

static std::string
decodeLayout(const std::string& value)
{
  std::string text = value;
  std::replace(text.begin(), text.end(), ';', '\n');
  return text;
}

void
MeshViewerModule::syncLayout()
{
  if (ic == nullptr || ic->envVars == nullptr) {
    return;
  }
  if (!m_layoutLoaded) {
    const std::string saved = ic->envVars->getVar("panelLayout").value;
    if (!saved.empty()) {
      m_dock.restore(decodeLayout(saved));
      Logger::LogTrace("Restored the saved viewer panel layout");
      m_layoutLoaded = true;
      m_savedLayout = m_dock.serialize();
      return;
    }
    if (m_savedLayout.empty()) {
      m_savedLayout = m_dock.serialize();
    }
  }
  if (m_dock.dragging()) {
    return;
  }
  const std::string current = m_dock.serialize();
  if (current == m_savedLayout) {
    return;
  }
  m_savedLayout = current;
  m_layoutLoaded = true;
  ic->envVars->setVar("panelLayout", encodeLayout(current));
  ic->envVars->save();
}

MeshViewerDisplaySettings
MeshViewerModule::displaySettings() const
{
  MeshViewerDisplaySettings settings;
  settings.grid = m_showGrid;
  settings.axes = m_showAxes;
  settings.sky = m_showSkybox;
  settings.wireframe = m_showWireframe;
  if (m_meshVisual) {
    settings.lighting = m_meshVisual->isLightingEnabled();
    settings.lightDirection = m_meshVisual->getLightDirection();
    settings.ambient = m_meshVisual->getAmbientColor().y;
    settings.shadows = m_meshVisual->isShadowsEnabled();
    settings.softShadows = m_meshVisual->isShadowPcfEnabled();
    settings.shadowRadius = m_meshVisual->getShadowRadius();
    settings.shadowBias = m_meshVisual->getShadowBias();
    settings.motionBlur = m_meshVisual->isMotionBlurEnabled();
    settings.motionBlurAmount = m_meshVisual->getMotionBlurAmount();
  }
  if (ic != nullptr && ic->envVars != nullptr) {
    // The direction is stored unnormalized; show what was set.
    const std::array<const char*, 3> keys = { "lightDirX",
                                              "lightDirY",
                                              "lightDirZ" };
    for (int axis = 0; axis < 3; ++axis) {
      const EnvVar& value =
        ic->envVars->getVar(keys[static_cast<std::size_t>(axis)]);
      if (!value.value.empty()) {
        settings.lightDirection[axis] = static_cast<float>(value.valueAsDouble);
      }
    }
  }
  return settings;
}

void
MeshViewerModule::applyDisplayEdits(
  const std::vector<MeshViewerDisplayEdit>& edits)
{
  bool settingsChanged = false;
  for (const MeshViewerDisplayEdit& edit : edits) {
    if (edit.action != MeshViewerAction::None) {
      handleAction(edit.action);
    } else if (!edit.key.empty() && ic != nullptr && ic->envVars != nullptr) {
      ic->envVars->setVar(edit.key, edit.value);
      settingsChanged = true;
    }
  }
  if (settingsChanged) {
    applyLightingFromEnv();
    applyShadowsFromEnv();
    applyMotionBlurFromEnv();
    ic->envVars->save();
  }
}

void
MeshViewerModule::storeToggle(const char* key, bool value)
{
  if (ic != nullptr && ic->envVars != nullptr) {
    ic->envVars->setVar(key, value ? "1" : "0");
    ic->envVars->save();
  }
}
