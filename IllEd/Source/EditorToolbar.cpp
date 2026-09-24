#include "EditorToolbar.h"
#include "EditorShortcuts.h"

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <queue>

EditorToolbar::EditorToolbar(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(512u)
  , m_status("Untitled")
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  rebuildMenus();
  updateLayout();
  m_viewport = { 0.0f, barHeight(), m_width, m_height - barHeight() };
  rebuildVisual();
}

void
EditorToolbar::setFontSize(float sizePt)
{
  m_fontSize = std::clamp(sizePt, 8.0f, 48.0f);
}

void
EditorToolbar::setStatus(const std::string& text)
{
  m_status = text;
}

void
EditorToolbar::setWorldMode(bool is3D)
{
  if (is3D != m_is3D) {
    m_is3D = is3D;
    rebuildMenus();
  }
}

void
EditorToolbar::showToast(const std::string& message,
                         ColorRgba color,
                         float duration)
{
  m_toastMessage = message;
  m_toastColor = color;
  m_toastElapsed = 0.0f;
  m_toastDuration = std::max(0.5f, duration);
}

void
EditorToolbar::closeMenus()
{
  m_openMenu = -1;
}

void
EditorToolbar::setHistoryLabels(const std::string& undoLabel,
                                const std::string& redoLabel)
{
  if (undoLabel == m_undoLabel && redoLabel == m_redoLabel) {
    return;
  }
  m_undoLabel = undoLabel;
  m_redoLabel = redoLabel;
  rebuildMenus();
}

void
EditorToolbar::setPanels(const std::vector<EditorPanelMenuEntry>& panels,
                         bool canDetach)
{
  bool same = panels.size() == m_panels.size() && canDetach == m_canDetach;
  for (std::size_t index = 0; same && index < panels.size(); ++index) {
    same = panels[index].visible == m_panels[index].visible &&
           panels[index].detached == m_panels[index].detached &&
           panels[index].title == m_panels[index].title;
  }
  if (same) {
    return;
  }
  m_panels = panels;
  m_canDetach = canDetach;
  rebuildMenus();
}

EditorToolbar::MenuItem
EditorToolbar::item(const char* label, EditorCommand command)
{
  MenuItem entry;
  entry.label = label;
  entry.shortcut = EditorShortcuts::labelFor(command);
  entry.command = command;
  return entry;
}

EditorToolbar::MenuItem
EditorToolbar::separator()
{
  MenuItem entry;
  entry.separator = true;
  return entry;
}

