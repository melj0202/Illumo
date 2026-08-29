#include "EditorToolbar.h"
#include "EditorUiAtlas.h"

#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <queue>

EditorToolbar::EditorToolbar(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(512u)
  , m_openMenu(-1)
  , m_mouseWasDown(false)
  , m_consumedPress(false)
  , m_status("Untitled")
  , m_barWidth(1280.0f)
  , m_dropdownX(0.0f)
  , m_dropdownY(kBarHeight)
  , m_dropdownWidth(168.0f)
  , m_dropdownHeight(0.0f)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  rebuildMenus();
  updateLayout();
  rebuildVisual();
}

void
EditorToolbar::setAtlas(TextureHandle atlas)
{
  m_atlas = atlas;
  rebuildVisual();
}

void
EditorToolbar::setStatus(const std::string& text)
{
  m_status = text;
}

void
EditorToolbar::closeMenus()
{
  m_openMenu = -1;
}

float
EditorToolbar::estimateTextWidth(const std::string& text, float sizePt)
{
  return static_cast<float>(text.size()) * sizePt * 0.55f + 8.0f;
}

void
EditorToolbar::rebuildMenus()
{
  m_menus.clear();
  Menu fileMenu;
  fileMenu.title = "File";
  fileMenu.items.push_back({ "New", EditorCommand::NewDocument });
  fileMenu.items.push_back({ "Open...", EditorCommand::OpenDocument });
  fileMenu.items.push_back({ "Save", EditorCommand::SaveDocument });
  fileMenu.items.push_back({ "Save As...", EditorCommand::SaveDocumentAs });
  fileMenu.items.push_back({ "Exit", EditorCommand::ExitEditor });
  m_menus.push_back(fileMenu);

  Menu editMenu;
  editMenu.title = "Edit";
  editMenu.items.push_back({ "Delete", EditorCommand::DeleteNode });
  editMenu.items.push_back({ "Unparent", EditorCommand::UnparentNode });
  m_menus.push_back(editMenu);

  Menu createMenu;
  createMenu.title = "Create";
  createMenu.items.push_back({ "Empty Node", EditorCommand::CreateEmpty });
  createMenu.items.push_back({ "Rect", EditorCommand::CreateRect });
  createMenu.items.push_back({ "Ellipse", EditorCommand::CreateEllipse });
  createMenu.items.push_back({ "Triangle", EditorCommand::CreateTriangle });
  createMenu.items.push_back({ "Solid Cube", EditorCommand::CreateCube });
  createMenu.items.push_back({ "Pyramid", EditorCommand::CreatePyramid });
  createMenu.items.push_back({ "Sphere", EditorCommand::CreateSphere });
  m_menus.push_back(createMenu);

  Menu viewMenu;
  viewMenu.title = "View";
  viewMenu.items.push_back({ "2D World", EditorCommand::SetMode2D });
  viewMenu.items.push_back({ "3D World", EditorCommand::SetMode3D });
  viewMenu.items.push_back({ "Reset Camera", EditorCommand::ResetCamera });
  m_menus.push_back(viewMenu);
}

void
EditorToolbar::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  (void)height;
  m_barWidth = virtualWidth;
  float cursor = 8.0f;
  for (Menu& menu : m_menus) {
    menu.width = std::max(48.0f, estimateTextWidth(menu.title, 13.0f));
    menu.x = cursor;
    cursor += menu.width + 4.0f;
  }
  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    m_dropdownX = menu.x;
    m_dropdownY = kBarHeight;
    m_dropdownWidth = 176.0f;
    m_dropdownHeight = 6.0f + static_cast<float>(menu.items.size()) * 22.0f;
  }
}

bool
EditorToolbar::containsScreenPoint(float x, float y) const
{
  if (y >= 0.0f && y <= kBarHeight && x >= 0.0f && x <= m_barWidth) {
    return true;
  }
  if (m_openMenu >= 0 && x >= m_dropdownX &&
      x <= m_dropdownX + m_dropdownWidth && y >= m_dropdownY &&
      y <= m_dropdownY + m_dropdownHeight) {
    return true;
  }
  return false;
}

EditorCommand
EditorToolbar::clickAt(float x, float y)
{
  updateLayout();
  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    if (x >= m_dropdownX && x <= m_dropdownX + m_dropdownWidth &&
        y >= m_dropdownY && y <= m_dropdownY + m_dropdownHeight) {
      const float localY = y - m_dropdownY - 4.0f;
      const int item = static_cast<int>(localY / 22.0f);
      if (item >= 0 && static_cast<size_t>(item) < menu.items.size()) {
        const EditorCommand command =
          menu.items[static_cast<size_t>(item)].command;
        m_openMenu = -1;
        updateLayout();
        return command;
      }
    }
  }

  if (y >= 0.0f && y <= kBarHeight) {
    for (size_t i = 0; i < m_menus.size(); ++i) {
      const Menu& menu = m_menus[i];
      if (x >= menu.x && x <= menu.x + menu.width) {
        if (m_openMenu == static_cast<int>(i)) {
          m_openMenu = -1;
        } else {
          m_openMenu = static_cast<int>(i);
        }
        updateLayout();
        return EditorCommand::None;
      }
    }
  }

  m_openMenu = -1;
  updateLayout();
  return EditorCommand::None;
}

