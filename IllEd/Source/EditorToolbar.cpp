#include "EditorToolbar.h"
#include "EditorUiAtlas.h"

#include <Illumo/Gui/GuiKit.h>
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
  , m_fontSize(kDefaultFontSize)
  , m_barHeight(kDefaultBarHeight)
  , m_statusHeight(kDefaultStatusHeight)
  , m_dropdownX(0.0f)
  , m_dropdownY(kDefaultBarHeight)
  , m_dropdownWidth(168.0f)
  , m_dropdownHeight(0.0f)
  , m_dropdownAnim(1.0f)
  , m_toastElapsed(10.0f)
  , m_toastDuration(0.0f)
  , m_toastMessage("")
  , m_toastColor(ColorRgba{ 66, 214, 210, 255 })
  , m_is3D(false)
  , m_animTime(0.0f)
  , m_hoverMenu(-1)
  , m_hoverItem(-1)
  , m_mouseX(0.0f)
  , m_mouseY(0.0f)
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
EditorToolbar::setFontSize(float sizePt)
{
  const float clamped = std::clamp(sizePt, 8.0f, 48.0f);
  if (std::abs(m_fontSize - clamped) > 0.001f) {
    m_fontSize = clamped;
    updateLayout();
    rebuildVisual();
  }
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

  const float fontScale = m_fontSize / kDefaultFontSize;
  m_barHeight = std::max(28.0f, std::round(28.0f * fontScale));
  m_statusHeight = std::max(22.0f, std::round(22.0f * fontScale));

  float cursor = 8.0f * fontScale;
  const bool hasAtlas = m_atlas.isValid();
  const float iconWidth =
    hasAtlas ? std::max(16.0f, std::round(16.0f * fontScale)) : 0.0f;
  const float iconPad =
    hasAtlas ? std::max(6.0f, std::round(6.0f * fontScale)) : 0.0f;
  for (Menu& menu : m_menus) {
    const float textW = estimateTextWidth(menu.title, m_fontSize);
    menu.width = std::max(56.0f * fontScale,
                          textW + (hasAtlas ? (iconWidth + iconPad) : 0.0f) +
                            12.0f * fontScale);
    menu.x = cursor;
    cursor += menu.width + 4.0f * fontScale;
  }
  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    const float itemHeight = std::max(24.0f, std::round(24.0f * fontScale));
    const float shortcutFont = std::max(9.0f, std::round(11.0f * fontScale));
    float maxItemWidth = 210.0f * fontScale;
    for (const MenuItem& item : menu.items) {
      const float itemW = estimateTextWidth(item.label, m_fontSize) +
                          (item.shortcut.empty()
                             ? 0.0f
                             : (estimateTextWidth(item.shortcut, shortcutFont) +
                                16.0f * fontScale)) +
                          28.0f * fontScale;
      maxItemWidth = std::max(maxItemWidth, itemW);
    }
    m_dropdownX = menu.x;
    m_dropdownY = m_barHeight;
    m_dropdownWidth = maxItemWidth;
    m_dropdownHeight =
      8.0f * fontScale + static_cast<float>(menu.items.size()) * itemHeight;
  }
}

