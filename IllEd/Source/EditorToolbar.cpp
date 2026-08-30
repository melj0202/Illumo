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
  , m_dropdownAnim(1.0f)
  , m_toastElapsed(10.0f)
  , m_toastDuration(0.0f)
  , m_toastMessage("")
  , m_toastColor(ColorRgba{ 66, 214, 210, 255 })
  , m_is3D(false)
  , m_animTime(0.0f)
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
  m_dropdownAnim = 1.0f;
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
  fileMenu.items.push_back({ "New", "Ctrl+N", EditorCommand::NewDocument });
  fileMenu.items.push_back(
    { "Open...", "Ctrl+O", EditorCommand::OpenDocument });
  fileMenu.items.push_back({ "Save", "Ctrl+S", EditorCommand::SaveDocument });
  fileMenu.items.push_back(
    { "Save As...", "Ctrl+Shift+S", EditorCommand::SaveDocumentAs });
  fileMenu.items.push_back({ "Exit", "Alt+F4", EditorCommand::ExitEditor });
  m_menus.push_back(fileMenu);

  Menu editMenu;
  editMenu.title = "Edit";
  editMenu.items.push_back({ "Delete Node", "Del", EditorCommand::DeleteNode });
  editMenu.items.push_back(
    { "Unparent Node", "U", EditorCommand::UnparentNode });
  m_menus.push_back(editMenu);

  Menu createMenu;
  createMenu.title = "Create";
  createMenu.items.push_back({ "Empty Node", "", EditorCommand::CreateEmpty });
  createMenu.items.push_back({ "Rect", "", EditorCommand::CreateRect });
  createMenu.items.push_back({ "Ellipse", "", EditorCommand::CreateEllipse });
  createMenu.items.push_back({ "Triangle", "", EditorCommand::CreateTriangle });
  createMenu.items.push_back({ "Solid Cube", "", EditorCommand::CreateCube });
  createMenu.items.push_back({ "Pyramid", "", EditorCommand::CreatePyramid });
  createMenu.items.push_back({ "Sphere", "", EditorCommand::CreateSphere });
  m_menus.push_back(createMenu);

  Menu viewMenu;
  viewMenu.title = "View";
  viewMenu.items.push_back({ "2D World", "2", EditorCommand::SetMode2D });
  viewMenu.items.push_back({ "3D World", "3", EditorCommand::SetMode3D });
  viewMenu.items.push_back({ "Reset Camera", "R", EditorCommand::ResetCamera });
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
  const bool hasAtlas = m_atlas.isValid();
  const float iconWidth = hasAtlas ? 16.0f : 0.0f;
  const float iconPad = hasAtlas ? 6.0f : 0.0f;
  for (Menu& menu : m_menus) {
    const float textW = estimateTextWidth(menu.title, 13.0f);
    menu.width = std::max(
      56.0f, textW + (hasAtlas ? (iconWidth + iconPad) : 0.0f) + 12.0f);
    menu.x = cursor;
    cursor += menu.width + 4.0f;
  }
  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    m_dropdownX = menu.x;
    m_dropdownY = kBarHeight;
    m_dropdownWidth = 210.0f;
    m_dropdownHeight = 8.0f + static_cast<float>(menu.items.size()) * 24.0f;
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
      const int item = static_cast<int>(localY / 24.0f);
      if (item >= 0 && static_cast<size_t>(item) < menu.items.size()) {
        const EditorCommand command =
          menu.items[static_cast<size_t>(item)].command;
        m_openMenu = -1;
        m_dropdownAnim = 1.0f;
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
          m_dropdownAnim = 1.0f;
        } else {
          m_openMenu = static_cast<int>(i);
          m_dropdownAnim = 0.0f;
        }
        updateLayout();
        return EditorCommand::None;
      }
    }
  }

  m_openMenu = -1;
  m_dropdownAnim = 1.0f;
  updateLayout();
  return EditorCommand::None;
}

EditorCommand
EditorToolbar::clickAtForTesting(float x, float y)
{
  return clickAt(x, y);
}