EditorCommand
EditorToolbar::clickAtForTesting(float x, float y)
{
  return clickAt(x, y);
}

EditorCommand
EditorToolbar::update(InputManager* inputManager)
{
  updateLayout();
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
    if (control && event.key == KeyCode::N) {
      command = EditorCommand::NewDocument;
    } else if (control && event.key == KeyCode::O) {
      command = EditorCommand::OpenDocument;
    } else if (control && event.key == KeyCode::S) {
      command = inputManager->isShiftPressed() ? EditorCommand::SaveDocumentAs
                                               : EditorCommand::SaveDocument;
    } else if (event.key == KeyCode::Delete) {
      command = EditorCommand::DeleteNode;
    } else if (event.key == KeyCode::Escape) {
      closeMenus();
    } else {
      kept.push(event);
    }
  }
  keyQueue = kept;

  m_consumedPress = false;
  const bool mouseDown = inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  if (mouseDown && !m_mouseWasDown) {
    const bool menuWasOpen = m_openMenu >= 0;
    std::array<double, 2> mouse{ 0.0, 0.0 };
    if (m_window != nullptr) {
      mouse = m_window->getMouseCoords();
    }
    const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
    const float uiX =
      static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f);
    const float uiY =
      static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f);
    const EditorCommand clicked = clickAt(uiX, uiY);
    if (clicked != EditorCommand::None) {
      command = clicked;
    }
    m_consumedPress = command != EditorCommand::None || menuWasOpen ||
                      containsScreenPoint(uiX, uiY);
  }
  m_mouseWasDown = mouseDown;
  rebuildVisual();
  return command;
}

void
EditorToolbar::rebuildVisual()
{
  m_visual.clearPrimitives();
  updateLayout();

  int height = 720;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    height = std::max(1, dimensions[1]);
  }
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const ColorRgba bar = UiTheme::panelRaised();
  const ColorRgba border = UiTheme::panelBorder();
  const ColorRgba text = UiTheme::textPrimary();
  const ColorRgba muted = UiTheme::textMuted();
  m_visual.addFilledRect(0.0f, 0.0f, m_barWidth, kBarHeight, bar);
  m_visual.addLine(0.0f, kBarHeight, m_barWidth, kBarHeight, border, 1.0f);
  for (size_t i = 0; i < m_menus.size(); ++i) {
    const Menu& menu = m_menus[i];
    if (m_openMenu == static_cast<int>(i)) {
      m_visual.addFilledRect(
        menu.x, 2.0f, menu.width, kBarHeight - 4.0f, UiTheme::selection());
    }
    float titleX = menu.x + 8.0f;
    if (m_atlas.isValid()) {
      EditorCommand iconCommand = EditorCommand::None;
      if (i == 0) {
        iconCommand = EditorCommand::NewDocument;
      } else if (i == 1) {
        iconCommand = EditorCommand::DeleteNode;
      } else if (i == 2) {
        iconCommand = EditorCommand::CreateCube;
      } else if (i == 3) {
        iconCommand = EditorCommand::ResetCamera;
      }
      if (iconCommand != EditorCommand::None) {
        m_visual.addCenteredSprite(m_atlas,
                                   menu.x + 10.0f,
                                   kBarHeight * 0.5f,
                                   EditorUiAtlas::kIconSize,
                                   EditorUiAtlas::kIconSize,
                                   EditorUiAtlas::regionFor(iconCommand));
        titleX = menu.x + 20.0f;
      }
    }
    m_visual.addText(menu.title, titleX, 6.0f, 13.0f, text);
  }

  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    m_visual.addFilledRect(m_dropdownX,
                           m_dropdownY,
                           m_dropdownWidth,
                           m_dropdownHeight,
                           UiTheme::panelSurface());
    m_visual.addOutlineRect(m_dropdownX,
                            m_dropdownY,
                            m_dropdownWidth,
                            m_dropdownHeight,
                            border,
                            1.0f);
    float itemY = m_dropdownY + 4.0f;
    for (const MenuItem& item : menu.items) {
      m_visual.addText(
        item.label, m_dropdownX + 10.0f, itemY + 2.0f, 13.0f, text);
      itemY += 22.0f;
    }
  }

  const float statusY = virtualHeight - kStatusHeight;
  m_visual.addFilledRect(
    0.0f, statusY, m_barWidth, kStatusHeight, UiTheme::panelInset());
  m_visual.addLine(0.0f, statusY, m_barWidth, statusY, border, 1.0f);
  m_visual.addText(m_status, 8.0f, statusY + 3.0f, 12.0f, muted);
}

bool
EditorToolbar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