void
EditorToolbar::rebuildMenus()
{
  m_menus.clear();
  // Shortcut hints come from EditorShortcuts, the same table key handling
  // uses, so every hint shown here works.
  Menu fileMenu;
  fileMenu.title = "File";
  fileMenu.items.push_back(item("New", EditorCommand::NewDocument));
  fileMenu.items.push_back(item("Open...", EditorCommand::OpenDocument));
  fileMenu.items.push_back(item("Save", EditorCommand::SaveDocument));
  fileMenu.items.push_back(item("Save As...", EditorCommand::SaveDocumentAs));
  fileMenu.items.push_back(separator());
  fileMenu.items.push_back(
    item("Save to Project", EditorCommand::SaveToProject));
  fileMenu.items.push_back(
    item("Import to Project...", EditorCommand::ImportAsset));
  fileMenu.items.push_back(item("Pack Project...", EditorCommand::PackProject));
  fileMenu.items.push_back(separator());
  // Alt+F4 is the window's own close path (OnCloseRequested).
  MenuItem exit = item("Exit", EditorCommand::ExitEditor);
  exit.shortcut = "Alt+F4";
  fileMenu.items.push_back(exit);
  m_menus.push_back(fileMenu);

  Menu editMenu;
  editMenu.title = "Edit";
  MenuItem undo = item("Undo", EditorCommand::Undo);
  if (!m_undoLabel.empty()) {
    undo.label = "Undo " + m_undoLabel;
  }
  undo.enabled = !m_undoLabel.empty();
  editMenu.items.push_back(undo);
  MenuItem redo = item("Redo", EditorCommand::Redo);
  if (!m_redoLabel.empty()) {
    redo.label = "Redo " + m_redoLabel;
  }
  redo.enabled = !m_redoLabel.empty();
  editMenu.items.push_back(redo);
  editMenu.items.push_back(separator());
  editMenu.items.push_back(item("Cut", EditorCommand::Cut));
  editMenu.items.push_back(item("Copy", EditorCommand::Copy));
  editMenu.items.push_back(item("Paste", EditorCommand::Paste));
  editMenu.items.push_back(item("Duplicate", EditorCommand::Duplicate));
  editMenu.items.push_back(separator());
  editMenu.items.push_back(item("Select All", EditorCommand::SelectAll));
  editMenu.items.push_back(item("Deselect", EditorCommand::DeselectAll));
  editMenu.items.push_back(item("Rename", EditorCommand::Rename));
  editMenu.items.push_back(item("Delete", EditorCommand::DeleteNode));
  editMenu.items.push_back(item("Unparent", EditorCommand::UnparentNode));
  editMenu.items.push_back(item("Show/Hide", EditorCommand::ToggleVisible));
  m_menus.push_back(editMenu);

  Menu createMenu;
  createMenu.title = "Create";
  createMenu.items.push_back(item("Empty Node", EditorCommand::CreateEmpty));
  createMenu.items.push_back(item("Rect", EditorCommand::CreateRect));
  createMenu.items.push_back(item("Ellipse", EditorCommand::CreateEllipse));
  createMenu.items.push_back(item("Triangle", EditorCommand::CreateTriangle));
  createMenu.items.push_back(item("Cube", EditorCommand::CreateCube));
  createMenu.items.push_back(item("Pyramid", EditorCommand::CreatePyramid));
  createMenu.items.push_back(item("Sphere", EditorCommand::CreateSphere));
  createMenu.items.push_back(item("Wire Cube", EditorCommand::CreateWireCube));
  createMenu.items.push_back(
    item("Wire Sphere", EditorCommand::CreateWireSphere));
  createMenu.items.push_back(separator());
  createMenu.items.push_back(item("Light", EditorCommand::CreateLight));
  createMenu.items.push_back(item("Camera", EditorCommand::CreateCamera));
  m_menus.push_back(createMenu);

  Menu toolsMenu;
  toolsMenu.title = "Tools";
  toolsMenu.items.push_back(item("Select", EditorCommand::SelectTool));
  toolsMenu.items.push_back(item("Move", EditorCommand::TranslateMode));
  toolsMenu.items.push_back(item("Rotate", EditorCommand::RotateMode));
  toolsMenu.items.push_back(item("Scale", EditorCommand::ScaleMode));
  toolsMenu.items.push_back(separator());
  toolsMenu.items.push_back(
    item("Toggle World/Local", EditorCommand::ToggleGizmoSpace));
  toolsMenu.items.push_back(item("Toggle Snap", EditorCommand::ToggleSnap));
  m_menus.push_back(toolsMenu);

  Menu viewMenu;
  viewMenu.title = "View";
  MenuItem world2d = item("2D World", EditorCommand::SetMode2D);
  world2d.checked = !m_is3D;
  viewMenu.items.push_back(world2d);
  MenuItem world3d = item("3D World", EditorCommand::SetMode3D);
  world3d.checked = m_is3D;
  viewMenu.items.push_back(world3d);
  viewMenu.items.push_back(
    item("Frame Selection", EditorCommand::FrameSelection));
  viewMenu.items.push_back(item("Reset Camera", EditorCommand::ResetCamera));
  if (!m_panels.empty()) {
    viewMenu.items.push_back(separator());
    for (const EditorPanelMenuEntry& panel : m_panels) {
      MenuItem shown = item(panel.title.c_str(), panel.toggle);
      shown.checked = panel.visible;
      viewMenu.items.push_back(shown);
    }
    viewMenu.items.push_back(separator());
    for (const EditorPanelMenuEntry& panel : m_panels) {
      const std::string label =
        (panel.detached ? "Dock " : "Pop Out ") + panel.title;
      MenuItem popOut = item(label.c_str(), panel.popOut);
      popOut.enabled = panel.detached || m_canDetach;
      viewMenu.items.push_back(popOut);
    }
    viewMenu.items.push_back(separator());
    viewMenu.items.push_back(item("Reset Layout", EditorCommand::ResetLayout));
  }
  m_menus.push_back(viewMenu);
}

std::vector<std::string>
EditorToolbar::titles() const
{
  std::vector<std::string> result;
  for (const Menu& menu : m_menus) {
    result.push_back(menu.title);
  }
  return result;
}

void
EditorToolbar::updateLayout()
{
  const GuiPanelFit view = GuiPanelLayout::viewport(m_window, m_renderer);
  m_width = view.virtualWidth;
  m_height = view.virtualHeight;
}

GuiToolRect
EditorToolbar::barRect() const
{
  return { 0.0f, 0.0f, m_width, barHeight() };
}

std::vector<GuiToolStyle::MenuItem>
EditorToolbar::styleItems(const Menu& menu) const
{
  std::vector<GuiToolStyle::MenuItem> items;
  for (const MenuItem& entry : menu.items) {
    GuiToolStyle::MenuItem styled;
    styled.label = entry.label;
    styled.hint = entry.shortcut;
    styled.enabled = entry.enabled;
    styled.checked = entry.checked;
    styled.separator = entry.separator;
    items.push_back(styled);
  }
  return items;
}