bool
EditorToolbar::containsScreenPoint(float x, float y) const
{
  if (y >= 0.0f && y <= m_barHeight && x >= 0.0f && x <= m_barWidth) {
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
  const float fontScale = m_fontSize / kDefaultFontSize;
  const float itemHeight = std::max(24.0f, std::round(24.0f * fontScale));

  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    if (x >= m_dropdownX && x <= m_dropdownX + m_dropdownWidth &&
        y >= m_dropdownY && y <= m_dropdownY + m_dropdownHeight) {
      const float localY = y - m_dropdownY - 4.0f * fontScale;
      const int item = static_cast<int>(localY / itemHeight);
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

  if (y >= 0.0f && y <= m_barHeight) {
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
  const float fontScale = m_fontSize / kDefaultFontSize;
  const float itemHeight = std::max(24.0f, std::round(24.0f * fontScale));

  // Mouse hover tracking
  m_hoverMenu = -1;
  m_hoverItem = -1;
  if (m_window != nullptr) {
    const std::array<double, 2> mouseCoords = m_window->getMouseCoords();
    const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
    m_mouseX =
      static_cast<float>(mouseCoords[0]) / (scale > 0.0f ? scale : 1.0f);
    m_mouseY =
      static_cast<float>(mouseCoords[1]) / (scale > 0.0f ? scale : 1.0f);

    if (m_mouseY >= 0.0f && m_mouseY <= m_barHeight) {
      for (size_t i = 0; i < m_menus.size(); ++i) {
        if (m_mouseX >= m_menus[i].x &&
            m_mouseX <= m_menus[i].x + m_menus[i].width) {
          m_hoverMenu = static_cast<int>(i);
          break;
        }
      }
    } else if (m_openMenu >= 0 &&
               static_cast<size_t>(m_openMenu) < m_menus.size()) {
      if (m_mouseX >= m_dropdownX &&
          m_mouseX <= m_dropdownX + m_dropdownWidth &&
          m_mouseY >= m_dropdownY &&
          m_mouseY <= m_dropdownY + m_dropdownHeight) {
        const float localY = m_mouseY - m_dropdownY - 4.0f * fontScale;
        const int item = static_cast<int>(localY / itemHeight);
        if (item >= 0 &&
            static_cast<size_t>(item) <
              m_menus[static_cast<size_t>(m_openMenu)].items.size()) {
          m_hoverItem = item;
        }
      }
    }
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

  const float fontScale = m_fontSize / kDefaultFontSize;
  const float iconSize =
    std::max(16.0f, std::round(EditorUiAtlas::kIconSize * fontScale));

  const ColorRgba barBg{ 14, 20, 30, 255 };
  const ColorRgba barBorder{ 38, 54, 76, 255 };
  const ColorRgba text = UiTheme::textPrimary();
  const ColorRgba cyanAccent{ 66, 214, 210, 255 };

  // Top menu bar
  m_visual.addFilledRect(0.0f, 0.0f, m_barWidth, m_barHeight, barBg);
  m_visual.addLine(0.0f, m_barHeight, m_barWidth, m_barHeight, barBorder, 1.0f);

  // Subtle animated glowing accent stripe at very top edge
  const float glowPulse = 0.5f + 0.5f * std::sin(m_animTime * 2.5f);
  const unsigned char topGlowAlpha =
    static_cast<unsigned char>(140.0f * glowPulse);
  m_visual.addFilledRect(
    0.0f, 0.0f, m_barWidth, 1.0f, ColorRgba{ 66, 214, 210, topGlowAlpha });

  for (size_t i = 0; i < m_menus.size(); ++i) {
    const Menu& menu = m_menus[i];
    const bool isOpen = (m_openMenu == static_cast<int>(i));
    const bool isHovered = (m_hoverMenu == static_cast<int>(i));

    if (isOpen) {
      m_visual.addFilledRect(menu.x,
                             2.0f,
                             menu.width,
                             m_barHeight - 4.0f,
                             ColorRgba{ 30, 52, 78, 255 });
      m_visual.addOutlineRect(menu.x,
                              2.0f,
                              menu.width,
                              m_barHeight - 4.0f,
                              ColorRgba{ 66, 214, 210, 220 },
                              1.0f);
    } else if (isHovered) {
      m_visual.addFilledRect(menu.x,
                             2.0f,
                             menu.width,
                             m_barHeight - 4.0f,
                             ColorRgba{ 24, 38, 56, 230 });
      m_visual.addOutlineRect(menu.x,
                              2.0f,
                              menu.width,
                              m_barHeight - 4.0f,
                              ColorRgba{ 55, 85, 120, 200 },
                              1.0f);
    }

    float titleX = menu.x + 8.0f * fontScale;
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
        const float iconCenterX = menu.x + 8.0f * fontScale + iconSize * 0.5f;
        m_visual.addCenteredSprite(m_atlas,
                                   iconCenterX,
                                   m_barHeight * 0.5f,
                                   iconSize,
                                   iconSize,
                                   EditorUiAtlas::regionFor(iconCommand));
        titleX = iconCenterX + iconSize * 0.5f + 6.0f * fontScale;
      }
    }
    const float textY =
      std::max(0.0f, std::round((m_barHeight - m_fontSize) * 0.5f));
    m_visual.addText(menu.title,
                     titleX,
                     textY,
                     m_fontSize,
                     (isOpen || isHovered) ? ColorRgba{ 255, 255, 255, 255 }
                                           : text);
  }

  // Viewport HUD Watermark pill in top-left
  {
    const float hudFontSize = std::max(9.0f, std::round(10.0f * fontScale));
    const float hudX = 12.0f * fontScale;
    const float hudY = m_barHeight + 10.0f * fontScale;
    const float hudW =
      std::max(144.0f * fontScale,
               estimateTextWidth(m_is3D ? "3D PERSPECTIVE" : "2D ORTHOGRAPHIC",
                                 hudFontSize) +
                 36.0f * fontScale);
    const float hudH = std::max(24.0f, std::round(24.0f * fontScale));

    m_visual.addFilledRect(
      hudX, hudY, hudW, hudH, ColorRgba{ 12, 18, 28, 210 });
    m_visual.addOutlineRect(
      hudX, hudY, hudW, hudH, ColorRgba{ 45, 68, 96, 230 }, 1.0f);

    // Glowing left accent indicator bar on the HUD pill
    const float dotPulse = 0.65f + 0.35f * std::sin(m_animTime * 4.0f);
    const float hudTextY =
      hudY + std::max(0.0f, std::round((hudH - hudFontSize) * 0.5f));
    const float dotCenterY = hudY + hudH * 0.5f;

    if (m_is3D) {
      const unsigned char dotAlpha =
        static_cast<unsigned char>(255.0f * dotPulse);
      m_visual.addFilledRect(
        hudX, hudY, 3.0f, hudH, ColorRgba{ 70, 160, 255, 240 });
      m_visual.addFilledEllipse(hudX + 13.0f * fontScale,
                                dotCenterY,
                                6.0f * fontScale,
                                6.0f * fontScale,
                                ColorRgba{ 70, 160, 255, dotAlpha });
      m_visual.addText("3D PERSPECTIVE",
                       hudX + 24.0f * fontScale,
                       hudTextY,
                       hudFontSize,
                       ColorRgba{ 200, 230, 255, 255 });
    } else {
      const unsigned char dotAlpha =
        static_cast<unsigned char>(255.0f * dotPulse);
      m_visual.addFilledRect(
        hudX, hudY, 3.0f, hudH, ColorRgba{ 60, 220, 120, 240 });
      m_visual.addFilledEllipse(hudX + 13.0f * fontScale,
                                dotCenterY,
                                6.0f * fontScale,
                                6.0f * fontScale,
                                ColorRgba{ 60, 220, 120, dotAlpha });
      m_visual.addText("2D ORTHOGRAPHIC",
                       hudX + 24.0f * fontScale,
                       hudTextY,
                       hudFontSize,
                       ColorRgba{ 200, 250, 225, 255 });
    }
  }

  // Dropdown menu with opening ease animation & hover states
  if (m_openMenu >= 0 && static_cast<size_t>(m_openMenu) < m_menus.size()) {
    const Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    const float t = std::clamp(m_dropdownAnim, 0.0f, 1.0f);
    const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
    const float animHeight = m_dropdownHeight * (0.6f + 0.4f * ease);
    const float itemHeight = std::max(24.0f, std::round(24.0f * fontScale));
    const float shortcutFont = std::max(9.0f, std::round(11.0f * fontScale));

    // Dropdown surface with shadow and border via GuiKit
    GuiPanelChrome dropChrome;
    dropChrome.background =
      ColorRgba{ 12, 18, 28, static_cast<unsigned char>(252 * ease) };
    dropChrome.border =
      ColorRgba{ 56, 82, 116, static_cast<unsigned char>(255 * ease) };
    dropChrome.shadow =
      ColorRgba{ 0, 0, 0, static_cast<unsigned char>(130 * ease) };
    dropChrome.shadowOffset = 5.0f;
    dropChrome.drawShadow = true;
    dropChrome.drawAccent = false;
    GuiKit::drawPanel(m_visual,
                      m_dropdownX,
                      m_dropdownY,
                      m_dropdownWidth,
                      animHeight,
                      dropChrome);

    float itemY = m_dropdownY + 4.0f * fontScale;
    for (size_t itemIdx = 0; itemIdx < menu.items.size(); ++itemIdx) {
      if (itemY + itemHeight * 0.8f > m_dropdownY + animHeight) {
        break;
      }
      const MenuItem& item = menu.items[itemIdx];
      const bool isItemHovered = (m_hoverItem == static_cast<int>(itemIdx));

      if (isItemHovered) {
        // Item hover highlight pill & left accent glow
        m_visual.addFilledRect(m_dropdownX + 3.0f,
                               itemY,
                               m_dropdownWidth - 6.0f,
                               itemHeight - 2.0f,
                               ColorRgba{ 26, 44, 66, 240 });
        m_visual.addFilledRect(m_dropdownX + 3.0f,
                               itemY,
                               3.0f,
                               itemHeight - 2.0f,
                               ColorRgba{ 66, 214, 210, 240 });
      }

      const float itemTextY =
        itemY + std::max(0.0f, std::round((itemHeight - m_fontSize) * 0.5f));
      m_visual.addText(item.label,
                       m_dropdownX + 14.0f * fontScale,
                       itemTextY,
                       m_fontSize,
                       isItemHovered ? ColorRgba{ 255, 255, 255, 255 } : text);
      if (!item.shortcut.empty()) {
        const float scWidth = estimateTextWidth(item.shortcut, shortcutFont);
        const float scTextY =
          itemY +
          std::max(0.0f, std::round((itemHeight - shortcutFont) * 0.5f));
        m_visual.addText(
          item.shortcut,
          m_dropdownX + m_dropdownWidth - scWidth - 14.0f * fontScale,
          scTextY,
          shortcutFont,
          isItemHovered ? cyanAccent : ColorRgba{ 110, 135, 160, 255 });
      }
      itemY += itemHeight;
    }
  }

  // Toast notification banner with life progress bar
  if (m_toastElapsed < m_toastDuration && !m_toastMessage.empty()) {
    const float inT = std::clamp(m_toastElapsed / 0.20f, 0.0f, 1.0f);
    const float inEase = 1.0f - std::pow(1.0f - inT, 3.0f);
    const float outT =
      std::clamp((m_toastDuration - m_toastElapsed) / 0.35f, 0.0f, 1.0f);
    const float alphaFactor = std::min(inEase, outT);
    const float slideUp = (1.0f - inEase) * 14.0f;

    const float toastFontSize = std::max(10.0f, std::round(12.0f * fontScale));
    const float toastW =
      estimateTextWidth(m_toastMessage, toastFontSize) + 36.0f * fontScale;
    const float toastH = std::max(30.0f, std::round(30.0f * fontScale));
    const float toastX =
      std::max(20.0f, m_barWidth - toastW - 240.0f * fontScale);
    const float statusY = virtualHeight - m_statusHeight;
    const float toastY = statusY - toastH - 12.0f * fontScale - slideUp;

    const unsigned char bgA = static_cast<unsigned char>(245.0f * alphaFactor);
    const unsigned char borderA =
      static_cast<unsigned char>(255.0f * alphaFactor);

    GuiPanelChrome toastChrome;
    toastChrome.background = ColorRgba{ 14, 22, 34, bgA };
    toastChrome.border = ColorRgba{ 58, 86, 122, borderA };
    toastChrome.shadow =
      ColorRgba{ 0, 0, 0, static_cast<unsigned char>(120 * alphaFactor) };
    toastChrome.shadowOffset = 4.0f;
    toastChrome.drawShadow = true;
    toastChrome.drawAccent = true;
    toastChrome.accent = m_toastColor;
    toastChrome.accent.a = borderA;
    toastChrome.accentWidth = 4.0f;
    GuiKit::drawPanel(m_visual, toastX, toastY, toastW, toastH, toastChrome);

    // Animated lifetime progress bar along the bottom of the toast
    const float remainingRatio =
      std::clamp(1.0f - (m_toastElapsed / m_toastDuration), 0.0f, 1.0f);
    const float progressW = (toastW - 4.0f) * remainingRatio;
    m_visual.addFilledRect(
      toastX + 4.0f,
      toastY + toastH - 2.0f,
      progressW,
      2.0f,
      ColorRgba{ accentCol.r,
                 accentCol.g,
                 accentCol.b,
                 static_cast<unsigned char>(180 * alphaFactor) });

    // Toast message text
    ColorRgba msgText{ 240, 250, 255, borderA };
    const float toastTextY =
      toastY + std::max(0.0f, std::round((toastH - toastFontSize) * 0.5f));
    m_visual.addText(m_toastMessage,
                     toastX + 16.0f * fontScale,
                     toastTextY,
                     toastFontSize,
                     msgText);
  }

  // Bottom status bar with status indicator pip
  const float statusY = virtualHeight - m_statusHeight;
  const float statusFontSize = std::max(10.0f, std::round(12.0f * fontScale));
  m_visual.addFilledRect(
    0.0f, statusY, m_barWidth, m_statusHeight, ColorRgba{ 10, 15, 24, 255 });
  m_visual.addLine(
    0.0f, statusY, m_barWidth, statusY, ColorRgba{ 35, 48, 66, 255 }, 1.0f);

  // Status green active pip
  const float pipPulse = 0.70f + 0.30f * std::sin(m_animTime * 3.0f);
  const unsigned char pipAlpha = static_cast<unsigned char>(255.0f * pipPulse);
  m_visual.addFilledEllipse(10.0f * fontScale,
                            statusY + m_statusHeight * 0.5f,
                            5.0f * fontScale,
                            5.0f * fontScale,
                            ColorRgba{ 60, 220, 120, pipAlpha });

  const float statusTextY =
    statusY +
    std::max(0.0f, std::round((m_statusHeight - statusFontSize) * 0.5f));
  m_visual.addText(m_status,
                   20.0f * fontScale,
                   statusTextY,
                   statusFontSize,
                   ColorRgba{ 170, 192, 215, 255 });
}

bool
EditorToolbar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
