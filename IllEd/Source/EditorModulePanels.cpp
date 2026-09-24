#include "EditorModule.h"

#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <memory>

// The four dock panels, in dock order. Surface ids are the panels' window ids
// while detached; they only need to be unique within IllEd.
struct EditorPanelInfo
{
  const char* id;
  const char* title;
  GuiDockSide side;
  std::uint32_t surface;
  float weight;
  float detachedWidth;
  float detachedHeight;
  EditorCommand toggle;
  EditorCommand popOut;
};

static const std::array<EditorPanelInfo, 4> kPanels = { {
  { "hierarchy",
    "Hierarchy",
    GuiDockSide::Left,
    101u,
    1.6f,
    300.0f,
    480.0f,
    EditorCommand::ToggleHierarchyPanel,
    EditorCommand::PopOutHierarchyPanel },
  { "assets",
    "Assets",
    GuiDockSide::Left,
    102u,
    1.0f,
    320.0f,
    360.0f,
    EditorCommand::ToggleAssetsPanel,
    EditorCommand::PopOutAssetsPanel },
  { "tools",
    "Tools",
    GuiDockSide::Right,
    103u,
    1.3f,
    280.0f,
    400.0f,
    EditorCommand::ToggleToolsPanel,
    EditorCommand::PopOutToolsPanel },
  { "inspector",
    "Inspector",
    GuiDockSide::Right,
    104u,
    1.0f,
    320.0f,
    520.0f,
    EditorCommand::ToggleInspectorPanel,
    EditorCommand::PopOutInspectorPanel },
} };