GuiToolRect
EditorToolbar::dropdownRect() const
{
  if (m_openMenu < 0 ||
      static_cast<std::size_t>(m_openMenu) >= m_menus.size()) {
    return {};
  }
  const GuiToolRect title =
    GuiToolStyle::menuRect(barRect(), titles(), m_openMenu);
  const std::vector<GuiToolStyle::MenuItem> items =
    styleItems(m_menus[static_cast<std::size_t>(m_openMenu)]);
  float height = 6.0f;
  for (const GuiToolStyle::MenuItem& entry : items) {
    height += entry.separator ? 7.0f : GuiToolStyle::kRowHeight;
  }
  return { title.x, barHeight(), GuiToolStyle::dropdownWidth(items), height };
}

int
EditorToolbar::menuAt(float x, float y) const
{
  const GuiToolRect bar = barRect();
  if (!bar.contains(x, y)) {
    return -1;
  }
  const std::vector<std::string> names = titles();
  for (int index = 0; index < static_cast<int>(names.size()); ++index) {
    if (GuiToolStyle::menuRect(bar, names, index).contains(x, y)) {
      return index;
    }
  }
  return -1;
}

int
EditorToolbar::itemAt(float x, float y) const
{
  if (m_openMenu < 0 ||
      static_cast<std::size_t>(m_openMenu) >= m_menus.size()) {
    return -1;
  }
  const GuiToolRect drop = dropdownRect();
  return GuiToolStyle::dropdownItemAt(
    drop.x,
    drop.y,
    styleItems(m_menus[static_cast<std::size_t>(m_openMenu)]),
    x,
    y);
}

bool
EditorToolbar::containsScreenPoint(float x, float y) const
{
  if (barRect().contains(x, y)) {
    return true;
  }
  return m_openMenu >= 0 && dropdownRect().contains(x, y);
}

EditorCommand
EditorToolbar::clickAt(float x, float y)
{
  updateLayout();
  if (m_openMenu >= 0) {
    const int index = itemAt(x, y);
    if (index >= 0) {
      const MenuItem& chosen = m_menus[static_cast<std::size_t>(m_openMenu)]
                                 .items[static_cast<std::size_t>(index)];
      if (!chosen.enabled) {
        return EditorCommand::None;
      }
      m_openMenu = -1;
      return chosen.command;
    }
  }
  const int menu = menuAt(x, y);
  if (menu >= 0) {
    m_openMenu = m_openMenu == menu ? -1 : menu;
    return EditorCommand::None;
  }
  m_openMenu = -1;
  return EditorCommand::None;
}

std::string
EditorToolbar::menuHintForTesting(EditorCommand command) const
{
  for (const Menu& menu : m_menus) {
    for (const MenuItem& entry : menu.items) {
      if (!entry.separator && entry.command == command) {
        return entry.shortcut;
      }
    }
  }
  return {};
}

bool
EditorToolbar::menuItemCenterForTesting(EditorCommand command,
                                        float* x,
                                        float* y)
{
  updateLayout();
  for (std::size_t menu = 0; menu < m_menus.size(); ++menu) {
    const std::vector<MenuItem>& items = m_menus[menu].items;
    float top = barHeight() + 3.0f;
    for (const MenuItem& entry : items) {
      const float height = entry.separator ? 7.0f : GuiToolStyle::kRowHeight;
      if (!entry.separator && entry.command == command) {
        m_openMenu = static_cast<int>(menu);
        const GuiToolRect drop = dropdownRect();
        *x = drop.x + drop.w * 0.5f;
        *y = top + height * 0.5f;
        return true;
      }
      top += height;
    }
  }
  return false;
}

EditorCommand
EditorToolbar::clickAtForTesting(float x, float y)
{
  return clickAt(x, y);
}