EditorCommand
EditorToolbar::update(InputManager* inputManager, float dt)
{
  m_animTime += std::max(0.0f, dt);
  if (m_openMenu >= 0) {
    m_dropdownAnim =
      std::min(1.0f, m_dropdownAnim + std::max(0.0f, dt) / 0.10f);
  }
  m_toastElapsed += std::max(0.0f, dt);

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

  const ColorRgba barBg{ 14, 20, 30, 255 };
  const ColorRgba barBorder{ 38, 54, 76, 255 };
  const ColorRgba text = UiTheme::textPrimary();

  // Top menu bar
  m_visual.addFilledRect(0.0f, 0.0f, m_barWidth, kBarHeight, barBg);
  m_visual.addLine(0.0f, kBarHeight, m_barWidth, kBarHeight, barBorder, 1.0f);

  for (size_t i = 0; i < m_menus.size(); ++i) {
    const Menu& menu = m_menus[i];
    const bool isOpen = (m_openMenu == static_cast<int>(i));
    if (isOpen) {
      m_visual.addFilledRect(menu.x,
                             2.0f,
                             menu.width,
                             kBarHeight - 4.0f,
                             ColorRgba{ 30, 52, 78, 255 });
      m_visual.addOutlineRect(menu.x,
                              2.0f,
                              menu.width,
                              kBarHeight - 4.0f,
                              ColorRgba{ 66, 214, 210, 180 },
                              1.0f);
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
                                   menu.x + 15.0f,
                                   kBarHeight * 0.5f,
                                   EditorUiAtlas::kIconSize,
                                   EditorUiAtlas::kIconSize,
                                   EditorUiAtlas::regionFor(iconCommand));
        titleX = menu.x + 27.0f;
      }
    }
    m_visual.addText(menu.title,
                     titleX,
                     7.0f,
                     13.0f,
                     isOpen ? ColorRgba{ 255, 255, 255, 255 } : text);
  }

  // Viewport HUD Watermark pill in top-left
  {
    const float hudX = 12.0f;
    const float hudY = kBarHeight + 10.0f;
    const float hudW = 140.0f;
    const float hudH = 22.0f;

    m_visual.addFilledRect(
      hudX, hudY, hudW, hudH, ColorRgba{ 12, 18, 28, 190 });
    m_visual.addOutlineRect(
      hudX, hudY, hudW, hudH, ColorRgba{ 40, 60, 85, 220 }, 1.0f);

    const float dotPulse = 0.65f + 0.35f * std::sin(m_animTime * 4.0f);
    if (m_is3D) {
      const unsigned char dotAlpha =
        static_cast<unsigned char>(255.0f * dotPulse);
      m_visual.addText("●",
                       hudX + 8.0f,
                       hudY + 4.0f,
                       11.0f,
                       ColorRgba{ 70, 160, 255, dotAlpha });
      m_visual.addText("3D PERSPECTIVE",
                       hudX + 22.0f,
                       hudY + 4.0f,
                       10.0f,
                       ColorRgba{ 190, 220, 255, 240 });
    } else {
      const unsigned char dotAlpha =
        static_cast<unsigned char>(255.0f * dotPulse);
      m_visual.addText("●",
                       hudX + 8.0f,
                       hudY + 4.0f,
                       11.0f,
                       ColorRgba{ 60, 220, 120, dotAlpha });
      m_visual.addText("2D ORTHOGRAPHIC",
                       hudX + 22.0f,
                       hudY + 4.0f,
                       10.0f,
                       ColorRgba{ 190, 245, 215, 240 });
    }
  }

  // Dropdown menu with opening ease animation
  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    const float t = std::clamp(m_dropdownAnim, 0.0f, 1.0f);
    const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
    const float animHeight = m_dropdownHeight * (0.6f + 0.4f * ease);

    // Drop shadow
    m_visual.addFilledRect(
      m_dropdownX + 4.0f,
      m_dropdownY + 4.0f,
      m_dropdownWidth,
      animHeight,
      ColorRgba{ 0, 0, 0, static_cast<unsigned char>(120 * ease) });
    // Dropdown surface
    m_visual.addFilledRect(
      m_dropdownX,
      m_dropdownY,
      m_dropdownWidth,
      animHeight,
      ColorRgba{ 12, 18, 28, static_cast<unsigned char>(252 * ease) });
    m_visual.addOutlineRect(
      m_dropdownX,
      m_dropdownY,
      m_dropdownWidth,
      animHeight,
      ColorRgba{ 52, 75, 105, static_cast<unsigned char>(255 * ease) },
      1.0f);

    float itemY = m_dropdownY + 4.0f;
    for (const MenuItem& item : menu.items) {
      if (itemY + 20.0f > m_dropdownY + animHeight) {
        break;
      }
      m_visual.addText(
        item.label, m_dropdownX + 12.0f, itemY + 4.0f, 13.0f, text);
      if (!item.shortcut.empty()) {
        const float scWidth =
          static_cast<float>(item.shortcut.size()) * 11.0f * 0.55f;
        m_visual.addText(item.shortcut,
                         m_dropdownX + m_dropdownWidth - scWidth - 14.0f,
                         itemY + 5.0f,
                         11.0f,
                         ColorRgba{ 110, 135, 160, 255 });
      }
      itemY += 24.0f;
    }
  }

  // Toast notification banner
  if (m_toastElapsed < m_toastDuration && !m_toastMessage.empty()) {
    const float inT = std::clamp(m_toastElapsed / 0.20f, 0.0f, 1.0f);
    const float inEase = 1.0f - std::pow(1.0f - inT, 3.0f);
    const float outT =
      std::clamp((m_toastDuration - m_toastElapsed) / 0.35f, 0.0f, 1.0f);
    const float alphaFactor = std::min(inEase, outT);
    const float slideUp = (1.0f - inEase) * 12.0f;

    const float toastW = estimateTextWidth(m_toastMessage, 12.0f) + 32.0f;
    const float toastH = 28.0f;
    const float toastX = std::max(20.0f, m_barWidth - toastW - 240.0f);
    const float statusY = virtualHeight - kStatusHeight;
    const float toastY = statusY - toastH - 10.0f - slideUp;

    const unsigned char bgA = static_cast<unsigned char>(240.0f * alphaFactor);
    const unsigned char borderA =
      static_cast<unsigned char>(255.0f * alphaFactor);

    // Toast shadow
    m_visual.addFilledRect(
      toastX + 3.0f,
      toastY + 3.0f,
      toastW,
      toastH,
      ColorRgba{ 0, 0, 0, static_cast<unsigned char>(100 * alphaFactor) });
    // Toast body
    m_visual.addFilledRect(
      toastX, toastY, toastW, toastH, ColorRgba{ 14, 22, 34, bgA });
    m_visual.addOutlineRect(
      toastX, toastY, toastW, toastH, ColorRgba{ 55, 82, 118, borderA }, 1.0f);

    // Toast left accent stripe
    ColorRgba accentCol = m_toastColor;
    accentCol.a = borderA;
    m_visual.addFilledRect(toastX, toastY, 3.0f, toastH, accentCol);

    // Toast message text
    ColorRgba msgText{ 240, 250, 255, borderA };
    m_visual.addText(
      m_toastMessage, toastX + 14.0f, toastY + 6.0f, 12.0f, msgText);
  }

  // Bottom status bar
  const float statusY = virtualHeight - kStatusHeight;
  m_visual.addFilledRect(
    0.0f, statusY, m_barWidth, kStatusHeight, ColorRgba{ 10, 15, 24, 255 });
  m_visual.addLine(
    0.0f, statusY, m_barWidth, statusY, ColorRgba{ 35, 48, 66, 255 }, 1.0f);
  m_visual.addText(
    m_status, 10.0f, statusY + 4.0f, 12.0f, ColorRgba{ 150, 172, 195, 255 });
}

bool
EditorToolbar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