// Settings values are single lines; the dock's layout text uses newlines.
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
EditorModule::setupDock()
{
  m_dock = GuiPanelDock{};
  for (const EditorPanelInfo& info : kPanels) {
    GuiDockPanelSpec spec;
    spec.id = info.id;
    spec.title = info.title;
    spec.side = info.side;
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
EditorModule::layoutDock()
{
  if (ic == nullptr || !m_toolbar) {
    return;
  }
  m_dock.setSurfaces(ic->panelSurfaces);
  const GuiPanelFit view = GuiPanelLayout::viewport(ic->window, ic->renderer);
  m_dock.layout(view.virtualWidth,
                view.virtualHeight,
                m_toolbar->barHeight(),
                m_toolbar->statusHeight(),
                view.layoutScale);
}

GuiPanelPlacement
EditorModule::placementFor(const std::string& id) const
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
EditorModule::updateDock(bool modal)
{
  if (ic == nullptr || !m_toolbar || ic->window == nullptr) {
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
  // Menus draw over the dock, so a press on them (or while a dialog is up)
  // belongs to them for as long as the button stays down.
  const bool mainBlocked =
    modal || m_toolbar->isMenuOpen() || m_toolbar->containsScreenPoint(x, y);
  if (pressEdge && mainBlocked) {
    m_dockPressHeld = true;
  }
  GuiDockPointer pointer;
  pointer.x = x;
  pointer.y = y;
  pointer.down = left && !m_dockPressHeld;
  m_dock.update(pointer);
  // Buttons, splitters and window events change the layout this frame.
  layoutDock();
  m_mainPanelsBlocked = m_dockPressHeld ||
                        m_dock.consumedPress(IPanelSurfaces::kMainSurface) ||
                        m_dock.dragging();

  if (m_sceneGraphView) {
    m_sceneGraphView->setPlacement(placementFor("hierarchy"));
  }
  if (m_assetBrowser) {
    m_assetBrowser->setPlacement(placementFor("assets"));
  }
  if (m_tools) {
    m_tools->setPlacement(placementFor("tools"));
  }
  if (m_inspector) {
    m_inspector->setPlacement(placementFor("inspector"));
  }
  m_toolbar->setViewport(m_dock.center());
  m_toolbar->setPanels(panelMenu(), m_dock.canDetach());

  if (m_dockVisual) {
    m_dockVisual->clearPrimitives();
    m_dock.drawDocked(*m_dockVisual);
  }
  for (std::size_t index = 0; index < kPanels.size(); ++index) {
    GameVisual* chrome = m_detachedChrome[index].get();
    if (chrome != nullptr) {
      chrome->clearPrimitives();
      m_dock.drawDetached(kPanels[index].id, *chrome);
    }
  }
}

std::vector<EditorPanelMenuEntry>
EditorModule::panelMenu() const
{
  std::vector<EditorPanelMenuEntry> entries;
  for (const EditorPanelInfo& info : kPanels) {
    const GuiDockMode mode = m_dock.mode(info.id);
    EditorPanelMenuEntry entry;
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
EditorModule::handlePanelCommand(EditorCommand command)
{
  if (command == EditorCommand::ResetLayout) {
    m_dock.reset();
    layoutDock();
    toast("Panel layout reset", GuiToolPalette::accent);
    return true;
  }
  for (const EditorPanelInfo& info : kPanels) {
    const GuiDockMode mode = m_dock.mode(info.id);
    if (command == info.toggle) {
      m_dock.setHidden(info.id, mode != GuiDockMode::Hidden);
      layoutDock();
      return true;
    }
    if (command == info.popOut) {
      if (mode == GuiDockMode::Detached || mode == GuiDockMode::Opening) {
        m_dock.dock(info.id);
      } else if (!m_dock.detach(info.id)) {
        Logger::LogWarning(std::string("Panel windows are unavailable; the ") +
                           info.title + " panel stays docked");
        toast("Separate windows are not available here",
              GuiToolPalette::warning);
      }
      layoutDock();
      return true;
    }
  }
  return false;
}

void
EditorModule::syncLayout()
{
  if (ic == nullptr || ic->envVars == nullptr) {
    return;
  }
  if (!m_layoutLoaded) {
    // Guest settings load asynchronously; apply them once they arrive,
    // unless the layout was already changed by hand.
    const std::string saved = ic->envVars->getVar("panelLayout").value;
    if (!saved.empty()) {
      m_dock.restore(decodeLayout(saved));
      Logger::LogTrace("Restored the saved editor panel layout");
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

bool
EditorModule::dropToMain(const EditorAssetBrowser::Drop& drop,
                         float* pixelX,
                         float* pixelY) const
{
  if (drop.surface == IPanelSurfaces::kMainSurface) {
    *pixelX = drop.pixelX;
    *pixelY = drop.pixelY;
    return true;
  }
  if (ic == nullptr || ic->panelSurfaces == nullptr) {
    return false;
  }
  // Both windows report their client origin on the screen.
  const std::array<int, 2> from = ic->panelSurfaces->origin(drop.surface);
  const std::array<int, 2> main =
    ic->panelSurfaces->origin(IPanelSurfaces::kMainSurface);
  *pixelX = drop.pixelX + static_cast<float>(from[0] - main[0]);
  *pixelY = drop.pixelY + static_cast<float>(from[1] - main[1]);
  return true;
}

void
EditorModule::dispatchPanels(Scene* scene)
{
  if (m_dockVisual) {
    scene->AddDrawable(m_dockVisual.get(), RenderLayerId::UI);
  }
  const std::array<DrawableBase*, 4> content = { m_sceneGraphView.get(),
                                                 m_assetBrowser.get(),
                                                 m_tools.get(),
                                                 m_inspector.get() };
  for (std::size_t index = 0; index < kPanels.size(); ++index) {
    const GuiDockView& view = m_dock.view(kPanels[index].id);
    if (!view.visible || content[index] == nullptr) {
      continue;
    }
    Scene* target = scene;
    if (view.surface != IPanelSurfaces::kMainSurface) {
      target = ic != nullptr && ic->panelSurfaces != nullptr
                 ? ic->panelSurfaces->scene(view.surface)
                 : nullptr;
      if (target == nullptr) {
        continue;
      }
      if (m_detachedChrome[index]) {
        target->AddDrawable(m_detachedChrome[index].get(), RenderLayerId::UI);
      }
    }
    target->AddDrawable(content[index], RenderLayerId::UI);
  }
}

void
EditorModule::closePanelWindows()
{
  if (ic == nullptr || ic->panelSurfaces == nullptr) {
    return;
  }
  // The layout (with its detached panels) is already saved; they reopen on
  // the next start.
  for (const EditorPanelInfo& info : kPanels) {
    const GuiDockMode mode = m_dock.mode(info.id);
    if (mode == GuiDockMode::Detached || mode == GuiDockMode::Opening) {
      ic->panelSurfaces->close(info.surface);
    }
  }
}