EditorCommand
EditorToolbar::update(InputManager* inputManager, float dt)
{
  m_toastElapsed += std::max(0.0f, dt);
  updateLayout();
  m_pointer.sample(m_placement, m_window, m_renderer, inputManager);
  const float x = m_pointer.x();
  const float y = m_pointer.y();
  m_hoverMenu = menuAt(x, y);
  m_hoverItem = itemAt(x, y);
  if (m_openMenu >= 0 && m_hoverMenu >= 0) {
    // With a menu open, hovering another title switches to it.
    m_openMenu = m_hoverMenu;
  }

  if (inputManager == nullptr) {
    rebuildVisual();
    return EditorCommand::None;
  }

  EditorCommand command = EditorCommand::None;
  std::queue<InputManager::KeyPressEvent>& keyQueue =
    inputManager->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> kept;
  while (!keyQueue.empty()) {
    const InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();
    if (event.action != InputAction::Press) {
      kept.push(event);
      continue;
    }
    const bool control =
      inputManager->isControlPressed() || (event.modifiers & 0x2) != 0;
    const bool shift =
      inputManager->isShiftPressed() || (event.modifiers & 0x1) != 0;
    const bool alt =
      inputManager->isAltPressed() || (event.modifiers & 0x4) != 0;
    if (event.key == KeyCode::Escape && m_openMenu >= 0) {
      closeMenus();
      continue;
    }
    const EditorCommand matched =
      EditorShortcuts::match(event.key, control, shift, alt);
    if (matched != EditorCommand::None) {
      command = matched;
    } else {
      kept.push(event);
    }
  }
  keyQueue = kept;

  m_consumedPress = false;
  if (m_pointer.clicked()) {
    const bool menuWasOpen = m_openMenu >= 0;
    const EditorCommand clicked = clickAt(x, y);
    if (clicked != EditorCommand::None) {
      command = clicked;
    }
    m_consumedPress = command != EditorCommand::None || menuWasOpen ||
                      containsScreenPoint(x, y);
  }
  rebuildVisual();
  return command;
}

void
EditorToolbar::rebuildVisual()
{
  m_visual.clearPrimitives();
  updateLayout();
  const std::vector<std::string> names = titles();
  GuiToolStyle::menuBar(m_visual, barRect(), names, m_openMenu, m_hoverMenu);

  // The viewport's mode, plain, at its top left.
  const std::string mode = m_is3D ? "3D Perspective" : "2D Orthographic";
  const float labelWidth =
    GuiToolStyle::textWidth(mode, GuiToolStyle::kSmallFontSize) + 16.0f;
  if (m_viewport.w > labelWidth + 16.0f) {
    const float labelX = m_viewport.x + 8.0f;
    const float labelY = m_viewport.y + 8.0f;
    m_visual.addFilledRect(
      labelX, labelY, labelWidth, 18.0f, GuiToolPalette::bar);
    m_visual.addOutlineRect(
      labelX, labelY, labelWidth, 18.0f, GuiToolPalette::border, 1.0f);
    GuiToolStyle::text(m_visual,
                       mode,
                       labelX + 8.0f,
                       labelY + 3.0f,
                       GuiToolStyle::kSmallFontSize,
                       GuiToolPalette::dim);
  }

  // A toast at the viewport's bottom right, fading out at its end.
  if (m_toastElapsed < m_toastDuration && !m_toastMessage.empty()) {
    const float fade =
      std::clamp((m_toastDuration - m_toastElapsed) / 0.3f, 0.0f, 1.0f);
    const unsigned char alpha = static_cast<unsigned char>(255.0f * fade);
    const float width =
      GuiToolStyle::textWidth(m_toastMessage, GuiToolStyle::kFontSize) + 28.0f;
    const float height = 26.0f;
    const float toastX = std::max(m_viewport.x + 8.0f,
                                  m_viewport.x + m_viewport.w - width - 12.0f);
    const float toastY = m_viewport.y + m_viewport.h - height - 12.0f;
    ColorRgba background = GuiToolPalette::bar;
    background.a = alpha;
    ColorRgba border = GuiToolPalette::border;
    border.a = alpha;
    ColorRgba stripe = m_toastColor;
    stripe.a = alpha;
    ColorRgba message = GuiToolPalette::text;
    message.a = alpha;
    m_visual.addFilledRect(toastX, toastY, width, height, background);
    m_visual.addOutlineRect(toastX, toastY, width, height, border, 1.0f);
    m_visual.addFilledRect(toastX, toastY, 3.0f, height, stripe);
    GuiToolStyle::text(m_visual,
                       m_toastMessage,
                       toastX + 14.0f,
                       toastY + 5.0f,
                       GuiToolStyle::kFontSize,
                       message);
  }

  GuiToolStyle::statusBar(
    m_visual,
    GuiToolRect{ 0.0f, m_height - statusHeight(), m_width, statusHeight() },
    m_status,
    std::string());

  // The open dropdown last, above everything else.
  if (m_openMenu >= 0 &&
      static_cast<std::size_t>(m_openMenu) < m_menus.size()) {
    const GuiToolRect drop = dropdownRect();
    GuiToolStyle::dropdown(
      m_visual,
      drop.x,
      drop.y,
      styleItems(m_menus[static_cast<std::size_t>(m_openMenu)]),
      m_hoverItem);
  }
}

bool
EditorToolbar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
